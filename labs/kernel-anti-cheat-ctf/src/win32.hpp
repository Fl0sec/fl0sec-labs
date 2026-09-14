#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

using NtSuspendProcessFn = LONG(NTAPI*)(HANDLE);
using NtResumeProcessFn = LONG(NTAPI*)(HANDLE);

class UniqueHandle {
public:
    UniqueHandle() = default;
    explicit UniqueHandle(HANDLE handle) : handle_(handle) {}
    ~UniqueHandle() { reset(); }

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;
    UniqueHandle(UniqueHandle&& other) noexcept
        : handle_(std::exchange(other.handle_, nullptr)) {}
    UniqueHandle& operator=(UniqueHandle&& other) noexcept {
        if (this != &other) {
            reset(std::exchange(other.handle_, nullptr));
        }
        return *this;
    }

    HANDLE get() const { return handle_; }
    explicit operator bool() const { return handle_ != nullptr; }

    void reset(HANDLE handle = nullptr) {
        if (handle_) {
            CloseHandle(handle_);
        }
        handle_ = handle;
    }

private:
    HANDLE handle_ = nullptr;
};

template <typename Function>
Function ntdll_function(const char* name) {
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    const FARPROC address = ntdll ? GetProcAddress(ntdll, name) : nullptr;
    if (!address) {
        throw std::runtime_error(std::string("Missing ntdll export: ") + name);
    }
    return reinterpret_cast<Function>(address);
}

class ScopedProcessSuspend {
public:
    ScopedProcessSuspend(HANDLE process, NtSuspendProcessFn suspend,
                         NtResumeProcessFn resume)
        : process_(process), resume_(resume) {
        if (suspend(process_) < 0) {
            throw std::runtime_error("NtSuspendProcess failed");
        }
    }

    ~ScopedProcessSuspend() {
        if (suspended_) {
            resume_(process_);
        }
    }

    ScopedProcessSuspend(const ScopedProcessSuspend&) = delete;
    ScopedProcessSuspend& operator=(const ScopedProcessSuspend&) = delete;

    void resume() {
        if (suspended_ && resume_(process_) < 0) {
            throw std::runtime_error("NtResumeProcess failed");
        }
        suspended_ = false;
    }

private:
    HANDLE process_;
    NtResumeProcessFn resume_;
    bool suspended_ = true;
};

inline void log_stage(const std::wstring& message) {
    std::wcout << L"[" << GetTickCount64() << L" ms] " << message << std::endl;
}
