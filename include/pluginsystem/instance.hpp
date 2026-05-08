#pragma once

#include <pluginsystem/invocation_context.hpp>

#include <condition_variable>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>

namespace pluginsystem {

class PluginInstanceBackend {
public:
    virtual ~PluginInstanceBackend() = default;
    virtual int32_t invoke(std::string_view entrypoint_id, InvocationContext& context) = 0;
    virtual std::filesystem::path loaded_path() const;
};

class BuiltinPluginInstanceBackend final : public PluginInstanceBackend {
public:
    using InvokeFn = std::function<int32_t(std::string_view entrypoint_id, InvocationContext& context)>;

    explicit BuiltinPluginInstanceBackend(InvokeFn invoke);
    int32_t invoke(std::string_view entrypoint_id, InvocationContext& context) override;

private:
    InvokeFn invoke_;
};

class PluginInstance {
public:
    ~PluginInstance();

    PluginInstance(const PluginInstance&) = delete;
    PluginInstance& operator=(const PluginInstance&) = delete;
    PluginInstance(PluginInstance&&) = delete;
    PluginInstance& operator=(PluginInstance&&) = delete;

    const PluginDescriptor& descriptor() const noexcept;
    const PluginInstanceConfig& config() const noexcept;
    std::filesystem::path loaded_path() const;

    SharedMemoryChannel& port(std::string_view port_id);
    const SharedMemoryChannel& port(std::string_view port_id) const;
    SharedPropertyBlock& properties();
    const SharedPropertyBlock& properties() const;

    int32_t invoke(std::string_view entrypoint_id);
    JobHandle submit(std::string_view entrypoint_id);
    JobStatus job_status(JobHandle handle) const;
    int32_t wait(JobHandle handle);
    std::optional<int32_t> result(JobHandle handle) const;
    bool cancel(JobHandle handle);

private:
    friend class PluginRegistry;

    struct AsyncJob;

    PluginInstance(
        PluginDescriptor descriptor,
        PluginInstanceConfig config,
        RuntimeBindings bindings,
        std::unique_ptr<PluginInstanceBackend> backend
    );

    const EntrypointDescriptor& find_entrypoint(std::string_view entrypoint_id) const;
    int32_t invoke_locked(const EntrypointDescriptor& entrypoint);
    std::shared_ptr<AsyncJob> find_job(JobHandle handle) const;
    void join_jobs() noexcept;

    PluginDescriptor descriptor_;
    PluginInstanceConfig config_;
    RuntimeBindings bindings_;
    std::unique_ptr<PluginInstanceBackend> backend_;
    mutable std::mutex instance_mutex_;
    mutable std::mutex entrypoint_mutexes_mutex_;
    std::unordered_map<std::string, std::unique_ptr<std::mutex>> entrypoint_mutexes_;

    mutable std::mutex jobs_mutex_;
    std::unordered_map<JobHandle, std::shared_ptr<AsyncJob>> jobs_;
    JobHandle next_job_handle_{1};
};

}

