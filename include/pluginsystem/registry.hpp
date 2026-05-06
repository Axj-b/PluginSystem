#pragma once

#include <pluginsystem/providers.hpp>

#include <memory>
#include <string_view>
#include <vector>

namespace pluginsystem {

class PluginRegistry {
public:
    explicit PluginRegistry(PluginHost host = {});

    void add_provider(std::unique_ptr<PluginProvider> provider);
    void add_dll_plugin(const std::filesystem::path& library_path);
    void register_builtin(BuiltinPluginDefinition definition);

    std::vector<PluginDescriptor> discover_plugins();
    std::unique_ptr<PluginInstance> create_instance(std::string_view plugin_id, PluginInstanceConfig config);

    PluginHost& host() noexcept;
    const PluginHost& host() const noexcept;

private:
    struct CacheEntry {
        PluginDescriptor descriptor;
        PluginProvider* provider{nullptr};
    };

    RuntimeBindings create_bindings(const PluginDescriptor& descriptor, const PluginInstanceConfig& config);
    void rebuild_cache();

    PluginHost host_;
    std::vector<std::unique_ptr<PluginProvider>> providers_;
    BuiltinPluginProvider* builtin_provider_{nullptr};
    std::vector<CacheEntry> cache_;
};

}

