#pragma once

#include <pluginsystem/plugin_api.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pluginsystem {

enum class LogLevel {
    trace = PS_LOG_TRACE,
    debug = PS_LOG_DEBUG,
    info = PS_LOG_INFO,
    warning = PS_LOG_WARNING,
    error = PS_LOG_ERROR
};

enum class PortDirection {
    input = PS_PORT_INPUT,
    output = PS_PORT_OUTPUT
};

enum class PortAccessMode {
    direct_block = PS_PORT_DIRECT_BLOCK,
    buffered_latest = PS_PORT_BUFFERED_LATEST
};

enum class ConcurrencyPolicy {
    instance_serialized = PS_CONCURRENCY_INSTANCE_SERIALIZED,
    entrypoint_serialized = PS_CONCURRENCY_ENTRYPOINT_SERIALIZED,
    fully_concurrent = PS_CONCURRENCY_FULLY_CONCURRENT
};

enum class JobStatus {
    pending,
    running,
    completed,
    failed,
    canceled
};

using JobHandle = std::uint64_t;

struct PluginHost {
    std::function<void(LogLevel level, std::string_view source, std::string_view message)> log;
    std::function<void*(std::string_view service_name, std::uint32_t service_version)> get_service;
};

struct PortDescriptor {
    std::string id;
    std::string name;
    PortDirection direction{PortDirection::input};
    PortAccessMode access_mode{PortAccessMode::direct_block};
    std::uint64_t byte_size{0};
    std::uint64_t alignment{alignof(std::max_align_t)};
    std::string type_name;
};

struct PropertyDescriptor {
    std::string id;
    std::string name;
    std::string type_name;
    std::uint64_t byte_size{0};
    bool readable{true};
    bool writable{true};
    std::optional<double> default_value;
    std::optional<double> min_value;
    std::optional<double> max_value;
    std::vector<std::string> enum_options;
};

struct EntrypointDescriptor {
    std::string id;
    std::string name;
    std::string description;
    ConcurrencyPolicy concurrency{ConcurrencyPolicy::instance_serialized};
    std::vector<std::string> input_port_ids;
    std::vector<std::string> output_port_ids;
};

struct PluginDescriptor {
    std::string provider_id;
    std::string id;
    std::string name;
    std::string version;
    std::string description;
    ConcurrencyPolicy concurrency{ConcurrencyPolicy::instance_serialized};
    std::vector<EntrypointDescriptor> entrypoints;
    std::vector<PortDescriptor> ports;
    std::vector<PropertyDescriptor> properties;
    std::uint64_t raw_property_block_size{0};
};

struct PluginInstanceConfig {
    std::string blueprint_name{"Blueprint"};
    std::string instance_name{"Instance"};
    std::filesystem::path runtime_directory{"pluginsystem_runtime"};
};

std::string make_shared_memory_name(
    std::string_view blueprint_name,
    std::string_view instance_name,
    std::string_view member_name,
    std::string_view kind
);

bool is_plugin_library_path(const std::filesystem::path& path);

}

