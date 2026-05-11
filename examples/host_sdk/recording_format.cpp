#include "recording_format.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <map>

namespace pluginsystem::builtins {
namespace detail {
namespace {

std::string read_c_string(const char* data, std::size_t size)
{
    const auto end = std::find(data, data + size, '\0');
    return std::string{data, end};
}

void write_c_string(char* destination, std::size_t size, const std::string& value)
{
    if (size == 0) {
        return;
    }
    std::memset(destination, 0, size);
    std::strncpy(destination, value.c_str(), size - 1);
}

PortAccessMode read_access_mode(std::uint32_t raw)
{
    return raw == static_cast<std::uint32_t>(PortAccessMode::buffered_latest)
        ? PortAccessMode::buffered_latest
        : PortAccessMode::direct_block;
}

void update_timeline_bounds(RecordingTimeline& timeline, std::uint64_t timestamp_ns)
{
    if (timeline.first_timestamp_ns == 0 || timestamp_ns < timeline.first_timestamp_ns) {
        timeline.first_timestamp_ns = timestamp_ns;
    }
    if (timestamp_ns > timeline.last_timestamp_ns) {
        timeline.last_timestamp_ns = timestamp_ns;
    }
    timeline.duration_ns = timeline.last_timestamp_ns >= timeline.first_timestamp_ns
        ? timeline.last_timestamp_ns - timeline.first_timestamp_ns
        : 0;
}

bool ports_match(const std::vector<RecordedPortInfo>& actual, const std::vector<RecordedPortInfo>& expected)
{
    if (actual.size() != expected.size()) {
        return false;
    }
    for (std::size_t i = 0; i < actual.size(); ++i) {
        if (actual[i].byte_size != expected[i].byte_size || actual[i].type_name != expected[i].type_name) {
            return false;
        }
    }
    return true;
}

void sort_groups(std::map<std::uint64_t, std::vector<ReplaySampleEvent>>& grouped, std::vector<ReplayTimestampGroup>& groups)
{
    groups.clear();
    groups.reserve(grouped.size());
    for (auto& item : grouped) {
        auto group = ReplayTimestampGroup{item.first, std::move(item.second)};
        std::sort(group.samples.begin(), group.samples.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.sequence < rhs.sequence;
        });
        groups.push_back(std::move(group));
    }
}

bool build_v2_index(
    std::ifstream& file,
    const RecordingPreamble& preamble,
    const std::vector<RecordedPortInfo>& ports,
    std::vector<ReplayTimestampGroup>& groups
)
{
    std::map<std::uint64_t, std::vector<ReplaySampleEvent>> grouped;
    file.clear();
    file.seekg(preamble.data_offset, std::ios::beg);
    while (file.good()) {
        RecordingFrameHeader frame{};
        file.read(reinterpret_cast<char*>(&frame), sizeof(frame));
        if (!file) {
            break;
        }
        if (frame.port_index >= ports.size()) {
            return false;
        }
        const auto payload_offset = file.tellg();
        grouped[frame.timestamp_ns].push_back(ReplaySampleEvent{
            frame.timestamp_ns,
            frame.sequence,
            frame.port_index,
            payload_offset,
        });
        file.seekg(static_cast<std::streamoff>(ports[frame.port_index].byte_size), std::ios::cur);
    }
    sort_groups(grouped, groups);
    return true;
}

bool build_v3_index(
    std::ifstream& file,
    const RecordingPreamble& preamble,
    const std::vector<RecordedPortInfo>& ports,
    std::vector<ReplayTimestampGroup>& groups
)
{
    std::map<std::uint64_t, std::vector<ReplaySampleEvent>> grouped;
    file.clear();
    file.seekg(preamble.data_offset, std::ios::beg);
    while (file.good()) {
        RecordingV3EventHeader event{};
        file.read(reinterpret_cast<char*>(&event), sizeof(event));
        if (!file) {
            break;
        }
        if (event.event_type == k_v3_event_frame) {
            if (event.port_index >= ports.size() || event.payload_size != ports[event.port_index].byte_size) {
                return false;
            }
            const auto payload_offset = file.tellg();
            grouped[event.timestamp_ns].push_back(ReplaySampleEvent{
                event.timestamp_ns,
                event.sequence,
                event.port_index,
                payload_offset,
            });
            file.seekg(static_cast<std::streamoff>(event.payload_size), std::ios::cur);
        } else if (event.event_type == k_v3_event_marker) {
            file.seekg(static_cast<std::streamoff>(event.payload_size), std::ios::cur);
        } else {
            return false;
        }
    }
    sort_groups(grouped, groups);
    return true;
}

} // namespace

std::uint64_t steady_clock_ns()
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count()
    );
}

