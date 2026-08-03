#include "WindowsAppResolver.h"

#include "computer_cpp/StringUtils.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace ComputerCpp::Platform::WindowsApps {
namespace {

bool EqualsCaseInsensitive(const std::string& left, const std::string& right) {
    return Lowercase(left) == Lowercase(right);
}

std::string ExecutableLookupName(const CatalogEntry& entry) {
    if (entry.executablePath.empty()) {
        return {};
    }
    return NormalizeLookupName(
        std::filesystem::path(entry.executablePath).filename().string());
}

template <typename Predicate>
std::vector<CatalogEntry> Filter(
    const std::vector<CatalogEntry>& entries,
    Predicate predicate) {
    std::vector<CatalogEntry> matches;
    for (const auto& entry : entries) {
        if (predicate(entry)) {
            matches.push_back(entry);
        }
    }
    return matches;
}

CatalogMatch Select(std::vector<CatalogEntry> candidates) {
    CatalogMatch result;
    result.candidates = std::move(candidates);
    result.ambiguous = result.candidates.size() > 1;
    if (result.candidates.size() == 1) {
        result.entry = result.candidates.front();
    }
    return result;
}

} // namespace

std::string NormalizeLookupName(const std::string& value) {
    std::string lower = Lowercase(Trim(value));
    if (lower.size() > 4 && lower.ends_with(".exe")) {
        lower.resize(lower.size() - 4);
    }
    std::string normalized;
    normalized.reserve(lower.size());
    for (unsigned char c : lower) {
        if (std::isalnum(c)) {
            normalized.push_back(static_cast<char>(c));
        }
    }
    return normalized;
}

CatalogMatch MatchCatalog(
    const std::vector<CatalogEntry>& entries,
    const std::string& query) {
    if (query.empty()) {
        return {};
    }

    auto identityMatches = Filter(entries, [&](const CatalogEntry& entry) {
        return (!entry.appUserModelId.empty() &&
                   EqualsCaseInsensitive(query, entry.appUserModelId)) ||
            (!entry.parsingName.empty() &&
                EqualsCaseInsensitive(query, entry.parsingName)) ||
            (!entry.executablePath.empty() &&
                EqualsCaseInsensitive(query, entry.executablePath));
    });
    if (!identityMatches.empty()) {
        return Select(std::move(identityMatches));
    }

    const std::string normalizedQuery = NormalizeLookupName(query);
    if (normalizedQuery.empty()) {
        return {};
    }
    auto exactMatches = Filter(entries, [&](const CatalogEntry& entry) {
        return NormalizeLookupName(entry.displayName) == normalizedQuery ||
            ExecutableLookupName(entry) == normalizedQuery;
    });
    if (!exactMatches.empty()) {
        return Select(std::move(exactMatches));
    }

    auto partialMatches = Filter(entries, [&](const CatalogEntry& entry) {
        const std::string display = NormalizeLookupName(entry.displayName);
        const std::string executable = ExecutableLookupName(entry);
        return (!display.empty() && display.find(normalizedQuery) != std::string::npos) ||
            (!executable.empty() && executable.find(normalizedQuery) != std::string::npos);
    });
    return Select(std::move(partialMatches));
}

} // namespace ComputerCpp::Platform::WindowsApps
