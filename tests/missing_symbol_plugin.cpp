#include <pluginsystem/plugin_api.h>

extern "C" PLUGINSYSTEM_EXPORT int32_t intentionally_not_the_discovery_symbol()
{
    return PS_OK;
}
