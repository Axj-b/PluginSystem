#include "detail/shared_library.hpp"

#include "detail/platform_error.hpp"

#include <pluginsystem/error.hpp>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace pluginsystem::detail {

SharedLibrary::SharedLibrary(const std::filesystem::path& path)
    : path_{path}
{
#if defined(_WIN32)
    handle_ = LoadLibraryW(path.wstring().c_str());
#else
    handle_ = nullptr;
#endif
    if (handle_ == nullptr) {
        throw PluginError{"Failed to load plugin library '" + path.string() + "': " + last_platform_error()};
    }
}

SharedLibrary::~SharedLibrary() noexcept
{
#if defined(_WIN32)
    if (handle_ != nullptr) {
        FreeLibrary(static_cast<HMODULE>(handle_));
    }
#endif
}

const std::filesystem::path& SharedLibrary::path() const noexcept
{
    return path_;
}

void* SharedLibrary::symbol_address(const char* name) const
{
#if defined(_WIN32)
    auto* address = reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle_), name));
#else
    void* address = nullptr;
#endif
    if (address == nullptr) {
        throw PluginError{"Failed to resolve plugin symbol '" + std::string{name} + "': " + last_platform_error()};
    }

    return address;
}

}

