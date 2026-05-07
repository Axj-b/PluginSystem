#pragma once

#include <pluginsystem/sdk/plugin.hpp>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace pluginsystem::sdk {

enum class PortDirection {
    Input,
    Output
};

enum class PortAccessMode {
    DirectBlock,
    BufferedLatest
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
        const auto& context = InvocationScope::current();
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
        const auto& context = InvocationScope::current();
        const auto result = context.write_port(context.user_data, id_.c_str(), &value, sizeof(T));
        if (result != PS_OK) {
            throw std::runtime_error{"Failed to write output port '" + id_ + "'."};
        }
    }

private:
    std::string id_;
    std::string name_;
};

inline ps_port_direction to_c_direction(PortDirection direction)
{
    return direction == PortDirection::Output ? PS_PORT_OUTPUT : PS_PORT_INPUT;
}

inline ps_port_access_mode to_c_access_mode(PortAccessMode access_mode)
{
    return access_mode == PortAccessMode::BufferedLatest ? PS_PORT_BUFFERED_LATEST : PS_PORT_DIRECT_BLOCK;
}

} // namespace pluginsystem::sdk