bool RecordingWriter::open(const std::filesystem::path& path, const std::vector<RecordedPortInfo>& ports, bool append)
{
    const auto flags = std::ios::binary | (append ? std::ios::app : std::ios::trunc);
    file_.open(path, flags);
    if (!file_.is_open()) {
        return false;
    }
    if (append) {
        return true;
    }

    RecordingPreamble preamble{};
    preamble.magic = k_recording_magic;
    preamble.version = k_current_recording_version;
    preamble.num_ports = static_cast<std::uint32_t>(ports.size());
    preamble.data_offset = static_cast<std::uint32_t>(sizeof(RecordingPreamble))
        + preamble.num_ports * static_cast<std::uint32_t>(sizeof(RecordingPortSlot));
    file_.write(reinterpret_cast<const char*>(&preamble), sizeof(preamble));

    for (const auto& port : ports) {
        RecordingPortSlot slot{};
        write_c_string(slot.type_name, sizeof(slot.type_name), port.type_name);
        slot.byte_size = port.byte_size;
        write_c_string(slot.port_name, sizeof(slot.port_name), port.port_name);
        slot.access_mode_raw = static_cast<std::uint32_t>(port.access_mode);
        file_.write(reinterpret_cast<const char*>(&slot), sizeof(slot));
    }
    return static_cast<bool>(file_);
}

bool RecordingWriter::is_open() const noexcept
{
    return file_.is_open();
}

void RecordingWriter::close()
{
    if (file_.is_open()) {
        file_.flush();
        file_.close();
    }
}

void RecordingWriter::flush()
{
    if (file_.is_open()) {
        file_.flush();
    }
}

bool RecordingWriter::write_frame(std::uint64_t timestamp_ns, std::uint64_t sequence, std::uint32_t port_index, const void* data, std::uint64_t byte_size)
{
    if (!file_.is_open() || byte_size > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    RecordingV3EventHeader event{};
    event.timestamp_ns = timestamp_ns;
    event.sequence = sequence;
    event.event_type = k_v3_event_frame;
    event.port_index = port_index;
    event.payload_size = static_cast<std::uint32_t>(byte_size);
    event.marker_id = 0;
    file_.write(reinterpret_cast<const char*>(&event), sizeof(event));
    file_.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(byte_size));
    return static_cast<bool>(file_);
}

bool RecordingWriter::write_marker(std::uint64_t timestamp_ns, std::uint64_t sequence, std::uint32_t marker_id, const std::string& label)
{
    if (!file_.is_open()) {
        return false;
    }
    const auto label_size = static_cast<std::uint32_t>(std::min<std::size_t>(label.size(), 63));
    RecordingV3EventHeader event{};
    event.timestamp_ns = timestamp_ns;
    event.sequence = sequence;
    event.event_type = k_v3_event_marker;
    event.port_index = k_invalid_port_index;
    event.payload_size = label_size;
    event.marker_id = marker_id;
    file_.write(reinterpret_cast<const char*>(&event), sizeof(event));
    if (label_size > 0) {
        file_.write(label.data(), static_cast<std::streamsize>(label_size));
    }
    return static_cast<bool>(file_);
}

bool read_recording_header(
    std::ifstream& file,
    RecordingPreamble& preamble,
    std::vector<RecordedPortInfo>& ports
)
{
    file.read(reinterpret_cast<char*>(&preamble), sizeof(preamble));
    if (!file
        || preamble.magic != k_recording_magic
        || (preamble.version != k_recording_v2 && preamble.version != k_recording_v3)
        || preamble.num_ports > k_max_recorder_ports) {
        return false;
    }

    ports.clear();
    ports.reserve(preamble.num_ports);
    for (std::uint32_t i = 0; i < preamble.num_ports; ++i) {
        RecordingPortSlot slot{};
        file.read(reinterpret_cast<char*>(&slot), sizeof(slot));
        if (!file) {
            return false;
        }
        ports.push_back({
            read_c_string(slot.type_name, sizeof(slot.type_name)),
            slot.byte_size,
            read_c_string(slot.port_name, sizeof(slot.port_name)),
            read_access_mode(slot.access_mode_raw),
        });
    }
    return true;
}

