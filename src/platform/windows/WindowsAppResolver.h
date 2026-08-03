#pragma once

#include <optional>
#include <string>
#include <vector>

namespace ComputerCpp::Platform::WindowsApps {

struct CatalogEntry {
    std::string displayName;
    std::string appUserModelId;
    std::string executablePath;
    std::string parsingName;
};

struct CatalogMatch {
    std::optional<CatalogEntry> entry;
    std::vector<CatalogEntry> candidates;
    bool ambiguous = false;
};

std::string NormalizeLookupName(const std::string& value);
CatalogMatch MatchCatalog(
    const std::vector<CatalogEntry>& entries,
    const std::string& query);

} // namespace ComputerCpp::Platform::WindowsApps
