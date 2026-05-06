#pragma once

#include <pluginsystem/plugin_api.h>
#include <pluginsystem/types.hpp>

#include <filesystem>
#include <string>
#include <string_view>

namespace pluginsystem::detail {

std::string safe_string(const char* value);
std::string lower_extension(std::filesystem::path path);
std::string sanitize_name_part(std::string_view value);
std::string provider_id_for_path(const std::filesystem::path& path);

PortDirection to_port_direction(ps_port_direction value);
PortAccessMode to_port_access_mode(ps_port_access_mode value);
ConcurrencyPolicy to_concurrency_policy(ps_concurrency_policy value);
ps_port_direction to_c_port_direction(PortDirection value);
ps_port_access_mode to_c_port_access_mode(PortAccessMode value);

ps_host_context empty_host_context();

void validate_plugin_descriptor(const PluginDescriptor& descriptor);
PluginDescriptor copy_descriptor(const ps_plugin_descriptor& source, std::string provider_id);

}

