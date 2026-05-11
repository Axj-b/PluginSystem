#include <pluginsystem/graph.hpp>

#include <pluginsystem/error.hpp>
#include <pluginsystem/registry.hpp>

#include <algorithm>
#include <deque>
#include <set>
#include <stdexcept>
#include <utility>

namespace pluginsystem {
namespace {

bool is_terminal(GraphJobStatus status)
{
    return status == GraphJobStatus::completed || status == GraphJobStatus::failed || status == GraphJobStatus::canceled;
}

std::string member_name(const PortDescriptor& port)
{
    return port.name.empty() ? port.id : port.name;
}

} // namespace

GraphRuntime::GraphRuntime(PluginRegistry& registry, GraphConfig config)
{
    compile(registry, std::move(config));
}

GraphRuntime::~GraphRuntime()
{
    try {
        stop();
    } catch (...) {
    }
}

void GraphRuntime::start()
{
    State expected = State::idle;
    if (!state_.compare_exchange_strong(expected, State::starting)) {
        // If another thread is concurrently starting, wait for it to finish.
        while (state_.load(std::memory_order_acquire) == State::starting) {
            std::this_thread::yield();
        }
        return;
    }

    try {
        for (const auto node_index : topological_order_) {
            auto& node = nodes_[node_index];
            if (!node.config.start_entrypoint.empty() && has_entrypoint(node.descriptor, node.config.start_entrypoint)) {
                const auto result = node.instance->invoke(node.config.start_entrypoint);
                if (result != PS_OK) {
                    throw PluginError{"Graph node start failed: " + node.config.node_id};
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock{worker_mutex_};
            workers_.reserve(worker_count_);
            for (std::uint32_t index = 0; index < worker_count_; ++index) {
                workers_.emplace_back([this]() { worker_loop(); });
            }
        }

        state_.store(State::running, std::memory_order_release);
    } catch (...) {
        state_.store(State::idle, std::memory_order_release);
        throw;
    }
}

void GraphRuntime::stop()
{
    State expected = State::running;
    if (!state_.compare_exchange_strong(expected, State::stopping)) {
        return;
    }

    worker_cv_.notify_all();

    std::vector<std::thread> workers_to_join;
    {
        std::lock_guard<std::mutex> lock{worker_mutex_};
        workers_to_join = std::move(workers_);
    }
    for (auto& worker : workers_to_join) {
        if (worker.joinable() && worker.get_id() != std::this_thread::get_id()) {
            worker.join();
        }
    }

    // Reset state before calling Stop entrypoints so it is always consistent,
    // even if an entrypoint throws.
    state_.store(State::idle, std::memory_order_release);

    for (auto iterator = topological_order_.rbegin(); iterator != topological_order_.rend(); ++iterator) {
        auto& node = nodes_[*iterator];
        if (!node.config.stop_entrypoint.empty() && has_entrypoint(node.descriptor, node.config.stop_entrypoint)) {
            const auto result = node.instance->invoke(node.config.stop_entrypoint);
            if (result != PS_OK) {
                throw PluginError{"Graph node stop failed: " + node.config.node_id};
            }
        }
    }
}

GraphJobHandle GraphRuntime::submit_run()
{
    start();

    if (state_.load(std::memory_order_acquire) != State::running) {
        throw PluginError{"Cannot submit run: graph failed to start or is stopping"};
    }

    auto job = std::make_shared<Job>();

    // Initialize per-run in-degree tracking. unique_ptr<atomic[]> avoids the
    // MoveInsertable requirement that vector<atomic> would impose.
    job->node_in_degrees = std::make_unique<std::atomic<int>[]>(nodes_.size());
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        job->node_in_degrees[i].store(static_cast<int>(node_base_indegrees_[i]), std::memory_order_relaxed);
    }

    // Count initially-ready nodes (in-degree 0) so pending_nodes is set before
    // any worker can pick up the first tasks and decrement it to zero prematurely.
    std::size_t ready_count = 0;
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        if (node_base_indegrees_[i] == 0) {
            ++ready_count;
        }
    }
    job->pending_nodes.store(ready_count, std::memory_order_relaxed);

    GraphJobHandle handle = 0;
    {
        std::lock_guard<std::mutex> lock{jobs_mutex_};
        handle = next_job_handle_++;
        jobs_.emplace(handle, job);
    }

    {
        std::lock_guard<std::mutex> lock{job->mutex};
        job->status = GraphJobStatus::running;
    }
    job->cv.notify_all();

    {
        std::lock_guard<std::mutex> lock{worker_mutex_};
        for (std::size_t i = 0; i < nodes_.size(); ++i) {
            if (node_base_indegrees_[i] == 0) {
                node_queue_.push({handle, i});
            }
        }
    }
    worker_cv_.notify_all();

    return handle;
}

GraphJobHandle GraphRuntime::submit_single_node(std::string_view node_id)
{
    start();

    if (state_.load(std::memory_order_acquire) != State::running) {
        throw PluginError{"Cannot submit step: graph failed to start or is stopping"};
    }

    const auto found = node_indices_.find(std::string{node_id});
    if (found == node_indices_.end()) {
        throw PluginError{"Graph node does not exist: " + std::string{node_id}};
    }
    const std::size_t node_index = found->second;

    auto job = std::make_shared<Job>();
    job->single_node = true;
    job->node_in_degrees = std::make_unique<std::atomic<int>[]>(nodes_.size());
    job->pending_nodes.store(1, std::memory_order_relaxed);

    GraphJobHandle handle = 0;
    {
        std::lock_guard<std::mutex> lock{jobs_mutex_};
        handle = next_job_handle_++;
        jobs_.emplace(handle, job);
    }

    {
        std::lock_guard<std::mutex> lock{job->mutex};
        job->status = GraphJobStatus::running;
    }
    job->cv.notify_all();

    {
        std::lock_guard<std::mutex> lock{worker_mutex_};
        node_queue_.push({handle, node_index});
    }
    worker_cv_.notify_one();

    return handle;
}

std::vector<std::string> GraphRuntime::topological_node_ids() const
{
    std::vector<std::string> ids;
    ids.reserve(topological_order_.size());
    for (const auto idx : topological_order_) {
        ids.push_back(nodes_[idx].config.node_id);
    }
    return ids;
}

GraphJobStatus GraphRuntime::status(GraphJobHandle handle) const
{
    auto job = find_job(handle);
    std::lock_guard<std::mutex> lock{job->mutex};
    return job->status;
}

GraphRunResult GraphRuntime::wait(GraphJobHandle handle)
{
    auto job = find_job(handle);
    std::unique_lock<std::mutex> lock{job->mutex};
    job->cv.wait(lock, [&job]() {
        return is_terminal(job->status);
    });
    if (job->status == GraphJobStatus::failed && job->error) {
        std::rethrow_exception(job->error);
    }
    if (job->status == GraphJobStatus::canceled) {
        throw PluginError{"Graph job was canceled"};
    }
    return job->result;
}

std::optional<GraphRunResult> GraphRuntime::result(GraphJobHandle handle) const
{
    auto job = find_job(handle);
    std::lock_guard<std::mutex> lock{job->mutex};
    if (job->status != GraphJobStatus::completed) {
        return std::nullopt;
    }
    return job->result;
}

bool GraphRuntime::cancel(GraphJobHandle handle)
{
    auto job = find_job(handle);
    std::lock_guard<std::mutex> lock{job->mutex};
    if (is_terminal(job->status)) {
        return false;
    }
    job->status = GraphJobStatus::canceled;
    job->cv.notify_all();
    return true;
}

SharedMemoryChannel& GraphRuntime::port(std::string_view node_id, std::string_view port_id)
{
    auto& node = find_node(node_id);
    return node.instance->port(port_id);
}

const SharedMemoryChannel& GraphRuntime::port(std::string_view node_id, std::string_view port_id) const
{
    const auto& node = find_node(node_id);
    return node.instance->port(port_id);
}

SharedPropertyBlock& GraphRuntime::properties(std::string_view node_id)
{
    return find_node(node_id).instance->properties();
}

const SharedPropertyBlock& GraphRuntime::properties(std::string_view node_id) const
{
    return find_node(node_id).instance->properties();
}

const PluginDescriptor& GraphRuntime::node_descriptor(std::string_view node_id) const
{
    return find_node(node_id).descriptor;
}

void GraphRuntime::invoke_all(std::string_view entrypoint_id, void* user_context)
{
    for (auto& node : nodes_) {
        if (has_entrypoint(node.descriptor, entrypoint_id)) {
            node.instance->invoke(entrypoint_id, user_context);
        }
    }
}

void GraphRuntime::compile(PluginRegistry& registry, GraphConfig config)
{
    if (config.nodes.empty()) {
        throw PluginError{"Graph must contain at least one node"};
    }
    worker_count_ = config.worker_count == 0 ? 1 : config.worker_count;

    nodes_.reserve(config.nodes.size());
    std::set<std::string> instance_names;
    for (auto node_config : config.nodes) {
        if (node_config.node_id.empty()) {
            throw PluginError{"Graph node id cannot be empty"};
        }
        if (node_config.plugin_id.empty()) {
            throw PluginError{"Graph node plugin id cannot be empty: " + node_config.node_id};
        }
        if (node_config.instance_name.empty()) {
            node_config.instance_name = node_config.node_id;
        }
        if (!node_indices_.emplace(node_config.node_id, nodes_.size()).second) {
            throw PluginError{"Duplicate graph node id: " + node_config.node_id};
        }
        if (!instance_names.insert(node_config.instance_name).second) {
            throw PluginError{"Duplicate graph instance name: " + node_config.instance_name};
        }

        auto& entry = registry.find_entry(node_config.plugin_id);
        if (!node_config.process_entrypoint.empty() && !has_entrypoint(entry.descriptor, node_config.process_entrypoint)) {
            throw PluginError{"Graph node process entrypoint is not declared: " + node_config.node_id};
        }

        nodes_.push_back(CompiledNode{
            std::move(node_config),
            entry.descriptor,
            {},
        });
    }

    std::unordered_map<std::string, std::string> input_sources;
    validate_edges(config, node_indices_, input_sources);
    topological_order_ = compute_topological_order(config, node_indices_);

    std::unordered_map<std::string, std::shared_ptr<SharedMemoryChannel>> output_channels;
    output_channels.reserve(nodes_.size());
    std::set<std::string> shared_memory_names;

    for (const auto& node : nodes_) {
        for (const auto& port : node.descriptor.ports) {
            if (port.direction != PortDirection::output) {
                continue;
            }

            auto name = make_shared_memory_name(config.blueprint_name, node.config.instance_name, member_name(port), "output");
            if (!shared_memory_names.insert(name).second) {
                throw PluginError{"Duplicate generated graph shared memory name: " + name};
            }
            output_channels.emplace(
                port_key(node.config.node_id, port.id),
                SharedMemoryChannel::create(std::move(name), port.byte_size)
            );
        }
    }

    for (auto& node : nodes_) {
        auto bindings = create_bindings_for_node(config, node, output_channels, input_sources);

        PluginInstanceConfig instance_config;
        instance_config.blueprint_name = config.blueprint_name;
        instance_config.instance_name = node.config.instance_name;
        instance_config.runtime_directory = config.runtime_directory;
        node.instance = registry.create_instance_with_bindings(
            node.config.plugin_id,
            std::move(instance_config),
            std::move(bindings)
        );
    }
}

RuntimeBindings GraphRuntime::create_bindings_for_node(
    const GraphConfig& config,
    const CompiledNode& node,
    const std::unordered_map<std::string, std::shared_ptr<SharedMemoryChannel>>& output_channels,
    const std::unordered_map<std::string, std::string>& input_sources
)
{
    RuntimeBindings bindings;
    bindings.ports.reserve(node.descriptor.ports.size());

    for (const auto& port : node.descriptor.ports) {
        std::shared_ptr<SharedMemoryChannel> channel;
        const auto key = port_key(node.config.node_id, port.id);
        if (port.direction == PortDirection::output) {
            const auto found = output_channels.find(key);
            if (found == output_channels.end()) {
                throw PluginError{"Graph output channel was not created: " + key};
            }
            channel = found->second;
        } else {
            const auto source = input_sources.find(key);
            if (source != input_sources.end()) {
                const auto found = output_channels.find(source->second);
                if (found == output_channels.end()) {
                    throw PluginError{"Graph connected source channel was not created: " + source->second};
                }
                channel = found->second;
            } else {
                auto name = make_shared_memory_name(config.blueprint_name, node.config.instance_name, member_name(port), "input");
                channel = SharedMemoryChannel::create(std::move(name), port.byte_size);
            }
        }

        bindings.ports.push_back(PortRuntimeBinding{
            port,
            std::move(channel),
        });
    }

    const auto properties_name = make_shared_memory_name(config.blueprint_name, node.config.instance_name, "properties", "properties");
    bindings.properties = SharedPropertyBlock::create(properties_name, node.descriptor.properties, node.descriptor.raw_property_block_size);
    return bindings;
}

void GraphRuntime::validate_edges(
    const GraphConfig& config,
    const std::unordered_map<std::string, std::size_t>& node_indices,
    std::unordered_map<std::string, std::string>& input_sources
) const
{
    for (const auto& edge : config.edges) {
        const auto source_node_found = node_indices.find(edge.source_node_id);
        if (source_node_found == node_indices.end()) {
            throw PluginError{"Graph edge source node does not exist: " + edge.source_node_id};
        }
        const auto target_node_found = node_indices.find(edge.target_node_id);
        if (target_node_found == node_indices.end()) {
            throw PluginError{"Graph edge target node does not exist: " + edge.target_node_id};
        }

        const auto& source_node = nodes_[source_node_found->second];
        const auto& target_node = nodes_[target_node_found->second];
        const auto& source_port = find_port_descriptor(source_node.descriptor, edge.source_port_id);
        const auto& target_port = find_port_descriptor(target_node.descriptor, edge.target_port_id);

        if (source_port.direction != PortDirection::output) {
            throw PluginError{"Graph edge source port is not an output: " + edge.source_port_id};
        }
        if (target_port.direction != PortDirection::input) {
            throw PluginError{"Graph edge target port is not an input: " + edge.target_port_id};
        }
        if (source_port.type_name != target_port.type_name) {
            throw PluginError{"Graph edge port type mismatch: " + edge.source_port_id + " -> " + edge.target_port_id};
        }
        if (source_port.byte_size != target_port.byte_size) {
            throw PluginError{"Graph edge port size mismatch: " + edge.source_port_id + " -> " + edge.target_port_id};
        }
        if (source_port.access_mode != target_port.access_mode) {
            throw PluginError{"Graph edge port access-mode mismatch: " + edge.source_port_id + " -> " + edge.target_port_id};
        }

        const auto target_key = port_key(edge.target_node_id, edge.target_port_id);
        const auto source_key = port_key(edge.source_node_id, edge.source_port_id);
        if (!input_sources.emplace(target_key, source_key).second) {
            throw PluginError{"Graph fan-in is not supported for input port: " + target_key};
        }
    }
}

std::vector<std::size_t> GraphRuntime::compute_topological_order(
    const GraphConfig& config,
    const std::unordered_map<std::string, std::size_t>& node_indices
)
{
    node_adjacency_.assign(nodes_.size(), {});
    node_base_indegrees_.assign(nodes_.size(), 0);

    for (const auto& edge : config.edges) {
        const auto source = node_indices.at(edge.source_node_id);
        const auto target = node_indices.at(edge.target_node_id);
        node_adjacency_[source].push_back(target);
        ++node_base_indegrees_[target];
    }

    // Kahn's algorithm on a working copy of in-degrees.
    std::vector<std::size_t> indegree_copy = node_base_indegrees_;
    std::deque<std::size_t> ready;
    for (std::size_t index = 0; index < indegree_copy.size(); ++index) {
        if (indegree_copy[index] == 0) {
            ready.push_back(index);
        }
    }

    std::vector<std::size_t> order;
    order.reserve(nodes_.size());
    while (!ready.empty()) {
        const auto current = ready.front();
        ready.pop_front();
        order.push_back(current);

        for (const auto target : node_adjacency_[current]) {
            --indegree_copy[target];
            if (indegree_copy[target] == 0) {
                ready.push_back(target);
            }
        }
    }

    if (order.size() != nodes_.size()) {
        throw PluginError{"Graph cycles are not supported"};
    }
    return order;
}

void GraphRuntime::worker_loop()
{
    for (;;) {
        NodeTask task{};
        {
            std::unique_lock<std::mutex> lock{worker_mutex_};
            worker_cv_.wait(lock, [this]() {
                return state_.load(std::memory_order_relaxed) == State::stopping || !node_queue_.empty();
            });
            if (state_.load(std::memory_order_relaxed) == State::stopping && node_queue_.empty()) {
                return;
            }
            task = node_queue_.front();
            node_queue_.pop();
        }

        auto job = find_job(task.job_handle);

        // Skip execution if the job was canceled or failed by another branch.
        bool execute = false;
        {
            std::lock_guard<std::mutex> lock{job->mutex};
            execute = (job->status == GraphJobStatus::running);
        }

        bool node_ok = execute;
        if (execute) {
            auto& node = nodes_[task.node_index];
            if (!node.config.process_entrypoint.empty()) {
                try {
                    const int32_t node_result = node.instance->invoke(node.config.process_entrypoint);
                    if (node_result != PS_OK) {
                        std::lock_guard<std::mutex> lock{job->mutex};
                        if (job->result.result == PS_OK) {
                            job->result = GraphRunResult{node_result, node.config.node_id};
                        }
                        node_ok = false;
                    }
                } catch (...) {
                    std::lock_guard<std::mutex> lock{job->mutex};
                    if (!job->error) {
                        job->error = std::current_exception();
                    }
                    node_ok = false;
                }
            }
        }

        // Propagate to successors only when this node succeeded and this is a full run.
        // The worker that atomically drives a successor's in-degree to zero is the
        // sole worker that enqueues it — no double-enqueue is possible.
        std::size_t newly_enqueued = 0;
        if (node_ok && !job->single_node) {
            for (const auto successor : node_adjacency_[task.node_index]) {
                const auto prev = job->node_in_degrees[successor].fetch_sub(1, std::memory_order_acq_rel);
                if (prev == 1) {
                    {
                        std::lock_guard<std::mutex> lock{worker_mutex_};
                        node_queue_.push({task.job_handle, successor});
                    }
                    ++newly_enqueued;
                }
            }
        }

        // Increment pending_nodes BEFORE decrementing our own count so the counter
        // never reaches zero while there are still tasks in the queue.
        if (newly_enqueued > 0) {
            job->pending_nodes.fetch_add(newly_enqueued, std::memory_order_relaxed);
            worker_cv_.notify_all();
        }

        const auto remaining = job->pending_nodes.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (remaining == 0) {
            std::lock_guard<std::mutex> lock{job->mutex};
            if (job->status == GraphJobStatus::running) {
                job->status = job->error ? GraphJobStatus::failed : GraphJobStatus::completed;
                job->cv.notify_all();
            }
        }
    }
}

std::shared_ptr<GraphRuntime::Job> GraphRuntime::find_job(GraphJobHandle handle) const
{
    std::lock_guard<std::mutex> lock{jobs_mutex_};
    const auto found = jobs_.find(handle);
    if (found == jobs_.end()) {
        throw PluginError{"Graph job does not exist"};
    }
    return found->second;
}

GraphRuntime::CompiledNode& GraphRuntime::find_node(std::string_view node_id)
{
    const auto found = node_indices_.find(std::string{node_id});
    if (found == node_indices_.end()) {
        throw PluginError{"Graph node does not exist: " + std::string{node_id}};
    }
    return nodes_[found->second];
}

const GraphRuntime::CompiledNode& GraphRuntime::find_node(std::string_view node_id) const
{
    const auto found = node_indices_.find(std::string{node_id});
    if (found == node_indices_.end()) {
        throw PluginError{"Graph node does not exist: " + std::string{node_id}};
    }
    return nodes_[found->second];
}

std::string GraphRuntime::port_key(std::string_view node_id, std::string_view port_id)
{
    return std::string{node_id} + "." + std::string{port_id};
}

const PortDescriptor& GraphRuntime::find_port_descriptor(const PluginDescriptor& descriptor, std::string_view port_id)
{
    const auto found = std::find_if(descriptor.ports.begin(), descriptor.ports.end(), [port_id](const auto& port) {
        return port.id == port_id;
    });
    if (found == descriptor.ports.end()) {
        throw PluginError{"Graph port does not exist: " + std::string{port_id}};
    }
    return *found;
}

bool GraphRuntime::has_entrypoint(const PluginDescriptor& descriptor, std::string_view entrypoint_id)
{
    return std::any_of(descriptor.entrypoints.begin(), descriptor.entrypoints.end(), [entrypoint_id](const auto& entrypoint) {
        return entrypoint.id == entrypoint_id;
    });
}

} // namespace pluginsystem
