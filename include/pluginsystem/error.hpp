#pragma once

#include <stdexcept>
#include <string>

namespace pluginsystem {

class PluginError : public std::runtime_error {
public:
    explicit PluginError(const std::string& message);
};

}

