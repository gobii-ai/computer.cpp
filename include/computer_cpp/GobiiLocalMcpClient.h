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
        int port,
        std::string bearerToken,
        std::set<std::string> knownApps,
        std::shared_ptr<GobiiHttpTransport> transport);

    GobiiLocalMcpResult Forward(
        const std::string& app,
        const nlohmann::json& payload,
        std::chrono::system_clock::time_point deadline);

private:
    int port_;
    std::string bearerToken_;
    std::set<std::string> knownApps_;
    std::shared_ptr<GobiiHttpTransport> transport_;
};

} // namespace ComputerCpp
