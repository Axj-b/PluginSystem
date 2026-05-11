# PluginSystem

PluginSystem is a Windows-first C++17 runtime library for node-editor style pipelines. It supports DLL plugins, built-in plugins, and application-provided plugins through one provider registry.

The library owns plugin discovery, dynamic instance creation, host-created shared-memory ports, shared-memory properties, and synchronous/asynchronous entrypoint invocation. The consuming application owns blueprint editing, graph scheduling, process orchestration, and IPC.

## Build

```powershell
cmake -S . -B build
cmake --build build
ctest --test-dir build -C Debug --output-on-failure
```

With presets:

```powershell
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug
```

VS Code uses the same presets through `.vscode/settings.json`. The launch configurations can debug `plugin_manager_tests` and `host_app`.

## Use In Another App

Add the library to your build:

```cmake
add_subdirectory(path/to/PluginSystem)
target_link_libraries(my_app PRIVATE PluginSystem::pluginsystem)
```

Register providers, discover plugins, create instances, and trigger entrypoints:

```cpp
#include <pluginsystem/plugin_manager.hpp>

pluginsystem::PluginRegistry registry;
registry.add_dll_plugin("my_plugin.dll");

auto descriptors = registry.discover_plugins();

pluginsystem::PluginInstanceConfig config;
config.blueprint_name = "MainBlueprint";
config.instance_name = "NodeA";
config.runtime_directory = "pluginsystem_runtime";

auto instance = registry.create_instance("my.plugin.id", config);
instance->invoke("Function1");

auto job = instance->submit("Function2");
instance->wait(job);
```

Ports and properties are created by the host during instance creation. Port shared-memory names are readable and derived from blueprint, instance, port, and kind names.

## Write A DLL Plugin

The SDK provides a C++ adapter layer that handles all ABI wiring. Your plugin is a plain C++ class; you never touch the raw C exports.

### CMake

```cmake
add_subdirectory(path/to/PluginSystem)

add_library(my_plugin SHARED my_plugin.cpp)
target_link_libraries(my_plugin PRIVATE pluginsystem_plugin_sdk)
```

`pluginsystem_plugin_sdk` is an INTERFACE target that adds `examples/` to your include path, making `<sdk/dll_adapter.hpp>` available alongside the core `<pluginsystem/sdk.hpp>` types.

### Plugin header

```cpp
// my_plugin.h
#pragma once
#include <pluginsystem/sdk.hpp>   // core types (PluginBase, ports, properties)
#include "my_data.h"              // your TypeName<>-registered sample struct

class MyPlugin final : public pluginsystem::sdk::PluginBase {
public:
    static void Register(pluginsystem::sdk::PluginRegistration<MyPlugin>& api);

    int  Init();
    void Process();
    void Reset();

private:
    pluginsystem::sdk::InputPort<MyData>   data_in_{"DataIn"};
    pluginsystem::sdk::OutputPort<MyData>  data_out_{"DataOut"};
    pluginsystem::sdk::Property<float>     threshold_{"Threshold", "Threshold"};
};
```

Ports and properties must be declared as **members** — registration discovers their IDs by inspecting a default-constructed probe instance.

### Register entrypoints

All plugin metadata, ports, properties, and entrypoints are declared in the static `Register` method. No engine source is modified.

```cpp
// my_plugin.cpp
#include "my_plugin.h"
#include <sdk/dll_adapter.hpp>   // DllAdapter + PLUGINSYSTEM_EXPORT_PLUGIN

void MyPlugin::Register(pluginsystem::sdk::PluginRegistration<MyPlugin>& api)
{
    api.set_plugin(
        "my.plugin",                            // unique id
        "My Plugin",                            // display name
        "1.0.0",
        "Short description",
        PS_CONCURRENCY_INSTANCE_SERIALIZED
    );

    // Declare ports
    api.input (&MyPlugin::data_in_,  pluginsystem::sdk::PortAccessMode::BufferedLatest);
    api.output(&MyPlugin::data_out_, pluginsystem::sdk::PortAccessMode::BufferedLatest);

    // Declare properties  { default, min, max }
    api.property(&MyPlugin::threshold_, /*readable=*/true, /*writable=*/true, {0.5, 0.0, 1.0});

    // Register entrypoints
    api.entrypoint("Process", &MyPlugin::Process)
        .description("Run one processing step")
        .reads      (&MyPlugin::data_in_)
        .writes     (&MyPlugin::data_out_)
        .triggeredBy(&MyPlugin::data_in_);

    api.entrypoint("Reset", &MyPlugin::Reset)
        .description("Clear the output port")
        .writes(&MyPlugin::data_out_);
}

int MyPlugin::Init()
{
    return PS_OK;   // called once when the instance is created
}

void MyPlugin::Process()
{
    MyData data{};
    data_in_.read(data);
    if (data.value >= threshold_.read()) { /* ... */ }
    data_out_.write(data);
}

void MyPlugin::Reset()
{
    data_out_.write(MyData{});
}

PLUGINSYSTEM_EXPORT_PLUGIN(MyPlugin)   // generates the two required C exports
```

### Adding a new entrypoint

1. Declare the method in the header.
2. Register it inside `Register()` — the builder supports the same `.reads`/`.writes`/`.triggeredBy`/`.description`/`.concurrency` chain.
3. Implement it in the `.cpp`.

No changes to `src/` or to any host-side code are needed.

### Optional lifecycle and render hooks

`PluginBase` provides opt-in hooks that the adapter calls automatically — override only what you need:

| Override | Called by | Notes |
|----------|-----------|-------|
| `int Init()` | instance creation | Non-zero return aborts creation |
| `int Stop()` | host teardown | Called before `destroy` |
| `bool HasRender() const` | each frame | Return `true` to enable the render hook |
| `void Render(void* ctx)` | each UI frame | `ctx` is the host's UI context (e.g. `ImGuiContext*`) |

Render example for an ImGui-based host:

```cpp
// header
bool HasRender() const override { return true; }
void Render(void* user_context) override;

// cpp
void MyPlugin::Render(void* user_context)
{
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(user_context));
    ImGui::Begin("My Plugin");
    ImGui::Text("value: %.3f", last_value_);
    ImGui::End();
}
```

### Full working example

See `examples/MyUserPlugin.h` / `examples/MyUserPlugin.cpp` for a complete plugin with multiple entrypoints, an input port, an output port, and a property.

## Architecture Docs

- `docs/IMPLEMENTATION_PLAN.md`
- `docs/ARCHITECTURE.md`
- `docs/PLUGIN_ABI.md`
- `docs/SHARED_MEMORY.md`
- `docs/ROADMAP.md`

## Source Layout

- `include/pluginsystem/plugin_manager.hpp`: compatibility umbrella header
- `include/pluginsystem/types.hpp`: descriptors, enums, host/config types
- `include/pluginsystem/shared_memory.hpp`: shared-memory channels and property blocks
- `include/pluginsystem/invocation_context.hpp`: plugin invocation context and runtime bindings
- `include/pluginsystem/instance.hpp`: plugin instance and async job API
- `include/pluginsystem/providers.hpp`: DLL and built-in provider interfaces
- `include/pluginsystem/registry.hpp`: discovery and instance creation registry
- `src/detail/`: internal platform, DLL loading, and descriptor conversion helpers
