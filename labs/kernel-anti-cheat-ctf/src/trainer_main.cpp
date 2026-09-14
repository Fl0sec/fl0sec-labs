#include "game_offsets.hpp"
#include "game_trainer.hpp"
#include "remote_process.hpp"
#include "win32.hpp"

#include <shellapi.h>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "user32.lib")

namespace {

std::wstring format_error(DWORD error) {
    wchar_t* message = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error, 0, reinterpret_cast<wchar_t*>(&message), 0, nullptr);
    std::wstring result = length && message ? message : L"Unknown error";
    if (message) {
        LocalFree(message);
    }
    while (!result.empty() && (result.back() == L'\r' || result.back() == L'\n')) {
        result.pop_back();
    }
    return result;
}

std::wstring module_path() {
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
                                            static_cast<DWORD>(buffer.size()));
    if (!length || length == buffer.size()) {
        throw std::runtime_error("GetModuleFileNameW failed");
    }
    return std::wstring(buffer.data(), length);
}

std::wstring parent_directory(const std::wstring& path) {
    const size_t separator = path.find_last_of(L"\\/");
    return separator == std::wstring::npos ? L"." : path.substr(0, separator);
}

std::wstring quote_argument(const std::wstring& argument) {
    std::wstring quoted = L"\"";
    size_t backslashes = 0;
    for (const wchar_t character : argument) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        if (character == L'\"') {
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted.push_back(L'\"');
        } else {
            quoted.append(backslashes, L'\\');
            quoted.push_back(character);
        }
        backslashes = 0;
    }
    quoted.append(backslashes * 2, L'\\');
    quoted.push_back(L'\"');
    return quoted;
}

bool is_elevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        throw std::runtime_error("OpenProcessToken failed");
    }
    UniqueHandle token_handle(token);
    TOKEN_ELEVATION elevation{};
    DWORD returned = 0;
    if (!GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation),
                             &returned)) {
        throw std::runtime_error("GetTokenInformation failed");
    }
    return elevation.TokenIsElevated != 0;
}

std::string random_title() {
    constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    uint64_t state = static_cast<uint64_t>(counter.QuadPart) ^ GetTickCount64() ^
                     (static_cast<uint64_t>(GetCurrentProcessId()) << 32);
    std::string title(16, '\0');
    for (char& character : title) {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        character = alphabet[state % (sizeof(alphabet) - 1)];
    }
    return title;
}

using NtQueryInformationProcessFn = LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG,
                                                  PULONG);

uintptr_t image_base(HANDLE process) {
    struct ProcessBasicInformation {
        PVOID reserved1;
        PVOID peb_base_address;
        PVOID reserved2[2];
        ULONG_PTR process_id;
        PVOID reserved3;
    } information{};

    const auto query =
        ntdll_function<NtQueryInformationProcessFn>("NtQueryInformationProcess");
    if (query(process, 0, &information, sizeof(information), nullptr) < 0) {
        throw std::runtime_error("NtQueryInformationProcess failed");
    }
    const uintptr_t remote_base_address =
        reinterpret_cast<uintptr_t>(information.peb_base_address) + 0x10;
    const uintptr_t base = read_remote<uintptr_t>(process, remote_base_address);
    if (!base) {
        throw std::runtime_error("Could not read the game image base");
    }
    return base;
}

int relaunch_elevated(const std::wstring& self, const std::wstring& game) {
    const std::wstring parameters = quote_argument(game);
    const std::wstring self_directory = parent_directory(self);
    SHELLEXECUTEINFOW execute{};
    execute.cbSize = sizeof(execute);
    execute.fMask = SEE_MASK_NOCLOSEPROCESS;
    execute.lpVerb = L"runas";
    execute.lpFile = self.c_str();
    execute.lpParameters = parameters.c_str();
    execute.lpDirectory = self_directory.c_str();
    execute.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&execute)) {
        const DWORD error = GetLastError();
        std::wcerr << L"Elevation failed: " << format_error(error) << L" (" << error
                   << L")\n";
        return 1;
    }
    UniqueHandle elevated_process(execute.hProcess);
    return 0;
}

bool wait_for_initialization(HANDLE process, uintptr_t base) {
    for (unsigned attempt = 0; attempt < 300; ++attempt) {
        if (WaitForSingleObject(process, 0) == WAIT_OBJECT_0) {
            return false;
        }
        const bool storage_ready = read_remote<uint8_t>(
            process, base + game_offsets::launch::storage_initialized) == 1;
        const bool gameplay_ready = read_remote<uint8_t>(
            process, base + game_offsets::launch::gameplay_ready) == 1;
        if (storage_ready && gameplay_ready) {
            return true;
        }
        Sleep(100);
    }
    return false;
}

