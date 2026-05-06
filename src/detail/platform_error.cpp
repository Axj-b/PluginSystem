#include "detail/platform_error.hpp"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace pluginsystem::detail {

std::string last_platform_error()
{
#if defined(_WIN32)
    const DWORD error = GetLastError();
    if (error == 0) {
        return "unknown Windows error";
    }

    LPSTR buffer = nullptr;
    const DWORD size = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&buffer),
        0,
        nullptr
    );

    std::string message = size == 0 || buffer == nullptr ? "unknown Windows error" : std::string{buffer, size};
    if (buffer != nullptr) {
        LocalFree(buffer);
    }

    while (!message.empty() && (message.back() == '\r' || message.back() == '\n')) {
        message.pop_back();
    }

    return message;
#else
    return "platform operation failed";
#endif
}

}

