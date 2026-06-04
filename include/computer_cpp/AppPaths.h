#pragma once

#include <filesystem>
#include <string>

namespace ComputerCpp {

std::filesystem::path AppDataDir();
std::filesystem::path ConfigDir();
std::filesystem::path ConfigPath();
std::filesystem::path SessionDir(const std::string& session);
std::filesystem::path RefStorePath(const std::string& session);
std::filesystem::path DefaultArtifactDir();
std::filesystem::path TimelineDir(const std::string& session);
std::filesystem::path TimelineDbPath(const std::string& session);
void EnsureDirectory(const std::filesystem::path& path);

}
