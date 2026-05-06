# Plugin DLL ABI

## Goals

The DLL ABI keeps the host/plugin boundary C-compatible. The ABI does not expose C++ classes, STL containers, exceptions, virtual interfaces, or ownership of host memory.

All structs include `abi_version` or `struct_size` fields so the host can detect incompatible plugins.

## Required Exports

DLL plugins export two functions:

```c
int32_t pluginsystem_discover_plugin(const ps_host_context* host, ps_plugin_discovery* out_discovery);
int32_t pluginsystem_create_plugin_instance(const ps_host_context* host, const ps_instance_config* config, ps_plugin_instance* out_instance);
```

Discovery must be callable before any instance exists. Instance creation receives already-created host bindings.

## Discovery Contract

Discovery returns a `ps_plugin_descriptor` with arrays of:

- `ps_entrypoint_descriptor`
- `ps_port_descriptor`
- `ps_property_descriptor`

Descriptor memory is owned by the plugin and must stay valid while the DLL is loaded. The host copies the descriptor into C++ memory before unloading the discovery probe.

## Instance Creation Contract

Instance creation receives:

- blueprint name
- instance name
- bound port shared-memory names and payload pointers
- bound property shared-memory name, slots, and raw property block

The plugin may store binding metadata during creation. The bindings are fixed for the lifetime of the instance.

## Invocation Contract

The host invokes:

```c
int32_t invoke(void* instance, const char* entrypoint_id, const ps_invocation_context* context);
```

The plugin uses the invocation context to:

- read port data
- write port data
- get direct port payload pointers
- read properties
- write properties
- access the raw property block

The plugin must not free host-owned memory. It must not keep invocation-context pointers after the invocation returns.

## Ownership

- Host owns ports, properties, shared-memory mappings, and invocation context.
- Plugin owns its instance object.
- Provider owns static descriptor tables.
- The host copies descriptors into C++ model objects before using them outside the DLL load scope.

