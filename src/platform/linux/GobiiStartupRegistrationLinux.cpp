#include "computer_cpp/GobiiStartupRegistration.h"

namespace ComputerCpp {
namespace {

bool Unsupported(std::string* error) {
    if (error) {
        *error = "Gobii launch at login is unsupported on Linux";
    }
    return false;
}

} // namespace

bool GobiiStartupRegistration::IsSupported(std::string* error) {
    return Unsupported(error);
}

bool GobiiStartupRegistration::IsEnabled(std::string* error) {
    return Unsupported(error);
}

bool GobiiStartupRegistration::SetEnabled(
    bool,
    std::string* error
) {
    return Unsupported(error);
}

} // namespace ComputerCpp
