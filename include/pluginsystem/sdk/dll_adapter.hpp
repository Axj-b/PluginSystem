#pragma once

#include <pluginsystem/plugin_api.h>
#include <pluginsystem/sdk/plugin.hpp>
#include <pluginsystem/sdk/ports.hpp>
#include <pluginsystem/sdk/registration.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace pluginsystem::sdk {

template <typename TPlugin>
class DllAdapter {
public:
    static int32_t discover(const ps_host_context* host, ps_plugin_discovery* out_discovery)
    {
        (void)host;
        if (out_discovery == nullptr || out_discovery->abi_version != PLUGINSYSTEM_ABI_VERSION) {
            return PS_INVALID_ARGUMENT;
        }

        out_discovery->descriptor = &descriptor_store().plugin;
        return PS_OK;
    }

    static int32_t create_instance(
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
        if (config->port_count != store.plugin.port_count || config->property_count != store.plugin.property_count) {
            return PS_INVALID_ARGUMENT;
        }

        auto adapter = std::make_unique<Instance>();
        adapter->plugin = std::make_unique<TPlugin>();

        if (adapter->plugin->Init() != PS_OK) {
            return PS_ERROR;
        }

        out_instance->abi_version = PLUGINSYSTEM_ABI_VERSION;
        out_instance->struct_size = static_cast<std::uint32_t>(sizeof(ps_plugin_instance));
        out_instance->instance = adapter.release();
        out_instance->invoke = &invoke;
        out_instance->destroy = &destroy;
        return PS_OK;
    }

private:
    struct DescriptorStore {
        PluginDescription description;
        std::vector<ps_port_descriptor> ports;
        std::vector<ps_property_descriptor> properties;
        std::vector<ps_entrypoint_descriptor> entrypoints;
        std::vector<std::vector<const char*>> input_port_ids;
        std::vector<std::vector<const char*>> output_port_ids;
        std::vector<std::vector<const char*>> enum_option_ptrs;
        std::unordered_map<std::string, std::size_t> entrypoint_index;
        ps_plugin_descriptor plugin{};

        DescriptorStore()
        {
            PluginRegistration<TPlugin> registration;
            TPlugin::Register(registration);
            description = registration.description();

            ports.reserve(description.ports.size());
            for (const auto& port : description.ports) {
                ports.push_back(ps_port_descriptor{
                    static_cast<std::uint32_t>(sizeof(ps_port_descriptor)),
                    port.id.c_str(),
                    port.name.c_str(),
                    to_c_direction(port.direction),
                    to_c_access_mode(port.access_mode),
                    port.byte_size,
                    port.alignment,
                    port.type_name.c_str(),
                });
            }

            properties.reserve(description.properties.size());
            enum_option_ptrs.reserve(description.properties.size());
            for (const auto& property : description.properties) {
                enum_option_ptrs.push_back({});
                for (const auto& opt : property.enum_options) {
                    enum_option_ptrs.back().push_back(opt.c_str());
                }
                const auto* opts_ptr = enum_option_ptrs.back().empty()
                    ? nullptr : enum_option_ptrs.back().data();
                properties.push_back(ps_property_descriptor{
                    static_cast<std::uint32_t>(sizeof(ps_property_descriptor)),
                    property.id.c_str(),
                    property.name.c_str(),
                    property.type_name.c_str(),
                    property.byte_size,
                    property.readable ? 1u : 0u,
                    property.writable ? 1u : 0u,
                    property.default_value.has_value() ? 1u : 0u,
                    (property.min_value.has_value() && property.max_value.has_value()) ? 1u : 0u,
                    property.default_value.value_or(0.0),
                    property.min_value.value_or(0.0),
                    property.max_value.value_or(0.0),
                    static_cast<std::uint32_t>(enum_option_ptrs.back().size()),
                    opts_ptr,
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

                entrypoint_index.emplace(entrypoint.id, index);
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
            plugin.properties = properties.data();
            plugin.property_count = static_cast<std::uint32_t>(properties.size());
            plugin.raw_property_block_size = description.raw_property_block_size;
        }
    };

    struct Instance {
        std::unique_ptr<TPlugin> plugin;
    };

    static DescriptorStore& descriptor_store()
    {
        static DescriptorStore store;
        return store;
    }

    static int32_t invoke(void* instance, const char* entrypoint_id, const ps_invocation_context* context)
    {
        auto* adapter = static_cast<Instance*>(instance);
        if (adapter == nullptr || adapter->plugin == nullptr || entrypoint_id == nullptr || context == nullptr) {
            return PS_INVALID_ARGUMENT;
        }

        const auto& store = descriptor_store();
        const auto found = store.entrypoint_index.find(entrypoint_id);
        if (found == store.entrypoint_index.end()) {
            return PS_NOT_FOUND;
        }

        try {
            InvocationScope invocation_scope{context};
            return store.description.entrypoints[found->second].invoke(*adapter->plugin);
        } catch (...) {
            return PS_ERROR;
        }
    }

    static void destroy(void* instance)
    {
        auto* adapter = static_cast<Instance*>(instance);
        if (adapter != nullptr && adapter->plugin != nullptr) {
            (void)adapter->plugin->Stop();
        }
        delete adapter;
    }
};

} // namespace pluginsystem::sdk

#define PLUGINSYSTEM_EXPORT_PLUGIN(plugin_type) \
    extern "C" PLUGINSYSTEM_EXPORT int32_t pluginsystem_discover_plugin( \
        const ps_host_context* host, \
        ps_plugin_discovery* out_discovery \
    ) \
    { \
        return ::pluginsystem::sdk::DllAdapter<plugin_type>::discover(host, out_discovery); \
    } \
    extern "C" PLUGINSYSTEM_EXPORT int32_t pluginsystem_create_plugin_instance( \
        const ps_host_context* host, \
        const ps_instance_config* config, \
        ps_plugin_instance* out_instance \
    ) \
    { \
        return ::pluginsystem::sdk::DllAdapter<plugin_type>::create_instance(host, config, out_instance); \
    }
