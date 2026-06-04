#pragma once

#include "computer_cpp/Platform.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ComputerCpp {

void SaveRefs(const std::filesystem::path& path, const std::vector<Platform::RefRecord>& refs);
std::vector<Platform::RefRecord> LoadRefs(const std::filesystem::path& path);
std::optional<Platform::RefRecord> FindRef(const std::vector<Platform::RefRecord>& refs, const std::string& ref);

}
