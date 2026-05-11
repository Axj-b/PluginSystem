#include <recorder_plugins.hpp>

#include "recording_format.hpp"

#include <pluginsystem/instance.hpp>
#include <pluginsystem/plugin_api.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
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

PropertyDescriptor make_int32_property(const char* id, const char* display_name, double default_value, bool writable = true)
{
    PropertyDescriptor p;
    p.id = id;
    p.name = display_name;
    p.type_name = "int32_t";
    p.byte_size = sizeof(std::int32_t);
    p.readable = true;
    p.writable = writable;
    p.default_value = default_value;
    return p;
}

PropertyDescriptor make_int64_property(const char* id, const char* display_name, double default_value, bool writable = true)
{
    PropertyDescriptor p;
    p.id = id;
    p.name = display_name;
    p.type_name = "int64_t";
    p.byte_size = sizeof(std::int64_t);
    p.readable = true;
    p.writable = writable;
    p.default_value = default_value;
    return p;
}

PortDescriptor make_typed_output_port(
    std::uint32_t index,
    const RecordedPortInfo& info
)
{
    const auto suffix = std::to_string(index);
    PortDescriptor port;
    port.id = "Output" + suffix;
    port.name = info.port_name.empty() ? "Output " + suffix : info.port_name;
    port.direction = PortDirection::output;
    port.access_mode = info.access_mode;
    port.byte_size = info.byte_size;
    port.alignment = alignof(std::max_align_t);
    port.type_name = info.type_name;
    return port;
}

struct ReplayPortState {
    std::vector<std::uint8_t> buffer;
    bool has_value{false};
};

struct ReplayState {
    std::ifstream file;
    bool started{false};
    std::uint32_t num_ports{0};
    std::vector<ReplayPortState> ports;
    std::vector<detail::ReplayTimestampGroup> groups;
    std::size_t current_group{0};
    std::int32_t last_seek_request{0};
    std::uint64_t current_timestamp_ns{0};
    std::uint64_t next_timestamp_ns{0};
    std::int32_t end_reached{0};
};

std::int64_t clamp_to_int64(std::uint64_t value)
{
    return static_cast<std::int64_t>(std::min<std::uint64_t>(
        value,
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())
    ));
}

void reset_buffers(ReplayState& state)
{
    for (auto& port : state.ports) {
        std::fill(port.buffer.begin(), port.buffer.end(), 0);
        port.has_value = false;
    }
}

void write_status(ReplayState& state, InvocationContext& ctx)
{
    const auto current = clamp_to_int64(state.current_timestamp_ns);
    const auto next = clamp_to_int64(state.next_timestamp_ns);
    ctx.write_property("CurrentTimestampNs", &current, sizeof(current));
    ctx.write_property("NextTimestampNs", &next, sizeof(next));
    ctx.write_property("EndReached", &state.end_reached, sizeof(state.end_reached));
}

std::size_t find_group_at_or_after(const std::vector<detail::ReplayTimestampGroup>& groups, std::uint64_t timestamp_ns)
{
    const auto found = std::lower_bound(groups.begin(), groups.end(), timestamp_ns, [](const auto& group, const auto timestamp) {
        return group.timestamp_ns < timestamp;
    });
    return static_cast<std::size_t>(std::distance(groups.begin(), found));
}

void emit_zero_outputs(ReplayState& state, InvocationContext& ctx, const std::vector<RecordedPortInfo>& port_infos)
{
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(port_infos.size()); ++i) {
        auto& port = state.ports[i];
        std::fill(port.buffer.begin(), port.buffer.end(), 0);
        port.has_value = false;
        const auto port_id = "Output" + std::to_string(i);
        ctx.write_port(port_id, port.buffer.data(), port_infos[i].byte_size);
    }
}

} // namespace

