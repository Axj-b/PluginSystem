#include "editor_model.hpp"

#include <recorder_plugins.hpp>

#include <nlohmann/json.hpp>
#include <pluginsystem/graph.hpp>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <thread>
#include <deque>
#include <fstream>
#include <optional>
#include <set>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace pluginsystem::examples::node_editor {
namespace {

using json = nlohmann::json;

std::string port_key(std::string_view node_id, std::string_view port_id)
{
    return std::string{node_id} + "." + std::string{port_id};
}

bool has_entrypoint(const PluginDescriptor& descriptor, std::string_view entrypoint_id)
{
    return std::any_of(descriptor.entrypoints.begin(), descriptor.entrypoints.end(), [entrypoint_id](const auto& entrypoint) {
        return entrypoint.id == entrypoint_id;
    });
}

bool is_replay_plugin_id(std::string_view plugin_id)
{
    constexpr std::string_view replay_prefix = "pluginsystem.builtin.replay";
    return plugin_id == replay_prefix
        || (plugin_id.size() > replay_prefix.size()
            && plugin_id.substr(0, replay_prefix.size()) == replay_prefix
            && plugin_id[replay_prefix.size()] == '.');
}

bool is_replay_status_property(std::string_view property_id)
{
    return property_id == "CurrentTimestampNs"
        || property_id == "NextTimestampNs"
        || property_id == "EndReached";
}

std::string sanitize_replay_id_part(std::string_view value)
{
    std::string result;
    result.reserve(value.size());
    for (const auto character : value) {
        const auto byte = static_cast<unsigned char>(character);
        result.push_back(std::isalnum(byte) || character == '_' ? character : '_');
    }
    return result.empty() ? "unnamed" : result;
}

std::string replay_plugin_id_for_ports(const std::vector<pluginsystem::builtins::RecordedPortInfo>& port_infos)
{
    std::string plugin_id = "pluginsystem.builtin.replay." + std::to_string(port_infos.size());
    for (const auto& info : port_infos) {
        plugin_id += "." + std::to_string(info.byte_size);
    }
    for (const auto& info : port_infos) {
        plugin_id += "." + sanitize_replay_id_part(info.type_name);
    }
    for (const auto& info : port_infos) {
        plugin_id += "." + sanitize_replay_id_part(info.port_name);
    }
    for (const auto& info : port_infos) {
        plugin_id += info.access_mode == PortAccessMode::buffered_latest ? ".latest" : ".direct";
    }
    return plugin_id;
}

bool registry_contains_plugin(PluginRegistry& registry, std::string_view plugin_id)
{
    const auto descriptors = registry.discover_plugins();
    return std::any_of(descriptors.begin(), descriptors.end(), [plugin_id](const auto& descriptor) {
        return descriptor.id == plugin_id;
    });
}

void add_unique_path(std::vector<std::filesystem::path>& paths, std::filesystem::path path)
{
    path = path.lexically_normal();
    const auto already_exists = std::any_of(paths.begin(), paths.end(), [&path](const auto& existing) {
        return existing.lexically_normal() == path;
    });
    if (!already_exists && !path.empty()) {
        paths.push_back(std::move(path));
    }
}

} // namespace

