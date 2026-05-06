# Shared Memory Runtime

## Platform

v1 targets Windows named file mappings. The C++ API uses abstractions that can later receive POSIX implementations.

## Names

Shared-memory names are generated from readable blueprint data:

```text
blueprintName_instanceName_portName_kind
```

Invalid OS characters are replaced with underscores. Names are prefixed with a PluginSystem namespace. Collisions are rejected.

## Layout

Every shared-memory object starts with a header:

- magic
- header version
- total size
- payload size
- lock state
- data version

The payload begins immediately after the header.

## Synchronization

PluginSystem owns synchronization. Read/write helpers lock the object, copy data or expose the block, update the data version on writes, and unlock.

The v1 lock is intentionally simple: one writer or reader operation at a time per shared-memory object.

## Port Modes

### direct_block

PluginSystem allocates a fixed-size block and knows only its size. The plugin/application defines the layout. Plugins can obtain the payload pointer or use read/write helpers.

### buffered_latest

Each write copies a complete fixed-size sample into shared memory. Readers get the newest complete sample. History is not retained in v1.

## Property Blocks

Properties live in a shared-memory object per instance. The payload contains:

- typed property slots at known offsets
- optional raw property block after the typed slots

Host/UI code and plugin code must read/write properties through PluginSystem APIs so lock/version metadata stays correct.

