#include "IPlugin.h"
#include "MyUserPlugin.h"

#include <pluginsystem/plugin_api.h>

#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

struct DescriptorStore {
    ExamplePluginSdk::PluginDescription description;
    std::vector<ps_port_descriptor> ports;
    std::vector<ps_entrypoint_descriptor> entrypoints;
    std::vector<std::vector<const char*>> input_port_ids;
    std::vector<std::vector<const char*>> output_port_ids;
    ps_plugin_descriptor plugin{};

    DescriptorStore()
    {
        ExamplePluginSdk::PluginRegistration<MyUserPlugin> registration;
        MyUserPlugin::Register(registration);
        description = registration.description();

        ports.reserve(description.ports.size());
        for (const auto& port : description.ports) {
            ports.push_back(ps_port_descriptor{
                static_cast<std::uint32_t>(sizeof(ps_port_descriptor)),
                port.id.c_str(),
                port.name.c_str(),
                ExamplePluginSdk::to_c_direction(port.direction),
                ExamplePluginSdk::to_c_access_mode(port.access_mode),
                port.byte_size,
                port.alignment,
                port.type_name.c_str(),
            });
        }

        input_port_ids.resize(description.entrypoints.size());
        output_port_ids.resize(description.entrypoints.size());
        entrypoints.reserve(description.entrypoints.size());

        for (std::size_t index = 0; index < description.entrypoints.size(); ++index) {
            const auto& entrypoint = description.entrypoints[index];

            input_port_ids[index].reserve(entrypoint.input_port_ids.size());
            for (const auto& id : entrypoint.input_port_ids) {
                input_port_ids[index].push_back(id.c_str());
            }

            output_port_ids[index].reserve(entrypoint.output_port_ids.size());
            for (const auto& id : entrypoint.output_port_ids) {
                output_port_ids[index].push_back(id.c_str());
            }

            entrypoints.push_back(ps_entrypoint_descriptor{
                static_cast<std::uint32_t>(sizeof(ps_entrypoint_descriptor)),
                entrypoint.id.c_str(),
                entrypoint.name.c_str(),
                entrypoint.description.c_str(),
                entrypoint.concurrency,
                input_port_ids[index].data(),
                static_cast<std::uint32_t>(input_port_ids[index].size()),
                output_port_ids[index].data(),
                static_cast<std::uint32_t>(output_port_ids[index].size()),
            });
        }

        plugin.abi_version = PLUGINSYSTEM_ABI_VERSION;
        plugin.struct_size = static_cast<std::uint32_t>(sizeof(ps_plugin_descriptor));
        plugin.id = description.id.c_str();
        plugin.name = description.name.c_str();
        plugin.version = description.version.c_str();
        plugin.description = description.description.c_str();
        plugin.concurrency = description.concurrency;
        plugin.entrypoints = entrypoints.data();
        plugin.entrypoint_count = static_cast<std::uint32_t>(entrypoints.size());
        plugin.ports = ports.data();
        plugin.port_count = static_cast<std::uint32_t>(ports.size());
        plugin.properties = nullptr;
        plugin.property_count = 0;
        plugin.raw_property_block_size = 0;
    }
};

DescriptorStore& descriptor_store()
{
    static DescriptorStore store;
    return store;
}

struct PluginAdapterInstance {
    std::unique_ptr<MyUserPlugin> plugin;
};

int32_t invoke(void* instance, const char* entrypoint_id, const ps_invocation_context* context)
{
    auto* adapter = static_cast<PluginAdapterInstance*>(instance);
    if (adapter == nullptr || adapter->plugin == nullptr || entrypoint_id == nullptr || context == nullptr) {
        return PS_INVALID_ARGUMENT;
    }

    if (std::strcmp(entrypoint_id, "Process") != 0) {
        return PS_NOT_FOUND;
    }

    try {
        ExamplePluginSdk::PluginInvocationScope invocation_scope{context};
        adapter->plugin->Process();
        return PS_OK;
    } catch (...) {
        return PS_ERROR;
    }
}

void destroy(void* instance)
{
    auto* adapter = static_cast<PluginAdapterInstance*>(instance);
    if (adapter != nullptr && adapter->plugin != nullptr) {
        (void)adapter->plugin->Stop();
    }
    delete adapter;
}

} // namespace

extern "C" PLUGINSYSTEM_EXPORT int32_t pluginsystem_discover_plugin(
    const ps_host_context* host,
    ps_plugin_discovery* out_discovery
)
{
    (void)host;
    if (out_discovery == nullptr || out_discovery->abi_version != PLUGINSYSTEM_ABI_VERSION) {
        return PS_INVALID_ARGUMENT;
    }

    out_discovery->descriptor = &descriptor_store().plugin;
    return PS_OK;
}

extern "C" PLUGINSYSTEM_EXPORT int32_t pluginsystem_create_plugin_instance(
    const ps_host_context* host,
    const ps_instance_config* config,
    ps_plugin_instance* out_instance
)
{
    (void)host;
    if (config == nullptr || out_instance == nullptr || config->abi_version != PLUGINSYSTEM_ABI_VERSION) {
        return PS_INVALID_ARGUMENT;
    }

    const auto& store = descriptor_store();
    if (config->port_count != store.plugin.port_count) {
        return PS_ERROR;
    }

    auto adapter = std::make_unique<PluginAdapterInstance>();
    adapter->plugin = std::make_unique<MyUserPlugin>();

    if (adapter->plugin->Init() != PS_OK) {
        return PS_ERROR;
    }
    if (adapter->plugin->Start() != PS_OK) {
        return PS_ERROR;
    }

    out_instance->abi_version = PLUGINSYSTEM_ABI_VERSION;
    out_instance->struct_size = static_cast<std::uint32_t>(sizeof(ps_plugin_instance));
    out_instance->instance = adapter.release();
    out_instance->invoke = invoke;
    out_instance->destroy = destroy;
    return PS_OK;
}
