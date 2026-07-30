#pragma once

#include <memory>
#include <optional>
#include <string>

namespace ComputerCpp {

class GobiiCredentialStore {
public:
    virtual ~GobiiCredentialStore() = default;
    virtual bool SaveRefreshToken(
        const std::string& deviceId,
        const std::string& token,
        std::string* error = nullptr) = 0;
    virtual std::optional<std::string> LoadRefreshToken(
        const std::string& deviceId,
        std::string* error = nullptr) = 0;
    virtual bool DeleteRefreshToken(
        const std::string& deviceId,
        std::string* error = nullptr) = 0;
    virtual bool Available(std::string* error = nullptr) const = 0;
};

std::unique_ptr<GobiiCredentialStore> CreateGobiiCredentialStore();

} // namespace ComputerCpp
