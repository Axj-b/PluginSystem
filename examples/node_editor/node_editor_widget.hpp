#pragma once

#include "editor_model.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class NodeEditorApp {
public:
    explicit NodeEditorApp(
        std::vector<std::filesystem::path> plugin_libraries,
        std::optional<std::filesystem::path> graph_path = {}
    );
    ~NodeEditorApp() = default;

    NodeEditorApp(const NodeEditorApp&) = delete;
    NodeEditorApp& operator=(const NodeEditorApp&) = delete;
    NodeEditorApp(NodeEditorApp&&) = delete;
    NodeEditorApp& operator=(NodeEditorApp&&) = delete;

    void draw();

private:
    struct PinRef {
        std::string node_id;
        std::string port_id;
        bool is_output{false};
    };

    void reload_plugins();
    void refresh_validation();
    void draw_top_bar();
    void draw_palette();
    void draw_canvas();
    void draw_inspector();
    void draw_bottom_panel();
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
    void try_call(std::function<void()> fn);
    void trim_messages();
    std::vector<std::filesystem::path> current_libraries() const;

    pluginsystem::examples::node_editor::EditorGraph graph_;
    std::filesystem::path graph_path_;
    std::string graph_path_text_;
    std::unique_ptr<pluginsystem::PluginRegistry> registry_;
    pluginsystem::examples::node_editor::DescriptorIndex descriptors_;
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
    std::size_t step_cursor_{0};
    std::vector<std::string> step_node_ids_;
};
