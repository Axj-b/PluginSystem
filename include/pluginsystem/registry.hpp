#pragma once

#include <pluginsystem/providers.hpp>

#include <memory>
#include <string_view>
#include <vector>

namespace pluginsystem {

struct GraphConfig;
class GraphRuntime;

class PluginRegistry {
public:
    explicit PluginRegistry(PluginHost host = {});

    void add_provider(std::unique_ptr<PluginProvider> provider);
    void register_builtin(BuiltinPluginDefinition definition);

    std::vector<PluginDescriptor> discover_plugins();
    std::unique_ptr<PluginInstance> create_instance(std::string_view plugin_id, PluginInstanceConfig config);
    std::unique_ptr<GraphRuntime> create_graph(GraphConfig config);

    PluginHost& host() noexcept;
    const PluginHost& host() const noexcept;

private:
    friend class GraphRuntime;

    struct CacheEntry {
        PluginDescriptor descriptor;
        PluginProvider* provider{nullptr};
    };

    RuntimeBindings create_bindings(const PluginDescriptor& descriptor, const PluginInstanceConfig& config);
    std::unique_ptr<PluginInstance> create_instance_with_bindings(
        std::string_view plugin_id,
        PluginInstanceConfig config,
        RuntimeBindings bindings
    );
    CacheEntry& find_entry(std::string_view plugin_id);
    void rebuild_cache();

    PluginHost host_;
    std::vector<std::unique_ptr<PluginProvider>> providers_;
    BuiltinPluginProvider* builtin_provider_{nullptr};
    std::vector<CacheEntry> cache_;
};

}
