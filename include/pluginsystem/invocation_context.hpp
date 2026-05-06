#pragma once

#include <pluginsystem/plugin_api.h>
#include <pluginsystem/shared_memory.hpp>

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace pluginsystem {

struct PortRuntimeBinding {
    PortDescriptor descriptor;
    std::shared_ptr<SharedMemoryChannel> channel;
};

struct RuntimeBindings {
    std::vector<PortRuntimeBinding> ports;
    std::shared_ptr<SharedPropertyBlock> properties;
};

class InvocationContext {
public:
    explicit InvocationContext(RuntimeBindings& bindings);

    void read_port(std::string_view port_id, void* out_data, std::uint64_t byte_size) const;
    void write_port(std::string_view port_id, const void* data, std::uint64_t byte_size);
    void* port_payload(std::string_view port_id, std::uint64_t& out_byte_size);

    void read_property(std::string_view property_id, void* out_data, std::uint64_t byte_size) const;
    void write_property(std::string_view property_id, const void* data, std::uint64_t byte_size);
    void* raw_property_block(std::uint64_t& out_byte_size);

    ps_invocation_context c_context() noexcept;

private:
    static int32_t read_port_thunk(void* user_data, const char* port_id, void* out_data, std::uint64_t byte_size);
    static int32_t write_port_thunk(void* user_data, const char* port_id, const void* data, std::uint64_t byte_size);
    static void* get_port_payload_thunk(void* user_data, const char* port_id, std::uint64_t* out_byte_size);
    static int32_t read_property_thunk(void* user_data, const char* property_id, void* out_data, std::uint64_t byte_size);
    static int32_t write_property_thunk(void* user_data, const char* property_id, const void* data, std::uint64_t byte_size);
    static void* get_raw_property_block_thunk(void* user_data, std::uint64_t* out_byte_size);

    PortRuntimeBinding& find_port(std::string_view port_id);
    const PortRuntimeBinding& find_port(std::string_view port_id) const;

    RuntimeBindings* bindings_{nullptr};
};

}

