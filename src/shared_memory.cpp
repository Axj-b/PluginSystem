#include <pluginsystem/shared_memory.hpp>

#include "detail/platform_error.hpp"

#include <pluginsystem/error.hpp>

#include <cstddef>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unordered_map>
#endif

namespace pluginsystem {
namespace {

constexpr std::uint32_t shared_memory_magic = 0x50534D48u; // PSMH
constexpr std::uint32_t shared_memory_version = 1u;

struct SharedMemoryHeader {
    std::uint32_t magic;
    std::uint32_t version;
    std::uint64_t total_size;
    std::uint64_t payload_size;
    volatile long lock_state;
    std::uint32_t reserved_{0};
    volatile long long data_version;
};

static_assert(offsetof(SharedMemoryHeader, data_version) % 8 == 0,
    "data_version must be 8-byte aligned for InterlockedIncrement64");

unsigned char* payload_start(SharedMemoryChannel::Impl& impl);
const unsigned char* payload_start(const SharedMemoryChannel::Impl& impl);

} // namespace

struct SharedMemoryChannel::Impl {
    std::string name;
    std::uint64_t payload_size{0};
    std::uint64_t total_size{0};
    SharedMemoryHeader* header{nullptr};
    unsigned char* view{nullptr};

    // Local mode (all platforms): heap-only, no OS mapping, no global name store.
    // Non-null local_mutex indicates local mode.
    std::vector<unsigned char> local_storage;
    std::unique_ptr<std::mutex> local_mutex;

#if defined(_WIN32)
    HANDLE mapping{nullptr};
#else
    std::shared_ptr<std::vector<unsigned char>> storage;
    mutable std::mutex fallback_mutex;
#endif

    ~Impl()
    {
#if defined(_WIN32)
        if (local_mutex) {
            return; // local_storage is a member; no OS handles to release
        }
        if (view != nullptr) {
            UnmapViewOfFile(view);
        }
        if (mapping != nullptr) {
            CloseHandle(mapping);
        }
#endif
    }
};

namespace {

struct ScopedSharedMemoryLock {
    explicit ScopedSharedMemoryLock(SharedMemoryChannel::Impl& impl_ref)
        : impl{impl_ref}
    {
        if (impl.local_mutex) {
            impl.local_mutex->lock();
            return;
        }
#if defined(_WIN32)
        constexpr int k_spin_before_yield = 1000;
        for (int spin = 0; InterlockedCompareExchange(&impl.header->lock_state, 1, 0) != 0; ++spin) {
            if (spin < k_spin_before_yield) {
                YieldProcessor();
            } else {
                SwitchToThread();
                spin = 0;
            }
        }
#else
        impl.fallback_mutex.lock();
        impl.header->lock_state = 1;
#endif
    }

    ~ScopedSharedMemoryLock()
    {
        if (impl.local_mutex) {
            impl.local_mutex->unlock();
            return;
        }
#if defined(_WIN32)
        InterlockedExchange(&impl.header->lock_state, 0);
#else
        impl.header->lock_state = 0;
        impl.fallback_mutex.unlock();
#endif
    }

    ScopedSharedMemoryLock(const ScopedSharedMemoryLock&) = delete;
    ScopedSharedMemoryLock& operator=(const ScopedSharedMemoryLock&) = delete;

