#include "editor_model.hpp"

#include "../PipelineSamples.h"

#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imnodes.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace node_editor = pluginsystem::examples::node_editor;

namespace {

struct CliOptions {
    bool headless_run{false};
    std::optional<std::filesystem::path> graph_path;
    std::vector<std::filesystem::path> plugin_libraries;
};

struct PinRef {
    std::string node_id;
    std::string port_id;
    bool is_output{false};
};

void print_usage()
{
    std::cerr << "Usage:\n"
              << "  node_editor_app <plugin.dll>...\n"
              << "  node_editor_app --graph <graph.json> <plugin.dll>...\n"
              << "  node_editor_app --headless-run <graph.json> <plugin.dll>...\n";
}

CliOptions parse_cli(int argc, char** argv)
{
    CliOptions options;
    int index = 1;
    if (index < argc && std::string_view{argv[index]} == "--headless-run") {
        options.headless_run = true;
        ++index;
        if (index >= argc) {
            throw std::invalid_argument{"--headless-run requires a graph JSON path."};
        }
        options.graph_path = argv[index++];
    } else if (index < argc && std::string_view{argv[index]} == "--graph") {
        ++index;
        if (index >= argc) {
            throw std::invalid_argument{"--graph requires a graph JSON path."};
        }
        options.graph_path = argv[index++];
    }

    for (; index < argc; ++index) {
        options.plugin_libraries.push_back(argv[index]);
    }

    if (!options.graph_path && options.plugin_libraries.empty()) {
        throw std::invalid_argument{"At least one plugin DLL path or --graph path is required."};
    }
    if (options.headless_run && !options.graph_path) {
        throw std::invalid_argument{"--headless-run requires a graph JSON path."};
    }

    return options;
}

int stable_id(std::string_view text)
{
    std::uint32_t hash = 2166136261u;
    for (const auto character : text) {
        hash ^= static_cast<std::uint8_t>(character);
        hash *= 16777619u;
    }
    return static_cast<int>(hash & 0x7fffffffU) + 1;
}

int pin_id(std::string_view node_id, std::string_view port_id, bool is_output)
{
    return stable_id(std::string{node_id} + "|" + std::string{port_id} + (is_output ? "|out" : "|in"));
}

std::string sanitize_id(std::string_view value)
{
    std::string result;
    result.reserve(value.size());
    for (const auto character : value) {
        if ((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9')) {
            result.push_back(character);
        } else if (!result.empty() && result.back() != '_') {
            result.push_back('_');
        }
    }
    if (result.empty()) {
        result = "node";
    }
    return result;
}

std::string make_unique_node_id(const node_editor::EditorGraph& graph, std::string_view plugin_id)
{
    const auto base = sanitize_id(plugin_id);
    for (int suffix = 1;; ++suffix) {
        auto candidate = base + "_" + std::to_string(suffix);
        if (node_editor::find_node(graph, candidate) == nullptr) {
            return candidate;
        }
    }
}

bool input_text_string(const char* label, std::string& value)
{
    std::array<char, 512> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%s", value.c_str());
    if (ImGui::InputText(label, buffer.data(), buffer.size())) {
        value = buffer.data();
        return true;
    }
    return false;
}

std::unique_ptr<pluginsystem::PluginRegistry> make_registry(const std::vector<std::filesystem::path>& libraries)
{
    auto registry = std::make_unique<pluginsystem::PluginRegistry>();
    for (const auto& library : libraries) {
        registry->add_dll_plugin(library);
    }
    return registry;
}

void apply_float_properties(pluginsystem::GraphRuntime& runtime, const node_editor::EditorGraph& graph, const node_editor::DescriptorIndex& descriptors)
{
    for (const auto& node : graph.nodes) {
        const auto* descriptor = node_editor::find_descriptor(descriptors, node.plugin_id);
        if (descriptor == nullptr) {
            continue;
        }

        for (const auto& property : descriptor->properties) {
            if (property.type_name != "float" || property.byte_size != sizeof(float)) {
                continue;
            }

            const auto found = node.float_properties.find(property.id);
            if (found != node.float_properties.end()) {
                const float value = found->second;
                runtime.properties(node.node_id).write(property.id, &value, sizeof(value));
            }
        }
    }
}

std::vector<std::string> read_output_summaries(pluginsystem::GraphRuntime& runtime, const node_editor::EditorGraph& graph, const node_editor::DescriptorIndex& descriptors)
{
    std::vector<std::string> summaries;
    for (const auto& node : graph.nodes) {
        const auto* descriptor = node_editor::find_descriptor(descriptors, node.plugin_id);
        if (descriptor == nullptr) {
            continue;
        }

        for (const auto& port : descriptor->ports) {
            if (port.direction != pluginsystem::PortDirection::output) {
                continue;
            }

            auto& channel = runtime.port(node.node_id, port.id);
            if (port.type_name == "PipelineExample.PipelineFrame" && port.byte_size == sizeof(PipelineExample::PipelineFrame)) {
                PipelineExample::PipelineFrame frame{};
                channel.read(&frame, sizeof(frame));
                summaries.push_back(
                    node.node_id + "." + port.id
                    + ": sequence=" + std::to_string(frame.sequence)
                    + " raw=" + std::to_string(frame.raw_value)
                    + " processed=" + std::to_string(frame.processed_value)
                    + " status=" + frame.status
                );
            } else {
                summaries.push_back(
                    node.node_id + "." + port.id
                    + ": " + port.type_name
                    + ", version=" + std::to_string(channel.version())
                );
            }
        }
    }
    return summaries;
}

std::vector<std::string> run_graph_once(
    pluginsystem::PluginRegistry& registry,
    const node_editor::EditorGraph& graph,
    const node_editor::DescriptorIndex& descriptors
)
{
    const auto validation = node_editor::validate_editor_graph(graph, descriptors);
    if (!validation.empty()) {
        throw std::runtime_error{"Graph validation failed: " + validation.front()};
    }

    auto runtime = registry.create_graph(node_editor::make_graph_config(graph));
    apply_float_properties(*runtime, graph, descriptors);
    const auto job = runtime->submit_run();
    const auto result = runtime->wait(job);
    if (result.result != PS_OK) {
        throw std::runtime_error{"Graph run failed at node: " + result.failed_node_id};
    }

    auto summaries = read_output_summaries(*runtime, graph, descriptors);
    runtime->stop();
    return summaries;
}

int run_headless(const CliOptions& options)
{
    auto graph = node_editor::load_editor_graph(*options.graph_path);
    graph.plugin_libraries = node_editor::merge_plugin_libraries(graph.plugin_libraries, options.plugin_libraries);
    auto registry = make_registry(graph.plugin_libraries);
    auto descriptors = node_editor::make_descriptor_index(registry->discover_plugins());

    const auto summaries = run_graph_once(*registry, graph, descriptors);
    std::cout << "Headless graph run completed.\n";
    for (const auto& summary : summaries) {
        std::cout << "  " << summary << '\n';
    }
    return 0;
}

class NodeEditorApp {
public:
    explicit NodeEditorApp(CliOptions options)
        : graph_path_{options.graph_path.value_or("node_editor_graph.json")}
    {
        if (options.graph_path) {
            graph_ = node_editor::load_editor_graph(*options.graph_path);
        }
        graph_.plugin_libraries = node_editor::merge_plugin_libraries(graph_.plugin_libraries, options.plugin_libraries);
        graph_path_text_ = graph_path_.string();
        reload_plugins();
        refresh_validation();
    }

    void draw()
    {
        tick_continuous_run();
        draw_top_bar();
        draw_palette();
        draw_canvas();
        draw_inspector();
        draw_bottom_panel();
    }

private:
    void reload_plugins()
    {
        running_continuously_ = false;
        paused_ = false;
        continuous_job_.reset();
        continuous_run_count_ = 0;
        stop_runtime_internal();
        registry_ = make_registry(graph_.plugin_libraries);
        descriptors_ = node_editor::make_descriptor_index(registry_->discover_plugins());
        dirty_ = true;
        messages_.push_back("Discovered " + std::to_string(descriptors_.descriptors.size()) + " plugin(s).");
    }

    void refresh_validation()
    {
        validation_messages_ = node_editor::validate_editor_graph(graph_, descriptors_);
    }

    void draw_top_bar()
    {
        ImGui::SetNextWindowPos(ImVec2{0, 0}, ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2{ImGui::GetIO().DisplaySize.x, 72}, ImGuiCond_Always);
        ImGui::Begin("Toolbar", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

        if (input_text_string("Blueprint", graph_.blueprint_name)) {
            dirty_ = true;
        }

        const bool can_run = validation_messages_.empty();

        ImGui::SameLine();
        if (running_continuously_) { ImGui::BeginDisabled(); }
        if (ImGui::Button("Run Once") && can_run) { run_once_from_gui(); }
        if (running_continuously_) { ImGui::EndDisabled(); }

        ImGui::SameLine();
        if (running_continuously_) {
            const auto label = "Running (" + std::to_string(continuous_run_count_) + ")##cont";
            ImGui::BeginDisabled();
            ImGui::Button(label.c_str());
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Pause")) { pause_continuous_run(); }
        } else if (paused_) {
            if (ImGui::Button("Resume")) { resume_continuous_run(); }
        } else {
            if (ImGui::Button("Run") && can_run) { start_continuous_run(); }
        }

        ImGui::SameLine();
        if (ImGui::Button("Stop")) { stop_runtime(); }
        ImGui::SameLine();
        if (ImGui::Button("Reload Plugins")) {
            try_call([this]() {
                reload_plugins();
                refresh_validation();
            });
        }

        input_text_string("Graph JSON", graph_path_text_);
        ImGui::SameLine();
        if (ImGui::Button("Load")) {
            try_call([this]() {
                graph_path_ = graph_path_text_;
                graph_ = node_editor::load_editor_graph(graph_path_);
                positioned_node_ids_.clear();
                selected_node_id_.clear();
                reload_plugins();
                refresh_validation();
            });
        }
        ImGui::SameLine();
        if (ImGui::Button("Save")) {
            try_call([this]() {
                graph_path_ = graph_path_text_;
                graph_.plugin_libraries = current_libraries();
                node_editor::save_editor_graph(graph_path_, graph_);
                messages_.push_back("Saved graph: " + graph_path_.string());
            });
        }

        ImGui::End();
    }

    void draw_palette()
    {
        ImGui::SetNextWindowPos(ImVec2{0, 72}, ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2{270, ImGui::GetIO().DisplaySize.y - 232}, ImGuiCond_Always);
        ImGui::Begin("Plugin Palette", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

        for (const auto& descriptor : descriptors_.descriptors) {
            ImGui::TextWrapped("%s", descriptor.name.c_str());
            ImGui::TextDisabled("%s", descriptor.id.c_str());
            if (ImGui::Button(("Add##" + descriptor.id).c_str())) {
                add_node(descriptor);
            }
            ImGui::Separator();
        }

        ImGui::End();
    }

    void draw_canvas()
    {
        ImGui::SetNextWindowPos(ImVec2{270, 72}, ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2{ImGui::GetIO().DisplaySize.x - 610, ImGui::GetIO().DisplaySize.y - 232}, ImGuiCond_Always);
        ImGui::Begin("Graph", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

        pin_refs_.clear();
        ImNodes::BeginNodeEditor();

        for (auto& node : graph_.nodes) {
            const auto* descriptor = node_editor::find_descriptor(descriptors_, node.plugin_id);
            if (positioned_node_ids_.insert(node.ui_id).second) {
                ImNodes::SetNodeGridSpacePos(node.ui_id, ImVec2{node.x, node.y});
            }
            ImNodes::BeginNode(node.ui_id);

            ImNodes::BeginNodeTitleBar();
            ImGui::TextUnformatted(descriptor != nullptr ? descriptor->name.c_str() : node.plugin_id.c_str());
            ImNodes::EndNodeTitleBar();

            if (descriptor == nullptr) {
                ImGui::TextUnformatted("Missing plugin");
            } else {
                for (const auto& port : descriptor->ports) {
                    if (port.direction != pluginsystem::PortDirection::input) {
                        continue;
                    }
                    const auto id = pin_id(node.node_id, port.id, false);
                    pin_refs_[id] = PinRef{node.node_id, port.id, false};
                    ImNodes::BeginInputAttribute(id);
                    ImGui::Text("%s", port.id.c_str());
                    ImNodes::EndInputAttribute();
                }

                for (const auto& port : descriptor->ports) {
                    if (port.direction != pluginsystem::PortDirection::output) {
                        continue;
                    }
                    const auto id = pin_id(node.node_id, port.id, true);
                    pin_refs_[id] = PinRef{node.node_id, port.id, true};
                    ImNodes::BeginOutputAttribute(id);
                    ImGui::Indent(50.0F);
                    ImGui::Text("%s", port.id.c_str());
                    ImNodes::EndOutputAttribute();
                }
            }

            ImNodes::EndNode();
            const auto position = ImNodes::GetNodeGridSpacePos(node.ui_id);
            if (node.x != position.x || node.y != position.y) {
                node.x = position.x;
                node.y = position.y;
                dirty_ = true;
            }
            if (ImNodes::IsNodeSelected(node.ui_id)) {
                selected_node_id_ = node.node_id;
            }
        }

        for (const auto& edge : graph_.edges) {
            ImNodes::Link(
                edge.ui_id,
                pin_id(edge.source_node_id, edge.source_port_id, true),
                pin_id(edge.target_node_id, edge.target_port_id, false)
            );
        }

        ImNodes::MiniMap();
        ImNodes::EndNodeEditor();

        handle_link_creation();
        handle_link_deletion();

        ImGui::End();
    }

    void draw_inspector()
    {
        ImGui::SetNextWindowPos(ImVec2{ImGui::GetIO().DisplaySize.x - 340, 72}, ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2{340, ImGui::GetIO().DisplaySize.y - 232}, ImGuiCond_Always);
        ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

        if (ImGui::BeginCombo("Selected Node", selected_node_id_.empty() ? "<none>" : selected_node_id_.c_str())) {
            for (const auto& node : graph_.nodes) {
                const bool selected = node.node_id == selected_node_id_;
                if (ImGui::Selectable(node.node_id.c_str(), selected)) {
                    selected_node_id_ = node.node_id;
                }
            }
            ImGui::EndCombo();
        }

        auto* node = node_editor::find_node(graph_, selected_node_id_);
        if (node == nullptr) {
            ImGui::TextUnformatted("No node selected.");
            ImGui::End();
            return;
        }

        const auto* descriptor = node_editor::find_descriptor(descriptors_, node->plugin_id);
        ImGui::SeparatorText("Node");
        ImGui::Text("Id: %s", node->node_id.c_str());
        ImGui::Text("Plugin: %s", node->plugin_id.c_str());
        if (input_text_string("Instance", node->instance_name)) {
            dirty_ = true;
            refresh_validation();
        }

        if (descriptor != nullptr) {
            draw_entrypoint_combo("Process", *descriptor, node->process_entrypoint);
            draw_entrypoint_combo("Start", *descriptor, node->start_entrypoint);
            draw_entrypoint_combo("Stop", *descriptor, node->stop_entrypoint);

            ImGui::SeparatorText("Properties");
            for (const auto& property : descriptor->properties) {
                if (property.type_name == "float" && property.byte_size == sizeof(float) && property.writable) {
                    auto& value = node->float_properties[property.id];
                    ImGui::InputFloat(property.id.c_str(), &value);
                } else {
                    ImGui::TextDisabled(
                        "%s: %s (%llu bytes)",
                        property.id.c_str(),
                        property.type_name.c_str(),
                        static_cast<unsigned long long>(property.byte_size)
                    );
                }
            }
        }

        ImGui::Separator();
        if (ImGui::Button("Delete Node")) {
            delete_selected_node();
        }

        ImGui::End();
    }

    void draw_bottom_panel()
    {
        ImGui::SetNextWindowPos(ImVec2{0, ImGui::GetIO().DisplaySize.y - 160}, ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2{ImGui::GetIO().DisplaySize.x, 160}, ImGuiCond_Always);
        ImGui::Begin("Validation And Output", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

        ImGui::Columns(2);
        ImGui::TextUnformatted("Validation");
        if (validation_messages_.empty()) {
            ImGui::TextColored(ImVec4{0.3F, 0.9F, 0.4F, 1.0F}, "OK");
        } else {
            for (const auto& message : validation_messages_) {
                ImGui::TextWrapped("%s", message.c_str());
            }
        }

        ImGui::NextColumn();
        ImGui::TextUnformatted("Messages");
        for (const auto& message : messages_) {
            ImGui::TextWrapped("%s", message.c_str());
        }
        ImGui::Columns(1);
        ImGui::End();
    }

    void draw_entrypoint_combo(const char* label, const pluginsystem::PluginDescriptor& descriptor, std::string& value)
    {
        if (ImGui::BeginCombo(label, value.empty() ? "<none>" : value.c_str())) {
            if (ImGui::Selectable("<none>", value.empty())) {
                value.clear();
                dirty_ = true;
            }
            for (const auto& entrypoint : descriptor.entrypoints) {
                const bool selected = entrypoint.id == value;
                if (ImGui::Selectable(entrypoint.id.c_str(), selected)) {
                    value = entrypoint.id;
                    dirty_ = true;
                    refresh_validation();
                }
            }
            ImGui::EndCombo();
        }
    }

    void add_node(const pluginsystem::PluginDescriptor& descriptor)
    {
        node_editor::EditorNode node;
        node.ui_id = graph_.next_node_ui_id++;
        node.plugin_id = descriptor.id;
        node.node_id = make_unique_node_id(graph_, descriptor.id);
        node.instance_name = node.node_id;
        node.x = 40.0F + static_cast<float>(graph_.nodes.size()) * 40.0F;
        node.y = 40.0F + static_cast<float>(graph_.nodes.size()) * 40.0F;
        for (const auto& property : descriptor.properties) {
            if (property.type_name == "float" && property.byte_size == sizeof(float)) {
                node.float_properties[property.id] = 0.0F;
            }
        }
        selected_node_id_ = node.node_id;
        graph_.nodes.push_back(std::move(node));
        dirty_ = true;
        refresh_validation();
    }

    void handle_link_creation()
    {
        int start_pin = 0;
        int end_pin = 0;
        if (!ImNodes::IsLinkCreated(&start_pin, &end_pin)) {
            return;
        }

        const auto start = pin_refs_.find(start_pin);
        const auto end = pin_refs_.find(end_pin);
        if (start == pin_refs_.end() || end == pin_refs_.end()) {
            return;
        }

        const auto* source = &start->second;
        const auto* target = &end->second;
        if (!source->is_output && target->is_output) {
            std::swap(source, target);
        }
        if (!source->is_output || target->is_output) {
            messages_.push_back("Links must connect an output port to an input port.");
            return;
        }

        node_editor::EditorEdge edge;
        edge.ui_id = graph_.next_edge_ui_id++;
        edge.source_node_id = source->node_id;
        edge.source_port_id = source->port_id;
        edge.target_node_id = target->node_id;
        edge.target_port_id = target->port_id;

        auto candidate = graph_;
        candidate.edges.push_back(edge);
        const auto errors = node_editor::validate_editor_graph(candidate, descriptors_);
        if (!errors.empty()) {
            messages_.push_back("Rejected link: " + errors.front());
            return;
        }

        graph_.edges.push_back(std::move(edge));
        dirty_ = true;
        refresh_validation();
    }

    void handle_link_deletion()
    {
        int link_id = 0;
        if (!ImNodes::IsLinkDestroyed(&link_id)) {
            return;
        }

        const auto old_size = graph_.edges.size();
        graph_.edges.erase(
            std::remove_if(graph_.edges.begin(), graph_.edges.end(), [link_id](const auto& edge) {
                return edge.ui_id == link_id;
            }),
            graph_.edges.end()
        );
        if (graph_.edges.size() != old_size) {
            dirty_ = true;
            refresh_validation();
        }
    }

    void delete_selected_node()
    {
        if (selected_node_id_.empty()) {
            return;
        }

        graph_.nodes.erase(
            std::remove_if(graph_.nodes.begin(), graph_.nodes.end(), [this](const auto& node) {
                if (node.node_id == selected_node_id_) {
                    positioned_node_ids_.erase(node.ui_id);
                    return true;
                }
                return false;
            }),
            graph_.nodes.end()
        );
        graph_.edges.erase(
            std::remove_if(graph_.edges.begin(), graph_.edges.end(), [this](const auto& edge) {
                return edge.source_node_id == selected_node_id_ || edge.target_node_id == selected_node_id_;
            }),
            graph_.edges.end()
        );
        selected_node_id_.clear();
        dirty_ = true;
        refresh_validation();
    }

    void run_once_from_gui()
    {
        try_call([this]() {
            refresh_validation();
            if (!validation_messages_.empty()) {
                messages_.push_back("Graph validation failed: " + validation_messages_.front());
                return;
            }

            if (!runtime_ || dirty_) {
                stop_runtime();
                runtime_ = registry_->create_graph(node_editor::make_graph_config(graph_));
                dirty_ = false;
                messages_.push_back("Graph runtime rebuilt.");
            }

            apply_float_properties(*runtime_, graph_, descriptors_);
            const auto job = runtime_->submit_run();
            const auto result = runtime_->wait(job);
            if (result.result != PS_OK) {
                messages_.push_back("Graph run failed at node: " + result.failed_node_id);
                return;
            }

            messages_.push_back("Graph run completed.");
            const auto summaries = read_output_summaries(*runtime_, graph_, descriptors_);
            messages_.insert(messages_.end(), summaries.begin(), summaries.end());
            trim_messages();
        });
    }

    void stop_runtime_internal()
    {
        if (runtime_) {
            runtime_->stop();
            runtime_.reset();
        }
    }

    void stop_runtime()
    {
        const bool was_running = running_continuously_ || paused_;
        const auto count = continuous_run_count_;
        running_continuously_ = false;
        paused_ = false;
        continuous_job_.reset();
        continuous_run_count_ = 0;
        stop_runtime_internal();
        if (was_running) {
            messages_.push_back("Continuous run stopped after " + std::to_string(count) + " iteration(s).");
            std::cout << "Continuous run stopped after " << count << " iteration(s).\n" << std::flush;
        } else {
            messages_.push_back("Graph runtime stopped.");
        }
        trim_messages();
    }

    void pause_continuous_run()
    {
        running_continuously_ = false;
        paused_ = true;
        continuous_job_.reset();
        messages_.push_back("Paused after " + std::to_string(continuous_run_count_) + " iteration(s).");
        trim_messages();
        std::cout << "Continuous run paused after " << continuous_run_count_ << " iteration(s).\n" << std::flush;
    }

    void resume_continuous_run()
    {
        paused_ = false;
        running_continuously_ = true;
        messages_.push_back("Continuous run resumed.");
        trim_messages();
        std::cout << "Continuous run resumed.\n" << std::flush;
    }

    void start_continuous_run()
    {
        try_call([this]() {
            refresh_validation();
            if (!validation_messages_.empty()) {
                messages_.push_back("Graph validation failed: " + validation_messages_.front());
                return;
            }
            if (!runtime_ || dirty_) {
                stop_runtime_internal();
                runtime_ = registry_->create_graph(node_editor::make_graph_config(graph_));
                dirty_ = false;
            }
            running_continuously_ = true;
            continuous_job_.reset();
            continuous_run_count_ = 0;
            messages_.push_back("Continuous run started.");
            trim_messages();
            std::cout << "Continuous run started.\n" << std::flush;
        });
    }

    void tick_continuous_run()
    {
        if (!running_continuously_) {
            return;
        }

        if (dirty_) {
            continuous_job_.reset(); // old runtime is being destroyed; handle is no longer valid
            try_call([this]() {
                stop_runtime_internal();
                runtime_ = registry_->create_graph(node_editor::make_graph_config(graph_));
                dirty_ = false;
            });
            if (!runtime_) {
                running_continuously_ = false;
                return;
            }
        }

        if (continuous_job_) {
            const auto s = runtime_->status(*continuous_job_);
            if (s == pluginsystem::GraphJobStatus::pending || s == pluginsystem::GraphJobStatus::running) {
                return;
            }
            if (s == pluginsystem::GraphJobStatus::completed) {
                const auto r = runtime_->result(*continuous_job_);
                if (r && r->result != PS_OK) {
                    messages_.push_back("Continuous run failed at node: " + r->failed_node_id);
                    trim_messages();
                    std::cout << "[FAIL] at node: " << r->failed_node_id << '\n' << std::flush;
                    running_continuously_ = false;
                    continuous_job_.reset();
                    return;
                }
                ++continuous_run_count_;
                std::cout << "[Run " << continuous_run_count_ << "] completed\n" << std::flush;
            } else {
                try_call([this]() { runtime_->wait(*continuous_job_); });
                running_continuously_ = false;
                continuous_job_.reset();
                return;
            }
            continuous_job_.reset();
            return;
        }

        try_call([this]() {
            apply_float_properties(*runtime_, graph_, descriptors_);
            continuous_job_ = runtime_->submit_run();
        });
        if (!continuous_job_) {
            running_continuously_ = false;
        }
    }

    template <typename Fn>
    void try_call(Fn&& fn)
    {
        try {
            fn();
        } catch (const std::exception& error) {
            messages_.push_back(error.what());
            trim_messages();
        }
    }

    void trim_messages()
    {
        constexpr std::size_t max_messages = 80;
        if (messages_.size() > max_messages) {
            messages_.erase(messages_.begin(), messages_.begin() + static_cast<std::ptrdiff_t>(messages_.size() - max_messages));
        }
    }

    std::vector<std::filesystem::path> current_libraries() const
    {
        return graph_.plugin_libraries;
    }

    node_editor::EditorGraph graph_;
    std::filesystem::path graph_path_;
    std::string graph_path_text_;
    std::unique_ptr<pluginsystem::PluginRegistry> registry_;
    node_editor::DescriptorIndex descriptors_;
    std::unique_ptr<pluginsystem::GraphRuntime> runtime_;
    std::unordered_map<int, PinRef> pin_refs_;
    std::unordered_set<int> positioned_node_ids_;
    std::vector<std::string> validation_messages_;
    std::vector<std::string> messages_;
    std::string selected_node_id_;
    bool dirty_{true};
    bool running_continuously_{false};
    bool paused_{false};
    std::optional<pluginsystem::GraphJobHandle> continuous_job_{};
    std::uint64_t continuous_run_count_{0};
};

int run_gui(CliOptions options)
{
    if (!glfwInit()) {
        throw std::runtime_error{"Failed to initialize GLFW."};
    }

    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    GLFWwindow* window = glfwCreateWindow(1500, 900, "PluginSystem Node Editor", nullptr, nullptr);
    if (window == nullptr) {
        glfwTerminate();
        throw std::runtime_error{"Failed to create GLFW window."};
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImNodes::CreateContext();

    ImGui::StyleColorsDark();
    ImNodes::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    NodeEditorApp app{std::move(options)};

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        app.draw();

        ImGui::Render();
        int display_width = 0;
        int display_height = 0;
        glfwGetFramebufferSize(window, &display_width, &display_height);
        glViewport(0, 0, display_width, display_height);
        glClearColor(0.08F, 0.08F, 0.09F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImNodes::DestroyContext();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    try {
        auto options = parse_cli(argc, argv);
        if (options.headless_run) {
            return run_headless(options);
        }
        return run_gui(std::move(options));
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        print_usage();
        return 1;
    }
}
