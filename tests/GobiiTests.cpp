#include "TestSupport.h"

#include "computer_cpp/AppConfig.h"
#include "computer_cpp/AppPaths.h"
#include "computer_cpp/GobiiArtifactUploader.h"
#include "computer_cpp/GobiiLocalMcpClient.h"
#include "computer_cpp/GobiiPairingClient.h"
#include "computer_cpp/GobiiRelayProtocol.h"
#include "computer_cpp/GobiiTypes.h"
#include "computer_cpp/Sha256.h"
#include "computer_cpp/LuaRunner.h"

#include <cassert>
#include <deque>
#include <fstream>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <vector>

namespace ComputerCpp::Tests {
namespace {

class FakeHttpTransport final : public GobiiHttpTransport {
public:
    GobiiHttpResponse Send(
        const GobiiHttpRequest& request
    ) override {
        requests.push_back(request);
        assert(!responses.empty());
        auto response = std::move(responses.front());
        responses.pop_front();
        return response;
    }

    std::deque<GobiiHttpResponse> responses;
    std::vector<GobiiHttpRequest> requests;
};

void TestConfigAndResources() {
    AppConfig config = DefaultAppConfig();
    assert(config.server.host == "127.0.0.1");
    assert(config.gobii.baseUrl == "https://gobii.ai");
    assert(config.gobii.autoConnect);
    config.gobii.machineId = "machine";
    config.gobii.deviceId = "device";
    config.gobii.assignedAgentName = "Agent";
    config.gobii.requiredVersion = "1.0.0";
    config.gobii.updateRequiredInstalledVersion = "0.21.0";
    const std::string toml = AppConfigToToml(config);
    assert(toml.find("[gobii]") != std::string::npos);
    assert(toml.find("machine_id = \"machine\"") !=
        std::string::npos);
    assert(toml.find("device_id = \"device\"") !=
        std::string::npos);
    assert(toml.find("required_version = \"1.0.0\"") !=
        std::string::npos);
    const auto json = AppConfigToJson(config);
    assert(json["gobii"]["deviceId"] == "device");
    assert(json["gobii"]["machineId"] == "machine");
    assert(!json["gobii"].contains("deviceRefreshToken"));

    std::string error;
    assert(EnsureGobiiDesktopApp(config, &error));
    assert(error.empty());
    assert(config.server.apps.contains("gobii-desktop"));
    const std::string original =
        config.server.apps["gobii-desktop"].path;
    assert(!EnsureGobiiDesktopApp(config, &error));
    assert(config.server.apps["gobii-desktop"].path == original);
    assert(!InstalledResourcePath(
        "apps/gobii-desktop.lua").empty());

    LuaRunOptions lua;
    lua.scriptPath = InstalledResourcePath(
        "apps/gobii-desktop.lua");
    lua.jsonOutput = true;
    lua.dryRun = true;
    lua.vars["__ac_app_mode"] = "schema";
    const LuaRunResult schemaResult =
        RunLuaScriptCapture(lua);
    const auto schema = nlohmann::json::parse(
        schemaResult.stdoutText, nullptr, false);
    assert(!schema.is_discarded());
    assert(schema.value("ok", false));
    const auto& commands = schema["data"]["commands"];
    for (const char* name : {
        "observe", "click", "type_text", "press_key", "scroll",
        "open_application", "focus_application", "wait_stable",
        "computer_status"
    }) {
        assert(commands.contains(name));
    }
}

void TestPairingAndRefresh() {
    auto http = std::make_shared<FakeHttpTransport>();
    http->responses.push_back({
        201,
        nlohmann::json{
            {"pairing_id",
                "01234567-89ab-cdef-0123-456789abcdef"},
            {"device_code", "secret"},
            {"user_code", "ABCD-EFGH"},
            {"verification_uri", "https://gobii.ai/connect"},
            {"verification_uri_complete",
                "https://gobii.ai/connect?claim=x"},
            {"expires_at", "2030-07-30T20:00:00Z"},
            {"interval", 5},
        }.dump(),
        "",
    });
    http->responses.push_back({
        400,
        R"({"error":"authorization_pending"})",
        "",
    });
    http->responses.push_back({
        200,
        nlohmann::json{
            {"device_id", "device"},
            {"refresh_token", "refresh"},
            {"access_token", "access"},
            {"token_type", "Bearer"},
            {"expires_in", 300},
            {"relay_url", "wss://gobii.ai/ws/computers/connect/"},
            {"agent_id", "agent"},
        }.dump(),
        "",
    });
    http->responses.push_back({
        200,
        nlohmann::json{
            {"device_id", "device"},
            {"refresh_token", "rotated-refresh"},
            {"access_token", "rotated-access"},
            {"token_type", "Bearer"},
            {"expires_in", 300},
            {"relay_url", "wss://gobii.ai/ws/computers/connect/"},
        }.dump(),
        "",
    });
    GobiiPairingClient client("https://gobii.ai", http);
    GobiiPairingSession session;
    std::string error;
    GobiiPairingRequest request;
    request.machineId = "machine";
    request.deviceName = "Desktop";
    request.platform = "macos";
    request.architecture = "arm64";
    request.clientVersion = "0.21.0";
    request.apps.push_back({
        "gobii-desktop",
        "Gobii Desktop",
        std::string(64, 'a'),
        "bundled",
    });
    assert(client.CreatePairing(
        request,
        session,
        error));
    assert(session.deviceCode == "secret");
    assert(session.userCode == "ABCD-EFGH");
    assert(client.Poll(session).state ==
        GobiiPairingPollState::Pending);
    auto approved = client.Poll(session);
    if (approved.state != GobiiPairingPollState::Approved) {
        std::cerr << "pairing approval parse error: "
                  << approved.error << "\n";
    }
    assert(approved.state ==
        GobiiPairingPollState::Approved);
    assert(approved.token.deviceId == "device");
    assert(http->requests[0].url ==
        "https://gobii.ai/api/computer/v1/pairings/");
    const auto pairingBody =
        nlohmann::json::parse(http->requests[0].body);
    assert(pairingBody["machine_id"] == "machine");
    assert(pairingBody["protocol_version"] == 1);
    assert(pairingBody["apps"][0]["key"] ==
        "gobii-desktop");
    assert(http->requests[1].url ==
        "https://gobii.ai/api/computer/v1/pairings/"
        "01234567-89ab-cdef-0123-456789abcdef/exchange/");
    GobiiTokenResponse refreshed;
    assert(client.Refresh(
        "refresh", "0.21.0", refreshed, error));
    assert(refreshed.deviceRefreshToken ==
        "rotated-refresh");
    assert(http->requests[3].url ==
        "https://gobii.ai/api/computer/v1/tokens/refresh/");
    const auto refreshBody =
        nlohmann::json::parse(http->requests[3].body);
    assert(refreshBody["refresh_token"] == "refresh");
    assert(refreshBody["protocol_version"] == 1);
}

void TestPairingHttpError() {
    auto http = std::make_shared<FakeHttpTransport>();
    http->responses.push_back({
        404,
        "<!doctype html><title>Not Found</title>",
        "",
    });
    GobiiPairingClient client("http://127.0.0.1:8001", http);
    GobiiPairingSession session;
    std::string error;
    GobiiPairingRequest request;
    request.machineId = "machine";
    request.deviceName = "Desktop";
    request.platform = "macos";
    request.architecture = "arm64";
    request.clientVersion = "0.21.0";
    assert(!client.CreatePairing(
        request,
        session,
        error));
    assert(error == "Gobii request returned HTTP 404");
}

void TestRelayProtocolAndLedger() {
    const std::string request = nlohmann::json{
        {"type", "mcp.request"},
        {"request_id", "request-1"},
        {"app", "gobii-desktop"},
        {"deadline_ms", 30000},
        {"payload", {
            {"jsonrpc", "2.0"},
            {"id", 1},
            {"method", "tools/list"},
            {"params", nlohmann::json::object()},
        }},
    }.dump();
    GobiiRelayRequest parsed;
    std::string code;
    std::string error;
    assert(ParseGobiiRelayRequest(
        request, parsed, code, error));
    assert(parsed.requestId == "request-1");

    GobiiRequestLedger ledger(2, std::chrono::minutes(15));
    assert(ledger.Start("one") ==
        GobiiRequestLedger::StartResult::Started);
    assert(ledger.Start("one") ==
        GobiiRequestLedger::StartResult::RunningDuplicate);
    ledger.Complete("one", "cached", false);
    std::string cached;
    assert(ledger.Start("one", &cached) ==
        GobiiRequestLedger::StartResult::CompletedDuplicate);
    assert(cached == "cached");
}

void TestLoopbackMcpForwarding() {
    auto http = std::make_shared<FakeHttpTransport>();
    http->responses.push_back({
        200,
        R"({"jsonrpc":"2.0","id":1,"result":{}})",
        "",
    });
    GobiiLocalMcpClient client(
        8787,
        "local-token",
        {"gobii-desktop"},
        http);
    auto result = client.Forward(
        "gobii-desktop",
        {
            {"jsonrpc", "2.0"},
            {"id", 1},
            {"method", "tools/list"},
            {"params", nlohmann::json::object()},
        },
        std::chrono::system_clock::now() +
            std::chrono::seconds(10));
    assert(result.ok);
    assert(http->requests.size() == 1);
    assert(http->requests[0].url ==
        "http://127.0.0.1:8787/apps/gobii-desktop/mcp");
    assert(http->requests[0].headers.at("Authorization") ==
        "Bearer local-token");
    assert(http->requests[0].headers.at(
        "MCP-Protocol-Version") == "2025-11-25");

    auto unknown = client.Forward(
        "https://example.com",
        nlohmann::json::object(),
        std::chrono::system_clock::now() +
            std::chrono::seconds(10));
    assert(!unknown.ok);
    assert(unknown.code == "unknown_app");
}

void TestArtifactGateAndStates() {
    auto uploader = CreateDisabledGobiiArtifactUploader();
    assert(!uploader->Available());
    nlohmann::json response = {
        {"result", {
            {"content", nlohmann::json::array({
                {
                    {"type", "image"},
                    {"mimeType", "image/png"},
                    {"data", "aGVsbG8="},
                },
            })},
        }},
    };
    std::string error;
#if defined(COMPUTER_CPP_GOBII_DEV_INLINE_IMAGES)
    assert(PrepareGobiiMcpImages(
        response, "request", *uploader, error));
#else
    assert(!PrepareGobiiMcpImages(
        response, "request", *uploader, error));
    assert(error == "artifact_upload_unavailable");
#endif
    assert(std::string(GobiiConnectionStateName(
        GobiiConnectionState::Connected)) == "connected");
    assert(IsGobiiLoopbackUrl(
        "http://127.0.0.1:8001/api", "http"));
    assert(IsGobiiLoopbackUrl(
        "ws://[::1]:8001/relay", "ws"));
    assert(!IsGobiiLoopbackUrl(
        "http://127.0.0.1.example.com:8001", "http"));
    assert(!IsGobiiLoopbackUrl(
        "http://user@127.0.0.1:8001", "http"));
    assert(Sha256Hex("abc") ==
        "ba7816bf8f01cfea414140de5dae2223"
        "b00361a396177a9cb410ff61f20015ad");
}

} // namespace

void RunGobiiTests() {
    TestConfigAndResources();
    TestPairingAndRefresh();
    TestPairingHttpError();
    TestRelayProtocolAndLedger();
    TestLoopbackMcpForwarding();
    TestArtifactGateAndStates();
}

} // namespace ComputerCpp::Tests
