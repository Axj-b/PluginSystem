#include <recorder_plugins.hpp>

#include <pluginsystem/registry.hpp>

#include <algorithm>
#include <string_view>

namespace pluginsystem::builtins {
namespace {

bool registry_contains_plugin(PluginRegistry& registry, std::string_view plugin_id)
{
    const auto descriptors = registry.discover_plugins();
    return std::any_of(descriptors.begin(), descriptors.end(), [plugin_id](const auto& descriptor) {
        return descriptor.id == plugin_id;
    });
}

} // namespace

void register_default_plugins(PluginRegistry& registry)
{
    if (!registry_contains_plugin(registry, "pluginsystem.builtin.recorder")) {
        registry.register_builtin(make_recorder());
    }
    if (!registry_contains_plugin(registry, "pluginsystem.builtin.replay")) {
        registry.register_builtin(make_replay("pluginsystem.builtin.replay", {}));
    }
}

} // namespace pluginsystem::builtins