    SharedMemoryChannel::Impl& impl;
};

void increment_version(SharedMemoryChannel::Impl& impl)
{
    if (impl.local_mutex) {
        ++impl.header->data_version;
        return;
    }
#if defined(_WIN32)
    InterlockedIncrement64(&impl.header->data_version);
#else
    ++impl.header->data_version;
#endif
}

unsigned char* payload_start(SharedMemoryChannel::Impl& impl)
{
    return impl.view + sizeof(SharedMemoryHeader);
}

const unsigned char* payload_start(const SharedMemoryChannel::Impl& impl)
{
    return impl.view + sizeof(SharedMemoryHeader);
}

#if !defined(_WIN32)
std::unordered_map<std::string, std::weak_ptr<std::vector<unsigned char>>>& fallback_shared_memory_store()
{
    static std::unordered_map<std::string, std::weak_ptr<std::vector<unsigned char>>> store;
    return store;
}

std::mutex& fallback_shared_memory_store_mutex()
{
    static std::mutex mutex;
    return mutex;
}
#endif

std::unique_ptr<SharedMemoryChannel::Impl> create_shared_memory_impl(const std::string& name, std::uint64_t payload_size)
{
    if (payload_size == 0) {
        payload_size = 1;
    }

    auto impl = std::make_unique<SharedMemoryChannel::Impl>();
    impl->name = name;
    impl->payload_size = payload_size;
    impl->total_size = sizeof(SharedMemoryHeader) + payload_size;

#if defined(_WIN32)
    impl->mapping = CreateFileMappingA(
        INVALID_HANDLE_VALUE,
        nullptr,
        PAGE_READWRITE,
        static_cast<DWORD>((impl->total_size >> 32) & 0xFFFFFFFFu),
        static_cast<DWORD>(impl->total_size & 0xFFFFFFFFu),
        name.c_str()
    );
    if (impl->mapping == nullptr) {
        throw PluginError{"Failed to create shared memory '" + name + "': " + detail::last_platform_error()};
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        throw PluginError{"Shared memory name already exists: " + name};
    }

    impl->view = static_cast<unsigned char*>(MapViewOfFile(impl->mapping, FILE_MAP_ALL_ACCESS, 0, 0, impl->total_size));
    if (impl->view == nullptr) {
        throw PluginError{"Failed to map shared memory '" + name + "': " + detail::last_platform_error()};
    }
#else
    std::lock_guard<std::mutex> lock{fallback_shared_memory_store_mutex()};
    const auto existing = fallback_shared_memory_store().find(name);
    if (existing != fallback_shared_memory_store().end() && !existing->second.expired()) {
        throw PluginError{"Shared memory name already exists: " + name};
    }
    impl->storage = std::make_shared<std::vector<unsigned char>>(static_cast<std::size_t>(impl->total_size));
    fallback_shared_memory_store()[name] = impl->storage;
    impl->view = impl->storage->data();
#endif

    impl->header = reinterpret_cast<SharedMemoryHeader*>(impl->view);
    impl->header->magic = shared_memory_magic;
    impl->header->version = shared_memory_version;
    impl->header->total_size = impl->total_size;
    impl->header->payload_size = payload_size;
    impl->header->lock_state = 0;
    impl->header->data_version = 0;
    std::memset(payload_start(*impl), 0, static_cast<std::size_t>(payload_size));

    return impl;
}

std::unique_ptr<SharedMemoryChannel::Impl> create_local_impl(const std::string& name, std::uint64_t payload_size)
{
    if (payload_size == 0) {
        payload_size = 1;
    }

    auto impl = std::make_unique<SharedMemoryChannel::Impl>();
    impl->name = name;
    impl->payload_size = payload_size;
    impl->total_size = sizeof(SharedMemoryHeader) + payload_size;
    impl->local_mutex = std::make_unique<std::mutex>();
    impl->local_storage.resize(static_cast<std::size_t>(impl->total_size));
    impl->view = impl->local_storage.data();

    impl->header = reinterpret_cast<SharedMemoryHeader*>(impl->view);
    impl->header->magic = shared_memory_magic;
    impl->header->version = shared_memory_version;
    impl->header->total_size = impl->total_size;
    impl->header->payload_size = payload_size;
    impl->header->lock_state = 0;
    impl->header->data_version = 0;
    std::memset(payload_start(*impl), 0, static_cast<std::size_t>(payload_size));

    return impl;
}

std::unique_ptr<SharedMemoryChannel::Impl> open_shared_memory_impl(const std::string& name)
{
    auto impl = std::make_unique<SharedMemoryChannel::Impl>();
    impl->name = name;

#if defined(_WIN32)
    impl->mapping = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
    if (impl->mapping == nullptr) {
        throw PluginError{"Failed to open shared memory '" + name + "': " + detail::last_platform_error()};
    }
    impl->view = static_cast<unsigned char*>(MapViewOfFile(impl->mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0));
    if (impl->view == nullptr) {
        throw PluginError{"Failed to map shared memory '" + name + "': " + detail::last_platform_error()};
    }
#else
    std::lock_guard<std::mutex> lock{fallback_shared_memory_store_mutex()};
    const auto found = fallback_shared_memory_store().find(name);
    if (found == fallback_shared_memory_store().end()) {
        throw PluginError{"Failed to open shared memory '" + name + "'"};
    }
    impl->storage = found->second.lock();
    if (!impl->storage) {
        throw PluginError{"Shared memory expired: " + name};
    }
    impl->view = impl->storage->data();
#endif

    impl->header = reinterpret_cast<SharedMemoryHeader*>(impl->view);
    if (impl->header->magic != shared_memory_magic || impl->header->version != shared_memory_version) {
        throw PluginError{"Shared memory header is incompatible: " + name};
    }
    impl->payload_size = impl->header->payload_size;
    impl->total_size = impl->header->total_size;
    return impl;
}

} // namespace

SharedMemoryChannel::SharedMemoryChannel(std::unique_ptr<Impl> impl)
    : impl_{std::move(impl)}
{
}

SharedMemoryChannel::~SharedMemoryChannel() = default;

SharedMemoryChannel::SharedMemoryChannel(SharedMemoryChannel&&) noexcept = default;

SharedMemoryChannel& SharedMemoryChannel::operator=(SharedMemoryChannel&&) noexcept = default;

std::shared_ptr<SharedMemoryChannel> SharedMemoryChannel::create(std::string name, std::uint64_t payload_size)
{
    return std::shared_ptr<SharedMemoryChannel>{new SharedMemoryChannel{create_shared_memory_impl(name, payload_size)}};
}

std::shared_ptr<SharedMemoryChannel> SharedMemoryChannel::create_local(std::string name, std::uint64_t payload_size)
{
    return std::shared_ptr<SharedMemoryChannel>{new SharedMemoryChannel{create_local_impl(name, payload_size)}};
}

std::shared_ptr<SharedMemoryChannel> SharedMemoryChannel::open(std::string name)
{
    return std::shared_ptr<SharedMemoryChannel>{new SharedMemoryChannel{open_shared_memory_impl(name)}};
}

const std::string& SharedMemoryChannel::name() const noexcept
{
    return impl_->name;
}

std::uint64_t SharedMemoryChannel::payload_size() const noexcept
{
    return impl_->payload_size;
}

std::uint64_t SharedMemoryChannel::version() const noexcept
{
    return static_cast<std::uint64_t>(impl_->header->data_version);
}

void* SharedMemoryChannel::payload() noexcept
{
    return payload_start(*impl_);
}

const void* SharedMemoryChannel::payload() const noexcept
{
    return payload_start(*impl_);
}

void SharedMemoryChannel::read(void* out_data, std::uint64_t byte_size) const
{
    if (byte_size != impl_->payload_size) {
        throw PluginError{"Shared memory read size must match payload size"};
    }
    read_at(0, out_data, byte_size);
}

void SharedMemoryChannel::write(const void* data, std::uint64_t byte_size)
{
    if (byte_size != impl_->payload_size) {
        throw PluginError{"Shared memory write size must match payload size"};
    }
    write_at(0, data, byte_size);
}

void SharedMemoryChannel::read_at(std::uint64_t offset, void* out_data, std::uint64_t byte_size) const
{
    if (out_data == nullptr) {
        throw PluginError{"Shared memory read output is null"};
    }
    if (offset > impl_->payload_size || byte_size > impl_->payload_size - offset) {
        throw PluginError{"Shared memory read exceeds payload size"};
    }

    ScopedSharedMemoryLock lock{*impl_};
    std::memcpy(out_data, payload_start(*impl_) + offset, static_cast<std::size_t>(byte_size));
}

void SharedMemoryChannel::write_at(std::uint64_t offset, const void* data, std::uint64_t byte_size)
{
    if (data == nullptr) {
        throw PluginError{"Shared memory write input is null"};
    }
    if (offset > impl_->payload_size || byte_size > impl_->payload_size - offset) {
        throw PluginError{"Shared memory write exceeds payload size"};
    }

    ScopedSharedMemoryLock lock{*impl_};
    std::memcpy(payload_start(*impl_) + offset, data, static_cast<std::size_t>(byte_size));
    increment_version(*impl_);
}

}
