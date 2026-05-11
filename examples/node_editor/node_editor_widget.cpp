#include "node_editor_widget.hpp"

#include <dll_plugin_provider.hpp>
#include <recorder_plugins.hpp>

#include <imgui.h>
#include <imnodes.h>

#include <array>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace node_editor = pluginsystem::examples::node_editor;

namespace {

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
        registry->add_provider(std::make_unique<pluginsystem::DllPluginProvider>(library));
    }
    return registry;
}

bool is_integer_type(std::string_view type_name)
{
    return type_name == "int8_t" || type_name == "int16_t" || type_name == "int32_t"
        || type_name == "int64_t" || type_name == "uint8_t" || type_name == "uint16_t"
        || type_name == "uint32_t" || type_name == "uint64_t";
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
            summaries.push_back(
                node.node_id + "." + port.id
                + ": " + port.type_name
                + ", version=" + std::to_string(channel.version())
            );
        }
    }
    return summaries;
}

} // namespace

NodeEditorApp::NodeEditorApp(
    std::vector<std::filesystem::path> plugin_libraries,
    std::optional<std::filesystem::path> graph_path
)
    : graph_path_{graph_path.value_or("node_editor_graph.json")}
{
    if (graph_path) {
        graph_ = node_editor::load_editor_graph(*graph_path);
    }
    graph_.plugin_libraries = node_editor::merge_plugin_libraries(graph_.plugin_libraries, plugin_libraries);
    graph_path_text_ = graph_path_.string();
    reload_plugins();
    refresh_validation();
}

void NodeEditorApp::draw()
{
    tick_continuous_run();
    draw_top_bar();
    draw_palette();
    draw_canvas();
    draw_inspector();
    draw_bottom_panel();
    draw_plugin_windows();
}

void NodeEditorApp::draw_plugin_windows()
{
    if (runtime_) {
        runtime_->invoke_all("Render", ImGui::GetCurrentContext());
    }
}

void NodeEditorApp::reload_plugins()
{
    running_continuously_ = false;
    paused_ = false;
    continuous_job_.reset();
    continuous_run_count_ = 0;
    stop_runtime_internal();
    registry_ = make_registry(graph_.plugin_libraries);
    descriptors_ = node_editor::make_descriptor_index(registry_->discover_plugins());

    // Register one universal multi-port recorder and a blank replay template.
    registry_->register_builtin(pluginsystem::builtins::make_recorder());
    registry_->register_builtin(pluginsystem::builtins::make_replay(
        "pluginsystem.builtin.replay", {}));
    descriptors_ = node_editor::make_descriptor_index(registry_->discover_plugins());

    dirty_ = true;
    messages_.push_back("Discovered " + std::to_string(descriptors_.descriptors.size()) + " plugin(s).");
}

void NodeEditorApp::refresh_validation()
{
    validation_messages_ = node_editor::validate_editor_graph(graph_, descriptors_);
}

void NodeEditorApp::try_update_replay_v2_node(node_editor::EditorNode& node)
{
    const auto it = node.string_properties.find("InputPath");
    if (it == node.string_properties.end() || it->second.empty()) return;

    auto port_infos = pluginsystem::builtins::read_recording_ports(it->second);
    if (port_infos.empty()) return;

    // Sanitize a string for use in a plugin id
    auto sanitize = [](const std::string& s) {
        std::string r;
        for (const char c : s) {
            r += (std::isalnum(static_cast<unsigned char>(c)) || c == '_') ? c : '_';
        }
        return r.empty() ? "unnamed" : r;
    };

    // Canonical id encodes structure (byte sizes) + port names so each unique
    // recording layout gets its own typed descriptor
    std::string new_plugin_id = "pluginsystem.builtin.replay." + std::to_string(port_infos.size());
    for (const auto& info : port_infos) {
        new_plugin_id += "." + std::to_string(info.byte_size);
    }
    for (const auto& info : port_infos) {
        new_plugin_id += "." + sanitize(info.port_name);
    }

    if (node.plugin_id == new_plugin_id) return;

    // Register typed replay variant if not already known
    if (node_editor::find_descriptor(descriptors_, new_plugin_id) == nullptr) {
        registry_->register_builtin(pluginsystem::builtins::make_replay(new_plugin_id, port_infos));
        descriptors_ = node_editor::make_descriptor_index(registry_->discover_plugins());
    }

    // Remove edges from old output ports (they are now invalid)
    graph_.edges.erase(
        std::remove_if(graph_.edges.begin(), graph_.edges.end(),
            [&](const auto& edge) { return edge.source_node_id == node.node_id; }),
        graph_.edges.end()
    );

    node.plugin_id = new_plugin_id;
    if (node.string_properties.find("InputPath") == node.string_properties.end()) {
        node.string_properties["InputPath"] = it->second;
    }
    if (node.int_properties.find("Loop") == node.int_properties.end()) {
        node.int_properties["Loop"] = 0;
    }
    refresh_validation();
}

