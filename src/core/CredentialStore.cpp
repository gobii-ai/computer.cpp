#include "computer_cpp/CredentialStore.h"

#include "computer_cpp/AppPaths.h"

#include <cerrno>
#include <filesystem>
#include <fstream>
#include <string>

#if defined(__APPLE__)
#include <CoreFoundation/CFData.h>
#include <CoreFoundation/CFString.h>
#include <Security/SecItem.h>
#include <Security/SecRandom.h>
#elif defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <wincred.h>
#else
#include <gio/gio.h>
#include <libsecret/secret.h>
#include <sys/random.h>
#endif

namespace fs = std::filesystem;

namespace ComputerCpp {
namespace {

constexpr const char* kCredentialService = "org.computercpp.app";

CredentialStoreResult Success(std::string value = {}) {
    return {CredentialStoreStatus::Success, std::move(value), {}};
}

CredentialStoreResult Failure(CredentialStoreStatus status, std::string error) {
    return {status, {}, std::move(error)};
}

#if defined(__APPLE__)

CFStringRef Utf8String(const std::string& value) {
    return CFStringCreateWithBytes(
        kCFAllocatorDefault,
        reinterpret_cast<const UInt8*>(value.data()),
        static_cast<CFIndex>(value.size()),
        kCFStringEncodingUTF8,
        false);
}

CredentialStoreResult MacFailure(OSStatus status, const std::string& operation) {
    CredentialStoreStatus mapped = CredentialStoreStatus::Error;
    if (status == errSecItemNotFound) {
        mapped = CredentialStoreStatus::NotFound;
    } else if (status == errSecNotAvailable) {
        mapped = CredentialStoreStatus::Unavailable;
    } else if (status == errSecAuthFailed ||
               status == errSecInteractionNotAllowed ||
               status == errSecUserCanceled) {
        mapped = CredentialStoreStatus::AccessDenied;
    }
    return Failure(mapped, operation + " failed with Keychain status " + std::to_string(status));
}

CFMutableDictionaryRef MacQuery(const std::string& key) {
    CFMutableDictionaryRef query = CFDictionaryCreateMutable(
        kCFAllocatorDefault,
        0,
        &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);
    CFStringRef service = Utf8String(kCredentialService);
    CFStringRef account = Utf8String(key);
    if (query == nullptr || service == nullptr || account == nullptr) {
        if (query != nullptr) {
            CFRelease(query);
        }
        if (service != nullptr) {
            CFRelease(service);
        }
        if (account != nullptr) {
            CFRelease(account);
        }
        return nullptr;
    }
    CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue(query, kSecAttrService, service);
    CFDictionarySetValue(query, kSecAttrAccount, account);
    CFRelease(service);
    CFRelease(account);
    return query;
}

class NativeCredentialStore final : public CredentialStore {
public:
    CredentialStoreResult Read(const std::string& key) override {
        CFMutableDictionaryRef query = MacQuery(key);
        if (query == nullptr) {
            return Failure(CredentialStoreStatus::Error, "could not create Keychain lookup");
        }
        CFDictionarySetValue(query, kSecReturnData, kCFBooleanTrue);
        CFDictionarySetValue(query, kSecMatchLimit, kSecMatchLimitOne);
        CFTypeRef result = nullptr;
        OSStatus status = SecItemCopyMatching(query, &result);
        CFRelease(query);
        if (status != errSecSuccess) {
            if (result != nullptr) {
                CFRelease(result);
            }
            return MacFailure(status, "Keychain read");
        }
        if (result == nullptr || CFGetTypeID(result) != CFDataGetTypeID()) {
            if (result != nullptr) {
                CFRelease(result);
            }
            return Failure(CredentialStoreStatus::Error, "Keychain returned invalid credential data");
        }
        auto data = static_cast<CFDataRef>(result);
        std::string value(
            reinterpret_cast<const char*>(CFDataGetBytePtr(data)),
            static_cast<size_t>(CFDataGetLength(data)));
        CFRelease(result);
        return Success(std::move(value));
    }

