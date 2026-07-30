#include "computer_cpp/GobiiCredentialStore.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincred.h>

#include <memory>

namespace ComputerCpp {
namespace {

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS,
        value.data(), static_cast<int>(value.size()),
        nullptr, 0);
    if (size <= 0) return {};
    std::wstring output(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS,
        value.data(), static_cast<int>(value.size()),
        output.data(), size);
    return output;
}

std::wstring Target(const std::string& deviceId) {
    return L"ComputerCpp/Gobii/" + Utf8ToWide(deviceId);
}

void SetLastError(std::string* error, const char* action) {
    if (error) {
        *error = std::string(action) + " (Windows error " +
            std::to_string(GetLastError()) + ")";
    }
}

class WindowsGobiiCredentialStore final : public GobiiCredentialStore {
public:
    bool SaveRefreshToken(
        const std::string& deviceId,
        const std::string& token,
        std::string* error
    ) override {
        if (error) error->clear();
        std::wstring target = Target(deviceId);
        CREDENTIALW credential{};
        credential.Type = CRED_TYPE_GENERIC;
        credential.TargetName =
            const_cast<wchar_t*>(target.c_str());
        credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
        credential.CredentialBlobSize =
            static_cast<DWORD>(token.size());
        credential.CredentialBlob =
            reinterpret_cast<LPBYTE>(
                const_cast<char*>(token.data()));
        if (!CredWriteW(&credential, 0)) {
            SetLastError(error, "could not save Gobii credential");
            return false;
        }
        return true;
    }

    std::optional<std::string> LoadRefreshToken(
        const std::string& deviceId,
        std::string* error
    ) override {
        if (error) error->clear();
        std::wstring target = Target(deviceId);
        PCREDENTIALW credential = nullptr;
        if (!CredReadW(
                target.c_str(),
                CRED_TYPE_GENERIC,
                0,
                &credential)) {
            if (GetLastError() == ERROR_NOT_FOUND) {
                return std::nullopt;
            }
            SetLastError(error, "could not load Gobii credential");
            return std::nullopt;
        }
        std::string token(
            reinterpret_cast<const char*>(
                credential->CredentialBlob),
            credential->CredentialBlobSize);
        CredFree(credential);
        return token;
    }

    bool DeleteRefreshToken(
        const std::string& deviceId,
        std::string* error
    ) override {
        if (error) error->clear();
        const std::wstring target = Target(deviceId);
        if (!CredDeleteW(
                target.c_str(),
                CRED_TYPE_GENERIC,
                0) &&
            GetLastError() != ERROR_NOT_FOUND) {
            SetLastError(error, "could not delete Gobii credential");
            return false;
        }
        return true;
    }

    bool Available(std::string* error) const override {
        if (error) error->clear();
        return true;
    }
};

} // namespace

std::unique_ptr<GobiiCredentialStore>
CreatePlatformGobiiCredentialStore() {
    return std::make_unique<WindowsGobiiCredentialStore>();
}

} // namespace ComputerCpp
