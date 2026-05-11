#include <recorder_plugins.hpp>

#include <pluginsystem/error.hpp>
#include <pluginsystem/instance.hpp>
#include <pluginsystem/plugin_api.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace pluginsystem {
namespace builtins {
namespace {

constexpr std::uint32_t k_recording_magic   = 0x52454350u; // "RECP"
constexpr std::uint32_t k_recording_version = 2u;
constexpr std::uint32_t k_max_recorder_ports = 32u;

// ─── File format ─────────────────────────────────────────────────────────────
//
//  Preamble  (16 bytes, fixed):
//    uint32  magic       = 0x52454350
//    uint32  version     = 2
//    uint32  num_ports
//    uint32  data_offset = 16 + num_ports * 72
//
//  PortSlot × num_ports  (72 bytes each):
//    char[64]  type_name
//    uint64    byte_size
//
//  Frame × N  (24 + byte_size[port_index] bytes, repeating):
//    uint64  timestamp_ns
//    uint64  sequence       (global, increments across all ports)
//    uint32  port_index
//    uint32  reserved

#pragma pack(push, 1)
struct RecordingPreamble {
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t num_ports;
    std::uint32_t data_offset;
};
static_assert(sizeof(RecordingPreamble) == 16);

struct RecordingPortSlot {
    char          type_name[64];
    std::uint64_t byte_size;
    char          port_name[64];
    std::uint32_t access_mode_raw;
};
static_assert(sizeof(RecordingPortSlot) == 140);

struct RecordingFrameHeader {
    std::uint64_t timestamp_ns;
    std::uint64_t sequence;
    std::uint32_t port_index;
    std::uint32_t reserved;
};
static_assert(sizeof(RecordingFrameHeader) == 24);
#pragma pack(pop)

std::uint64_t steady_clock_ns()
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count()
    );
}

PortDescriptor make_any_input_port(std::uint32_t index)
{
    const auto s = std::to_string(index);
    PortDescriptor p;
    p.id        = "Input" + s;
    p.name      = "Input " + s;
    p.direction = PortDirection::input;
    p.byte_size = 0;
    p.alignment = alignof(std::max_align_t);
    p.any_type  = true;
    return p;
}

PortDescriptor make_typed_output_port(std::uint32_t index, const std::string& type_name, std::uint64_t byte_size, const std::string& port_name, PortAccessMode access_mode)
{
    const auto s = std::to_string(index);
    PortDescriptor p;
    p.id          = "Output" + s;
    p.name        = port_name.empty() ? "Output " + s : port_name;
    p.direction   = PortDirection::output;
    p.byte_size   = byte_size;
    p.alignment   = alignof(std::max_align_t);
    p.type_name   = type_name;
    p.access_mode = access_mode;
    return p;
}

PropertyDescriptor make_path_property(const char* id, const char* display_name)
{
    PropertyDescriptor p;
    p.id        = id;
    p.name      = display_name;
    p.type_name = "char[256]";
    p.byte_size = 256;
    p.readable  = true;
    p.writable  = true;
    return p;
}

PropertyDescriptor make_int32_property(const char* id, const char* display_name, double default_val)
{
    PropertyDescriptor p;
    p.id            = id;
    p.name          = display_name;
    p.type_name     = "int32_t";
    p.byte_size     = sizeof(std::int32_t);
    p.readable      = true;
    p.writable      = true;
    p.default_value = default_val;
    return p;
}

// ─── Recorder state ──────────────────────────────────────────────────────────

struct ActivePort {
    std::string    port_id;
    std::uint64_t  byte_size;
    std::string    type_name;
    std::string    port_name;
    PortAccessMode access_mode{PortAccessMode::direct_block};
};

struct RecorderState {
    std::ofstream            file;
    std::uint64_t            sequence{0};
    bool                     started{false};
    std::vector<ActivePort>  active_ports;
    std::vector<std::uint8_t> frame_buffer;
};

// ─── Replay state ─────────────────────────────────────────────────────────────

struct ReplayPortState {
    std::uint64_t              current_frame{0};
    std::uint64_t              total_frames{0};
    std::vector<std::streampos> frame_offsets;
    std::vector<std::uint8_t>  buffer;
};

