#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace ComputerCpp {

struct BrowserDescriptor {
    std::string id;
    std::string displayName;
    std::string applicationName;
    std::string windowQuery;
    std::string executable;
    bool installed = false;
    bool recommended = false;
};

std::string NormalizeBrowserId(const std::string& value);
BrowserDescriptor DescribeBrowser(const std::string& browserId);
std::vector<BrowserDescriptor> BrowserCatalog();
std::filesystem::path ManagedBrowserDataDir(
    const std::string& browserId,
    const std::string& profile);
void PrepareManagedBrowserDataDir(const std::filesystem::path& path);

} // namespace ComputerCpp
