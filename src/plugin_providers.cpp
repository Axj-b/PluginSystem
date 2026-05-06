#include <pluginsystem/providers.hpp>

#include "detail/plugin_utils.hpp"
#include "detail/shared_library.hpp"

#include <pluginsystem/error.hpp>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <utility>

namespace pluginsystem {
namespace {

class DllPluginInstanceBackend final : public PluginInstanceBackend {
public:
    DllPluginInstanceBackend(std::filesystem::path loaded_path, std::unique_ptr<detail::SharedLibrary> library, ps_plugin_instance instance)
        : loaded_path_{std::move(loaded_path)}
        , library_{std::move(library)}
        , instance_{instance}
    {
    }

    ~DllPluginInstanceBackend() override
    {
        if (instance_.destroy != nullptr && instance_.instance != nullptr) {
            instance_.destroy(instance_.instance);
        }
        instance_ = {};
        library_.reset();
    }

    int32_t invoke(std::string_view entrypoint_id, InvocationContext& context) override
    {
        if (instance_.invoke == nullptr) {
            throw PluginError{"DLL plugin instance does not provide invoke"};
        }
        auto c_context = context.c_context();
        const std::string entrypoint{entrypoint_id};
        return instance_.invoke(instance_.instance, entrypoint.c_str(), &c_context);
    }

    std::filesystem::path loaded_path() const override
    {
        return loaded_path_;
    }

private:
    std::filesystem::path loaded_path_;
    std::unique_ptr<detail::SharedLibrary> library_;
    ps_plugin_instance instance_{};
};

} // namespace

DllPluginProvider::DllPluginProvider(std::filesystem::path library_path, std::string provider_id)
    : library_path_{std::move(library_path)}
    , provider_id_{provider_id.empty() ? detail::provider_id_for_path(library_path_) : std::move(provider_id)}
{
}

std::string DllPluginProvider::provider_id() const
{
    return provider_id_;
}

std::vector<PluginDescriptor> DllPluginProvider::discover()
{
    auto library = std::make_unique<detail::SharedLibrary>(library_path_);
    const auto discover = library->symbol<ps_discover_plugin_fn>(PLUGINSYSTEM_DISCOVER_PLUGIN_SYMBOL);

    auto host_context = detail::empty_host_context();
    ps_plugin_discovery discovery{};
    discovery.abi_version = PLUGINSYSTEM_ABI_VERSION;
    discovery.struct_size = sizeof(ps_plugin_discovery);

    const int32_t result = discover(&host_context, &discovery);
    if (result != PS_OK || discovery.descriptor == nullptr) {
        throw PluginError{"Plugin discovery failed for '" + library_path_.string() + "'"};
    }

    return {detail::copy_descriptor(*discovery.descriptor, provider_id_)};
}

std::unique_ptr<PluginInstanceBackend> DllPluginProvider::create_instance(
    const PluginDescriptor& descriptor,
    const PluginInstanceConfig& config,
    const RuntimeBindings& bindings
)
{
    static std::atomic<std::uint64_t> instance_counter{1};

    std::filesystem::create_directories(config.runtime_directory);

    const auto sequence = instance_counter.fetch_add(1);
    const auto source_path = std::filesystem::absolute(library_path_);
    const auto runtime_path = config.runtime_directory
        / (source_path.stem().string()
           + "_"
           + detail::sanitize_name_part(config.blueprint_name)
           + "_"
           + detail::sanitize_name_part(config.instance_name)
           + "_"
           + std::to_string(sequence)
           + source_path.extension().string());

    std::filesystem::copy_file(source_path, runtime_path, std::filesystem::copy_options::overwrite_existing);

    auto library = std::make_unique<detail::SharedLibrary>(runtime_path);
    const auto create_instance = library->symbol<ps_create_plugin_instance_fn>(PLUGINSYSTEM_CREATE_PLUGIN_INSTANCE_SYMBOL);

    std::vector<ps_port_binding> c_ports;
    c_ports.reserve(bindings.ports.size());
    for (const auto& binding : bindings.ports) {
        c_ports.push_back(ps_port_binding{
            static_cast<std::uint32_t>(sizeof(ps_port_binding)),
            binding.descriptor.id.c_str(),
            binding.channel->name().c_str(),
            binding.channel->payload(),
            binding.channel->payload_size(),
            detail::to_c_port_direction(binding.descriptor.direction),
            detail::to_c_port_access_mode(binding.descriptor.access_mode),
        });
    }

    std::vector<ps_property_binding> c_properties;
    if (bindings.properties) {
        c_properties.reserve(bindings.properties->slots().size());
        for (const auto& slot : bindings.properties->slots()) {
            c_properties.push_back(ps_property_binding{
                static_cast<std::uint32_t>(sizeof(ps_property_binding)),
                slot.descriptor.id.c_str(),
                bindings.properties->name().c_str(),
                slot.offset,
                slot.descriptor.byte_size,
                slot.descriptor.type_name.c_str(),
                slot.descriptor.readable ? 1u : 0u,
                slot.descriptor.writable ? 1u : 0u,
            });
        }
    }

    ps_instance_config c_config{};
    c_config.abi_version = PLUGINSYSTEM_ABI_VERSION;
    c_config.struct_size = static_cast<std::uint32_t>(sizeof(ps_instance_config));
    c_config.blueprint_name = config.blueprint_name.c_str();
    c_config.instance_name = config.instance_name.c_str();
    c_config.ports = c_ports.data();
    c_config.port_count = static_cast<std::uint32_t>(c_ports.size());
    c_config.property_block_name = bindings.properties ? bindings.properties->name().c_str() : nullptr;
    c_config.property_block_payload = bindings.properties ? bindings.properties->memory().payload() : nullptr;
    c_config.property_block_size = bindings.properties ? bindings.properties->memory().payload_size() : 0;
    c_config.properties = c_properties.data();
    c_config.property_count = static_cast<std::uint32_t>(c_properties.size());
    c_config.raw_property_block = bindings.properties ? bindings.properties->raw_property_block() : nullptr;
    c_config.raw_property_block_size = bindings.properties ? bindings.properties->raw_property_block_size() : 0;

    auto host_context = detail::empty_host_context();
    ps_plugin_instance instance{};
    instance.abi_version = PLUGINSYSTEM_ABI_VERSION;
    instance.struct_size = static_cast<std::uint32_t>(sizeof(ps_plugin_instance));

    const int32_t result = create_instance(&host_context, &c_config, &instance);
    if (result != PS_OK) {
        throw PluginError{"Plugin instance creation failed for '" + descriptor.id + "'"};
    }
    if (instance.abi_version != PLUGINSYSTEM_ABI_VERSION || instance.struct_size < sizeof(ps_plugin_instance)) {
        throw PluginError{"Plugin instance ABI is incompatible for '" + descriptor.id + "'"};
    }
    if (instance.invoke == nullptr || instance.destroy == nullptr) {
        throw PluginError{"Plugin instance callbacks are incomplete for '" + descriptor.id + "'"};
    }

    return std::make_unique<DllPluginInstanceBackend>(runtime_path, std::move(library), instance);
}

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