void NodeEditorApp::draw_top_bar()
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
    {
        const bool can_step = can_run && !running_continuously_;
        if (!can_step) { ImGui::BeginDisabled(); }
        std::string step_label = "Step";
        if (!step_node_ids_.empty()) {
            step_label += " [" + step_node_ids_[step_cursor_] + "]";
        }
        if (ImGui::Button(step_label.c_str())) { step_node(); }
        if (!can_step) { ImGui::EndDisabled(); }
    }

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
    draw_zoom_controls();
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

void NodeEditorApp::draw_zoom_controls()
{
    constexpr float min_zoom = 0.35F;
    constexpr float max_zoom = 2.50F;

    float zoom = ImNodes::EditorContextGetZoom();
    if (ImGui::Button("-##zoom_out")) {
        ImNodes::EditorContextSetZoom(std::clamp(zoom / 1.15F, min_zoom, max_zoom));
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(96.0F);
    float zoom_percent = zoom * 100.0F;
    if (ImGui::SliderFloat("Zoom", &zoom_percent, min_zoom * 100.0F, max_zoom * 100.0F, "%.0f%%")) {
        ImNodes::EditorContextSetZoom(std::clamp(zoom_percent / 100.0F, min_zoom, max_zoom));
    }
    ImGui::SameLine();
    zoom = ImNodes::EditorContextGetZoom();
    if (ImGui::Button("+##zoom_in")) {
        ImNodes::EditorContextSetZoom(std::clamp(zoom * 1.15F, min_zoom, max_zoom));
    }
    ImGui::SameLine();
    if (ImGui::Button("100%##zoom_reset")) {
        ImNodes::EditorContextSetZoom(1.0F);
    }
}

void NodeEditorApp::draw_palette()
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

void NodeEditorApp::draw_canvas()
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
            if (descriptor->expandable_inputs) {
                // Dynamic: show connected ports + 1 unconnected (minimum 1)
                int last_connected = -1;
                for (const auto& edge : graph_.edges) {
                    if (edge.target_node_id != node.node_id) continue;
                    const auto& pid = edge.target_port_id;
                    if (pid.size() > 5 && pid.rfind("Input", 0) == 0) {
                        try { last_connected = std::max(last_connected, std::stoi(pid.substr(5))); }
                        catch (...) {}
                    }
                }
                int total_inputs = 0;
                for (const auto& p : descriptor->ports) {
                    if (p.direction == pluginsystem::PortDirection::input) ++total_inputs;
                }
                const int visible_count = std::min(std::max(last_connected + 2, 1), total_inputs);
                int drawn = 0;
                for (const auto& port : descriptor->ports) {
                    if (port.direction != pluginsystem::PortDirection::input) continue;
                    if (drawn >= visible_count) break;
                    ++drawn;
                    const auto id = pin_id(node.node_id, port.id, false);
                    pin_refs_[id] = PinRef{node.node_id, port.id, false};
                    ImNodes::BeginInputAttribute(id);
                    // Show the connected source port name instead of the generic input id
                    std::string label = port.id;
                    for (const auto& edge : graph_.edges) {
                        if (edge.target_node_id != node.node_id || edge.target_port_id != port.id) continue;
                        for (const auto& src_node : graph_.nodes) {
                            if (src_node.node_id != edge.source_node_id) continue;
                            const auto* src_desc = node_editor::find_descriptor(descriptors_, src_node.plugin_id);
                            if (src_desc) {
                                for (const auto& src_port : src_desc->ports) {
                                    if (src_port.id == edge.source_port_id && !src_port.name.empty()) {
                                        label = src_port.name;
                                    }
                                }
                            }
                            break;
                        }
                        break;
                    }
                    ImGui::Text("%s", label.c_str());
                    ImNodes::EndInputAttribute();
                }
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
            }

            for (const auto& port : descriptor->ports) {
                if (port.direction != pluginsystem::PortDirection::output) {
                    continue;
                }
                const auto id = pin_id(node.node_id, port.id, true);
                pin_refs_[id] = PinRef{node.node_id, port.id, true};
                ImNodes::BeginOutputAttribute(id);
                ImGui::Indent(50.0F);
                const auto& label = port.name.empty() ? port.id : port.name;
                ImGui::Text("%s", label.c_str());
                ImNodes::EndOutputAttribute();
            }

            // Nodes with no ports need at least one content item to avoid imgui layout assertion
            if (descriptor->ports.empty()) {
                ImGui::TextDisabled("Set InputPath to configure");
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

    // Del key: delete selected links and/or selected nodes
    if (ImGui::IsKeyPressed(ImGuiKey_Delete) && !ImGui::IsAnyItemActive()) {
        const int n_links = ImNodes::NumSelectedLinks();
        if (n_links > 0) {
            std::vector<int> sel(n_links);
            ImNodes::GetSelectedLinks(sel.data());
            graph_.edges.erase(
                std::remove_if(graph_.edges.begin(), graph_.edges.end(), [&](const auto& e) {
                    return std::find(sel.begin(), sel.end(), e.ui_id) != sel.end();
                }),
                graph_.edges.end()
            );
            dirty_ = true;
            refresh_validation();
        }

        const int n_nodes = ImNodes::NumSelectedNodes();
        if (n_nodes > 0) {
            std::vector<int> sel(n_nodes);
            ImNodes::GetSelectedNodes(sel.data());
            std::unordered_set<std::string> removed_ids;
            graph_.nodes.erase(
                std::remove_if(graph_.nodes.begin(), graph_.nodes.end(), [&](const auto& n) {
                    if (std::find(sel.begin(), sel.end(), n.ui_id) != sel.end()) {
                        positioned_node_ids_.erase(n.ui_id);
                        removed_ids.insert(n.node_id);
                        return true;
                    }
                    return false;
                }),
                graph_.nodes.end()
            );
            graph_.edges.erase(
                std::remove_if(graph_.edges.begin(), graph_.edges.end(), [&](const auto& e) {
                    return removed_ids.count(e.source_node_id) || removed_ids.count(e.target_node_id);
                }),
                graph_.edges.end()
            );
            if (removed_ids.count(selected_node_id_)) selected_node_id_.clear();
            dirty_ = true;
            refresh_validation();
        }
    }

    handle_link_creation();
    handle_link_deletion();

    ImGui::End();
}

void NodeEditorApp::draw_inspector()
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
            if (!property.writable) {
                ImGui::TextDisabled("%s: %s (read-only)", property.id.c_str(), property.type_name.c_str());
                continue;
            }
            if (property.type_name == "bool" && property.byte_size == sizeof(bool)) {
                auto& value = node->bool_properties[property.id];
                ImGui::Checkbox(property.id.c_str(), &value);
            } else if (property.type_name == "float" && property.byte_size == sizeof(float)) {
                auto& dval = node->float_properties[property.id];
                float fval = static_cast<float>(dval);
                if (property.min_value && property.max_value) {
                    if (ImGui::SliderFloat(property.id.c_str(), &fval,
                            static_cast<float>(*property.min_value),
                            static_cast<float>(*property.max_value)))
                        dval = static_cast<double>(fval);
                } else {
                    if (ImGui::InputFloat(property.id.c_str(), &fval))
                        dval = static_cast<double>(fval);
                }
            } else if (property.type_name == "double" && property.byte_size == sizeof(double)) {
                auto& value = node->float_properties[property.id];
                if (property.min_value && property.max_value) {
                    const double dmin = *property.min_value;
                    const double dmax = *property.max_value;
                    ImGui::SliderScalar(property.id.c_str(), ImGuiDataType_Double, &value, &dmin, &dmax);
                } else {
                    ImGui::InputDouble(property.id.c_str(), &value);
                }
            } else if (is_integer_type(property.type_name)) {
                auto& value = node->int_properties[property.id];
                if (!property.enum_options.empty()) {
                    int current = static_cast<int>(value);
                    std::string items;
                    for (const auto& opt : property.enum_options) { items += opt; items += '\0'; }
                    items += '\0';
                    if (ImGui::Combo(property.id.c_str(), &current, items.c_str()))
                        value = static_cast<std::int64_t>(current);
                } else if (property.min_value && property.max_value) {
                    const std::int64_t imin = static_cast<std::int64_t>(*property.min_value);
                    const std::int64_t imax = static_cast<std::int64_t>(*property.max_value);
                    ImGui::SliderScalar(property.id.c_str(), ImGuiDataType_S64, &value, &imin, &imax);
                } else {
                    ImGui::InputScalar(property.id.c_str(), ImGuiDataType_S64, &value);
                }
            } else if (property.type_name.rfind("char[", 0) == 0 && property.byte_size > 0) {
                auto& value = node->string_properties[property.id];
                if (input_text_string(property.id.c_str(), value)) {
                    dirty_ = true;
                    if (property.id == "InputPath" &&
                        node->plugin_id.rfind("pluginsystem.builtin.replay", 0) == 0) {
                        try_update_replay_v2_node(*node);
                        ImGui::End();
                        return; // descriptor is stale; descriptors_ was rebuilt inside the call
                    }
                }
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

void NodeEditorApp::draw_bottom_panel()
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

void NodeEditorApp::draw_entrypoint_combo(const char* label, const pluginsystem::PluginDescriptor& descriptor, std::string& value)
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

void NodeEditorApp::add_node(const pluginsystem::PluginDescriptor& descriptor)
{
    node_editor::EditorNode node;
    node.ui_id = graph_.next_node_ui_id++;
    node.plugin_id = descriptor.id;
    node.node_id = make_unique_node_id(graph_, descriptor.id);
    node.instance_name = node.node_id;
    node.x = 40.0F + static_cast<float>(graph_.nodes.size()) * 40.0F;
    node.y = 40.0F + static_cast<float>(graph_.nodes.size()) * 40.0F;
    for (const auto& property : descriptor.properties) {
        if (property.type_name == "bool") {
            node.bool_properties[property.id] =
                property.default_value ? (*property.default_value != 0.0) : false;
        } else if (property.type_name == "float" || property.type_name == "double") {
            node.float_properties[property.id] = property.default_value.value_or(0.0);
        } else if (is_integer_type(property.type_name)) {
            node.int_properties[property.id] =
                property.default_value ? static_cast<std::int64_t>(*property.default_value) : 0;
        } else if (property.type_name.rfind("char[", 0) == 0) {
            node.string_properties[property.id] = "";
        }
    }
    selected_node_id_ = node.node_id;
    graph_.nodes.push_back(std::move(node));
    dirty_ = true;
    refresh_validation();
}

void NodeEditorApp::handle_link_creation()
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

void NodeEditorApp::handle_link_deletion()
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

void NodeEditorApp::delete_selected_node()
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

void NodeEditorApp::run_once_from_gui()
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

        node_editor::apply_node_properties(*runtime_, graph_, descriptors_);
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

void NodeEditorApp::step_node()
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

        if (step_node_ids_.empty()) {
            step_node_ids_ = runtime_->topological_node_ids();
            step_cursor_ = 0;
        }

        if (step_node_ids_.empty()) {
            return;
        }

        const auto& node_id = step_node_ids_[step_cursor_];
        node_editor::apply_node_properties(*runtime_, graph_, descriptors_);
        const auto job = runtime_->submit_single_node(node_id);
        const auto result = runtime_->wait(job);

        if (result.result != PS_OK) {
            messages_.push_back("Step failed at node: " + node_id);
        } else {
            messages_.push_back("Stepped: " + node_id);
        }

        step_cursor_ = (step_cursor_ + 1) % step_node_ids_.size();
        trim_messages();
    });
}

