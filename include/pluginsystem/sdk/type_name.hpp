#pragma once

#include <cstdint>
#include <string>
#include <typeinfo>

namespace pluginsystem::sdk {

template <typename T>
struct TypeName {
    static std::string value()
    {
        return typeid(T).name();
    }
};

template <>
struct TypeName<bool> {
    static std::string value() { return "bool"; }
};

template <>
struct TypeName<float> {
    static std::string value() { return "float"; }
};

template <>
struct TypeName<double> {
    static std::string value() { return "double"; }
};

template <>
struct TypeName<std::int8_t> {
    static std::string value() { return "int8_t"; }
};

template <>
struct TypeName<std::int16_t> {
    static std::string value() { return "int16_t"; }
};

template <>
struct TypeName<std::int32_t> {
    static std::string value() { return "int32_t"; }
};

template <>
struct TypeName<std::int64_t> {
    static std::string value() { return "int64_t"; }
};

template <>
struct TypeName<std::uint8_t> {
    static std::string value() { return "uint8_t"; }
};

template <>
struct TypeName<std::uint16_t> {
    static std::string value() { return "uint16_t"; }
};

template <>
struct TypeName<std::uint32_t> {
    static std::string value() { return "uint32_t"; }
};

template <>
struct TypeName<std::uint64_t> {
    static std::string value() { return "uint64_t"; }
};

} // namespace pluginsystem::sdk