struct ReplayState {
    std::ifstream              file;
    bool                       started{false};
    std::uint32_t              num_ports{0};
    std::vector<ReplayPortState> ports;
};

} // namespace

// ─── make_recorder ────────────────────────────────────────────────────────────

BuiltinPluginDefinition make_recorder(std::string plugin_id)
{
    PluginDescriptor descriptor;
    descriptor.id               = plugin_id;
    descriptor.name             = "Recorder";
    descriptor.version          = "2.0.0";
    descriptor.description      = "Records any number of ports to a single binary file";
    descriptor.concurrency      = ConcurrencyPolicy::instance_serialized;
    descriptor.expandable_inputs = true;

    descriptor.properties.push_back(make_path_property("OutputPath", "Output Path"));
    descriptor.properties.push_back(make_int32_property("AppendMode", "Append Mode", 0.0));

    for (std::uint32_t i = 0; i < k_max_recorder_ports; ++i) {
        descriptor.ports.push_back(make_any_input_port(i));
    }

    std::vector<std::string> all_input_ids;
    all_input_ids.reserve(k_max_recorder_ports);
    for (std::uint32_t i = 0; i < k_max_recorder_ports; ++i) {
        all_input_ids.push_back("Input" + std::to_string(i));
    }
    descriptor.entrypoints.push_back({"Start",   "Start",   "Opens the recording file and writes the header",
        ConcurrencyPolicy::instance_serialized, {}, {}});
    descriptor.entrypoints.push_back({"Process", "Process", "Records one frame per active port",
        ConcurrencyPolicy::instance_serialized, all_input_ids, {}});
    descriptor.entrypoints.push_back({"Stop",    "Stop",    "Flushes and closes the recording file",
        ConcurrencyPolicy::instance_serialized, {}, {}});

    BuiltinPluginDefinition definition;
    definition.descriptor = std::move(descriptor);
    definition.factory = [plugin_id](
        const PluginDescriptor&,
        const PluginInstanceConfig& cfg,
        const RuntimeBindings& bindings
    ) {
        auto state = std::make_shared<RecorderState>();

        for (const auto& pb : bindings.ports) {
            if (pb.descriptor.direction != PortDirection::input) continue;
            if (pb.descriptor.byte_size == 0) continue; // unconnected
            state->active_ports.push_back({pb.descriptor.id, pb.descriptor.byte_size, pb.descriptor.type_name, pb.descriptor.name, pb.descriptor.access_mode});
        }

        std::uint64_t max_size = 0;
        for (const auto& ap : state->active_ports) max_size = std::max(max_size, ap.byte_size);
        state->frame_buffer.resize(static_cast<std::size_t>(max_size > 0 ? max_size : 1));

        const auto default_path = (cfg.runtime_directory / cfg.instance_name).string() + ".rec";

        return std::make_unique<BuiltinPluginInstanceBackend>(
            [state, default_path](std::string_view entrypoint, InvocationContext& ctx) -> int32_t {
                if (entrypoint == "Start") {
                    char path_buf[256]{};
                    ctx.read_property("OutputPath", path_buf, 256);
                    path_buf[255] = '\0';
                    const std::string path = path_buf[0] != '\0' ? path_buf : default_path;

                    std::int32_t append_mode{0};
                    ctx.read_property("AppendMode", &append_mode, sizeof(append_mode));
                    const auto flags = std::ios::binary | (append_mode ? std::ios::app : std::ios::trunc);

                    state->file.open(path, flags);
                    if (!state->file.is_open()) return static_cast<int32_t>(PS_ERROR);

                    if (!append_mode) {
                        const auto n = static_cast<std::uint32_t>(state->active_ports.size());
                        RecordingPreamble preamble{};
                        preamble.magic       = k_recording_magic;
                        preamble.version     = k_recording_version;
                        preamble.num_ports   = n;
                        preamble.data_offset = static_cast<std::uint32_t>(sizeof(RecordingPreamble)) +
                                               n * static_cast<std::uint32_t>(sizeof(RecordingPortSlot));
                        state->file.write(reinterpret_cast<const char*>(&preamble), sizeof(preamble));

                        for (const auto& ap : state->active_ports) {
                            RecordingPortSlot slot{};
                            std::strncpy(slot.type_name, ap.type_name.c_str(), sizeof(slot.type_name) - 1);
                            slot.byte_size = ap.byte_size;
                            std::strncpy(slot.port_name, ap.port_name.c_str(), sizeof(slot.port_name) - 1);
                            slot.access_mode_raw = static_cast<std::uint32_t>(ap.access_mode);
                            state->file.write(reinterpret_cast<const char*>(&slot), sizeof(slot));
                        }
                    }

                    state->sequence = 0;
                    state->started  = true;
                    return static_cast<int32_t>(PS_OK);
                }

                if (entrypoint == "Process") {
                    if (!state->started || !state->file.is_open()) return static_cast<int32_t>(PS_OK);

                    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(state->active_ports.size()); ++i) {
                        const auto& ap = state->active_ports[i];
                        ctx.read_port(ap.port_id, state->frame_buffer.data(), ap.byte_size);

                        RecordingFrameHeader fh{};
                        fh.timestamp_ns = steady_clock_ns();
                        fh.sequence     = state->sequence++;
                        fh.port_index   = i;
                        state->file.write(reinterpret_cast<const char*>(&fh), sizeof(fh));
                        state->file.write(reinterpret_cast<const char*>(state->frame_buffer.data()),
                                          static_cast<std::streamsize>(ap.byte_size));

                        if (!state->file) { state->started = false; return static_cast<int32_t>(PS_OK); }
                    }
                    return static_cast<int32_t>(PS_OK);
                }

                if (entrypoint == "Stop") {
                    if (state->file.is_open()) { state->file.flush(); state->file.close(); }
                    state->started = false;
                    return static_cast<int32_t>(PS_OK);
                }

                return static_cast<int32_t>(PS_NOT_FOUND);
            }
        );
    };

    return definition;
}