EditorGraph load_editor_graph(const std::filesystem::path& path)
{
    std::ifstream input{path};
    if (!input) {
        throw std::runtime_error{"Could not open graph JSON for reading: " + path.string()};
    }

    json data;
    input >> data;

    EditorGraph graph;
    graph.schema_version = data.value("version", 1);
    if (graph.schema_version != 1) {
        throw std::runtime_error{"Unsupported graph JSON version: " + std::to_string(graph.schema_version)};
    }

    graph.blueprint_name = data.value("blueprint_name", graph.blueprint_name);
    graph.runtime_directory = data.value("runtime_directory", graph.runtime_directory.string());

    for (const auto& library : data.value("plugin_libraries", json::array())) {
        graph.plugin_libraries.push_back(library.get<std::string>());
    }

    for (const auto& item : data.value("nodes", json::array())) {
        EditorNode node;
        node.ui_id = item.value("ui_id", graph.next_node_ui_id);
        node.enabled = item.value("enabled", true);
        node.node_id = item.value("id", std::string{});
        node.plugin_id = item.value("plugin_id", std::string{});
        node.instance_name = item.value("instance_name", node.node_id);
        node.process_entrypoint = item.value("process_entrypoint", "Process");
        node.start_entrypoint = item.value("start_entrypoint", "Start");
        node.stop_entrypoint = item.value("stop_entrypoint", "Stop");

        if (item.contains("position") && item["position"].is_array() && item["position"].size() >= 2) {
            node.x = item["position"][0].get<float>();
            node.y = item["position"][1].get<float>();
        }

        if (item.contains("properties") && item["properties"].is_object()) {
            for (const auto& property : item["properties"].items()) {
                if (property.value().is_number()) {
                    node.float_properties[property.key()] = property.value().get<double>();
                }
            }
        }

        if (item.contains("int_properties") && item["int_properties"].is_object()) {
            for (const auto& property : item["int_properties"].items()) {
                if (property.value().is_number_integer()) {
                    node.int_properties[property.key()] = property.value().get<std::int64_t>();
                }
            }
        }

        if (item.contains("bool_properties") && item["bool_properties"].is_object()) {
            for (const auto& property : item["bool_properties"].items()) {
                if (property.value().is_boolean()) {
                    node.bool_properties[property.key()] = property.value().get<bool>();
                }
            }
        }

        if (item.contains("string_properties") && item["string_properties"].is_object()) {
            for (const auto& property : item["string_properties"].items()) {
                if (property.value().is_string()) {
                    node.string_properties[property.key()] = property.value().get<std::string>();
                }
            }
        }

        graph.next_node_ui_id = std::max(graph.next_node_ui_id, node.ui_id + 1);
        graph.nodes.push_back(std::move(node));
    }

    for (const auto& item : data.value("edges", json::array())) {
        EditorEdge edge;
        edge.ui_id = item.value("ui_id", graph.next_edge_ui_id);
        edge.source_node_id = item.value("source_node_id", std::string{});
        edge.source_port_id = item.value("source_port_id", std::string{});
        edge.target_node_id = item.value("target_node_id", std::string{});
        edge.target_port_id = item.value("target_port_id", std::string{});
        graph.next_edge_ui_id = std::max(graph.next_edge_ui_id, edge.ui_id + 1);
        graph.edges.push_back(std::move(edge));
    }

    return graph;
}

void save_editor_graph(const std::filesystem::path& path, const EditorGraph& graph)
{
    json data;
    data["version"] = 1;
    data["blueprint_name"] = graph.blueprint_name;
    data["runtime_directory"] = graph.runtime_directory.string();

    data["plugin_libraries"] = json::array();
    for (const auto& library : graph.plugin_libraries) {
        data["plugin_libraries"].push_back(library.string());
    }

    data["nodes"] = json::array();
    for (const auto& node : graph.nodes) {
        json item;
        item["ui_id"] = node.ui_id;
        item["enabled"] = node.enabled;
        item["id"] = node.node_id;
        item["plugin_id"] = node.plugin_id;
        item["instance_name"] = node.instance_name;
        item["process_entrypoint"] = node.process_entrypoint;
        item["start_entrypoint"] = node.start_entrypoint;
        item["stop_entrypoint"] = node.stop_entrypoint;
        item["position"] = json::array({node.x, node.y});
        item["properties"] = json::object();
        for (const auto& property : node.float_properties) {
            item["properties"][property.first] = property.second;
        }
        item["int_properties"] = json::object();
        for (const auto& property : node.int_properties) {
            item["int_properties"][property.first] = property.second;
        }
        item["bool_properties"] = json::object();
        for (const auto& property : node.bool_properties) {
            item["bool_properties"][property.first] = property.second;
        }
        item["string_properties"] = json::object();
        for (const auto& property : node.string_properties) {
            item["string_properties"][property.first] = property.second;
        }
        data["nodes"].push_back(std::move(item));
    }

    data["edges"] = json::array();
    for (const auto& edge : graph.edges) {
        data["edges"].push_back(json{
            {"ui_id", edge.ui_id},
            {"source_node_id", edge.source_node_id},
            {"source_port_id", edge.source_port_id},
            {"target_node_id", edge.target_node_id},
            {"target_port_id", edge.target_port_id},
        });
    }

    std::ofstream output{path};
    if (!output) {
        throw std::runtime_error{"Could not open graph JSON for writing: " + path.string()};
    }
    output << data.dump(2) << '\n';
}

