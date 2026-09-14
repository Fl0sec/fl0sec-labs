#pragma once

#include "win32.hpp"

#include <array>
#include <cstdint>
#include <stdexcept>

template <typename Value>
Value read_remote(HANDLE process, uintptr_t address) {
    Value value{};
    SIZE_T read = 0;
    if (!ReadProcessMemory(process, reinterpret_cast<const void*>(address), &value,
                           sizeof(value), &read) ||
        read != sizeof(value)) {
        throw std::runtime_error("ReadProcessMemory failed");
    }
    return value;
}

template <typename Value>
void write_remote(HANDLE process, uintptr_t address, const Value& value) {
    SIZE_T written = 0;
    if (!WriteProcessMemory(process, reinterpret_cast<void*>(address), &value,
                            sizeof(value), &written) ||
        written != sizeof(value)) {
        throw std::runtime_error("WriteProcessMemory failed");
    }
}

template <size_t Size>
void write_protected(HANDLE process, uintptr_t address,
                     const std::array<uint8_t, Size>& bytes) {
    DWORD old_protection = 0;
    if (!VirtualProtectEx(process, reinterpret_cast<void*>(address), bytes.size(),
                          PAGE_EXECUTE_READWRITE, &old_protection)) {
        throw std::runtime_error("VirtualProtectEx writable failed");
    }
    try {
        write_remote(process, address, bytes);
        if (!FlushInstructionCache(process, reinterpret_cast<const void*>(address),
                                   bytes.size())) {
            throw std::runtime_error("FlushInstructionCache failed");
        }
    } catch (...) {
        DWORD ignored = 0;
        VirtualProtectEx(process, reinterpret_cast<void*>(address), bytes.size(),
                         old_protection, &ignored);
        throw;
    }
    DWORD ignored = 0;
    if (!VirtualProtectEx(process, reinterpret_cast<void*>(address), bytes.size(),
                          old_protection, &ignored)) {
        throw std::runtime_error("VirtualProtectEx restore failed");
    }
}

template <size_t Size>
class RemotePatch {
public:
    RemotePatch(HANDLE process, uintptr_t address,
                std::array<uint8_t, Size> original,
                std::array<uint8_t, Size> replacement)
        : process_(process), address_(address), original_(original),
          replacement_(replacement) {}

    bool enabled() const { return enabled_; }

    void enable() {
        set(replacement_, original_, "Unexpected original patch bytes");
        enabled_ = true;
    }

    void disable() {
        set(original_, replacement_, "Unexpected replacement patch bytes");
        enabled_ = false;
    }

private:
    void set(const std::array<uint8_t, Size>& desired,
             const std::array<uint8_t, Size>& expected,
             const char* mismatch_message) {
        const auto current = read_remote<std::array<uint8_t, Size>>(process_, address_);
        if (current == desired) {
            return;
        }
        if (current != expected) {
            throw std::runtime_error(mismatch_message);
        }
        write_protected(process_, address_, desired);
    }

    HANDLE process_;
    uintptr_t address_;
    std::array<uint8_t, Size> original_;
    std::array<uint8_t, Size> replacement_;
    bool enabled_ = false;
};
