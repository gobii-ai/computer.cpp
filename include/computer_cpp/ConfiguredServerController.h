#pragma once

#include <map>
#include <string>
#include <string_view>

namespace ComputerCpp {

struct ConfiguredServerInfo {
    bool running = false;
    int port = 0;
    std::string bearerToken;
    std::map<std::string, std::string> apps;
    std::string error;
};

bool IsConfiguredServerSchemaDigest(std::string_view value);
bool ConfiguredServerCatalogReady(const ConfiguredServerInfo& info);

class ConfiguredServerController {
public:
    virtual ~ConfiguredServerController() = default;
    virtual bool EnsureRunning(std::string& error) = 0;
    virtual ConfiguredServerInfo Status() const = 0;
    virtual void Stop() = 0;
};

} // namespace ComputerCpp
