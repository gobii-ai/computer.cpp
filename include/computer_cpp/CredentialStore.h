#pragma once

#include <cstddef>
#include <string>

namespace ComputerCpp {

enum class CredentialStoreStatus {
    Success,
    NotFound,
    Unavailable,
    AccessDenied,
    Error,
};

struct CredentialStoreResult {
    CredentialStoreStatus status = CredentialStoreStatus::Error;
    std::string value;
    std::string error;

    bool ok() const {
        return status == CredentialStoreStatus::Success;
    }
};

class CredentialStore {
public:
    virtual ~CredentialStore() = default;

    virtual CredentialStoreResult Read(const std::string& key) = 0;
    virtual CredentialStoreResult Write(const std::string& key, const std::string& value) = 0;
    virtual CredentialStoreResult Remove(const std::string& key) = 0;
};

CredentialStore& SystemCredentialStore();
std::string ServerAuthCredentialKey();
bool FillSecureRandom(void* data, size_t size, std::string* error = nullptr);

} // namespace ComputerCpp
