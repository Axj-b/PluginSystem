#include <pluginsystem/types.hpp>

#include "detail/plugin_utils.hpp"

namespace pluginsystem {

std::string make_shared_memory_name(
    std::string_view blueprint_name,
    std::string_view instance_name,
    std::string_view member_name,
    std::string_view kind
)
{
    return "Local\\PluginSystem_"
        + detail::sanitize_name_part(blueprint_name)
        + "_"
        + detail::sanitize_name_part(instance_name)
        + "_"
        + detail::sanitize_name_part(member_name)
        + "_"
        + detail::sanitize_name_part(kind);
}

bool is_plugin_library_path(const std::filesystem::path& path)
{
    const auto extension = detail::lower_extension(path);
#if defined(_WIN32)
    return extension == ".dll";
#elif defined(__APPLE__)
    return extension == ".dylib";
#else
    return extension == ".so";
#endif
}

}

