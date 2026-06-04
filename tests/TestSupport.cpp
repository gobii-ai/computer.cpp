#include "TestSupport.h"

#include <chrono>
#include <string>

namespace ComputerCpp::Tests {

std::filesystem::path MakeTempHome() {
    auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    std::filesystem::path path = std::filesystem::temp_directory_path() / ("computer.cpp-tests-" + std::to_string(stamp));
    std::filesystem::create_directories(path);
    return path;
}

}
