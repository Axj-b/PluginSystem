#pragma once

#include <pluginsystem/instance.hpp>

#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace pluginsystem {

class PluginRegistry;

using GraphJobHandle = std::uint64_t;

enum class GraphJobStatus {
    pending,
    running,
    completed,
    failed,
    canceled
};

struct GraphRunResult {
    int32_t result{PS_OK};
    std::string failed_node_id;
};

struct GraphNodeConfig {
    std::string node_id;
    std::string plugin_id;
    std::string instance_name;
    std::string process_entrypoint{"Process"};
    std::string start_entrypoint{"Start"};
    std::string stop_entrypoint{"Stop"};
};

struct GraphEdgeConfig {
    std::string source_node_id;
    std::string source_port_id;
    std::string target_node_id;
    std::string target_port_id;
};

struct GraphConfig {
    std::string blueprint_name{"Graph"};
    std::filesystem::path runtime_directory{"pluginsystem_runtime"};
    std::uint32_t worker_count{1};
    std::vector<GraphNodeConfig> nodes;
    std::vector<GraphEdgeConfig> edges;
};

class GraphRuntime {
public:
    GraphRuntime(PluginRegistry& registry, GraphConfig config);
    ~GraphRuntime();

    GraphRuntime(const GraphRuntime&) = delete;
    GraphRuntime& operator=(const GraphRuntime&) = delete;
    GraphRuntime(GraphRuntime&&) = delete;
    GraphRuntime& operator=(GraphRuntime&&) = delete;

    void start();
    void stop();

    GraphJobHandle submit_run();
    GraphJobStatus status(GraphJobHandle handle) const;
    GraphRunResult wait(GraphJobHandle handle);
    std::optional<GraphRunResult> result(GraphJobHandle handle) const;

    SharedMemoryChannel& port(std::string_view node_id, std::string_view port_id);
    const SharedMemoryChannel& port(std::string_view node_id, std::string_view port_id) const;
    SharedPropertyBlock& properties(std::string_view node_id);
    const SharedPropertyBlock& properties(std::string_view node_id) const;
    const PluginDescriptor& node_descriptor(std::string_view node_id) const;

private:
    struct CompiledNode {
        GraphNodeConfig config;
        PluginDescriptor descriptor;
        std::unique_ptr<PluginInstance> instance;
    };

    struct Job {
        mutable std::mutex mutex;
        std::condition_variable cv;
        GraphJobStatus status{GraphJobStatus::pending};
        GraphRunResult result;
        std::exception_ptr error;
    };

    void compile(PluginRegistry& registry, GraphConfig config);
    RuntimeBindings create_bindings_for_node(
        const GraphConfig& config,
        const CompiledNode& node,
        const std::unordered_map<std::string, std::shared_ptr<SharedMemoryChannel>>& output_channels,
        const std::unordered_map<std::string, std::string>& input_sources
    );
    std::vector<std::size_t> compute_topological_order(
        const GraphConfig& config,
        const std::unordered_map<std::string, std::size_t>& node_indices
    ) const;
    void validate_edges(
        const GraphConfig& config,
        const std::unordered_map<std::string, std::size_t>& node_indices,
        std::unordered_map<std::string, std::string>& input_sources
    ) const;
    GraphRunResult run_once();
    void worker_loop();
    std::shared_ptr<Job> find_job(GraphJobHandle handle) const;
    CompiledNode& find_node(std::string_view node_id);
    const CompiledNode& find_node(std::string_view node_id) const;

    static std::string port_key(std::string_view node_id, std::string_view port_id);
    static const PortDescriptor& find_port_descriptor(const PluginDescriptor& descriptor, std::string_view port_id);
    static bool has_entrypoint(const PluginDescriptor& descriptor, std::string_view entrypoint_id);

    std::vector<CompiledNode> nodes_;
    std::unordered_map<std::string, std::size_t> node_indices_;
    std::vector<std::size_t> topological_order_;
    std::uint32_t worker_count_{1};

    std::mutex run_mutex_;
    mutable std::mutex jobs_mutex_;
    std::unordered_map<GraphJobHandle, std::shared_ptr<Job>> jobs_;
    std::queue<GraphJobHandle> queue_;
    GraphJobHandle next_job_handle_{1};

    mutable std::mutex worker_mutex_;
    std::condition_variable worker_cv_;
    std::vector<std::thread> workers_;
    bool workers_started_{false};
    bool stopping_{false};
};

} // namespace pluginsystem
