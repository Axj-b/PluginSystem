# PluginSystem Full Implementation Plan

## Summary

Build PluginSystem step by step into a Windows-first C++ runtime for node-editor style pipelines. The library supports DLL plugins, built-in plugins, and app-provided plugins through one provider registry. It owns plugin discovery, instance creation, host-created shared-memory ports, shared-memory properties, and concurrent entrypoint invocation primitives.

PluginSystem does not own blueprint editing, graph scheduling, IPC orchestration, or process placement. Those concerns belong to the consuming application.

## Step 1: Documentation Foundation

- Create this implementation plan.
- Create `ARCHITECTURE.md` for providers, discovery, instances, entrypoints, ports, properties, concurrency, and blueprint responsibilities.
- Create `PLUGIN_ABI.md` for DLL discovery and instance factory exports.
- Create `SHARED_MEMORY.md` for Windows named shared-memory layout, lock/version metadata, direct block ports, buffered latest ports, and property blocks.
- Create `ROADMAP.md` for later graph runtime, POSIX support, FIFO ports, streaming ports, and richer node-editor integration.

## Step 2: Core Public Types

- Replace the original single `LoadedPlugin` model with generic descriptors and runtime handles.
- Add `PluginDescriptor`, `EntrypointDescriptor`, `PortDescriptor`, `PropertyDescriptor`, `PluginInstance`, `PluginProvider`, and `PluginRegistry`.
- Define port access modes: `direct_block` and `buffered_latest`.
- Define concurrency policies: `instance_serialized`, `entrypoint_serialized`, and `fully_concurrent`.
- Keep payload formats application/plugin-defined. PluginSystem stores names, sizes, access modes, and shared-memory bindings.

## Step 3: Provider Registry

- Implement `PluginRegistry` as the main discovery and creation surface.
- Add a DLL provider that discovers plugins from DLL paths.
- Add a built-in provider API so the library or consuming app can register source-code plugins without DLL loading.
- Ensure DLL and built-in plugins return the same descriptor model.

## Step 4: DLL ABI V2

- Add exported DLL discovery function: `pluginsystem_discover_plugin`.
- Add exported DLL instance creation function: `pluginsystem_create_plugin_instance`.
- Discovery must happen before instance creation.
- Discovery returns plugin metadata, entrypoints, ports, properties, and concurrency capabilities.
- Instance creation receives fixed host-owned port/property bindings.
- All ABI structs stay C-compatible and include size/version fields.

## Step 5: Shared Memory Runtime

- Implement Windows named shared memory using file mappings.
- Generate readable names from `blueprintName_instanceName_portName_kind`.
- Sanitize invalid OS characters and reject collisions.
- Implement shared-memory metadata: magic/version, total size, payload size, lock state, and data version counter.
- Implement direct block access where plugins can obtain a pointer to host-owned memory.
- Implement buffered latest access where updates copy a complete fixed-size sample into shared memory.

## Step 6: Shared Properties

- Implement host-owned shared-memory property blocks.
- Support typed fixed-size property slots.
- Support an optional raw property block for plugin-specific data.
- Allow Host/UI and plugin code to read/write properties through PluginSystem APIs.
- All property writes update version metadata under host-managed synchronization.

## Step 7: Instance Creation And Binding

- Create instances dynamically at runtime from a provider and plugin id.
- For DLL plugins, copy only the DLL to a unique runtime path before loading.
- For built-in plugins, instantiate directly from the registered factory.
- Bind ports and properties once at creation.
- Do not support live rewiring in v1. Reconfiguration requires rebuilding the affected instance or blueprint.

## Step 8: Entrypoint Invocation

- Implement synchronous invocation: call entrypoint and wait for completion.
- Implement asynchronous invocation with a PluginSystem worker thread and job handle.
- Support job status, wait, and result retrieval.
- Async cancellation only applies to pending jobs in v1.
- Enforce the declared concurrency policy for calls from multiple threads.

## Step 9: Examples

- Update the sample host app to register providers, discover plugins, create shared-memory ports/properties, instantiate plugins, and invoke entrypoints sync and async.
- Add a built-in plugin example.
- Add a DLL plugin example with multiple entrypoints, ports, and properties.

## Step 10: Tests

- Test DLL discovery before instance creation.
- Test built-in and DLL plugins through the same registry.
- Test multiple instances from one DLL using different copied DLL paths.
- Test direct block and buffered latest shared-memory ports.
- Test shared-memory properties across host/plugin access.
- Test concurrent entrypoint triggers and policy enforcement.
- Test async job lifecycle.
- Test invalid descriptors, duplicate shared-memory names, missing symbols, and buffer-size mismatches.

## Acceptance Criteria

- A host app can discover DLL and built-in plugins through one registry.
- A plugin can expose multiple entrypoints.
- A host can create multiple instances of one DLL by loading copied DLL paths.
- A host can create direct and buffered shared-memory ports before instance creation.
- A host can bind shared-memory properties to an instance.
- Multiple threads can trigger plugin entrypoints according to declared policy.
- Tests pass through CTest on a Windows Debug build.

