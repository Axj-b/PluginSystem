#pragma once

#include <pluginsystem/plugin_api.h>

#include <stdexcept>

namespace pluginsystem::sdk {

class PluginBase;

class InvocationScope {
public:
    explicit InvocationScope(const ps_invocation_context* context)
        : previous_{current_}
    {
        current_ = context;
    }

    ~InvocationScope()
    {
        current_ = previous_;
    }

    InvocationScope(const InvocationScope&) = delete;
    InvocationScope& operator=(const InvocationScope&) = delete;

    static const ps_invocation_context& current()
    {
        if (current_ == nullptr) {
            throw std::runtime_error{"Plugin SDK access requires an active invocation context."};
        }
        return *current_;
    }

private:
    const ps_invocation_context* previous_{nullptr};
    inline static thread_local const ps_invocation_context* current_{nullptr};
};

class PluginBase {
public:
    virtual ~PluginBase() = default;

    virtual int Init()
    {
        return PS_OK;
    }

    virtual int Start()
    {
        return PS_OK;
    }

    virtual int Stop()
    {
        return PS_OK;
    }

    virtual bool HasRender() const
    {
        return false;
    }

    virtual void Render(void* /*user_context*/) {}
};

} // namespace pluginsystem::sdk