DescriptorIndex make_descriptor_index(std::vector<PluginDescriptor> descriptors)
{
    DescriptorIndex index;
    index.descriptors = std::move(descriptors);
    for (std::size_t descriptor_index = 0; descriptor_index < index.descriptors.size(); ++descriptor_index) {
        index.by_plugin_id[index.descriptors[descriptor_index].id] = descriptor_index;
    }
    return index;
}

std::optional<std::string> register_replay_plugin_for_path(
    PluginRegistry& registry,
    const std::string& recording_path
)
{
    const auto port_infos = pluginsystem::builtins::read_recording_ports(recording_path);
    if (port_infos.empty()) {
        return std::nullopt;
    }

    const auto plugin_id = replay_plugin_id_for_ports(port_infos);
    if (!registry_contains_plugin(registry, plugin_id)) {
        registry.register_builtin(pluginsystem::builtins::make_replay(plugin_id, port_infos));
    }
    return plugin_id;
}

std::size_t prepare_replay_plugins_for_graph(PluginRegistry& registry, EditorGraph& graph)
{
    std::unordered_set<std::string> replay_nodes_to_check;
    std::size_t registered_or_updated = 0;

    for (auto& node : graph.nodes) {
        if (!is_replay_plugin_id(node.plugin_id)) {
            continue;
        }
        const auto input_path = node.string_properties.find("InputPath");
        if (input_path == node.string_properties.end() || input_path->second.empty()) {
            continue;
        }

        auto replay_plugin_id = register_replay_plugin_for_path(registry, input_path->second);
        if (!replay_plugin_id) {
            continue;
        }

        if (node.plugin_id != *replay_plugin_id) {
            node.plugin_id = *replay_plugin_id;
            ++registered_or_updated;
        }
        replay_nodes_to_check.insert(node.node_id);
    }

    if (!replay_nodes_to_check.empty()) {
        const auto descriptors = make_descriptor_index(registry.discover_plugins());
        graph.edges.erase(
            std::remove_if(graph.edges.begin(), graph.edges.end(), [&](const auto& edge) {
                if (replay_nodes_to_check.find(edge.source_node_id) == replay_nodes_to_check.end()) {
                    return false;
                }
                const auto* node = find_node(graph, edge.source_node_id);
                if (node == nullptr) {
                    return true;
                }
                const auto* descriptor = find_descriptor(descriptors, node->plugin_id);
                if (descriptor == nullptr) {
                    return true;
                }
                const auto* port = find_port(*descriptor, edge.source_port_id);
                return port == nullptr || port->direction != PortDirection::output;
            }),
            graph.edges.end()
        );
    }

    return registered_or_updated;
}

