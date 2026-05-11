#include <pluginsystem/registry.hpp>

#include "detail/plugin_utils.hpp"

#include <pluginsystem/error.hpp>
#include <pluginsystem/graph.hpp>

#include <algorithm>
#include <set>
#include <utility>

namespace pluginsystem {

PluginRegistry::PluginRegistry(PluginHost host)
    : host_{std::move(host)}
{
}

void PluginRegistry::add_provider(std::unique_ptr<PluginProvider> provider)
{
    if (!provider) {
        throw PluginError{"Cannot add null plugin provider"};
    }
    providers_.push_back(std::move(provider));
    cache_.clear();
}

void PluginRegistry::register_builtin(BuiltinPluginDefinition definition)
{
    if (builtin_provider_ == nullptr) {
        auto provider = std::make_unique<BuiltinPluginProvider>();
        builtin_provider_ = provider.get();
        add_provider(std::move(provider));
    }
    builtin_provider_->add(std::move(definition));
    cache_.clear();
}

std::vector<PluginDescriptor> PluginRegistry::discover_plugins()
{
    rebuild_cache();

    std::vector<PluginDescriptor> descriptors;
    descriptors.reserve(cache_.size());
    for (const auto& entry : cache_) {
        descriptors.push_back(entry.descriptor);
    }
    return descriptors;
}

std::unique_ptr<PluginInstance> PluginRegistry::create_instance(std::string_view plugin_id, PluginInstanceConfig config)
{
    if (cache_.empty()) {
        rebuild_cache();
    }

    const auto& entry = find_entry(plugin_id);
    auto bindings = create_bindings(entry.descriptor, config);
    return create_instance_with_bindings(plugin_id, std::move(config), std::move(bindings));
}

std::unique_ptr<GraphRuntime> PluginRegistry::create_graph(GraphConfig config)
{
    return std::make_unique<GraphRuntime>(*this, std::move(config));
}

PluginHost& PluginRegistry::host() noexcept
{
    return host_;
}

const PluginHost& PluginRegistry::host() const noexcept
{
    return host_;
}

RuntimeBindings PluginRegistry::create_bindings(const PluginDescriptor& descriptor, const PluginInstanceConfig& config)
{
    RuntimeBindings bindings;
    bindings.ports.reserve(descriptor.ports.size());

    std::set<std::string> shared_memory_names;
    for (const auto& port : descriptor.ports) {
        const std::string kind = port.direction == PortDirection::output ? "output" : "input";
        const auto member_name = port.name.empty() ? port.id : port.name;
        auto name = make_shared_memory_name(config.blueprint_name, config.instance_name, member_name, kind);
        if (!shared_memory_names.insert(name).second) {
            throw PluginError{"Duplicate generated shared memory name: " + name};
        }

        bindings.ports.push_back(PortRuntimeBinding{
            port,
            SharedMemoryChannel::create(std::move(name), port.byte_size),
        });
    }

    const auto properties_name = make_shared_memory_name(config.blueprint_name, config.instance_name, "properties", "properties");
    bindings.properties = SharedPropertyBlock::create(properties_name, descriptor.properties, descriptor.raw_property_block_size);
    return bindings;
}

std::unique_ptr<PluginInstance> PluginRegistry::create_instance_with_bindings(
    std::string_view plugin_id,
    PluginInstanceConfig config,
    RuntimeBindings bindings
)
{
    if (cache_.empty()) {
        rebuild_cache();
    }

    auto& entry = find_entry(plugin_id);
    auto backend = entry.provider->create_instance(entry.descriptor, config, bindings);
    return std::unique_ptr<PluginInstance>{
        new PluginInstance{entry.descriptor, std::move(config), std::move(bindings), std::move(backend)}
    };
}

PluginRegistry::CacheEntry& PluginRegistry::find_entry(std::string_view plugin_id)
{
    if (cache_.empty()) {
        rebuild_cache();
    }

    const auto found = std::find_if(cache_.begin(), cache_.end(), [plugin_id](const CacheEntry& entry) {
        return entry.descriptor.id == plugin_id;
    });
    if (found == cache_.end()) {
        throw PluginError{"Plugin is not discovered: " + std::string{plugin_id}};
    }
    return *found;
}

void PluginRegistry::rebuild_cache()
{
    cache_.clear();
    std::set<std::string> plugin_ids;

    for (const auto& provider : providers_) {
        auto descriptors = provider->discover();
        for (auto& descriptor : descriptors) {
            if (descriptor.provider_id.empty()) {
                descriptor.provider_id = provider->provider_id();
            }
            detail::validate_plugin_descriptor(descriptor);
            if (!plugin_ids.insert(descriptor.id).second) {
                throw PluginError{"Duplicate plugin id discovered: " + descriptor.id};
            }
            cache_.push_back(CacheEntry{std::move(descriptor), provider.get()});
        }
    }
}

}
