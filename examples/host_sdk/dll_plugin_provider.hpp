#pragma once

#include <pluginsystem/providers.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace pluginsystem {

class DllPluginProvider final : public PluginProvider {
public:
    explicit DllPluginProvider(std::filesystem::path library_path, std::string provider_id = {});

    std::string provider_id() const override;
    std::vector<PluginDescriptor> discover() override;
    std::unique_ptr<PluginInstanceBackend> create_instance(
        const PluginDescriptor& descriptor,
        const PluginInstanceConfig& config,
        const RuntimeBindings& bindings
    ) override;

private:
    std::filesystem::path library_path_;
    std::string provider_id_;
};

}
