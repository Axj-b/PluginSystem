#include <pluginsystem/providers.hpp>

#include "detail/plugin_utils.hpp"

#include <pluginsystem/error.hpp>

#include <algorithm>
#include <utility>

namespace pluginsystem {
BuiltinPluginProvider::BuiltinPluginProvider(std::string provider_id)
    : provider_id_{std::move(provider_id)}
{
}

void BuiltinPluginProvider::add(BuiltinPluginDefinition definition)
{
    if (!definition.factory) {
        throw PluginError{"Built-in plugin definition is missing a factory"};
    }
    definition.descriptor.provider_id = provider_id_;
    detail::validate_plugin_descriptor(definition.descriptor);
    definitions_.push_back(std::move(definition));
}

std::string BuiltinPluginProvider::provider_id() const
{
    return provider_id_;
}

std::vector<PluginDescriptor> BuiltinPluginProvider::discover()
{
    std::vector<PluginDescriptor> descriptors;
    descriptors.reserve(definitions_.size());
    for (const auto& definition : definitions_) {
        auto descriptor = definition.descriptor;
        descriptor.provider_id = provider_id_;
        descriptors.push_back(std::move(descriptor));
    }
    return descriptors;
}

std::unique_ptr<PluginInstanceBackend> BuiltinPluginProvider::create_instance(
    const PluginDescriptor& descriptor,
    const PluginInstanceConfig& config,
    const RuntimeBindings& bindings
)
{
    (void)config;
    (void)bindings;

    const auto found = std::find_if(definitions_.begin(), definitions_.end(), [&descriptor](const auto& definition) {
        return definition.descriptor.id == descriptor.id;
    });
    if (found == definitions_.end()) {
        throw PluginError{"Built-in plugin is not registered: " + descriptor.id};
    }

    return found->factory(descriptor, config, bindings);
}

}