    CredentialStoreResult Write(const std::string& key, const std::string& value) override {
        CFMutableDictionaryRef query = MacQuery(key);
        if (query == nullptr) {
            return Failure(CredentialStoreStatus::Error, "could not create Keychain update");
        }
        CFDataRef data = CFDataCreate(
            kCFAllocatorDefault,
            reinterpret_cast<const UInt8*>(value.data()),
            static_cast<CFIndex>(value.size()));
        if (data == nullptr) {
            CFRelease(query);
            return Failure(CredentialStoreStatus::Error, "could not encode Keychain credential");
        }
        const void* updateKeys[] = {kSecValueData};
        const void* updateValues[] = {data};
        CFDictionaryRef update = CFDictionaryCreate(
            kCFAllocatorDefault,
            updateKeys,
            updateValues,
            1,
            &kCFTypeDictionaryKeyCallBacks,
            &kCFTypeDictionaryValueCallBacks);
        OSStatus status = SecItemUpdate(query, update);
        CFRelease(update);
        if (status == errSecItemNotFound) {
            CFStringRef label = Utf8String("ComputerCpp server bearer token");
            CFDictionarySetValue(query, kSecValueData, data);
            if (label != nullptr) {
                CFDictionarySetValue(query, kSecAttrLabel, label);
                CFRelease(label);
            }
            status = SecItemAdd(query, nullptr);
        }
        CFRelease(data);
        CFRelease(query);
        return status == errSecSuccess ? Success() : MacFailure(status, "Keychain write");
    }

    CredentialStoreResult Remove(const std::string& key) override {
        CFMutableDictionaryRef query = MacQuery(key);
        if (query == nullptr) {
            return Failure(CredentialStoreStatus::Error, "could not create Keychain delete");
        }
        OSStatus status = SecItemDelete(query);
        CFRelease(query);
        if (status == errSecItemNotFound) {
            return Failure(CredentialStoreStatus::NotFound, "Keychain credential was not found");
        }
        return status == errSecSuccess ? Success() : MacFailure(status, "Keychain delete");
    }
};

#elif defined(_WIN32)

std::wstring Wide(const std::string& value) {
    if (value.empty()) {
        return {};
    }
    int size = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0);
    if (size <= 0) {
        return {};
    }
    std::wstring wide(static_cast<size_t>(size), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            value.data(),
            static_cast<int>(value.size()),
            wide.data(),
            size) != size) {
        return {};
    }
    return wide;
}

CredentialStoreResult WindowsFailure(DWORD code, const std::string& operation) {
    CredentialStoreStatus status = CredentialStoreStatus::Error;
    if (code == ERROR_NOT_FOUND) {
        status = CredentialStoreStatus::NotFound;
    } else if (code == ERROR_NO_SUCH_LOGON_SESSION || code == ERROR_NOT_CONNECTED) {
        status = CredentialStoreStatus::Unavailable;
    } else if (code == ERROR_ACCESS_DENIED || code == ERROR_CANCELLED) {
        status = CredentialStoreStatus::AccessDenied;
    }
    return Failure(status, operation + " failed with Windows error " + std::to_string(code));
}

class NativeCredentialStore final : public CredentialStore {
public:
    CredentialStoreResult Read(const std::string& key) override {
        std::wstring target = Wide(std::string(kCredentialService) + "/" + key);
        if (target.empty()) {
            return Failure(CredentialStoreStatus::Error, "could not encode credential target");
        }
        PCREDENTIALW credential = nullptr;
        if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &credential)) {
            return WindowsFailure(GetLastError(), "Credential Manager read");
        }
        std::string value(
            reinterpret_cast<const char*>(credential->CredentialBlob),
            static_cast<size_t>(credential->CredentialBlobSize));
        CredFree(credential);
        return Success(std::move(value));
    }

    CredentialStoreResult Write(const std::string& key, const std::string& value) override {
        std::wstring target = Wide(std::string(kCredentialService) + "/" + key);
        std::wstring username = Wide("ComputerCpp");
        if (target.empty() || username.empty()) {
            return Failure(CredentialStoreStatus::Error, "could not encode credential target");
        }
        CREDENTIALW credential {};
        credential.Type = CRED_TYPE_GENERIC;
        credential.TargetName = target.data();
        credential.CredentialBlobSize = static_cast<DWORD>(value.size());
        credential.CredentialBlob = reinterpret_cast<LPBYTE>(
            const_cast<char*>(value.data()));
        credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
        credential.UserName = username.data();
        if (!CredWriteW(&credential, 0)) {
            return WindowsFailure(GetLastError(), "Credential Manager write");
        }
        return Success();
    }

    CredentialStoreResult Remove(const std::string& key) override {
        std::wstring target = Wide(std::string(kCredentialService) + "/" + key);
        if (target.empty()) {
            return Failure(CredentialStoreStatus::Error, "could not encode credential target");
        }
        if (!CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0)) {
            return WindowsFailure(GetLastError(), "Credential Manager delete");
        }
        return Success();
    }
};

#else

const SecretSchema kServerTokenSchema = [] {
    SecretSchema schema {};
    schema.name = "org.computercpp.ServerAuthToken";
    schema.flags = SECRET_SCHEMA_NONE;
    schema.attributes[0].name = "account";
    schema.attributes[0].type = SECRET_SCHEMA_ATTRIBUTE_STRING;
    return schema;
}();

