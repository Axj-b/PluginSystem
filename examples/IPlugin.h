#pragma once

#include <pluginsystem/plugin_api.h>

namespace pluginsystem::examples {

class IPlugin {
public:
    virtual ~IPlugin() = default;

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

} // namespace pluginsystem::examples
