#include <pluginsystem/shared_memory.hpp>

#include <pluginsystem/error.hpp>

#include <algorithm>
#include <utility>

namespace pluginsystem {

std::shared_ptr<SharedPropertyBlock> SharedPropertyBlock::create(
    std::string name,
    const std::vector<PropertyDescriptor>& properties,
    std::uint64_t raw_property_block_size
)
{
    std::vector<Slot> slots;
    std::uint64_t offset = 0;
    slots.reserve(properties.size());

    for (const auto& property : properties) {
        slots.push_back(Slot{property, offset});
        offset += property.byte_size;
    }

    const std::uint64_t payload_size = offset + raw_property_block_size;
    auto memory = SharedMemoryChannel::create(std::move(name), payload_size == 0 ? 1 : payload_size);
    return std::shared_ptr<SharedPropertyBlock>{
        new SharedPropertyBlock{std::move(memory), std::move(slots), offset, raw_property_block_size}
    };
}

SharedPropertyBlock::SharedPropertyBlock(
    std::shared_ptr<SharedMemoryChannel> memory,
    std::vector<Slot> slots,
    std::uint64_t raw_offset,
    std::uint64_t raw_size
)
    : memory_{std::move(memory)}
    , slots_{std::move(slots)}
    , raw_offset_{raw_offset}
    , raw_size_{raw_size}
{
}

const std::string& SharedPropertyBlock::name() const noexcept
{
    return memory_->name();
}

const std::vector<SharedPropertyBlock::Slot>& SharedPropertyBlock::slots() const noexcept
{
    return slots_;
}

std::uint64_t SharedPropertyBlock::raw_property_block_size() const noexcept
{
    return raw_size_;
}

std::uint64_t SharedPropertyBlock::version() const noexcept
{
    return memory_->version();
}

void* SharedPropertyBlock::raw_property_block() noexcept
{
    return static_cast<unsigned char*>(memory_->payload()) + raw_offset_;
}

const void* SharedPropertyBlock::raw_property_block() const noexcept
{
    return static_cast<const unsigned char*>(memory_->payload()) + raw_offset_;
}

void SharedPropertyBlock::read(std::string_view property_id, void* out_data, std::uint64_t byte_size) const
{
    const auto& slot = find_slot(property_id);
    if (!slot.descriptor.readable) {
        throw PluginError{"Property is not readable: " + slot.descriptor.id};
    }
    if (byte_size != slot.descriptor.byte_size) {
        throw PluginError{"Property read size mismatch: " + slot.descriptor.id};
    }
    memory_->read_at(slot.offset, out_data, byte_size);
}

void SharedPropertyBlock::write(std::string_view property_id, const void* data, std::uint64_t byte_size)
{
    const auto& slot = find_slot(property_id);
    if (!slot.descriptor.writable) {
        throw PluginError{"Property is not writable: " + slot.descriptor.id};
    }
    if (byte_size != slot.descriptor.byte_size) {
        throw PluginError{"Property write size mismatch: " + slot.descriptor.id};
    }
    memory_->write_at(slot.offset, data, byte_size);
}

SharedMemoryChannel& SharedPropertyBlock::memory() noexcept
{
    return *memory_;
}

const SharedMemoryChannel& SharedPropertyBlock::memory() const noexcept
{
    return *memory_;
}

const SharedPropertyBlock::Slot& SharedPropertyBlock::find_slot(std::string_view property_id) const
{
    const auto found = std::find_if(slots_.begin(), slots_.end(), [property_id](const Slot& slot) {
        return slot.descriptor.id == property_id;
    });
    if (found == slots_.end()) {
        throw PluginError{"Property is not bound: " + std::string{property_id}};
    }
    return *found;
}

}

