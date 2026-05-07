#pragma once

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
struct TypeName<float> {
    static std::string value()
    {
        return "float";
    }
};

} // namespace pluginsystem::sdk
