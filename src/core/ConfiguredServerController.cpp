#include "computer_cpp/ConfiguredServerController.h"

#include <algorithm>
#include <cctype>

namespace ComputerCpp {

bool IsConfiguredServerSchemaDigest(std::string_view value) {
    return value.size() == 64 &&
        std::all_of(
            value.begin(),
            value.end(),
            [](unsigned char ch) { return std::isxdigit(ch) != 0; });
}

bool ConfiguredServerCatalogReady(const ConfiguredServerInfo& info) {
    if (!info.running || info.apps.empty()) {
        return false;
    }
    for (const auto& [_, catalogValue] : info.apps) {
        const size_t separator = catalogValue.find('\n');
        if (separator == std::string::npos || separator == 0 ||
            !IsConfiguredServerSchemaDigest(
                std::string_view(catalogValue).substr(separator + 1))) {
            return false;
        }
    }
    return true;
}

} // namespace ComputerCpp
