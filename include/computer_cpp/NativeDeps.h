#pragma once

#include <string>

namespace ComputerCpp::NativeDeps {

struct Versions {
    std::string curl;
};

Versions GetVersions();

}
