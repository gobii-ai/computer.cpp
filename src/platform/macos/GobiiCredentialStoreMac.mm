#include "computer_cpp/GobiiCredentialStore.h"

#include <Security/Security.h>

#include <memory>

namespace ComputerCpp {
namespace {

constexpr const char* kService = "org.computercpp.gobii";

void SetError(std::string* error, OSStatus status, const char* action) {
    if (!error) {
        return;
    }
    if (status == errSecAuthFailed) {
        *error = std::string(action) +
            ": macOS Keychain denied access. If ComputerCpp was "
            "rebuilt or updated while it was running, quit and "
            "reopen the app before trying again.";
        return;
    }
    CFStringRef message = SecCopyErrorMessageString(status, nullptr);
    char text[256] = {};
    const bool copied = message && CFStringGetCString(
        message,
        text,
        sizeof(text),
        kCFStringEncodingUTF8);
    *error = std::string(action) + ": " +
        (copied ? text : std::to_string(status));
    if (message) {
        CFRelease(message);
    }
}

class MacGobiiCredentialStore final : public GobiiCredentialStore {
public:
    bool SaveRefreshToken(
        const std::string& deviceId,
        const std::string& token,
        std::string* error
    ) override {
        if (error) error->clear();
        const void* keys[] = {
            kSecClass,
            kSecAttrService,
            kSecAttrAccount,
        };
        const void* values[] = {
            kSecClassGenericPassword,
            CFStringCreateWithCString(
                nullptr, kService, kCFStringEncodingUTF8),
            CFStringCreateWithCString(
                nullptr, deviceId.c_str(), kCFStringEncodingUTF8),
        };
        CFDictionaryRef query = CFDictionaryCreate(
            nullptr, keys, values, 3,
            &kCFTypeDictionaryKeyCallBacks,
            &kCFTypeDictionaryValueCallBacks);
        CFDataRef data = CFDataCreate(
            nullptr,
            reinterpret_cast<const UInt8*>(token.data()),
            static_cast<CFIndex>(token.size()));
        const void* updateKeys[] = {kSecValueData};
        const void* updateValues[] = {data};
        CFDictionaryRef update = CFDictionaryCreate(
            nullptr, updateKeys, updateValues, 1,
            &kCFTypeDictionaryKeyCallBacks,
            &kCFTypeDictionaryValueCallBacks);
        OSStatus status = SecItemUpdate(query, update);
        if (status == errSecItemNotFound) {
            CFMutableDictionaryRef add =
                CFDictionaryCreateMutableCopy(nullptr, 0, query);
            CFDictionarySetValue(add, kSecValueData, data);
            CFDictionarySetValue(
                add,
                kSecAttrAccessible,
                kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly);
            status = SecItemAdd(add, nullptr);
            CFRelease(add);
        }
        CFRelease(update);
        CFRelease(data);
        CFRelease(query);
        CFRelease(values[1]);
        CFRelease(values[2]);
        if (status != errSecSuccess) {
            SetError(error, status, "could not save Gobii credential");
            return false;
        }
        return true;
    }

    std::optional<std::string> LoadRefreshToken(
        const std::string& deviceId,
        std::string* error
    ) override {
        if (error) error->clear();
        CFStringRef service = CFStringCreateWithCString(
            nullptr, kService, kCFStringEncodingUTF8);
        CFStringRef account = CFStringCreateWithCString(
            nullptr, deviceId.c_str(), kCFStringEncodingUTF8);
        const void* keys[] = {
            kSecClass, kSecAttrService, kSecAttrAccount,
            kSecReturnData, kSecMatchLimit,
        };
        const void* values[] = {
            kSecClassGenericPassword, service, account,
            kCFBooleanTrue, kSecMatchLimitOne,
        };
        CFDictionaryRef query = CFDictionaryCreate(
            nullptr, keys, values, 5,
            &kCFTypeDictionaryKeyCallBacks,
            &kCFTypeDictionaryValueCallBacks);
        CFTypeRef result = nullptr;
        const OSStatus status = SecItemCopyMatching(query, &result);
        CFRelease(query);
        CFRelease(service);
        CFRelease(account);
        if (status == errSecItemNotFound) {
            return std::nullopt;
        }
        if (status != errSecSuccess || !result ||
            CFGetTypeID(result) != CFDataGetTypeID()) {
            if (result) CFRelease(result);
            SetError(error, status, "could not load Gobii credential");
            return std::nullopt;
        }
        CFDataRef data = static_cast<CFDataRef>(result);
        std::string token(
            reinterpret_cast<const char*>(CFDataGetBytePtr(data)),
            static_cast<size_t>(CFDataGetLength(data)));
        CFRelease(result);
        return token;
    }

    bool DeleteRefreshToken(
        const std::string& deviceId,
        std::string* error
    ) override {
        if (error) error->clear();
        CFStringRef service = CFStringCreateWithCString(
            nullptr, kService, kCFStringEncodingUTF8);
        CFStringRef account = CFStringCreateWithCString(
            nullptr, deviceId.c_str(), kCFStringEncodingUTF8);
        const void* keys[] = {kSecClass, kSecAttrService, kSecAttrAccount};
        const void* values[] = {
            kSecClassGenericPassword, service, account,
        };
        CFDictionaryRef query = CFDictionaryCreate(
            nullptr, keys, values, 3,
            &kCFTypeDictionaryKeyCallBacks,
            &kCFTypeDictionaryValueCallBacks);
        const OSStatus status = SecItemDelete(query);
        CFRelease(query);
        CFRelease(service);
        CFRelease(account);
        if (status != errSecSuccess && status != errSecItemNotFound) {
            SetError(error, status, "could not delete Gobii credential");
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
    return std::make_unique<MacGobiiCredentialStore>();
}

} // namespace ComputerCpp
