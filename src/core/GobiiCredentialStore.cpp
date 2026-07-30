#include "computer_cpp/GobiiCredentialStore.h"

namespace ComputerCpp {

std::unique_ptr<GobiiCredentialStore>
CreatePlatformGobiiCredentialStore();

std::unique_ptr<GobiiCredentialStore> CreateGobiiCredentialStore() {
    return CreatePlatformGobiiCredentialStore();
}

} // namespace ComputerCpp
