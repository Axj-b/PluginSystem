#pragma once

#include <filesystem>

namespace pluginsystem::detail {

class SharedLibrary {
public:
    explicit SharedLibrary(const std::filesystem::path& path);
    ~SharedLibrary() noexcept;

    SharedLibrary(const SharedLibrary&) = delete;
    SharedLibrary& operator=(const SharedLibrary&) = delete;

    template <typename Symbol>
    Symbol symbol(const char* name) const
    {
        return reinterpret_cast<Symbol>(symbol_address(name));
    }

    const std::filesystem::path& path() const noexcept;

private:
    void* symbol_address(const char* name) const;

    std::filesystem::path path_;
    void* handle_{nullptr};
};

}