void NodeEditorApp::reset_step()
{
    step_cursor_ = 0;
    step_node_ids_.clear();
}

void NodeEditorApp::stop_runtime_internal()
{
    if (runtime_) {
        runtime_->stop();
        runtime_.reset();
    }
    reset_step();
}

void NodeEditorApp::stop_runtime()
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

void NodeEditorApp::pause_continuous_run()
{
    running_continuously_ = false;
    paused_ = true;
    continuous_job_.reset();
    messages_.push_back("Paused after " + std::to_string(continuous_run_count_) + " iteration(s).");
    trim_messages();
    std::cout << "Continuous run paused after " << continuous_run_count_ << " iteration(s).\n" << std::flush;
}

void NodeEditorApp::resume_continuous_run()
{
    paused_ = false;
    running_continuously_ = true;
    messages_.push_back("Continuous run resumed.");
    trim_messages();
    std::cout << "Continuous run resumed.\n" << std::flush;
}

void NodeEditorApp::start_continuous_run()
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

void NodeEditorApp::tick_continuous_run()
{
    if (!running_continuously_) {
        return;
    }

    if (dirty_) {
        continuous_job_.reset();
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
        node_editor::apply_node_properties(*runtime_, graph_, descriptors_);
        continuous_job_ = runtime_->submit_run();
    });
    if (!continuous_job_) {
        running_continuously_ = false;
    }
}

void NodeEditorApp::try_call(std::function<void()> fn)
{
    try {
        fn();
    } catch (const std::exception& error) {
        messages_.push_back(error.what());
        trim_messages();
    }
}

void NodeEditorApp::trim_messages()
{
    constexpr std::size_t max_messages = 80;
    if (messages_.size() > max_messages) {
        messages_.erase(messages_.begin(), messages_.begin() + static_cast<std::ptrdiff_t>(messages_.size() - max_messages));
    }
}

std::vector<std::filesystem::path> NodeEditorApp::current_libraries() const
{
    return graph_.plugin_libraries;
}
