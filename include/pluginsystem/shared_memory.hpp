#pragma once

#include <pluginsystem/types.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace pluginsystem {

class SharedMemoryChannel {
public:
    struct Impl;

    ~SharedMemoryChannel();

    SharedMemoryChannel(const SharedMemoryChannel&) = delete;
    SharedMemoryChannel& operator=(const SharedMemoryChannel&) = delete;
    SharedMemoryChannel(SharedMemoryChannel&&) noexcept;
    SharedMemoryChannel& operator=(SharedMemoryChannel&&) noexcept;

    static std::shared_ptr<SharedMemoryChannel> create(std::string name, std::uint64_t payload_size);
    static std::shared_ptr<SharedMemoryChannel> open(std::string name);

    const std::string& name() const noexcept;
    std::uint64_t payload_size() const noexcept;
    std::uint64_t version() const noexcept;
    void* payload() noexcept;
    const void* payload() const noexcept;

    void read(void* out_data, std::uint64_t byte_size) const;
    void write(const void* data, std::uint64_t byte_size);
    void read_at(std::uint64_t offset, void* out_data, std::uint64_t byte_size) const;
    void write_at(std::uint64_t offset, const void* data, std::uint64_t byte_size);

private:
    explicit SharedMemoryChannel(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

class SharedPropertyBlock {
public:
    struct Slot {
        PropertyDescriptor descriptor;
        std::uint64_t offset{0};
    };

    static std::shared_ptr<SharedPropertyBlock> create(
        std::string name,
        const std::vector<PropertyDescriptor>& properties,
        std::uint64_t raw_property_block_size
    );

    const std::string& name() const noexcept;
    const std::vector<Slot>& slots() const noexcept;
    std::uint64_t raw_property_block_size() const noexcept;
    std::uint64_t version() const noexcept;
    void* raw_property_block() noexcept;
    const void* raw_property_block() const noexcept;

    void read(std::string_view property_id, void* out_data, std::uint64_t byte_size) const;
    void write(std::string_view property_id, const void* data, std::uint64_t byte_size);

    SharedMemoryChannel& memory() noexcept;
    const SharedMemoryChannel& memory() const noexcept;

private:
    SharedPropertyBlock(
        std::shared_ptr<SharedMemoryChannel> memory,
        std::vector<Slot> slots,
        std::uint64_t raw_offset,
        std::uint64_t raw_size
    );

    const Slot& find_slot(std::string_view property_id) const;

    std::shared_ptr<SharedMemoryChannel> memory_;
    std::vector<Slot> slots_;
    std::uint64_t raw_offset_{0};
    std::uint64_t raw_size_{0};
};

}

