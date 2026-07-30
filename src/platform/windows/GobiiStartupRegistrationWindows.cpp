#include "computer_cpp/GobiiStartupRegistration.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <filesystem>
#include <string>

namespace ComputerCpp {
namespace {

constexpr const wchar_t* kRunKey =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr const wchar_t* kValueName = L"ComputerCpp";

bool OpenRunKey(REGSAM access, HKEY& key, std::string* error) {
    const LONG result = RegCreateKeyExW(
        HKEY_CURRENT_USER,
        kRunKey,
        0,
        nullptr,
        0,
        access,
        nullptr,
        &key,
        nullptr);
    if (result != ERROR_SUCCESS) {
        if (error) {
            *error = "could not open the per-user startup registry key";
        }
        return false;
    }
    return true;
}

std::filesystem::path ExecutablePath() {
    std::wstring value(32768, L'\0');
    const DWORD size = GetModuleFileNameW(
        nullptr,
        value.data(),
        static_cast<DWORD>(value.size()));
    if (size == 0 || size >= value.size()) {
        return {};
    }
    value.resize(size);
    return std::filesystem::path(value);
}

} // namespace

bool GobiiStartupRegistration::IsSupported(std::string* error) {
    if (error) error->clear();
    return true;
}

bool GobiiStartupRegistration::IsEnabled(std::string* error) {
    if (error) error->clear();
    HKEY key = nullptr;
    if (!OpenRunKey(KEY_QUERY_VALUE, key, error)) {
        return false;
    }
    const LONG result = RegQueryValueExW(
        key, kValueName, nullptr, nullptr, nullptr, nullptr);
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

bool GobiiStartupRegistration::SetEnabled(
    bool enabled,
    std::string* error
) {
    if (error) error->clear();
    HKEY key = nullptr;
    if (!OpenRunKey(KEY_SET_VALUE, key, error)) {
        return false;
    }
    LONG result = ERROR_SUCCESS;
    if (enabled) {
        const std::filesystem::path path = ExecutablePath();
        if (path.empty() ||
            path.filename() != L"ComputerCpp.exe") {
            RegCloseKey(key);
            if (error) {
                *error =
                    "launch at login requires an installed ComputerCpp.exe";
            }
            return false;
        }
        const std::wstring command =
            L"\"" + path.wstring() + L"\"";
        result = RegSetValueExW(
            key,
            kValueName,
            0,
            REG_SZ,
            reinterpret_cast<const BYTE*>(command.c_str()),
            static_cast<DWORD>(
                (command.size() + 1) * sizeof(wchar_t)));
    } else {
        result = RegDeleteValueW(key, kValueName);
        if (result == ERROR_FILE_NOT_FOUND) {
            result = ERROR_SUCCESS;
        }
    }
    RegCloseKey(key);
    if (result != ERROR_SUCCESS) {
        if (error) {
            *error =
                "could not update the per-user startup registration";
        }
        return false;
    }
    return true;
}

} // namespace ComputerCpp