void print_controls(DWORD process_id) {
    std::wcout << L"PID: " << process_id << L"\n"
               << L"F1: toggle no-damage\n"
               << L"F2: set ammo to 200 and enable bound bypass\n"
               << L"F3: toggle movement speed between 220 and 440\n"
               << L"F4: activate persistent spread weapon\n"
               << L"F5: activate persistent fast weapon\n"
               << L"F6: activate persistent heavy weapon\n"
               << L"F7: toggle one-hit enemies\n"
               << L"End: restore ammo to 30, restore patches, and exit\n"
               << L"Trainer retains both handles until the game exits." << std::endl;
}

void run_hotkey_loop(HANDLE process, GameTrainer& trainer) {
    bool running = true;
    while (running && WaitForSingleObject(process, 50) == WAIT_TIMEOUT) {
        try {
            if ((GetAsyncKeyState(VK_F1) & 1) != 0) trainer.toggle_no_damage();
            if ((GetAsyncKeyState(VK_F2) & 1) != 0) trainer.set_ammo();
            if ((GetAsyncKeyState(VK_F3) & 1) != 0) trainer.toggle_movement_speed();
            if ((GetAsyncKeyState(VK_F4) & 1) != 0) trainer.activate_weapon(0, L"spread");
            if ((GetAsyncKeyState(VK_F5) & 1) != 0) trainer.activate_weapon(1, L"fast");
            if ((GetAsyncKeyState(VK_F6) & 1) != 0) trainer.activate_weapon(2, L"heavy");
            if ((GetAsyncKeyState(VK_F7) & 1) != 0) trainer.toggle_one_hit_enemies();
            trainer.update_weapon();
            trainer.update_one_hit_enemies();
            if ((GetAsyncKeyState(VK_END) & 1) != 0) {
                trainer.restore_all();
                running = false;
            }
        } catch (const std::exception& error) {
            std::cerr << "Trainer action failed: " << error.what() << '\n';
        }
    }
}

}

int wmain(int argc, wchar_t** argv) {
    SetConsoleTitleA(random_title().c_str());
    try {
        const std::wstring self = module_path();
        const std::wstring game = argc >= 2
            ? argv[1]
            : parent_directory(self) + L"\\" + game_offsets::launch::default_game_name;
        const std::wstring game_directory = parent_directory(game);
        const DWORD attributes = GetFileAttributesW(game.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES ||
            (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
            std::wcerr << L"Game executable not found: " << game << L"\n";
            return 1;
        }
        if (!is_elevated()) {
            std::wcout << L"Trainer is not elevated; requesting elevation.\n";
            return relaunch_elevated(self, game);
        }

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process_info{};
        std::wstring command_line = quote_argument(game);
        if (!CreateProcessW(game.c_str(), command_line.data(), nullptr, nullptr, FALSE,
                            CREATE_SUSPENDED, nullptr, game_directory.c_str(), &startup,
                            &process_info)) {
            const DWORD error = GetLastError();
            std::wcerr << L"CreateProcessW failed: " << format_error(error) << L" ("
                       << error << L")\n";
            return 1;
        }

        UniqueHandle process(process_info.hProcess);
        UniqueHandle primary_thread(process_info.hThread);
        const uintptr_t base = image_base(process.get());
        log_stage(L"Image base resolved (decimal): " +
                  std::to_wstring(static_cast<unsigned long long>(base)));
        write_remote(process.get(), base + game_offsets::launch::detected, uint8_t{1});
        log_stage(L"Pre-resume g_bDetected write completed.");

        if (ResumeThread(primary_thread.get()) == static_cast<DWORD>(-1)) {
            const DWORD error = GetLastError();
            TerminateProcess(process.get(), 0);
            std::wcerr << L"ResumeThread failed: " << format_error(error) << L" ("
                       << error << L")\n";
            return 1;
        }
        log_stage(L"Primary thread resumed; polling initialization.");
        if (!wait_for_initialization(process.get(), base)) {
            throw std::runtime_error("Game initialization did not complete");
        }
        log_stage(L"Storage and gameplay initialization reached ready state.");

        GameTrainer trainer(process.get(), base);
        print_controls(process_info.dwProcessId);
        run_hotkey_loop(process.get(), trainer);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