CredentialStoreStatus LinuxErrorStatus(const GError* error) {
    if (error == nullptr) {
        return CredentialStoreStatus::Error;
    }
    if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED) ||
        g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_ACCESS_DENIED) ||
        g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_AUTH_FAILED)) {
        return CredentialStoreStatus::AccessDenied;
    }
    if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_NOT_CONNECTED) ||
        g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CONNECTION_CLOSED) ||
        g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_SERVICE_UNKNOWN) ||
        g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_DISCONNECTED) ||
        g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_NO_REPLY)) {
        return CredentialStoreStatus::Unavailable;
    }
    return CredentialStoreStatus::Error;
}

CredentialStoreResult LinuxFailure(GError* error, const std::string& operation) {
    CredentialStoreStatus status = LinuxErrorStatus(error);
    std::string message = operation + " failed";
    if (error != nullptr && error->message != nullptr) {
        message += ": ";
        message += error->message;
    }
    if (error != nullptr) {
        g_error_free(error);
    }
    return Failure(status, std::move(message));
}

class NativeCredentialStore final : public CredentialStore {
public:
    CredentialStoreResult Read(const std::string& key) override {
        GError* error = nullptr;
        gchar* password = secret_password_lookup_sync(
            &kServerTokenSchema,
            nullptr,
            &error,
            "account",
            key.c_str(),
            nullptr);
        if (password == nullptr) {
            if (error != nullptr) {
                return LinuxFailure(error, "Secret Service read");
            }
            return Failure(CredentialStoreStatus::NotFound, "Secret Service credential was not found");
        }
        std::string value(password);
        secret_password_free(password);
        return Success(std::move(value));
    }

    CredentialStoreResult Write(const std::string& key, const std::string& value) override {
        GError* error = nullptr;
        gboolean stored = secret_password_store_sync(
            &kServerTokenSchema,
            SECRET_COLLECTION_DEFAULT,
            "ComputerCpp server bearer token",
            value.c_str(),
            nullptr,
            &error,
            "account",
            key.c_str(),
            nullptr);
        if (!stored) {
            return LinuxFailure(error, "Secret Service write");
        }
        return Success();
    }

    CredentialStoreResult Remove(const std::string& key) override {
        GError* error = nullptr;
        gboolean removed = secret_password_clear_sync(
            &kServerTokenSchema,
            nullptr,
            &error,
            "account",
            key.c_str(),
            nullptr);
        if (error != nullptr) {
            return LinuxFailure(error, "Secret Service delete");
        }
        if (!removed) {
            return Failure(CredentialStoreStatus::NotFound, "Secret Service credential was not found");
        }
        return Success();
    }
};

#endif

} // namespace

CredentialStore& SystemCredentialStore() {
    static NativeCredentialStore store;
    return store;
}

std::string ServerAuthCredentialKey() {
    std::error_code ec;
    fs::path path = fs::weakly_canonical(ConfigPath(), ec);
    if (ec) {
        ec.clear();
        path = fs::absolute(ConfigPath(), ec);
    }
    if (ec) {
        path = ConfigPath();
    }
    return "server-auth-token:" + path.lexically_normal().generic_string();
}

bool FillSecureRandom(void* data, size_t size, std::string* error) {
    if (data == nullptr && size != 0) {
        if (error) {
            *error = "secure random destination is null";
        }
        return false;
    }
#if defined(__APPLE__)
    if (SecRandomCopyBytes(kSecRandomDefault, size, static_cast<uint8_t*>(data)) == errSecSuccess) {
        return true;
    }
    if (error) {
        *error = "SecRandomCopyBytes failed";
    }
    return false;
#elif defined(_WIN32)
    NTSTATUS status = BCryptGenRandom(
        nullptr,
        static_cast<PUCHAR>(data),
        static_cast<ULONG>(size),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (status >= 0) {
        return true;
    }
    if (error) {
        *error = "BCryptGenRandom failed with status " + std::to_string(status);
    }
    return false;
#else
    auto* bytes = static_cast<unsigned char*>(data);
    size_t offset = 0;
    while (offset < size) {
        ssize_t count = ::getrandom(bytes + offset, size - offset, 0);
        if (count > 0) {
            offset += static_cast<size_t>(count);
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        break;
    }
    if (offset == size) {
        return true;
    }
    std::ifstream random("/dev/urandom", std::ios::binary);
    if (random.read(
            reinterpret_cast<char*>(bytes + offset),
            static_cast<std::streamsize>(size - offset))) {
        return true;
    }
    if (error) {
        *error = "operating-system secure random source is unavailable";
    }
    return false;
#endif
}

} // namespace ComputerCpp
