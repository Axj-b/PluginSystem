#include <recorder_plugins.hpp>

#include "recording_format.hpp"

#include <pluginsystem/instance.hpp>
#include <pluginsystem/plugin_api.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace pluginsystem::builtins {
namespace {

PropertyDescriptor make_path_property(const char* id, const char* display_name)
{
    PropertyDescriptor p;
    p.id = id;
    p.name = display_name;
    p.type_name = "char[256]";
    p.byte_size = 256;
    p.readable = true;
    p.writable = true;
    return p;
}

PropertyDescriptor make_char_array_property(const char* id, const char* display_name, std::uint64_t byte_size)
{
    PropertyDescriptor p;
    p.id = id;
    p.name = display_name;
    p.type_name = "char[" + std::to_string(byte_size) + "]";
    p.byte_size = byte_size;
    p.readable = true;
    p.writable = true;
    return p;
}

PropertyDescriptor make_int32_property(const char* id, const char* display_name, double default_value)
{
    PropertyDescriptor p;
    p.id = id;
    p.name = display_name;
    p.type_name = "int32_t";
    p.byte_size = sizeof(std::int32_t);
    p.readable = true;
    p.writable = true;
    p.default_value = default_value;
    return p;
}

PortDescriptor make_any_input_port(std::uint32_t index)
{
    const auto suffix = std::to_string(index);
    PortDescriptor port;
    port.id = "Input" + suffix;
    port.name = "Input " + suffix;
    port.direction = PortDirection::input;
    port.byte_size = 0;
    port.alignment = alignof(std::max_align_t);
    port.any_type = true;
    return port;
}

struct ActivePort {
    std::string port_id;
    RecordedPortInfo info;
};

struct RecorderState {
    detail::RecordingWriter writer;
    std::uint64_t sequence{0};
    std::uint32_t next_marker_id{1};
    std::uint64_t latest_timestamp_ns{0};
    bool started{false};
    std::vector<ActivePort> active_ports;
    std::vector<std::uint8_t> frame_buffer;
};

std::string read_label(InvocationContext& ctx)
{
    char buffer[64]{};
    ctx.read_property("MarkerLabel", buffer, sizeof(buffer));
    buffer[sizeof(buffer) - 1] = '\0';
    return buffer;
}

} // namespace

