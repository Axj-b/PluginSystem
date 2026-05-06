#pragma once

#include <pluginsystem/plugin_api.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

namespace ExamplePluginSdk {

enum class PortDirection {
    Input,
    Output
};

enum class PortAccessMode {
    DirectBlock,
    BufferedLatest
};

template <typename T>
struct TypeName {
    static std::string value()
    {
        return typeid(T).name();
    }
};

struct PortDescription {
    std::string id;
    std::string name;
    std::string type_name;
    PortDirection direction{PortDirection::Input};
    PortAccessMode access_mode{PortAccessMode::DirectBlock};
    std::uint64_t byte_size{0};
    std::uint64_t alignment{0};
};

struct EntrypointDescription {
    std::string id;
    std::string name;
    std::string description;
    ps_concurrency_policy concurrency{PS_CONCURRENCY_ENTRYPOINT_SERIALIZED};
    std::vector<std::string> input_port_ids;
    std::vector<std::string> output_port_ids;
    std::vector<std::string> trigger_port_ids;
};

struct PluginDescription {
    std::string id;
    std::string name;
    std::string version;
    std::string description;
    ps_concurrency_policy concurrency{PS_CONCURRENCY_ENTRYPOINT_SERIALIZED};
    std::vector<PortDescription> ports;
    std::vector<EntrypointDescription> entrypoints;
};

class PluginInvocationScope {
public:
    explicit PluginInvocationScope(const ps_invocation_context* context)
        : previous_{current_}
    {
        current_ = context;
    }

    ~PluginInvocationScope()
    {
        current_ = previous_;
    }

    PluginInvocationScope(const PluginInvocationScope&) = delete;
    PluginInvocationScope& operator=(const PluginInvocationScope&) = delete;

    static const ps_invocation_context& current()
    {
        if (current_ == nullptr) {
            throw std::runtime_error{"Plugin port access requires an active invocation context."};
        }
        return *current_;
    }

private:
    const ps_invocation_context* previous_{nullptr};
    inline static thread_local const ps_invocation_context* current_{nullptr};
};

template <typename T>
class InputPort {
public:
    using ValueType = T;

    static_assert(std::is_trivially_copyable_v<T>, "Shared-memory port sample types must be trivially copyable.");

    explicit InputPort(std::string id, std::string name = {})
        : id_{std::move(id)}
        , name_{name.empty() ? id_ : std::move(name)}
    {
    }

    const std::string& id() const noexcept
    {
        return id_;
    }

    const std::string& name() const noexcept
    {
        return name_;
    }

    void read(T& value) const
    {
        const auto& context = PluginInvocationScope::current();
        const auto result = context.read_port(context.user_data, id_.c_str(), &value, sizeof(T));
        if (result != PS_OK) {
            throw std::runtime_error{"Failed to read input port '" + id_ + "'."};
        }
    }

    T read() const
    {
        T value{};
        read(value);
        return value;
    }

private:
    std::string id_;
    std::string name_;
};

template <typename T>
class OutputPort {
public:
    using ValueType = T;

    static_assert(std::is_trivially_copyable_v<T>, "Shared-memory port sample types must be trivially copyable.");

    explicit OutputPort(std::string id, std::string name = {})
        : id_{std::move(id)}
        , name_{name.empty() ? id_ : std::move(name)}
    {
    }

    const std::string& id() const noexcept
    {
        return id_;
    }

    const std::string& name() const noexcept
    {
        return name_;
    }

    void write(const T& value) const
    {
        const auto& context = PluginInvocationScope::current();
        const auto result = context.write_port(context.user_data, id_.c_str(), &value, sizeof(T));
        if (result != PS_OK) {
            throw std::runtime_error{"Failed to write output port '" + id_ + "'."};
        }
    }

private:
    std::string id_;
    std::string name_;
};

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

    virtual void* GetRenderer()
    {
        return nullptr;
    }

    virtual void Process() = 0;
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

    EntrypointBuilder entrypoint(std::string id, void (TPlugin::* method)())
    {
        (void)method;

        description_.entrypoints.push_back(EntrypointDescription{
            id,
            std::move(id),
            {},
            PS_CONCURRENCY_ENTRYPOINT_SERIALIZED,
            {},
            {},
            {},
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
    std::string port_id(InputPort<T> TPlugin::* port) const
    {
        return (probe_.*port).id();
    }

    template <typename T>
    std::string port_id(OutputPort<T> TPlugin::* port) const
    {
        return (probe_.*port).id();
    }

    TPlugin probe_{};
    PluginDescription description_;
};

inline ps_port_direction to_c_direction(PortDirection direction)
{
    return direction == PortDirection::Output ? PS_PORT_OUTPUT : PS_PORT_INPUT;
}

inline ps_port_access_mode to_c_access_mode(PortAccessMode access_mode)
{
    return access_mode == PortAccessMode::BufferedLatest ? PS_PORT_BUFFERED_LATEST : PS_PORT_DIRECT_BLOCK;
}

} // namespace ExamplePluginSdk