std::vector<std::string> validate_editor_graph(const EditorGraph& graph, const DescriptorIndex& descriptors)
{
    std::vector<std::string> errors;
    if (graph.nodes.empty()) {
        errors.push_back("Graph has no nodes.");
    }

    std::unordered_map<std::string, std::size_t> all_node_indices;
    std::unordered_map<std::string, std::size_t> node_indices;
    std::set<std::string> instance_names;
    for (std::size_t node_index = 0; node_index < graph.nodes.size(); ++node_index) {
        const auto& node = graph.nodes[node_index];
        if (node.node_id.empty()) {
            errors.push_back("Node id cannot be empty.");
            continue;
        }
        if (!all_node_indices.emplace(node.node_id, node_index).second) {
            errors.push_back("Duplicate node id: " + node.node_id);
        }
        if (!node.enabled) {
            continue;
        }
        node_indices.emplace(node.node_id, node_index);
        if (!instance_names.insert(node.instance_name).second) {
            errors.push_back("Duplicate instance name: " + node.instance_name);
        }

        const auto* descriptor = find_descriptor(descriptors, node.plugin_id);
        if (descriptor == nullptr) {
            errors.push_back("Missing plugin descriptor for node " + node.node_id + ": " + node.plugin_id);
            continue;
        }
        if (!node.process_entrypoint.empty() && !has_entrypoint(*descriptor, node.process_entrypoint)) {
            errors.push_back("Missing process entrypoint '" + node.process_entrypoint + "' on node " + node.node_id);
        }
    }
    if (!graph.nodes.empty() && node_indices.empty()) {
        errors.push_back("Graph has no enabled nodes.");
    }

    std::unordered_map<std::string, std::string> input_sources;
    std::vector<std::vector<std::size_t>> adjacency(graph.nodes.size());
    std::vector<std::size_t> indegree(graph.nodes.size(), 0);

    for (const auto& edge : graph.edges) {
        const auto source_node_exists = all_node_indices.find(edge.source_node_id);
        const auto target_node_exists = all_node_indices.find(edge.target_node_id);
        if (source_node_exists == all_node_indices.end()) {
            errors.push_back("Edge source node does not exist: " + edge.source_node_id);
            continue;
        }
        if (target_node_exists == all_node_indices.end()) {
            errors.push_back("Edge target node does not exist: " + edge.target_node_id);
            continue;
        }
        const auto source_node_found = node_indices.find(edge.source_node_id);
        const auto target_node_found = node_indices.find(edge.target_node_id);
        if (source_node_found == node_indices.end() || target_node_found == node_indices.end()) {
            continue;
        }

        const auto& source_node = graph.nodes[source_node_found->second];
        const auto& target_node = graph.nodes[target_node_found->second];
        const auto* source_descriptor = find_descriptor(descriptors, source_node.plugin_id);
        const auto* target_descriptor = find_descriptor(descriptors, target_node.plugin_id);
        if (source_descriptor == nullptr || target_descriptor == nullptr) {
            continue;
        }

        const auto* source_port = find_port(*source_descriptor, edge.source_port_id);
        const auto* target_port = find_port(*target_descriptor, edge.target_port_id);
        if (source_port == nullptr) {
            errors.push_back("Edge source port does not exist: " + edge.source_node_id + "." + edge.source_port_id);
            continue;
        }
        if (target_port == nullptr) {
            errors.push_back("Edge target port does not exist: " + edge.target_node_id + "." + edge.target_port_id);
            continue;
        }

        if (source_port->direction != PortDirection::output) {
            errors.push_back("Edge source is not an output port: " + edge.source_node_id + "." + edge.source_port_id);
        }
        if (target_port->direction != PortDirection::input) {
            errors.push_back("Edge target is not an input port: " + edge.target_node_id + "." + edge.target_port_id);
        }
        if (!target_port->any_type) {
            if (source_port->type_name != target_port->type_name) {
                errors.push_back("Port type mismatch: " + edge.source_node_id + "." + edge.source_port_id + " -> " + edge.target_node_id + "." + edge.target_port_id);
            }
            if (source_port->byte_size != target_port->byte_size) {
                errors.push_back("Port byte-size mismatch: " + edge.source_node_id + "." + edge.source_port_id + " -> " + edge.target_node_id + "." + edge.target_port_id);
            }
            if (source_port->access_mode != target_port->access_mode) {
                errors.push_back("Port access-mode mismatch: " + edge.source_node_id + "." + edge.source_port_id + " -> " + edge.target_node_id + "." + edge.target_port_id);
            }
        }

        const auto target_key = port_key(edge.target_node_id, edge.target_port_id);
        if (!input_sources.emplace(target_key, port_key(edge.source_node_id, edge.source_port_id)).second) {
            errors.push_back("Fan-in is not supported for input port: " + target_key);
        }

        adjacency[source_node_found->second].push_back(target_node_found->second);
        ++indegree[target_node_found->second];
    }

    std::deque<std::size_t> ready;
    for (const auto& item : node_indices) {
        const auto index = item.second;
        if (indegree[index] == 0) {
            ready.push_back(index);
        }
    }

    std::size_t visited = 0;
    while (!ready.empty()) {
        const auto current = ready.front();
        ready.pop_front();
        ++visited;
        for (const auto target : adjacency[current]) {
            --indegree[target];
            if (indegree[target] == 0) {
                ready.push_back(target);
            }
        }
    }
    if (visited != node_indices.size()) {
        errors.push_back("Graph contains a cycle.");
    }

    return errors;
}

