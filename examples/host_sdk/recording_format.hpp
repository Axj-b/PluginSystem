#pragma once

#include <recorder_plugins.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace pluginsystem::builtins::detail {

constexpr std::uint32_t k_recording_magic = 0x52454350u; // "RECP"
constexpr std::uint32_t k_recording_v2 = 2u;
constexpr std::uint32_t k_recording_v3 = 3u;
constexpr std::uint32_t k_current_recording_version = k_recording_v3;
constexpr std::uint32_t k_max_recorder_ports = 32u;
constexpr std::uint32_t k_v3_event_frame = 1u;
constexpr std::uint32_t k_v3_event_marker = 2u;
constexpr std::uint32_t k_invalid_port_index = 0xffffffffu;

#pragma pack(push, 1)
struct RecordingPreamble {
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t num_ports;
    std::uint32_t data_offset;
};
static_assert(sizeof(RecordingPreamble) == 16);

struct RecordingPortSlot {
    char type_name[64];
    std::uint64_t byte_size;
    char port_name[64];
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

struct RecordingV3EventHeader {
    std::uint64_t timestamp_ns;
    std::uint64_t sequence;
    std::uint32_t event_type;
    std::uint32_t port_index;
    std::uint32_t payload_size;
    std::uint32_t marker_id;
};
static_assert(sizeof(RecordingV3EventHeader) == 32);
#pragma pack(pop)

struct ReplaySampleEvent {
    std::uint64_t timestamp_ns{0};
    std::uint64_t sequence{0};
    std::uint32_t port_index{0};
    std::streampos payload_offset{};
};

struct ReplayTimestampGroup {
    std::uint64_t timestamp_ns{0};
    std::vector<ReplaySampleEvent> samples;
};

std::uint64_t steady_clock_ns();

class RecordingWriter {
public:
    bool open(const std::filesystem::path& path, const std::vector<RecordedPortInfo>& ports, bool append);
    bool is_open() const noexcept;
    void close();
    void flush();
    bool write_frame(std::uint64_t timestamp_ns, std::uint64_t sequence, std::uint32_t port_index, const void* data, std::uint64_t byte_size);
    bool write_marker(std::uint64_t timestamp_ns, std::uint64_t sequence, std::uint32_t marker_id, const std::string& label);

private:
    std::ofstream file_;
};

bool read_recording_header(
    std::ifstream& file,
    RecordingPreamble& preamble,
    std::vector<RecordedPortInfo>& ports
);

bool build_replay_index(
    std::ifstream& file,
    const RecordingPreamble& preamble,
    const std::vector<RecordedPortInfo>& expected_ports,
    std::vector<ReplayTimestampGroup>& groups
);

} // namespace pluginsystem::builtins::detail