// ─── make_replay ─────────────────────────────────────────────────────────────

BuiltinPluginDefinition make_replay(
    std::string plugin_id,
    std::vector<RecordedPortInfo> port_infos
)
{
    PluginDescriptor descriptor;
    descriptor.id          = plugin_id;
    descriptor.name        = "Replay";
    descriptor.version     = "2.0.0";
    descriptor.description = "Replays a multi-port recording file";
    descriptor.concurrency = ConcurrencyPolicy::instance_serialized;

    descriptor.properties.push_back(make_path_property("InputPath", "Input Path"));
    descriptor.properties.push_back(make_int32_property("Loop", "Loop", 0.0));

    std::vector<std::string> output_port_ids;
    output_port_ids.reserve(port_infos.size());
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(port_infos.size()); ++i) {
        descriptor.ports.push_back(make_typed_output_port(i, port_infos[i].type_name, port_infos[i].byte_size, port_infos[i].port_name, port_infos[i].access_mode));
        output_port_ids.push_back("Output" + std::to_string(i));
    }

    descriptor.entrypoints.push_back({"Start",   "Start",   "Opens the file and indexes frame offsets per port",
        ConcurrencyPolicy::instance_serialized, {}, {}});
    descriptor.entrypoints.push_back({"Process", "Process", "Emits the next frame for each output port",
        ConcurrencyPolicy::instance_serialized, {}, output_port_ids});
    descriptor.entrypoints.push_back({"Stop",    "Stop",    "Closes the recording file",
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
                    char path_buf[256]{};
                    ctx.read_property("InputPath", path_buf, 256);
                    path_buf[255] = '\0';
                    if (path_buf[0] == '\0') return static_cast<int32_t>(PS_ERROR);

                    state->file.open(path_buf, std::ios::binary);
                    if (!state->file.is_open()) return static_cast<int32_t>(PS_ERROR);

                    RecordingPreamble preamble{};
                    state->file.read(reinterpret_cast<char*>(&preamble), sizeof(preamble));
                    if (!state->file ||
                        preamble.magic   != k_recording_magic ||
                        preamble.version != k_recording_version ||
                        preamble.num_ports != static_cast<std::uint32_t>(port_infos.size()))
                    {
                        state->file.close();
                        return static_cast<int32_t>(PS_ERROR);
                    }

                    for (std::uint32_t i = 0; i < preamble.num_ports; ++i) {
                        RecordingPortSlot slot{};
                        state->file.read(reinterpret_cast<char*>(&slot), sizeof(slot));
                        if (!state->file || slot.byte_size != port_infos[i].byte_size) {
                            state->file.close();
                            return static_cast<int32_t>(PS_ERROR);
                        }
                    }

                    state->num_ports = preamble.num_ports;
                    state->ports.resize(state->num_ports);
                    for (std::uint32_t i = 0; i < state->num_ports; ++i) {
                        state->ports[i].buffer.resize(static_cast<std::size_t>(port_infos[i].byte_size));
                    }

                    // Pre-scan: build per-port frame offset index for O(1) Process
                    state->file.seekg(preamble.data_offset, std::ios::beg);
                    while (state->file.good()) {
                        const auto pos = state->file.tellg();
                        RecordingFrameHeader fh{};
                        state->file.read(reinterpret_cast<char*>(&fh), sizeof(fh));
                        if (!state->file) break;
                        if (fh.port_index >= state->num_ports) { state->file.close(); return static_cast<int32_t>(PS_ERROR); }
                        state->ports[fh.port_index].frame_offsets.push_back(pos);
                        state->ports[fh.port_index].total_frames++;
                        state->file.seekg(static_cast<std::streamoff>(port_infos[fh.port_index].byte_size), std::ios::cur);
                    }

                    for (auto& ps : state->ports) ps.current_frame = 0;
                    state->started = true;
                    return static_cast<int32_t>(PS_OK);
                }

                if (entrypoint == "Process") {
                    for (std::uint32_t i = 0; i < state->num_ports; ++i) {
                        auto& ps = state->ports[i];
                        const auto port_id     = "Output" + std::to_string(i);
                        const auto payload_size = port_infos[i].byte_size;

                        if (!state->started || ps.total_frames == 0) {
                            std::fill(ps.buffer.begin(), ps.buffer.end(), 0);
                            ctx.write_port(port_id, ps.buffer.data(), payload_size);
                            continue;
                        }

                        if (ps.current_frame >= ps.total_frames) {
                            std::int32_t loop{0};
                            ctx.read_property("Loop", &loop, sizeof(loop));
                            if (loop) { ps.current_frame = 0; }
                            else {
                                std::fill(ps.buffer.begin(), ps.buffer.end(), 0);
                                ctx.write_port(port_id, ps.buffer.data(), payload_size);
                                continue;
                            }
                        }

                        state->file.clear();
                        state->file.seekg(ps.frame_offsets[ps.current_frame]);
                        RecordingFrameHeader fh{};
                        state->file.read(reinterpret_cast<char*>(&fh), sizeof(fh));
                        state->file.read(reinterpret_cast<char*>(ps.buffer.data()),
                                         static_cast<std::streamsize>(payload_size));

                        if (!state->file) std::fill(ps.buffer.begin(), ps.buffer.end(), 0);
                        else              ++ps.current_frame;

                        ctx.write_port(port_id, ps.buffer.data(), payload_size);
                    }
                    return static_cast<int32_t>(PS_OK);
                }

                if (entrypoint == "Stop") {
                    if (state->file.is_open()) state->file.close();
                    state->started = false;
                    return static_cast<int32_t>(PS_OK);
                }

                return static_cast<int32_t>(PS_NOT_FOUND);
            }
        );
    };

    return definition;
}

// ─── read_recording_ports ─────────────────────────────────────────────────────

std::vector<RecordedPortInfo> read_recording_ports(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};

    RecordingPreamble preamble{};
    file.read(reinterpret_cast<char*>(&preamble), sizeof(preamble));
    if (!file || preamble.magic != k_recording_magic || preamble.version != k_recording_version) return {};

    std::vector<RecordedPortInfo> ports;
    ports.reserve(preamble.num_ports);
    for (std::uint32_t i = 0; i < preamble.num_ports; ++i) {
        RecordingPortSlot slot{};
        file.read(reinterpret_cast<char*>(&slot), sizeof(slot));
        if (!file) return {};
        slot.type_name[sizeof(slot.type_name) - 1] = '\0';
        slot.port_name[sizeof(slot.port_name) - 1] = '\0';
        const auto am = slot.access_mode_raw == static_cast<std::uint32_t>(PortAccessMode::buffered_latest)
            ? PortAccessMode::buffered_latest : PortAccessMode::direct_block;
        ports.push_back({slot.type_name, slot.byte_size, slot.port_name, am});
    }
    return ports;
}

} // namespace builtins
} // namespace pluginsystem