GraphConfig make_graph_config(const EditorGraph& graph)
{
    GraphConfig config;
    config.blueprint_name = graph.blueprint_name;
    config.runtime_directory = graph.runtime_directory;
    config.worker_count = std::thread::hardware_concurrency();
    std::unordered_set<std::string> enabled_node_ids;
    for (const auto& node : graph.nodes) {
        if (!node.enabled) {
            continue;
        }
        enabled_node_ids.insert(node.node_id);
        config.nodes.push_back(GraphNodeConfig{
            node.node_id,
            node.plugin_id,
            node.instance_name,
            node.process_entrypoint,
            node.start_entrypoint,
            node.stop_entrypoint,
        });
    }
    for (const auto& edge : graph.edges) {
        if (enabled_node_ids.find(edge.source_node_id) == enabled_node_ids.end()
            || enabled_node_ids.find(edge.target_node_id) == enabled_node_ids.end()) {
            continue;
        }
        config.edges.push_back(GraphEdgeConfig{
            edge.source_node_id,
            edge.source_port_id,
            edge.target_node_id,
            edge.target_port_id,
        });
    }
    return config;
}

const PluginDescriptor* find_descriptor(const DescriptorIndex& descriptors, std::string_view plugin_id)
{
    const auto found = descriptors.by_plugin_id.find(std::string{plugin_id});
    if (found == descriptors.by_plugin_id.end()) {
        return nullptr;
    }
    return &descriptors.descriptors[found->second];
}

const PortDescriptor* find_port(const PluginDescriptor& descriptor, std::string_view port_id)
{
    const auto found = std::find_if(descriptor.ports.begin(), descriptor.ports.end(), [port_id](const auto& port) {
        return port.id == port_id;
    });
    return found == descriptor.ports.end() ? nullptr : &*found;
}

const PropertyDescriptor* find_property(const PluginDescriptor& descriptor, std::string_view property_id)
{
    const auto found = std::find_if(descriptor.properties.begin(), descriptor.properties.end(), [property_id](const auto& property) {
        return property.id == property_id;
    });
    return found == descriptor.properties.end() ? nullptr : &*found;
}

const EditorNode* find_node(const EditorGraph& graph, std::string_view node_id)
{
    const auto found = std::find_if(graph.nodes.begin(), graph.nodes.end(), [node_id](const auto& node) {
        return node.node_id == node_id;
    });
    return found == graph.nodes.end() ? nullptr : &*found;
}

EditorNode* find_node(EditorGraph& graph, std::string_view node_id)
{
    const auto found = std::find_if(graph.nodes.begin(), graph.nodes.end(), [node_id](const auto& node) {
        return node.node_id == node_id;
    });
    return found == graph.nodes.end() ? nullptr : &*found;
}

std::vector<std::filesystem::path> merge_plugin_libraries(
    const std::vector<std::filesystem::path>& from_graph,
    const std::vector<std::filesystem::path>& from_cli
)
{
    std::vector<std::filesystem::path> merged;
    for (const auto& library : from_graph) {
        add_unique_path(merged, library);
    }
    for (const auto& library : from_cli) {
        add_unique_path(merged, library);
    }
    return merged;
}

