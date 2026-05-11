#pragma once

#include <pluginsystem/sdk/plugin.hpp>
#include <pluginsystem/sdk/ports.hpp>
#include <pluginsystem/sdk/properties.hpp>
#include <pluginsystem/sdk/type_name.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace pluginsystem::sdk {

struct EntrypointDescription {
    std::string id;
    std::string name;
    std::string description;
    ps_concurrency_policy concurrency{PS_CONCURRENCY_ENTRYPOINT_SERIALIZED};
    std::vector<std::string> input_port_ids;
    std::vector<std::string> output_port_ids;
    std::vector<std::string> trigger_port_ids;
    std::function<int32_t(void*)> invoke;
};

struct PluginDescription {
    std::string id;
    std::string name;
    std::string version;
    std::string description;
    ps_concurrency_policy concurrency{PS_CONCURRENCY_ENTRYPOINT_SERIALIZED};
    std::vector<PortDescription> ports;
    std::vector<PropertyDescription> properties;
    std::vector<EntrypointDescription> entrypoints;
    std::uint64_t raw_property_block_size{0};
};

template <typename TPlugin>
class PluginRegistration {
public:
    class EntrypointBuilder {
    public:
        EntrypointBuilder(PluginRegistration& registration, std::size_t entrypoint_index)
            : registration_{registration}
            , entrypoint_index_{entrypoint_index}
        {
        }

        EntrypointBuilder& description(std::string description)
        {
            entrypoint().description = std::move(description);
            return *this;
        }

        EntrypointBuilder& name(std::string name)
        {
            entrypoint().name = std::move(name);
            return *this;
        }

        EntrypointBuilder& concurrency(ps_concurrency_policy concurrency)
        {
            entrypoint().concurrency = concurrency;
            return *this;
        }

        template <typename T>
        EntrypointBuilder& reads(InputPort<T> TPlugin::* port)
        {
            entrypoint().input_port_ids.push_back(registration_.port_id(port));
            return *this;
        }

        template <typename T>
        EntrypointBuilder& writes(OutputPort<T> TPlugin::* port)
        {
            entrypoint().output_port_ids.push_back(registration_.port_id(port));
            return *this;
        }

        template <typename T>
        EntrypointBuilder& triggeredBy(InputPort<T> TPlugin::* port)
        {
            entrypoint().trigger_port_ids.push_back(registration_.port_id(port));
            return *this;
        }

    private:
        EntrypointDescription& entrypoint()
        {
            return registration_.description_.entrypoints[entrypoint_index_];
        }

        PluginRegistration& registration_;
        std::size_t entrypoint_index_{0};
    };

    void set_plugin(
        std::string id,
        std::string name,
        std::string version,
        std::string description,
        ps_concurrency_policy concurrency = PS_CONCURRENCY_ENTRYPOINT_SERIALIZED
    )
    {
        description_.id = std::move(id);
        description_.name = std::move(name);
        description_.version = std::move(version);
        description_.description = std::move(description);
        description_.concurrency = concurrency;
    }

    template <typename T>
    void input(InputPort<T> TPlugin::* port, PortAccessMode access_mode)
    {
        register_port(port, PortDirection::Input, access_mode);
    }

    template <typename T>
    void output(OutputPort<T> TPlugin::* port, PortAccessMode access_mode)
    {
        register_port(port, PortDirection::Output, access_mode);
    }

    template <typename T>
    void property(Property<T> TPlugin::* property, bool readable = true, bool writable = true,
                  PropertyConstraints constraints = {})
    {
        const auto& property_instance = probe_.*property;
        register_property_description<T>(property_instance.id(), property_instance.name(), readable, writable, constraints);
    }

    void raw_property_block_size(std::uint64_t size)
    {
        description_.raw_property_block_size = size;
    }

