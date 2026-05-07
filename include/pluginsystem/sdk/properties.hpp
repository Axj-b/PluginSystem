#pragma once

#include <pluginsystem/sdk/plugin.hpp>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace pluginsystem::sdk {

struct PropertyDescription {
    std::string id;
    std::string name;
    std::string type_name;
    std::uint64_t byte_size{0};
    bool readable{true};
    bool writable{true};
};

template <typename T>
class Property {
public:
    using ValueType = T;

    static_assert(std::is_trivially_copyable_v<T>, "Shared-memory property types must be trivially copyable.");

    explicit Property(std::string id, std::string name = {})
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
        const auto result = context.read_property(context.user_data, id_.c_str(), &value, sizeof(T));
        if (result != PS_OK) {
            throw std::runtime_error{"Failed to read property '" + id_ + "'."};
        }
    }

    T read() const
    {
        T value{};
        read(value);
        return value;
    }

    void write(const T& value) const
    {
        const auto& context = InvocationScope::current();
        const auto result = context.write_property(context.user_data, id_.c_str(), &value, sizeof(T));
        if (result != PS_OK) {
            throw std::runtime_error{"Failed to write property '" + id_ + "'."};
        }
    }

private:
    std::string id_;
    std::string name_;
};

} // namespace pluginsystem::sdk