void apply_node_properties(pluginsystem::GraphRuntime& runtime, const EditorGraph& graph, const DescriptorIndex& descriptors)
{
    for (const auto& node : graph.nodes) {
        if (!node.enabled) {
            continue;
        }
        const auto* descriptor = find_descriptor(descriptors, node.plugin_id);
        if (descriptor == nullptr) {
            continue;
        }

        for (const auto& property : descriptor->properties) {
            if (!property.writable || is_replay_status_property(property.id)) {
                continue;
            }
            auto& props = runtime.properties(node.node_id);

            if (property.type_name == "bool" && property.byte_size == sizeof(bool)) {
                const auto it = node.bool_properties.find(property.id);
                if (it != node.bool_properties.end()) {
                    const bool value = it->second;
                    props.write(property.id, &value, sizeof(value));
                }
            } else if (property.type_name.rfind("char[", 0) == 0 && property.byte_size > 0) {
                const auto it = node.string_properties.find(property.id);
                if (it != node.string_properties.end()) {
                    std::vector<char> buffer(static_cast<std::size_t>(property.byte_size), '\0');
                    const auto len = std::min(it->second.size(), buffer.size() - 1);
                    std::memcpy(buffer.data(), it->second.c_str(), len);
                    props.write(property.id, buffer.data(), property.byte_size);
                }
            } else if (property.type_name == "float" && property.byte_size == sizeof(float)) {
                const auto it = node.float_properties.find(property.id);
                if (it != node.float_properties.end()) {
                    const float value = static_cast<float>(it->second);
                    props.write(property.id, &value, sizeof(value));
                }
            } else if (property.type_name == "double" && property.byte_size == sizeof(double)) {
                const auto it = node.float_properties.find(property.id);
                if (it != node.float_properties.end()) {
                    const double value = it->second;
                    props.write(property.id, &value, sizeof(value));
                }
            } else {
                const auto it = node.int_properties.find(property.id);
                if (it == node.int_properties.end()) {
                    continue;
                }
                const std::int64_t raw = it->second;
                if (property.type_name == "int8_t" && property.byte_size == sizeof(std::int8_t)) {
                    const auto value = static_cast<std::int8_t>(raw);
                    props.write(property.id, &value, sizeof(value));
                } else if (property.type_name == "int16_t" && property.byte_size == sizeof(std::int16_t)) {
                    const auto value = static_cast<std::int16_t>(raw);
                    props.write(property.id, &value, sizeof(value));
                } else if (property.type_name == "int32_t" && property.byte_size == sizeof(std::int32_t)) {
                    const auto value = static_cast<std::int32_t>(raw);
                    props.write(property.id, &value, sizeof(value));
                } else if (property.type_name == "int64_t" && property.byte_size == sizeof(std::int64_t)) {
                    props.write(property.id, &raw, sizeof(raw));
                } else if (property.type_name == "uint8_t" && property.byte_size == sizeof(std::uint8_t)) {
                    const auto value = static_cast<std::uint8_t>(raw);
                    props.write(property.id, &value, sizeof(value));
                } else if (property.type_name == "uint16_t" && property.byte_size == sizeof(std::uint16_t)) {
                    const auto value = static_cast<std::uint16_t>(raw);
                    props.write(property.id, &value, sizeof(value));
                } else if (property.type_name == "uint32_t" && property.byte_size == sizeof(std::uint32_t)) {
                    const auto value = static_cast<std::uint32_t>(raw);
                    props.write(property.id, &value, sizeof(value));
                } else if (property.type_name == "uint64_t" && property.byte_size == sizeof(std::uint64_t)) {
                    const auto value = static_cast<std::uint64_t>(raw);
                    props.write(property.id, &value, sizeof(value));
                }
            }
        }
    }
}

} // namespace pluginsystem::examples::node_editor
