#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ComputerCpp {

struct LuaRunOptions {
    std::string session = "default";
    std::string controlSessionToken;
    std::string controlScope = "desktop:local";
    std::string leaseOwner;
    std::string leasePurpose;
    std::filesystem::path executablePath;
    std::filesystem::path scriptPath;
    std::vector<std::string> scriptArgs;
    nlohmann::json vars = nlohmann::json::object();
    int64_t leaseTtlMs = 10 * 60 * 1000;
    int64_t leaseWaitMs = 0;
    int64_t leaseMaxRuntimeMs = 4 * 60 * 60 * 1000LL;
    bool dryRun = false;
    bool agentStdio = false;
    bool jsonOutput = false;
    bool acquireControlSession = false;
};

struct LuaRunResult {
    int exitCode = 1;
    std::string stdoutText;
    std::string stderrText;
};

int RunLuaScript(const LuaRunOptions& options);
LuaRunResult RunLuaScriptCapture(const LuaRunOptions& options);
LuaRunResult RunLuaScriptCapture(const LuaRunOptions& options, bool streamStderr);
std::filesystem::path FindLuaInterpreter(const std::filesystem::path& executablePath = {});

}
