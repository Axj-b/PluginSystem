#pragma once

#include <pluginsystem/plugin_manager.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace pluginsystem::examples::node_editor {

struct EditorNode {
    int ui_id{0};
    std::string node_id;
    std::string plugin_id;
    std::string instance_name;
    std::string process_entrypoint{"Process"};
    std::string start_entrypoint{"Start"};
    std::string stop_entrypoint{"Stop"};
    float x{0.0F};
    float y{0.0F};
    std::unordered_map<std::string, double> float_properties;
    std::unordered_map<std::string, std::int64_t> int_properties;
    std::unordered_map<std::string, bool> bool_properties;
    std::unordered_map<std::string, std::string> string_properties;
};

struct EditorEdge {
    int ui_id{0};
    std::string source_node_id;
    std::string source_port_id;
    std::string target_node_id;
    std::string target_port_id;
};

struct EditorGraph {
    int schema_version{1};
    std::string blueprint_name{"NodeEditorGraph"};
    std::filesystem::path runtime_directory{"pluginsystem_runtime"};
    std::vector<std::filesystem::path> plugin_libraries;
    std::vector<EditorNode> nodes;
    std::vector<EditorEdge> edges;
    int next_node_ui_id{1};
    int next_edge_ui_id{1};
};

struct DescriptorIndex {
    std::vector<PluginDescriptor> descriptors;
    std::unordered_map<std::string, std::size_t> by_plugin_id;
};

EditorGraph load_editor_graph(const std::filesystem::path& path);
void save_editor_graph(const std::filesystem::path& path, const EditorGraph& graph);
DescriptorIndex make_descriptor_index(std::vector<PluginDescriptor> descriptors);
std::vector<std::string> validate_editor_graph(const EditorGraph& graph, const DescriptorIndex& descriptors);
GraphConfig make_graph_config(const EditorGraph& graph);
std::optional<std::string> register_replay_plugin_for_path(
    PluginRegistry& registry,
    const std::string& recording_path
);
std::size_t prepare_replay_plugins_for_graph(PluginRegistry& registry, EditorGraph& graph);

const PluginDescriptor* find_descriptor(const DescriptorIndex& descriptors, std::string_view plugin_id);
const PortDescriptor* find_port(const PluginDescriptor& descriptor, std::string_view port_id);
const PropertyDescriptor* find_property(const PluginDescriptor& descriptor, std::string_view property_id);
const EditorNode* find_node(const EditorGraph& graph, std::string_view node_id);
EditorNode* find_node(EditorGraph& graph, std::string_view node_id);

std::vector<std::filesystem::path> merge_plugin_libraries(
    const std::vector<std::filesystem::path>& from_graph,
    const std::vector<std::filesystem::path>& from_cli
);

void apply_node_properties(
    pluginsystem::GraphRuntime& runtime,
    const EditorGraph& graph,
    const DescriptorIndex& descriptors
);

} // namespace pluginsystem::examples::node_editor
