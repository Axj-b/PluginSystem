#include "node_editor_widget.hpp"

#include <cassert>
#include <cstddef>
#include <string>
#include <string_view>

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
    return 0;
}
