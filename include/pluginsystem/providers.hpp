#pragma once

#include <pluginsystem/instance.hpp>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace pluginsystem {

class PluginProvider {
public:
    virtual ~PluginProvider() = default;
    virtual std::string provider_id() const = 0;
    virtual std::vector<PluginDescriptor> discover() = 0;
    virtual std::unique_ptr<PluginInstanceBackend> create_instance(
        const PluginDescriptor& descriptor,
        const PluginInstanceConfig& config,
        const RuntimeBindings& bindings
    ) = 0;
};

struct BuiltinPluginDefinition {
    PluginDescriptor descriptor;
    std::function<std::unique_ptr<PluginInstanceBackend>(
        const PluginDescriptor& descriptor,
        const PluginInstanceConfig& config,
        const RuntimeBindings& bindings
    )> factory;
};

class BuiltinPluginProvider final : public PluginProvider {
public:
    explicit BuiltinPluginProvider(std::string provider_id = "builtin");

    void add(BuiltinPluginDefinition definition);

    std::string provider_id() const override;
    std::vector<PluginDescriptor> discover() override;
    std::unique_ptr<PluginInstanceBackend> create_instance(
        const PluginDescriptor& descriptor,
        const PluginInstanceConfig& config,
        const RuntimeBindings& bindings
    ) override;

private:
    std::string provider_id_;
    std::vector<BuiltinPluginDefinition> definitions_;
};

}

