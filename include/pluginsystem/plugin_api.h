#ifndef PLUGINSYSTEM_PLUGIN_API_H
#define PLUGINSYSTEM_PLUGIN_API_H

#include <stdint.h>

#define PLUGINSYSTEM_ABI_VERSION 2u
#define PLUGINSYSTEM_DISCOVER_PLUGIN_SYMBOL "pluginsystem_discover_plugin"
#define PLUGINSYSTEM_CREATE_PLUGIN_INSTANCE_SYMBOL "pluginsystem_create_plugin_instance"

#if defined(_WIN32)
#define PLUGINSYSTEM_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define PLUGINSYSTEM_EXPORT __attribute__((visibility("default")))
#else
#define PLUGINSYSTEM_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ps_result {
    PS_OK = 0,
    PS_ERROR = 1,
    PS_NOT_FOUND = 2,
    PS_INVALID_ARGUMENT = 3,
    PS_BUFFER_TOO_SMALL = 4
} ps_result;

typedef enum ps_log_level {
    PS_LOG_TRACE = 0,
    PS_LOG_DEBUG = 1,
    PS_LOG_INFO = 2,
    PS_LOG_WARNING = 3,
    PS_LOG_ERROR = 4
} ps_log_level;

typedef enum ps_port_direction {
    PS_PORT_INPUT = 0,
    PS_PORT_OUTPUT = 1
} ps_port_direction;

typedef enum ps_port_access_mode {
    PS_PORT_DIRECT_BLOCK = 0,
    PS_PORT_BUFFERED_LATEST = 1
} ps_port_access_mode;

typedef enum ps_concurrency_policy {
    PS_CONCURRENCY_INSTANCE_SERIALIZED = 0,
    PS_CONCURRENCY_ENTRYPOINT_SERIALIZED = 1,
    PS_CONCURRENCY_FULLY_CONCURRENT = 2
} ps_concurrency_policy;

typedef struct ps_host_context {
    uint32_t abi_version;
    void* user_data;
    void (*log)(void* user_data, ps_log_level level, const char* source, const char* message);
    void* (*get_service)(void* user_data, const char* service_name, uint32_t service_version);
} ps_host_context;

typedef struct ps_port_descriptor {
    uint32_t struct_size;
    const char* id;
    const char* name;
    ps_port_direction direction;
    ps_port_access_mode access_mode;
    uint64_t byte_size;
    uint64_t alignment;
    const char* type_name;
} ps_port_descriptor;

typedef struct ps_property_descriptor {
    uint32_t struct_size;
    const char* id;
    const char* name;
    const char* type_name;
    uint64_t byte_size;
    uint32_t readable;
    uint32_t writable;
} ps_property_descriptor;

typedef struct ps_entrypoint_descriptor {
    uint32_t struct_size;
    const char* id;
    const char* name;
    const char* description;
    ps_concurrency_policy concurrency;
    const char* const* input_port_ids;
    uint32_t input_port_count;
    const char* const* output_port_ids;
    uint32_t output_port_count;
} ps_entrypoint_descriptor;

typedef struct ps_plugin_descriptor {
    uint32_t abi_version;
    uint32_t struct_size;
    const char* id;
    const char* name;
    const char* version;
    const char* description;
    ps_concurrency_policy concurrency;
    const ps_entrypoint_descriptor* entrypoints;
    uint32_t entrypoint_count;
    const ps_port_descriptor* ports;
    uint32_t port_count;
    const ps_property_descriptor* properties;
    uint32_t property_count;
    uint64_t raw_property_block_size;
} ps_plugin_descriptor;

typedef struct ps_plugin_discovery {
    uint32_t abi_version;
    uint32_t struct_size;
    const ps_plugin_descriptor* descriptor;
} ps_plugin_discovery;

typedef struct ps_port_binding {
    uint32_t struct_size;
    const char* port_id;
    const char* shared_memory_name;
    void* payload;
    uint64_t payload_size;
    ps_port_direction direction;
    ps_port_access_mode access_mode;
} ps_port_binding;

typedef struct ps_property_binding {
    uint32_t struct_size;
    const char* property_id;
    const char* shared_memory_name;
    uint64_t offset;
    uint64_t byte_size;
    const char* type_name;
    uint32_t readable;
    uint32_t writable;
} ps_property_binding;

typedef struct ps_instance_config {
    uint32_t abi_version;
    uint32_t struct_size;
    const char* blueprint_name;
    const char* instance_name;
    const ps_port_binding* ports;
    uint32_t port_count;
    const char* property_block_name;
    void* property_block_payload;
    uint64_t property_block_size;
    const ps_property_binding* properties;
    uint32_t property_count;
    void* raw_property_block;
    uint64_t raw_property_block_size;
} ps_instance_config;

typedef int32_t (*ps_read_port_fn)(void* user_data, const char* port_id, void* out_data, uint64_t byte_size);
typedef int32_t (*ps_write_port_fn)(void* user_data, const char* port_id, const void* data, uint64_t byte_size);
typedef void* (*ps_get_port_payload_fn)(void* user_data, const char* port_id, uint64_t* out_byte_size);
typedef int32_t (*ps_read_property_fn)(void* user_data, const char* property_id, void* out_data, uint64_t byte_size);
typedef int32_t (*ps_write_property_fn)(void* user_data, const char* property_id, const void* data, uint64_t byte_size);
typedef void* (*ps_get_raw_property_block_fn)(void* user_data, uint64_t* out_byte_size);

typedef struct ps_invocation_context {
    uint32_t abi_version;
    uint32_t struct_size;
    void* user_data;
    ps_read_port_fn read_port;
    ps_write_port_fn write_port;
    ps_get_port_payload_fn get_port_payload;
    ps_read_property_fn read_property;
    ps_write_property_fn write_property;
    ps_get_raw_property_block_fn get_raw_property_block;
} ps_invocation_context;

typedef struct ps_plugin_instance {
    uint32_t abi_version;
    uint32_t struct_size;
    void* instance;
    int32_t (*invoke)(void* instance, const char* entrypoint_id, const ps_invocation_context* context);
    void (*destroy)(void* instance);
} ps_plugin_instance;

typedef int32_t (*ps_discover_plugin_fn)(const ps_host_context* host, ps_plugin_discovery* out_discovery);
typedef int32_t (*ps_create_plugin_instance_fn)(
    const ps_host_context* host,
    const ps_instance_config* config,
    ps_plugin_instance* out_instance
);

#ifdef __cplusplus
}
#endif

#endif
