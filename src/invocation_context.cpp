#include <pluginsystem/invocation_context.hpp>

#include "detail/plugin_utils.hpp"

#include <pluginsystem/error.hpp>

#include <algorithm>

namespace pluginsystem {

InvocationContext::InvocationContext(RuntimeBindings& bindings, void* user_context)
    : bindings_{&bindings}
    , user_context_{user_context}
{
}

void InvocationContext::read_port(std::string_view port_id, void* out_data, std::uint64_t byte_size) const
{
    const auto& binding = find_port(port_id);
    if (byte_size != binding.descriptor.byte_size) {
        throw PluginError{"Port read size mismatch: " + binding.descriptor.id};
    }
    binding.channel->read(out_data, byte_size);
}

void InvocationContext::write_port(std::string_view port_id, const void* data, std::uint64_t byte_size)
{
    auto& binding = find_port(port_id);
    if (byte_size != binding.descriptor.byte_size) {
        throw PluginError{"Port write size mismatch: " + binding.descriptor.id};
    }
    binding.channel->write(data, byte_size);
}

void* InvocationContext::port_payload(std::string_view port_id, std::uint64_t& out_byte_size)
{
    auto& binding = find_port(port_id);
    out_byte_size = binding.descriptor.byte_size;
    return binding.channel->payload();
}

void InvocationContext::read_property(std::string_view property_id, void* out_data, std::uint64_t byte_size) const
{
    if (!bindings_->properties) {
        throw PluginError{"No property block is bound"};
    }
    bindings_->properties->read(property_id, out_data, byte_size);
}

void InvocationContext::write_property(std::string_view property_id, const void* data, std::uint64_t byte_size)
{
    if (!bindings_->properties) {
        throw PluginError{"No property block is bound"};
    }
    bindings_->properties->write(property_id, data, byte_size);
}

void* InvocationContext::raw_property_block(std::uint64_t& out_byte_size)
{
    if (!bindings_->properties) {
        out_byte_size = 0;
        return nullptr;
    }
    out_byte_size = bindings_->properties->raw_property_block_size();
    return bindings_->properties->raw_property_block();
}

ps_invocation_context InvocationContext::c_context() noexcept
{
    ps_invocation_context context{};
    context.abi_version = PLUGINSYSTEM_ABI_VERSION;
    context.struct_size = sizeof(ps_invocation_context);
    context.user_data = this;
    context.read_port = &InvocationContext::read_port_thunk;
    context.write_port = &InvocationContext::write_port_thunk;
    context.get_port_payload = &InvocationContext::get_port_payload_thunk;
    context.read_property = &InvocationContext::read_property_thunk;
    context.write_property = &InvocationContext::write_property_thunk;
    context.get_raw_property_block = &InvocationContext::get_raw_property_block_thunk;
    context.user_context = user_context_;
    return context;
}

int32_t InvocationContext::read_port_thunk(void* user_data, const char* port_id, void* out_data, std::uint64_t byte_size)
{
    try {
        static_cast<InvocationContext*>(user_data)->read_port(detail::safe_string(port_id), out_data, byte_size);
        return PS_OK;
    } catch (...) {
        return PS_ERROR;
    }
}

int32_t InvocationContext::write_port_thunk(void* user_data, const char* port_id, const void* data, std::uint64_t byte_size)
{
    try {
        static_cast<InvocationContext*>(user_data)->write_port(detail::safe_string(port_id), data, byte_size);
        return PS_OK;
    } catch (...) {
        return PS_ERROR;
    }
}

void* InvocationContext::get_port_payload_thunk(void* user_data, const char* port_id, std::uint64_t* out_byte_size)
{
    try {
        std::uint64_t byte_size = 0;
        void* payload = static_cast<InvocationContext*>(user_data)->port_payload(detail::safe_string(port_id), byte_size);
        if (out_byte_size != nullptr) {
            *out_byte_size = byte_size;
        }
        return payload;
    } catch (...) {
        if (out_byte_size != nullptr) {
            *out_byte_size = 0;
        }
        return nullptr;
    }
}

int32_t InvocationContext::read_property_thunk(void* user_data, const char* property_id, void* out_data, std::uint64_t byte_size)
{
    try {
        static_cast<InvocationContext*>(user_data)->read_property(detail::safe_string(property_id), out_data, byte_size);
        return PS_OK;
    } catch (...) {
        return PS_ERROR;
    }
}

int32_t InvocationContext::write_property_thunk(void* user_data, const char* property_id, const void* data, std::uint64_t byte_size)
{
    try {
        static_cast<InvocationContext*>(user_data)->write_property(detail::safe_string(property_id), data, byte_size);
        return PS_OK;
    } catch (...) {
        return PS_ERROR;
    }
}

void* InvocationContext::get_raw_property_block_thunk(void* user_data, std::uint64_t* out_byte_size)
{
    try {
        std::uint64_t byte_size = 0;
        void* payload = static_cast<InvocationContext*>(user_data)->raw_property_block(byte_size);
        if (out_byte_size != nullptr) {
            *out_byte_size = byte_size;
        }
        return payload;
    } catch (...) {
        if (out_byte_size != nullptr) {
            *out_byte_size = 0;
        }
        return nullptr;
    }
}

PortRuntimeBinding& InvocationContext::find_port(std::string_view port_id)
{
    const auto found = std::find_if(bindings_->ports.begin(), bindings_->ports.end(), [port_id](const auto& binding) {
        return binding.descriptor.id == port_id;
    });
    if (found == bindings_->ports.end()) {
        throw PluginError{"Port is not bound: " + std::string{port_id}};
    }
    return *found;
}

const PortRuntimeBinding& InvocationContext::find_port(std::string_view port_id) const
{
    const auto found = std::find_if(bindings_->ports.begin(), bindings_->ports.end(), [port_id](const auto& binding) {
        return binding.descriptor.id == port_id;
    });
    if (found == bindings_->ports.end()) {
        throw PluginError{"Port is not bound: " + std::string{port_id}};
    }
    return *found;
}

}

