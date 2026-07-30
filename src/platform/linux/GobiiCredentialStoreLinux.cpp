#include "computer_cpp/GobiiCredentialStore.h"

#include <memory>

namespace ComputerCpp {
namespace {

class UnsupportedGobiiCredentialStore final :
    public GobiiCredentialStore {
public:
    bool SaveRefreshToken(
        const std::string&, const std::string&, std::string* error
    ) override {
        return Fail(error);
    }
    std::optional<std::string> LoadRefreshToken(
        const std::string&, std::string* error
    ) override {
        Fail(error);
        return std::nullopt;
    }
    bool DeleteRefreshToken(
        const std::string&, std::string* error
    ) override {
        return Fail(error);
    }
    bool Available(std::string* error) const override {
        return Fail(error);
    }

private:
    static bool Fail(std::string* error) {
        if (error) {
            *error =
                "Gobii secure credentials are unsupported on Linux";
        }
        return false;
    }
};

} // namespace

std::unique_ptr<GobiiCredentialStore>
CreatePlatformGobiiCredentialStore() {
    return std::make_unique<UnsupportedGobiiCredentialStore>();
}

} // namespace ComputerCpp
