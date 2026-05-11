#pragma once

#include "editor_model.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace pluginsystem::examples::node_editor {

enum class NodeEditorMessageLevel {
    info,
    warning,
    error,
};

struct NodeEditorConfig {
    std::string blueprint_name{"NodeEditorGraph"};
    std::filesystem::path runtime_directory{"pluginsystem_runtime"};
    std::filesystem::path graph_directory{"."};
    std::filesystem::path default_graph_path{"node_editor_graph.json"};
    std::optional<std::filesystem::path> initial_graph_path;
    std::vector<std::filesystem::path> plugin_libraries;
    std::vector<std::filesystem::path> plugin_directories;
    std::uint32_t worker_count{1};
    bool register_default_plugins{true};
};

struct NodeEditorCallbacks {
    std::function<void(PluginRegistry&)> register_plugins;
    std::function<std::optional<std::filesystem::path>()> load_graph_path;
    std::function<std::optional<std::filesystem::path>()> save_graph_path;
    std::function<void(NodeEditorMessageLevel, std::string_view)> log_message;
};

class NodeEditorWidget {
public:
    explicit NodeEditorWidget(NodeEditorConfig config = {}, NodeEditorCallbacks callbacks = {});
    NodeEditorWidget(
        std::vector<std::filesystem::path> plugin_libraries,
        std::optional<std::filesystem::path> graph_path = {}
    );
    ~NodeEditorWidget() = default;

    NodeEditorWidget(const NodeEditorWidget&) = delete;
    NodeEditorWidget& operator=(const NodeEditorWidget&) = delete;
    NodeEditorWidget(NodeEditorWidget&&) = delete;
    NodeEditorWidget& operator=(NodeEditorWidget&&) = delete;

    void OnImGuiRender();

    void ReloadPlugins();
    void LoadGraph(const std::filesystem::path& path);
    void SaveGraph(const std::filesystem::path& path);
    void RunOnce();
    void StartContinuousRun();
    void PauseContinuousRun();
    void ResumeContinuousRun();
    void StopRuntime();
    void StepNode();

    const EditorGraph& graph() const noexcept { return graph_; }
    EditorGraph& graph() noexcept { return graph_; }
    const DescriptorIndex& descriptors() const noexcept { return descriptors_; }
    const std::vector<std::string>& validation_messages() const noexcept { return validation_messages_; }
    const std::vector<std::string>& log_messages() const noexcept { return messages_; }
    bool dirty() const noexcept { return dirty_; }
    bool has_runtime() const noexcept { return runtime_ != nullptr; }
    bool running_continuously() const noexcept { return running_continuously_; }

private:
    struct PinRef {
        std::string node_id;
        std::string port_id;
        bool is_output{false};
    };

    static NodeEditorConfig make_legacy_config(
        std::vector<std::filesystem::path> plugin_libraries,
        std::optional<std::filesystem::path> graph_path
    );

    void reload_plugins();
    void refresh_validation();
    void draw_top_bar();
    void draw_zoom_controls();
    void draw_palette();
    void draw_canvas();
    void draw_inspector();
    void draw_bottom_panel();
    void draw_plugin_windows();
    void draw_entrypoint_combo(const char* label, const pluginsystem::PluginDescriptor& descriptor, std::string& value);
    void add_node(const pluginsystem::PluginDescriptor& descriptor);
    void handle_link_creation();
    void handle_link_deletion();
    void delete_selected_node();
    void step_node();
    void reset_step();
    void run_once_from_gui();
    void stop_runtime_internal();
    void stop_runtime();
    void pause_continuous_run();
    void resume_continuous_run();
    void start_continuous_run();
    void tick_continuous_run();
    GraphConfig make_runtime_graph_config() const;
    void try_call(std::function<void()> fn);
    void try_update_replay_v2_node(EditorNode& node);
    void log(NodeEditorMessageLevel level, std::string message);
    std::vector<std::filesystem::path> configured_libraries() const;
    void trim_messages();
    std::vector<std::filesystem::path> current_libraries() const;

    NodeEditorConfig config_;
    NodeEditorCallbacks callbacks_;
    EditorGraph graph_;
    std::filesystem::path graph_path_;
    std::string graph_path_text_;
    std::unique_ptr<PluginRegistry> registry_;
    DescriptorIndex descriptors_;
    std::unique_ptr<GraphRuntime> runtime_;
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
    std::size_t step_cursor_{0};
    std::vector<std::string> step_node_ids_;
};

} // namespace pluginsystem::examples::node_editor
