#include "node_editor_widget.hpp"

#include <cassert>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace {

void write_smoke_recording(const std::filesystem::path& path)
{
#pragma pack(push, 1)
    struct Preamble {
        std::uint32_t magic;
        std::uint32_t version;
        std::uint32_t num_ports;
        std::uint32_t data_offset;
    };
    struct PortSlot {
        char type_name[64];
        std::uint64_t byte_size;
        char port_name[64];
        std::uint32_t access_mode_raw;
    };
    struct FrameHeader {
        std::uint64_t timestamp_ns;
        std::uint64_t sequence;
        std::uint32_t port_index;
        std::uint32_t reserved;
    };
#pragma pack(pop)

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    Preamble preamble{0x52454350u, 2u, 1u, static_cast<std::uint32_t>(sizeof(Preamble) + sizeof(PortSlot))};
    file.write(reinterpret_cast<const char*>(&preamble), sizeof(preamble));
    PortSlot slot{};
    std::strncpy(slot.type_name, "smoke.Sample", sizeof(slot.type_name) - 1);
    slot.byte_size = sizeof(std::int32_t);
    std::strncpy(slot.port_name, "Smoke Track", sizeof(slot.port_name) - 1);
    slot.access_mode_raw = static_cast<std::uint32_t>(pluginsystem::PortAccessMode::buffered_latest);
    file.write(reinterpret_cast<const char*>(&slot), sizeof(slot));
    FrameHeader frame{42u, 1u, 0u, 0u};
    const std::int32_t payload{7};
    file.write(reinterpret_cast<const char*>(&frame), sizeof(frame));
    file.write(reinterpret_cast<const char*>(&payload), sizeof(payload));
}

} // namespace

int main()
{
    namespace node_editor = pluginsystem::examples::node_editor;

    node_editor::NodeEditorConfig config;
    config.blueprint_name = "SmokeGraph";
    config.runtime_directory = "smoke_runtime";
    config.register_default_plugins = false;

    bool callback_called = false;
    std::size_t log_count = 0;
    node_editor::NodeEditorCallbacks callbacks;
    callbacks.register_plugins = [&](pluginsystem::PluginRegistry&) {
        callback_called = true;
    };
    callbacks.log_message = [&](node_editor::NodeEditorMessageLevel, std::string_view message) {
        if (!message.empty()) {
            ++log_count;
        }
    };

    node_editor::NodeEditorWidget widget{config, callbacks};
    assert(callback_called);
    assert(log_count > 0);
    assert(widget.graph().blueprint_name == "SmokeGraph");
    assert(widget.graph().runtime_directory == "smoke_runtime");
    assert(widget.validation_messages().empty() == false);

    widget.ReloadPlugins();
    assert(widget.descriptors().descriptors.empty());

    const auto recording_path = std::filesystem::absolute("node_editor_widget_smoke.rec");
    write_smoke_recording(recording_path);

    node_editor::EditorNode replay_node;
    replay_node.ui_id = 1;
    replay_node.node_id = "replay";
    replay_node.plugin_id = "pluginsystem.builtin.replay";
    replay_node.instance_name = "Replay";
    replay_node.string_properties["InputPath"] = recording_path.string();
    widget.graph().nodes.push_back(std::move(replay_node));
    widget.RefreshTimeline();
    assert(widget.timeline_source_node_id() == "replay");
    assert(widget.timeline().tracks.size() == 1);
    assert(widget.timeline().tracks[0].port.port_name == "Smoke Track");

    return 0;
}