bool build_replay_index(
    std::ifstream& file,
    const RecordingPreamble& preamble,
    const std::vector<RecordedPortInfo>& expected_ports,
    std::vector<ReplayTimestampGroup>& groups
)
{
    file.clear();
    file.seekg(0, std::ios::beg);
    RecordingPreamble header{};
    std::vector<RecordedPortInfo> actual_ports;
    if (!read_recording_header(file, header, actual_ports)) {
        return false;
    }
    if (header.version != preamble.version || header.num_ports != preamble.num_ports || !ports_match(actual_ports, expected_ports)) {
        return false;
    }
    return header.version == k_recording_v2
        ? build_v2_index(file, header, actual_ports, groups)
        : build_v3_index(file, header, actual_ports, groups);
}

} // namespace detail

std::vector<RecordedPortInfo> read_recording_ports(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }

    detail::RecordingPreamble preamble{};
    std::vector<RecordedPortInfo> ports;
    if (!detail::read_recording_header(file, preamble, ports)) {
        return {};
    }
    return ports;
}

RecordingTimeline read_recording_timeline(const std::string& path)
{
    RecordingTimeline timeline;
    auto update_bounds = [&timeline](std::uint64_t timestamp_ns) {
        if (timeline.first_timestamp_ns == 0 || timestamp_ns < timeline.first_timestamp_ns) {
            timeline.first_timestamp_ns = timestamp_ns;
        }
        if (timestamp_ns > timeline.last_timestamp_ns) {
            timeline.last_timestamp_ns = timestamp_ns;
        }
        timeline.duration_ns = timeline.last_timestamp_ns >= timeline.first_timestamp_ns
            ? timeline.last_timestamp_ns - timeline.first_timestamp_ns
            : 0;
    };

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return timeline;
    }

    detail::RecordingPreamble preamble{};
    std::vector<RecordedPortInfo> ports;
    if (!detail::read_recording_header(file, preamble, ports)) {
        return {};
    }

    timeline.version = preamble.version;
    timeline.tracks.reserve(ports.size());
    for (auto& port : ports) {
        timeline.tracks.push_back(RecordingTimelineTrack{std::move(port), {}});
    }

    file.seekg(preamble.data_offset, std::ios::beg);
    if (preamble.version == detail::k_recording_v2) {
        while (file.good()) {
            detail::RecordingFrameHeader frame{};
            file.read(reinterpret_cast<char*>(&frame), sizeof(frame));
            if (!file) {
                break;
            }
            if (frame.port_index >= timeline.tracks.size()) {
                return {};
            }
            timeline.tracks[frame.port_index].sample_timestamps_ns.push_back(frame.timestamp_ns);
            update_bounds(frame.timestamp_ns);
            file.seekg(static_cast<std::streamoff>(timeline.tracks[frame.port_index].port.byte_size), std::ios::cur);
        }
        return timeline;
    }

    while (file.good()) {
        detail::RecordingV3EventHeader event{};
        file.read(reinterpret_cast<char*>(&event), sizeof(event));
        if (!file) {
            break;
        }

        if (event.event_type == detail::k_v3_event_frame) {
            if (event.port_index >= timeline.tracks.size()) {
                return {};
            }
            timeline.tracks[event.port_index].sample_timestamps_ns.push_back(event.timestamp_ns);
            update_bounds(event.timestamp_ns);
            file.seekg(static_cast<std::streamoff>(event.payload_size), std::ios::cur);
        } else if (event.event_type == detail::k_v3_event_marker) {
            std::string label;
            if (event.payload_size > 0) {
                label.resize(event.payload_size);
                file.read(label.data(), static_cast<std::streamsize>(label.size()));
                if (!file) {
                    return {};
                }
            }
            timeline.markers.push_back(RecordingTimelineMarker{event.marker_id, event.timestamp_ns, std::move(label)});
            update_bounds(event.timestamp_ns);
        } else {
            return {};
        }
    }

    return timeline;
}

} // namespace pluginsystem::builtins
