#pragma once

#include "computer_cpp/GobiiApiClient.h"

#include <chrono>
#include <memory>
#include <nlohmann/json.hpp>
#include <set>
#include <string>

namespace ComputerCpp {

struct GobiiLocalMcpResult {
    bool ok = false;
    std::string response;
    std::string code;
    std::string error;
};

class GobiiLocalMcpClient {
public:
    GobiiLocalMcpClient(
        std::string bindHost,
        int port,
        std::string bearerToken,
        std::string internalControlToken,
        std::set<std::string> knownApps,
        std::shared_ptr<GobiiHttpTransport> transport);

    GobiiLocalMcpResult Forward(
        const std::string& app,
        const nlohmann::json& payload,
        std::chrono::system_clock::time_point deadline);

private:
    std::string loopbackHost_;
    int port_;
    std::string bearerToken_;
    std::string internalControlToken_;
    std::set<std::string> knownApps_;
    std::shared_ptr<GobiiHttpTransport> transport_;
};

} // namespace ComputerCpp