BuiltinPluginDefinition make_recorder(std::string plugin_id)
{
    PluginDescriptor descriptor;
    descriptor.id = std::move(plugin_id);
    descriptor.name = "Recorder";
    descriptor.version = "3.0.0";
    descriptor.description = "Records any number of ports to a binary timeline file";
    descriptor.concurrency = ConcurrencyPolicy::instance_serialized;
    descriptor.expandable_inputs = true;

    descriptor.properties.push_back(make_path_property("OutputPath", "Output Path"));
    descriptor.properties.push_back(make_int32_property("AppendMode", "Append Mode", 0.0));
    descriptor.properties.push_back(make_char_array_property("MarkerLabel", "Marker Label", 64));

    for (std::uint32_t i = 0; i < detail::k_max_recorder_ports; ++i) {
        descriptor.ports.push_back(make_any_input_port(i));
    }

    std::vector<std::string> all_input_ids;
    all_input_ids.reserve(detail::k_max_recorder_ports);
    for (std::uint32_t i = 0; i < detail::k_max_recorder_ports; ++i) {
        all_input_ids.push_back("Input" + std::to_string(i));
    }

    descriptor.entrypoints.push_back({"Start", "Start", "Opens the recording file and writes the header",
        ConcurrencyPolicy::instance_serialized, {}, {}});
    descriptor.entrypoints.push_back({"Process", "Process", "Records one timestamp group per active port",
        ConcurrencyPolicy::instance_serialized, all_input_ids, {}});
    descriptor.entrypoints.push_back({"AddMarker", "Add Marker", "Adds a global timeline marker",
        ConcurrencyPolicy::instance_serialized, {}, {}});
    descriptor.entrypoints.push_back({"Stop", "Stop", "Flushes and closes the recording file",
        ConcurrencyPolicy::instance_serialized, {}, {}});

    BuiltinPluginDefinition definition;
    definition.descriptor = std::move(descriptor);
    definition.factory = [](
        const PluginDescriptor&,
        const PluginInstanceConfig& cfg,
        const RuntimeBindings& bindings
    ) {
        auto state = std::make_shared<RecorderState>();

        for (const auto& binding : bindings.ports) {
            if (binding.descriptor.direction != PortDirection::input || binding.descriptor.byte_size == 0) {
                continue;
            }
            state->active_ports.push_back(ActivePort{
                binding.descriptor.id,
                RecordedPortInfo{
                    binding.descriptor.type_name,
                    binding.descriptor.byte_size,
                    binding.descriptor.name,
                    binding.descriptor.access_mode,
                },
            });
        }

        std::uint64_t max_size = 0;
        for (const auto& port : state->active_ports) {
            max_size = std::max(max_size, port.info.byte_size);
        }
        state->frame_buffer.resize(static_cast<std::size_t>(max_size > 0 ? max_size : 1));

        const auto default_path = (cfg.runtime_directory / cfg.instance_name).string() + ".rec";

        return std::make_unique<BuiltinPluginInstanceBackend>(
            [state, default_path](std::string_view entrypoint, InvocationContext& ctx) -> int32_t {
                if (entrypoint == "Start") {
                    char path_buffer[256]{};
                    ctx.read_property("OutputPath", path_buffer, sizeof(path_buffer));
                    path_buffer[sizeof(path_buffer) - 1] = '\0';
                    const std::filesystem::path path = path_buffer[0] != '\0'
                        ? std::filesystem::path{path_buffer}
                        : std::filesystem::path{default_path};

                    std::int32_t append_mode{0};
                    ctx.read_property("AppendMode", &append_mode, sizeof(append_mode));

                    std::vector<RecordedPortInfo> ports;
                    ports.reserve(state->active_ports.size());
                    for (const auto& port : state->active_ports) {
                        ports.push_back(port.info);
                    }

                    if (!state->writer.open(path, ports, append_mode != 0)) {
                        return static_cast<int32_t>(PS_ERROR);
                    }
                    state->sequence = 0;
                    state->next_marker_id = 1;
                    state->latest_timestamp_ns = 0;
                    state->started = true;
                    return static_cast<int32_t>(PS_OK);
                }

                if (entrypoint == "Process") {
                    if (!state->started || !state->writer.is_open()) {
                        return static_cast<int32_t>(PS_OK);
                    }

                    const auto timestamp_ns = detail::steady_clock_ns();
                    state->latest_timestamp_ns = timestamp_ns;
                    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(state->active_ports.size()); ++i) {
                        const auto& port = state->active_ports[i];
                        ctx.read_port(port.port_id, state->frame_buffer.data(), port.info.byte_size);
                        if (!state->writer.write_frame(
                                timestamp_ns,
                                state->sequence++,
                                i,
                                state->frame_buffer.data(),
                                port.info.byte_size)) {
                            state->started = false;
                            return static_cast<int32_t>(PS_ERROR);
                        }
                    }
                    return static_cast<int32_t>(PS_OK);
                }

                if (entrypoint == "AddMarker") {
                    if (!state->started || !state->writer.is_open()) {
                        return static_cast<int32_t>(PS_OK);
                    }
                    const auto timestamp_ns = state->latest_timestamp_ns != 0
                        ? state->latest_timestamp_ns
                        : detail::steady_clock_ns();
                    if (!state->writer.write_marker(
                            timestamp_ns,
                            state->sequence++,
                            state->next_marker_id++,
                            read_label(ctx))) {
                        state->started = false;
                        return static_cast<int32_t>(PS_ERROR);
                    }
                    state->writer.flush();
                    return static_cast<int32_t>(PS_OK);
                }

                if (entrypoint == "Stop") {
                    state->writer.close();
                    state->started = false;
                    return static_cast<int32_t>(PS_OK);
                }

                return static_cast<int32_t>(PS_NOT_FOUND);
            }
        );
    };

    return definition;
}

} // namespace pluginsystem::builtins
