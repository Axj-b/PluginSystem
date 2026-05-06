#include <pluginsystem/error.hpp>

namespace pluginsystem {

PluginError::PluginError(const std::string& message)
    : std::runtime_error{message}
{
}

}

