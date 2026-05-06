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

A DLL plugin exports discovery and instance creation functions:

```cpp
#include <pluginsystem/plugin_api.h>

extern "C" PLUGINSYSTEM_EXPORT int32_t pluginsystem_discover_plugin(
    const ps_host_context* host,
    ps_plugin_discovery* out_discovery
);

extern "C" PLUGINSYSTEM_EXPORT int32_t pluginsystem_create_plugin_instance(
    const ps_host_context* host,
    const ps_instance_config* config,
    ps_plugin_instance* out_instance
);
```

Discovery returns plugin metadata, entrypoints, ports, properties, and concurrency policy. Instance creation receives fixed host-owned port/property bindings.

See:

- `examples/greeter_plugin.cpp`
- `examples/host_app.cpp`
- `tests/plugin_manager_tests.cpp`

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