    template <typename TResult>
    EntrypointBuilder entrypoint(std::string id, TResult (TPlugin::* method)())
    {        static_assert(
            std::is_same_v<TResult, void> || std::is_same_v<TResult, int>,
            "Plugin entrypoints must return void or int."
        );

        auto name = id;

        description_.entrypoints.push_back(EntrypointDescription{
            std::move(id),
            std::move(name),
            {},
            description_.concurrency,
            {},
            {},
            {},
            [method](void* plugin) {
                auto& typed_plugin = *static_cast<TPlugin*>(plugin);
                if constexpr (std::is_same_v<TResult, void>) {
                    (typed_plugin.*method)();
                    return static_cast<int32_t>(PS_OK);
                } else {
                    return static_cast<int32_t>((typed_plugin.*method)());
                }
            },
        });

        return EntrypointBuilder{*this, description_.entrypoints.size() - 1};
    }

    template <typename TResult>
    EntrypointBuilder entrypoint(std::string id, TResult (TPlugin::* method)(void*))
    {
        static_assert(
            std::is_same_v<TResult, void> || std::is_same_v<TResult, int>,
            "Plugin entrypoints must return void or int."
        );

        auto name = id;

        description_.entrypoints.push_back(EntrypointDescription{
            std::move(id),
            std::move(name),
            {},
            description_.concurrency,
            {},
            {},
            {},
            [method](void* plugin) {
                const auto& ctx = InvocationScope::current();
                constexpr auto kUserCtxEnd = static_cast<std::uint32_t>(
                    offsetof(ps_invocation_context, user_context)
                    + sizeof(ps_invocation_context::user_context));
                void* user_ctx = (ctx.struct_size >= kUserCtxEnd) ? ctx.user_context : nullptr;
                auto& typed_plugin = *static_cast<TPlugin*>(plugin);
                if constexpr (std::is_same_v<TResult, void>) {
                    (typed_plugin.*method)(user_ctx);
                    return static_cast<int32_t>(PS_OK);
                } else {
                    return static_cast<int32_t>((typed_plugin.*method)(user_ctx));
                }
            },
        });

        return EntrypointBuilder{*this, description_.entrypoints.size() - 1};
    }

    const PluginDescription& description() const noexcept
    {
        return description_;
    }

private:
    template <typename T>
    void register_port(InputPort<T> TPlugin::* port, PortDirection direction, PortAccessMode access_mode)
    {
        const auto& port_instance = probe_.*port;
        register_port_description<T>(port_instance.id(), port_instance.name(), direction, access_mode);
    }

    template <typename T>
    void register_port(OutputPort<T> TPlugin::* port, PortDirection direction, PortAccessMode access_mode)
    {
        const auto& port_instance = probe_.*port;
        register_port_description<T>(port_instance.id(), port_instance.name(), direction, access_mode);
    }

    template <typename T>
    void register_port_description(
        std::string id,
        std::string name,
        PortDirection direction,
        PortAccessMode access_mode
    )
    {
        static_assert(std::is_trivially_copyable_v<T>, "Shared-memory port sample types must be trivially copyable.");

        description_.ports.push_back(PortDescription{
            std::move(id),
            std::move(name),
            TypeName<T>::value(),
            direction,
            access_mode,
            sizeof(T),
            alignof(T),
        });
    }

    template <typename T>
    void register_property_description(std::string id, std::string name, bool readable, bool writable,
                                       const PropertyConstraints& constraints)
    {
        static_assert(std::is_trivially_copyable_v<T>, "Shared-memory property types must be trivially copyable.");

        description_.properties.push_back(PropertyDescription{
            std::move(id),
            std::move(name),
            TypeName<T>::value(),
            sizeof(T),
            readable,
            writable,
            constraints.default_value,
            constraints.min_value,
            constraints.max_value,
            constraints.enum_options,
        });
    }

    template <typename T>
    std::string port_id(InputPort<T> TPlugin::* port) const
    {
        return (probe_.*port).id();
    }

    template <typename T>
    std::string port_id(OutputPort<T> TPlugin::* port) const
    {
        return (probe_.*port).id();
    }

    static_assert(
        std::is_default_constructible_v<TPlugin>,
        "Plugin types must be default-constructible (required for port/property ID discovery via PluginRegistration)."
    );
    TPlugin probe_{};
    PluginDescription description_;
};

} // namespace pluginsystem::sdk
