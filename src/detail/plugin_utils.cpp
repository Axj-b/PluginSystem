#include "detail/plugin_utils.hpp"

#include <pluginsystem/error.hpp>

#include <algorithm>
#include <cctype>
#include <set>
#include <utility>

namespace pluginsystem::detail {

std::string safe_string(const char* value)
{
    return value == nullptr ? std::string{} : std::string{value};
}

std::string lower_extension(std::filesystem::path path)
{
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return extension;
}

std::string sanitize_name_part(std::string_view value)
{
    std::string result;
    result.reserve(value.size());

    for (const char character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if (std::isalnum(byte) != 0 || character == '_' || character == '-') {
            result.push_back(character);
        } else {
            result.push_back('_');
        }
    }

    return result.empty() ? "unnamed" : result;
}

std::string provider_id_for_path(const std::filesystem::path& path)
{
    return "dll:" + std::filesystem::absolute(path).string();
}

PortDirection to_port_direction(ps_port_direction value)
{
    return value == PS_PORT_OUTPUT ? PortDirection::output : PortDirection::input;
}

PortAccessMode to_port_access_mode(ps_port_access_mode value)
{
    return value == PS_PORT_BUFFERED_LATEST ? PortAccessMode::buffered_latest : PortAccessMode::direct_block;
}

ConcurrencyPolicy to_concurrency_policy(ps_concurrency_policy value)
{
    switch (value) {
    case PS_CONCURRENCY_ENTRYPOINT_SERIALIZED:
        return ConcurrencyPolicy::entrypoint_serialized;
    case PS_CONCURRENCY_FULLY_CONCURRENT:
        return ConcurrencyPolicy::fully_concurrent;
    case PS_CONCURRENCY_INSTANCE_SERIALIZED:
    default:
        return ConcurrencyPolicy::instance_serialized;
    }
}

ps_port_direction to_c_port_direction(PortDirection value)
{
    return value == PortDirection::output ? PS_PORT_OUTPUT : PS_PORT_INPUT;
}

ps_port_access_mode to_c_port_access_mode(PortAccessMode value)
{
    return value == PortAccessMode::buffered_latest ? PS_PORT_BUFFERED_LATEST : PS_PORT_DIRECT_BLOCK;
}

ps_host_context empty_host_context()
{
    ps_host_context context{};
    context.abi_version = PLUGINSYSTEM_ABI_VERSION;
    return context;
}

void validate_plugin_descriptor(const PluginDescriptor& descriptor)
{
    if (descriptor.id.empty()) {
        throw PluginError{"Plugin descriptor is missing an id"};
    }

    std::set<std::string> entrypoint_ids;
    for (const auto& entrypoint : descriptor.entrypoints) {
        if (entrypoint.id.empty()) {
            throw PluginError{"Plugin '" + descriptor.id + "' has an entrypoint without an id"};
        }
        if (!entrypoint_ids.insert(entrypoint.id).second) {
            throw PluginError{"Plugin '" + descriptor.id + "' has duplicate entrypoint id '" + entrypoint.id + "'"};
        }
    }

    std::set<std::string> port_ids;
    for (const auto& port : descriptor.ports) {
        if (port.id.empty()) {
            throw PluginError{"Plugin '" + descriptor.id + "' has a port without an id"};
        }
        if (port.byte_size == 0 && !port.any_type) {
            throw PluginError{"Plugin '" + descriptor.id + "' port '" + port.id + "' has byte size 0"};
        }
        if (!port_ids.insert(port.id).second) {
            throw PluginError{"Plugin '" + descriptor.id + "' has duplicate port id '" + port.id + "'"};
        }
    }

    std::set<std::string> property_ids;
    for (const auto& property : descriptor.properties) {
        if (property.id.empty()) {
            throw PluginError{"Plugin '" + descriptor.id + "' has a property without an id"};
        }
        if (property.byte_size == 0) {
            throw PluginError{"Plugin '" + descriptor.id + "' property '" + property.id + "' has byte size 0"};
        }
        if (!property_ids.insert(property.id).second) {
            throw PluginError{"Plugin '" + descriptor.id + "' has duplicate property id '" + property.id + "'"};
        }
    }
}

PluginDescriptor copy_descriptor(const ps_plugin_descriptor& source, std::string provider_id)
{
    if (source.abi_version != PLUGINSYSTEM_ABI_VERSION) {
        throw PluginError{"Plugin descriptor ABI version is incompatible"};
    }
    if (source.struct_size < sizeof(ps_plugin_descriptor)) {
        throw PluginError{"Plugin descriptor struct is too small"};
    }

    PluginDescriptor descriptor;
    descriptor.provider_id = std::move(provider_id);
    descriptor.id = safe_string(source.id);
    descriptor.name = safe_string(source.name);
    descriptor.version = safe_string(source.version);
    descriptor.description = safe_string(source.description);
    descriptor.concurrency = to_concurrency_policy(source.concurrency);
    descriptor.raw_property_block_size = source.raw_property_block_size;

    for (std::uint32_t index = 0; index < source.entrypoint_count; ++index) {
        const auto& c_entrypoint = source.entrypoints[index];
        if (c_entrypoint.struct_size < sizeof(ps_entrypoint_descriptor)) {
            throw PluginError{"Entrypoint descriptor struct is too small"};
        }

        EntrypointDescriptor entrypoint;
        entrypoint.id = safe_string(c_entrypoint.id);
        entrypoint.name = safe_string(c_entrypoint.name);
        entrypoint.description = safe_string(c_entrypoint.description);
        entrypoint.concurrency = to_concurrency_policy(c_entrypoint.concurrency);

        for (std::uint32_t input = 0; input < c_entrypoint.input_port_count; ++input) {
            entrypoint.input_port_ids.push_back(safe_string(c_entrypoint.input_port_ids[input]));
        }
        for (std::uint32_t output = 0; output < c_entrypoint.output_port_count; ++output) {
            entrypoint.output_port_ids.push_back(safe_string(c_entrypoint.output_port_ids[output]));
        }

        descriptor.entrypoints.push_back(std::move(entrypoint));
    }

    for (std::uint32_t index = 0; index < source.port_count; ++index) {
        const auto& c_port = source.ports[index];
        if (c_port.struct_size < sizeof(ps_port_descriptor)) {
            throw PluginError{"Port descriptor struct is too small"};
        }

        descriptor.ports.push_back(PortDescriptor{
            safe_string(c_port.id),
            safe_string(c_port.name),
            to_port_direction(c_port.direction),
            to_port_access_mode(c_port.access_mode),
            c_port.byte_size,
            c_port.alignment == 0 ? alignof(std::max_align_t) : c_port.alignment,
            safe_string(c_port.type_name),
        });
    }

    for (std::uint32_t index = 0; index < source.property_count; ++index) {
        const auto& c_property = source.properties[index];
        // Accept any struct at least as large as the v1 layout (without constraint fields).
        constexpr auto kPropertyDescV1Size =
            static_cast<std::uint32_t>(offsetof(ps_property_descriptor, has_default_value));
        // V2 adds constraint fields; V3 adds enum fields.
        constexpr auto kPropertyDescV2Size =
            static_cast<std::uint32_t>(offsetof(ps_property_descriptor, enum_option_count));
        if (c_property.struct_size < kPropertyDescV1Size) {
            throw PluginError{"Property descriptor struct is too small"};
        }

        PropertyDescriptor prop{
            safe_string(c_property.id),
            safe_string(c_property.name),
            safe_string(c_property.type_name),
            c_property.byte_size,
            c_property.readable != 0,
            c_property.writable != 0,
        };

        if (c_property.struct_size >= kPropertyDescV2Size) {
            if (c_property.has_default_value) {
                prop.default_value = c_property.default_value;
            }
            if (c_property.has_range) {
                prop.min_value = c_property.min_value;
                prop.max_value = c_property.max_value;
            }
        }

        if (c_property.struct_size >= static_cast<std::uint32_t>(sizeof(ps_property_descriptor))) {
            for (std::uint32_t opt = 0; opt < c_property.enum_option_count; ++opt) {
                if (c_property.enum_options && c_property.enum_options[opt]) {
                    prop.enum_options.push_back(c_property.enum_options[opt]);
                }
            }
        }

        descriptor.properties.push_back(std::move(prop));
    }

    validate_plugin_descriptor(descriptor);
    return descriptor;
}

}

