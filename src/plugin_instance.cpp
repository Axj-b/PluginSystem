#include <pluginsystem/instance.hpp>

#include <pluginsystem/error.hpp>

#include <algorithm>
#include <exception>
#include <utility>
#include <vector>

namespace pluginsystem {
namespace {

bool is_terminal(JobStatus status)
{
    return status == JobStatus::completed || status == JobStatus::failed || status == JobStatus::canceled;
}

} // namespace

std::filesystem::path PluginInstanceBackend::loaded_path() const
{
    return {};
}

BuiltinPluginInstanceBackend::BuiltinPluginInstanceBackend(InvokeFn invoke)
    : invoke_{std::move(invoke)}
{
}

int32_t BuiltinPluginInstanceBackend::invoke(std::string_view entrypoint_id, InvocationContext& context)
{
    if (!invoke_) {
        throw PluginError{"Built-in plugin backend does not provide invoke"};
    }
    return invoke_(entrypoint_id, context);
}

struct PluginInstance::AsyncJob {
    mutable std::mutex mutex;
    std::condition_variable cv;
    JobStatus status{JobStatus::pending};
    int32_t result{PS_ERROR};
    std::exception_ptr error;
    bool cancel_requested{false};
    std::thread worker;
};

PluginInstance::PluginInstance(
    PluginDescriptor descriptor,
    PluginInstanceConfig config,
    RuntimeBindings bindings,
    std::unique_ptr<PluginInstanceBackend> backend
)
    : descriptor_{std::move(descriptor)}
    , config_{std::move(config)}
    , bindings_{std::move(bindings)}
    , backend_{std::move(backend)}
{
}

PluginInstance::~PluginInstance()
{
    join_jobs();
}

const PluginDescriptor& PluginInstance::descriptor() const noexcept
{
    return descriptor_;
}

const PluginInstanceConfig& PluginInstance::config() const noexcept
{
    return config_;
}

std::filesystem::path PluginInstance::loaded_path() const
{
    return backend_ == nullptr ? std::filesystem::path{} : backend_->loaded_path();
}

SharedMemoryChannel& PluginInstance::port(std::string_view port_id)
{
    const auto found = std::find_if(bindings_.ports.begin(), bindings_.ports.end(), [port_id](const auto& binding) {
        return binding.descriptor.id == port_id;
    });
    if (found == bindings_.ports.end()) {
        throw PluginError{"Port is not bound: " + std::string{port_id}};
    }
    return *found->channel;
}

const SharedMemoryChannel& PluginInstance::port(std::string_view port_id) const
{
    const auto found = std::find_if(bindings_.ports.begin(), bindings_.ports.end(), [port_id](const auto& binding) {
        return binding.descriptor.id == port_id;
    });
    if (found == bindings_.ports.end()) {
        throw PluginError{"Port is not bound: " + std::string{port_id}};
    }
    return *found->channel;
}

SharedPropertyBlock& PluginInstance::properties()
{
    if (!bindings_.properties) {
        throw PluginError{"No property block is bound"};
    }
    return *bindings_.properties;
}

const SharedPropertyBlock& PluginInstance::properties() const
{
    if (!bindings_.properties) {
        throw PluginError{"No property block is bound"};
    }
    return *bindings_.properties;
}

int32_t PluginInstance::invoke(std::string_view entrypoint_id)
{
    switch (policy_for(entrypoint_id)) {
    case ConcurrencyPolicy::instance_serialized: {
        std::lock_guard<std::mutex> lock{instance_mutex_};
        return invoke_locked(entrypoint_id);
    }
    case ConcurrencyPolicy::entrypoint_serialized: {
        std::mutex* mutex = nullptr;
        {
            std::lock_guard<std::mutex> lock{entrypoint_mutexes_mutex_};
            auto& entry = entrypoint_mutexes_[std::string{entrypoint_id}];
            if (!entry) {
                entry = std::make_unique<std::mutex>();
            }
            mutex = entry.get();
        }
        std::lock_guard<std::mutex> lock{*mutex};
        return invoke_locked(entrypoint_id);
    }
    case ConcurrencyPolicy::fully_concurrent:
        return invoke_locked(entrypoint_id);
    }

    return invoke_locked(entrypoint_id);
}

JobHandle PluginInstance::submit(std::string entrypoint_id)
{
    const JobHandle handle = next_job_handle_++;
    auto job = std::make_shared<AsyncJob>();

    {
        std::lock_guard<std::mutex> lock{jobs_mutex_};
        jobs_.emplace(handle, job);
    }

    job->worker = std::thread([this, entrypoint_id = std::move(entrypoint_id), job]() {
        {
            std::lock_guard<std::mutex> lock{job->mutex};
            if (job->cancel_requested) {
                job->status = JobStatus::canceled;
                job->cv.notify_all();
                return;
            }
            job->status = JobStatus::running;
        }
        job->cv.notify_all();

        try {
            const int32_t result = invoke(entrypoint_id);
            {
                std::lock_guard<std::mutex> lock{job->mutex};
                job->result = result;
                job->status = JobStatus::completed;
            }
        } catch (...) {
            std::lock_guard<std::mutex> lock{job->mutex};
            job->error = std::current_exception();
            job->status = JobStatus::failed;
        }

        job->cv.notify_all();
    });

    return handle;
}

JobStatus PluginInstance::job_status(JobHandle handle) const
{
    auto job = find_job(handle);
    std::lock_guard<std::mutex> lock{job->mutex};
    return job->status;
}

int32_t PluginInstance::wait(JobHandle handle)
{
    auto job = find_job(handle);
    {
        std::unique_lock<std::mutex> lock{job->mutex};
        job->cv.wait(lock, [&job]() {
            return is_terminal(job->status);
        });
        if (job->status == JobStatus::failed && job->error) {
            std::rethrow_exception(job->error);
        }
        if (job->status == JobStatus::canceled) {
            throw PluginError{"Async job was canceled"};
        }
    }

    if (job->worker.joinable() && job->worker.get_id() != std::this_thread::get_id()) {
        job->worker.join();
    }

    std::lock_guard<std::mutex> lock{job->mutex};
    return job->result;
}

std::optional<int32_t> PluginInstance::result(JobHandle handle) const
{
    auto job = find_job(handle);
    std::lock_guard<std::mutex> lock{job->mutex};
    if (job->status != JobStatus::completed) {
        return std::nullopt;
    }
    return job->result;
}

bool PluginInstance::cancel(JobHandle handle)
{
    auto job = find_job(handle);
    std::lock_guard<std::mutex> lock{job->mutex};
    if (job->status != JobStatus::pending) {
        return false;
    }
    job->cancel_requested = true;
    return true;
}

const EntrypointDescriptor& PluginInstance::find_entrypoint(std::string_view entrypoint_id) const
{
    const auto found = std::find_if(descriptor_.entrypoints.begin(), descriptor_.entrypoints.end(), [entrypoint_id](const auto& entrypoint) {
        return entrypoint.id == entrypoint_id;
    });
    if (found == descriptor_.entrypoints.end()) {
        throw PluginError{"Entrypoint is not declared: " + std::string{entrypoint_id}};
    }
    return *found;
}

ConcurrencyPolicy PluginInstance::policy_for(std::string_view entrypoint_id) const
{
    const auto found = std::find_if(descriptor_.entrypoints.begin(), descriptor_.entrypoints.end(), [entrypoint_id](const auto& entrypoint) {
        return entrypoint.id == entrypoint_id;
    });
    return found == descriptor_.entrypoints.end() ? descriptor_.concurrency : found->concurrency;
}

int32_t PluginInstance::invoke_locked(std::string_view entrypoint_id)
{
    find_entrypoint(entrypoint_id);
    InvocationContext context{bindings_};
    return backend_->invoke(entrypoint_id, context);
}

std::shared_ptr<PluginInstance::AsyncJob> PluginInstance::find_job(JobHandle handle) const
{
    std::lock_guard<std::mutex> lock{jobs_mutex_};
    const auto found = jobs_.find(handle);
    if (found == jobs_.end()) {
        throw PluginError{"Async job does not exist"};
    }
    return found->second;
}

void PluginInstance::join_jobs() noexcept
{
    std::vector<std::shared_ptr<AsyncJob>> jobs;
    {
        std::lock_guard<std::mutex> lock{jobs_mutex_};
        jobs.reserve(jobs_.size());
        for (auto& job : jobs_) {
            jobs.push_back(job.second);
        }
        jobs_.clear();
    }

    for (auto& job : jobs) {
        if (job->worker.joinable() && job->worker.get_id() != std::this_thread::get_id()) {
            job->worker.join();
        }
    }
}

}