BuiltinPluginDefinition make_replay(
    std::string plugin_id,
    std::vector<RecordedPortInfo> port_infos
)
{
    PluginDescriptor descriptor;
    descriptor.id = std::move(plugin_id);
    descriptor.name = "Replay";
    descriptor.version = "3.0.0";
    descriptor.description = "Replays a multi-port recording file";
    descriptor.concurrency = ConcurrencyPolicy::instance_serialized;

    descriptor.properties.push_back(make_path_property("InputPath", "Input Path"));
    descriptor.properties.push_back(make_int32_property("Loop", "Loop", 0.0));
    descriptor.properties.push_back(make_int64_property("SeekTimestampNs", "Seek Timestamp Ns", 0.0));
    descriptor.properties.push_back(make_int32_property("SeekRequest", "Seek Request", 0.0));
    descriptor.properties.push_back(make_int64_property("CurrentTimestampNs", "Current Timestamp Ns", 0.0));
    descriptor.properties.push_back(make_int64_property("NextTimestampNs", "Next Timestamp Ns", 0.0));
    descriptor.properties.push_back(make_int32_property("EndReached", "End Reached", 0.0));

    std::vector<std::string> output_port_ids;
    output_port_ids.reserve(port_infos.size());
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(port_infos.size()); ++i) {
        descriptor.ports.push_back(make_typed_output_port(i, port_infos[i]));
        output_port_ids.push_back("Output" + std::to_string(i));
    }

    descriptor.entrypoints.push_back({"Start", "Start", "Opens the file and indexes timestamp groups",
        ConcurrencyPolicy::instance_serialized, {}, {}});
    descriptor.entrypoints.push_back({"Process", "Process", "Emits the next timestamp group",
        ConcurrencyPolicy::instance_serialized, {}, output_port_ids});
    descriptor.entrypoints.push_back({"Stop", "Stop", "Closes the recording file",
        ConcurrencyPolicy::instance_serialized, {}, {}});

    BuiltinPluginDefinition definition;
    definition.descriptor = std::move(descriptor);
    definition.factory = [port_infos = std::move(port_infos)](
        const PluginDescriptor&,
        const PluginInstanceConfig&,
        const RuntimeBindings&
    ) {
        auto state = std::make_shared<ReplayState>();

        return std::make_unique<BuiltinPluginInstanceBackend>(
            [state, port_infos](std::string_view entrypoint, InvocationContext& ctx) -> int32_t {
                if (entrypoint == "Start") {
                    char path_buffer[256]{};
                    ctx.read_property("InputPath", path_buffer, sizeof(path_buffer));
                    path_buffer[sizeof(path_buffer) - 1] = '\0';
                    if (path_buffer[0] == '\0') {
                        return static_cast<int32_t>(PS_ERROR);
                    }

                    state->file.close();
                    state->file.clear();
                    state->file.open(path_buffer, std::ios::binary);
                    if (!state->file.is_open()) {
                        return static_cast<int32_t>(PS_ERROR);
                    }

                    detail::RecordingPreamble preamble{};
                    std::vector<RecordedPortInfo> actual_ports;
                    if (!detail::read_recording_header(state->file, preamble, actual_ports)
                        || actual_ports.size() != port_infos.size()
                        || !detail::build_replay_index(state->file, preamble, port_infos, state->groups)) {
                        state->file.close();
                        return static_cast<int32_t>(PS_ERROR);
                    }

                    state->num_ports = static_cast<std::uint32_t>(port_infos.size());
                    state->ports.clear();
                    state->ports.resize(state->num_ports);
                    for (std::uint32_t i = 0; i < state->num_ports; ++i) {
                        state->ports[i].buffer.resize(static_cast<std::size_t>(port_infos[i].byte_size));
                    }

                    std::int32_t seek_request{0};
                    ctx.read_property("SeekRequest", &seek_request, sizeof(seek_request));
                    state->last_seek_request = seek_request;
                    std::int64_t seek_timestamp{0};
                    ctx.read_property("SeekTimestampNs", &seek_timestamp, sizeof(seek_timestamp));
                    state->current_group = seek_request != 0
                        ? find_group_at_or_after(state->groups, seek_timestamp < 0 ? 0 : static_cast<std::uint64_t>(seek_timestamp))
                        : 0;
                    state->current_timestamp_ns = 0;
                    state->next_timestamp_ns = state->current_group < state->groups.size() ? state->groups[state->current_group].timestamp_ns : 0;
                    state->end_reached = state->current_group >= state->groups.size() ? 1 : 0;
                    reset_buffers(*state);
                    state->started = true;
                    write_status(*state, ctx);
                    return static_cast<int32_t>(PS_OK);
                }

                if (entrypoint == "Process") {
                    if (!state->started || !state->file.is_open() || state->groups.empty()) {
                        emit_zero_outputs(*state, ctx, port_infos);
                        state->current_timestamp_ns = 0;
                        state->next_timestamp_ns = 0;
                        state->end_reached = 1;
                        write_status(*state, ctx);
                        return static_cast<int32_t>(PS_OK);
                    }

                    std::int32_t seek_request{0};
                    ctx.read_property("SeekRequest", &seek_request, sizeof(seek_request));
                    if (seek_request != state->last_seek_request) {
                        std::int64_t seek_timestamp{0};
                        ctx.read_property("SeekTimestampNs", &seek_timestamp, sizeof(seek_timestamp));
                        const auto clamped_seek = seek_timestamp < 0 ? 0 : static_cast<std::uint64_t>(seek_timestamp);
                        state->current_group = find_group_at_or_after(state->groups, clamped_seek);
                        state->last_seek_request = seek_request;
                        state->current_timestamp_ns = 0;
                        reset_buffers(*state);
                    }

                    if (state->current_group >= state->groups.size()) {
                        std::int32_t loop{0};
                        ctx.read_property("Loop", &loop, sizeof(loop));
                        if (loop) {
                            state->current_group = 0;
                            reset_buffers(*state);
                        } else {
                            emit_zero_outputs(*state, ctx, port_infos);
                            state->next_timestamp_ns = 0;
                            state->end_reached = 1;
                            write_status(*state, ctx);
                            return static_cast<int32_t>(PS_OK);
                        }
                    }

                    const auto& group = state->groups[state->current_group];
                    state->file.clear();
                    for (const auto& sample : group.samples) {
                        if (sample.port_index >= state->ports.size()) {
                            continue;
                        }
                        auto& port = state->ports[sample.port_index];
                        state->file.seekg(sample.payload_offset);
                        state->file.read(
                            reinterpret_cast<char*>(port.buffer.data()),
                            static_cast<std::streamsize>(port_infos[sample.port_index].byte_size)
                        );
                        if (!state->file) {
                            std::fill(port.buffer.begin(), port.buffer.end(), 0);
                        }
                        port.has_value = true;
                    }

                    for (std::uint32_t i = 0; i < state->num_ports; ++i) {
                        auto& port = state->ports[i];
                        if (!port.has_value) {
                            std::fill(port.buffer.begin(), port.buffer.end(), 0);
                        }
                        const auto port_id = "Output" + std::to_string(i);
                        ctx.write_port(port_id, port.buffer.data(), port_infos[i].byte_size);
                    }

                    state->current_timestamp_ns = group.timestamp_ns;
                    ++state->current_group;
                    state->next_timestamp_ns = state->current_group < state->groups.size()
                        ? state->groups[state->current_group].timestamp_ns
                        : 0;
                    state->end_reached = state->current_group >= state->groups.size() ? 1 : 0;
                    write_status(*state, ctx);
                    return static_cast<int32_t>(PS_OK);
                }

                if (entrypoint == "Stop") {
                    if (state->file.is_open()) {
                        state->file.close();
                    }
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
