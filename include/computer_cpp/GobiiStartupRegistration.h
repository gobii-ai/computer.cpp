#pragma once

#include <string>

namespace ComputerCpp {

class GobiiStartupRegistration {
public:
    static bool IsSupported(std::string* error = nullptr);
    static bool IsEnabled(std::string* error = nullptr);
    static bool SetEnabled(
        bool enabled,
        std::string* error = nullptr);
};

} // namespace ComputerCpp
