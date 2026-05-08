#include "node_editor_widget.hpp"

#include "../PipelineSamples.h"

#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imnodes.h>

#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace node_editor = pluginsystem::examples::node_editor;

namespace {

struct CliOptions {
    bool headless_run{false};
    std::optional<std::filesystem::path> graph_path;
    std::vector<std::filesystem::path> plugin_libraries;
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

std::unique_ptr<pluginsystem::PluginRegistry> make_registry(const std::vector<std::filesystem::path>& libraries)
{
    auto registry = std::make_unique<pluginsystem::PluginRegistry>();
    for (const auto& library : libraries) {
        registry->add_dll_plugin(library);
    }
    return registry;
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
    node_editor::apply_node_properties(*runtime, graph, descriptors);
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

    NodeEditorApp app{std::move(options.plugin_libraries), options.graph_path};

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
