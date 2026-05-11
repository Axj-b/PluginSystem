#pragma once

#include <pluginsystem/providers.hpp>
#include <pluginsystem/types.hpp>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace pluginsystem {

class PluginRegistry;

namespace builtins {

struct RecordedPortInfo {
    std::string    type_name;
    std::uint64_t  byte_size{0};
    std::string    port_name;
    PortAccessMode access_mode{PortAccessMode::direct_block};
};

struct RecordingTimelineTrack {
    RecordedPortInfo port;
    std::vector<std::uint64_t> sample_timestamps_ns;
};

struct RecordingTimelineMarker {
    std::uint32_t marker_id{0};
    std::uint64_t timestamp_ns{0};
    std::string label;
};

struct RecordingTimeline {
    std::uint32_t version{0};
    std::uint64_t first_timestamp_ns{0};
    std::uint64_t last_timestamp_ns{0};
    std::uint64_t duration_ns{0};
    std::vector<RecordingTimelineTrack> tracks;
    std::vector<RecordingTimelineMarker> markers;
};

// Universal multi-port recorder.
//
// Accepts up to MAX_RECORDER_PORTS input ports (Input0..InputN-1), all any_type.
// The node editor shows only (connected_count + 1) pins, growing dynamically.
// All connected ports are recorded to one binary file in invocation order.
// Port names (from connected source ports) are embedded in the file header.
//
// File format: V3 variable-length event stream (see recorder_plugins.cpp).
// V2 recordings remain readable.
//
// Properties:
//   OutputPath  char[256]  Output file path. Default: <runtime_dir>/<instance_name>.rec
//   AppendMode  int32_t    1 = append, 0 = overwrite (default).
//   MarkerLabel char[64]   Label used by AddMarker.
//
// Entrypoints: Start, Process, AddMarker, Stop
BuiltinPluginDefinition make_recorder(
    std::string plugin_id = "pluginsystem.builtin.recorder"
);

// Multi-port replay.
//
// port_infos: {type_name, byte_size, port_name} for each recorded port, in file order.
// Creates Output0..OutputN-1 ports with the recorded port names.
// Registered dynamically when InputPath is set in the node editor.
//
// Properties:
//   InputPath   char[256]  Recording file to replay.
//   Loop        int32_t    1 = restart at EOF, 0 = emit zeros (default).
//   SeekTimestampNs int64_t  Timestamp requested by the UI/runtime.
//   SeekRequest     int32_t  Increment to apply a new seek request.
//   CurrentTimestampNs int64_t Last emitted timestamp.
//   NextTimestampNs    int64_t Next timestamp to be emitted.
//   EndReached         int32_t 1 after EOF when Loop is disabled.
//
// Entrypoints: Start, Process, Stop
BuiltinPluginDefinition make_replay(
    std::string plugin_id,
    std::vector<RecordedPortInfo> port_infos
);

// Registers the default host-side built-ins once:
// - pluginsystem.builtin.recorder
// - pluginsystem.builtin.replay
void register_default_plugins(PluginRegistry& registry);

// Reads port info from a .rec file header.
// Returns an empty vector if the file does not exist or is not a valid recording.
std::vector<RecordedPortInfo> read_recording_ports(const std::string& path);

// Reads timeline metadata from a V2 or V3 .rec file.
// V2 files contain tracks and sample timestamps, but no markers.
RecordingTimeline read_recording_timeline(const std::string& path);

} // namespace builtins
} // namespace pluginsystem
