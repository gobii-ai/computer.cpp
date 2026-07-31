#include "TestSupport.h"

#include "computer_cpp/AppConfig.h"
#include "computer_cpp/AppPaths.h"
#include "computer_cpp/GobiiArtifactUploader.h"
#include "computer_cpp/GobiiConnectionController.h"
#include "computer_cpp/GobiiCredentialStore.h"
#include "computer_cpp/GobiiLocalMcpClient.h"
#include "computer_cpp/GobiiPairingClient.h"
#include "computer_cpp/GobiiRelayProtocol.h"
#include "computer_cpp/GobiiRelayClient.h"
#include "computer_cpp/GobiiStatusPresentation.h"
#include "computer_cpp/GobiiTypes.h"
#include "computer_cpp/Sha256.h"
#include "computer_cpp/LuaRunner.h"

#include <cassert>
#include <deque>
#include <fstream>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <thread>
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

class FakeArtifactUploader final : public GobiiArtifactUploader {
public:
    void Configure(
        const std::string&,
        const std::string&) override {}
    void ClearAuthentication() override {}
    bool Available() const override { return true; }
    bool Upload(
        const std::string&,
        std::span<const std::uint8_t> bytes,
        const std::string& mimeType,
        GobiiArtifactReference& reference,
        std::string& error) override {
        error.clear();
        uploadedBytes = bytes.size();
        reference.id =
            "01234567-89ab-cdef-0123-456789abcdef";
        reference.mimeType = mimeType;
        return true;
    }

    size_t uploadedBytes = 0;
};

class FakeGobiiCredentialStore final : public GobiiCredentialStore {
public:
    bool SaveRefreshToken(
        const std::string&,
        const std::string& token,
        std::string* error
    ) override {
        if (error) error->clear();
        value = token;
        return true;
    }

    std::optional<std::string> LoadRefreshToken(
        const std::string&,
        std::string* error
    ) override {
        if (error) error->clear();
        return value.empty()
            ? std::nullopt
            : std::optional<std::string>(value);
    }

    bool DeleteRefreshToken(
        const std::string&,
        std::string* error
    ) override {
        if (error) error->clear();
        value.clear();
        return true;
    }

    bool Available(std::string* error) const override {
        if (error) error->clear();
        return true;
    }

    std::string value;
};

class FakeConfiguredServerController final
    : public ConfiguredServerController {
public:
    bool EnsureRunning(std::string& error) override {
        error.clear();
        return true;
    }

    ConfiguredServerInfo Status() const override {
        return {
            true,
            "127.0.0.1",
            8787,
            "local-token",
            "internal-token",
            {{"gobii-desktop", "Gobii Desktop\n" +
                std::string(64, 'a')}},
            "",
        };
    }

    void Stop() override {}
};

void TestConfigAndResources() {
    AppConfig config = DefaultAppConfig();
    assert(config.server.host == "127.0.0.1");
    ConfiguredServerInfo catalog;
    assert(!ConfiguredServerCatalogReady(catalog));
    catalog.running = true;
    catalog.apps["gobii-desktop"] = "Gobii Desktop\n";
    assert(!ConfiguredServerCatalogReady(catalog));
    catalog.apps["gobii-desktop"] =
        "Gobii Desktop\n" + std::string(64, 'a');
    assert(ConfiguredServerCatalogReady(catalog));
    catalog.apps["gobii-desktop"].back() = 'z';
    assert(!ConfiguredServerCatalogReady(catalog));
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

    if (FindLuaInterpreter().empty()) {
        std::cout
            << "[skip] Gobii desktop Lua schema: "
               "Lua interpreter not available\n";
        return;
    }

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
        assert(!commands[name].contains("output"));
    }
}

void TestPairingAndRefresh() {
    const auto utc = ParseGobiiTimestamp("2030-07-30T20:00:00Z");
    const auto offset = ParseGobiiTimestamp(
        "2030-07-30T16:00:00-04:00");
    const auto fractionalOffset = ParseGobiiTimestamp(
        "2030-07-30T22:30:00.123+02:30");
    assert(utc && offset && fractionalOffset);
    assert(*utc == *offset);
    assert(*utc == *fractionalOffset);
    assert(!ParseGobiiTimestamp("2030-07-30T20:00:00+24:00"));

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

void TestPairingRejectsIncompleteCatalog() {
    auto http = std::make_shared<FakeHttpTransport>();
    GobiiPairingClient client("http://127.0.0.1:8001", http);
    GobiiPairingSession session;
    std::string error;
    GobiiPairingRequest request;
    request.machineId = "machine";
    request.deviceName = "Desktop";
    request.platform = "macos";
    request.architecture = "arm64";
    request.clientVersion = "0.24.0";
    request.apps.push_back({
        "gobii-desktop",
        "Gobii Desktop",
        "",
        "bundled",
    });
    assert(!client.CreatePairing(request, session, error));
    assert(error.find("schema digest") != std::string::npos);
    assert(http->requests.empty());
}

void TestRefreshFailureClassification() {
    auto http = std::make_shared<FakeHttpTransport>();
    GobiiPairingClient client("http://127.0.0.1:8001", http);
    GobiiTokenResponse token;
    std::string error;
    GobiiRefreshFailure failure = GobiiRefreshFailure::None;

    http->responses.push_back({302, "", ""});
    assert(!client.Refresh("refresh", "0.24.0", token, error, &failure));
    assert(failure == GobiiRefreshFailure::Transient);
    assert(error == "Gobii request returned HTTP 302");

    http->responses.push_back({
        401,
        R"({"error":"invalid_grant","error_description":"invalid"})",
        "",
    });
    assert(!client.Refresh("refresh", "0.24.0", token, error, &failure));
    assert(failure == GobiiRefreshFailure::Authentication);

    http->responses.push_back({
        426,
        R"({"error":"update_required"})",
        "",
    });
    assert(!client.Refresh("refresh", "0.24.0", token, error, &failure));
    assert(failure == GobiiRefreshFailure::UpdateRequired);
}

void TestRelayProtocolAndLedger() {
    GobiiRelayMessageAssembler assembler;
    std::string assembled;
    std::string assemblyError;
    assert(assembler.Consume(
        true, false, false, true, 0, 0, "first-",
        assembled, assemblyError) ==
        GobiiRelayMessageAssembler::Result::Incomplete);
    assert(assembler.Consume(
        false, false, true, false, 0, 0, "ping",
        assembled, assemblyError) ==
        GobiiRelayMessageAssembler::Result::Incomplete);
    assert(assembler.Consume(
        true, false, false, false, 0, 0, "second",
        assembled, assemblyError) ==
        GobiiRelayMessageAssembler::Result::Complete);
    assert(assembled == "first-second");

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
    assert(ledger.Start("two") ==
        GobiiRequestLedger::StartResult::Started);
    assert(ledger.Start("three") ==
        GobiiRequestLedger::StartResult::Started);
    assert(!ledger.Find("one"));

    GobiiRequestLedger runningLedger(
        2, std::chrono::minutes(15));
    assert(runningLedger.Start("one") ==
        GobiiRequestLedger::StartResult::Started);
    assert(runningLedger.Start("two") ==
        GobiiRequestLedger::StartResult::Started);
    assert(runningLedger.Start("three") ==
        GobiiRequestLedger::StartResult::CapacityExceeded);
}

void TestLoopbackMcpForwarding() {
    auto http = std::make_shared<FakeHttpTransport>();
    http->responses.push_back({
        200,
        R"({"jsonrpc":"2.0","id":1,"result":{}})",
        "",
    });
    GobiiLocalMcpClient client(
        "127.0.0.1",
        8787,
        "local-token",
        "internal-token",
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
    assert(http->requests[0].headers.at(
        "X-ComputerCpp-Internal-Token") == "internal-token");

    http->responses.push_back({
        0,
        "",
        "Timeout was reached",
        GobiiHttpResponse::ErrorType::Timeout,
    });
    auto timedOut = client.Forward(
        "gobii-desktop",
        {
            {"jsonrpc", "2.0"},
            {"id", 2},
            {"method", "tools/list"},
        },
        std::chrono::system_clock::now() +
            std::chrono::seconds(10));
    assert(!timedOut.ok);
    assert(timedOut.code == "deadline_exceeded");

    auto unknown = client.Forward(
        "https://example.com",
        nlohmann::json::object(),
        std::chrono::system_clock::now() +
            std::chrono::seconds(10));
    assert(!unknown.ok);
    assert(unknown.code == "unknown_app");

    GobiiLocalMcpClient nonLoopback(
        "192.0.2.10",
        8787,
        "local-token",
        "internal-token",
        {"gobii-desktop"},
        http);
    auto inaccessible = nonLoopback.Forward(
        "gobii-desktop",
        {
            {"jsonrpc", "2.0"},
            {"id", 3},
            {"method", "tools/list"},
        },
        std::chrono::system_clock::now() +
            std::chrono::seconds(10));
    assert(!inaccessible.ok);
    assert(inaccessible.code == "local_server_unavailable");
}

void TestArtifactGateAndStates() {
    auto uploader = CreateDisabledGobiiArtifactUploader();
    assert(!uploader->Available());
    uploader->Configure("https://gobii.ai", "access-token");
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

    nlohmann::json uploadedResponse = {
        {"result", {
            {"content", nlohmann::json::array({
                {
                    {"type", "image"},
                    {"mimeType", "image/png"},
                    {"data", std::string(
                        128 * 1024 + 4, 'A')},
                },
            })},
        }},
    };
    FakeArtifactUploader fakeUploader;
    assert(PrepareGobiiMcpImages(
        uploadedResponse,
        "request",
        fakeUploader,
        error));
    assert(fakeUploader.uploadedBytes > 0);
    const auto& artifactContent =
        uploadedResponse["result"]["content"][0];
    assert(artifactContent["type"] == "gobii_artifact");
    assert(artifactContent["_gobii_artifact"]["id"] ==
        "01234567-89ab-cdef-0123-456789abcdef");
    assert(artifactContent["_gobii_artifact"]["mime_type"] ==
        "image/png");

    auto artifactHttp = std::make_shared<FakeHttpTransport>();
    const std::string pngBytes =
        "\x89PNG\r\n\x1a\nunit-test-image";
    artifactHttp->responses.push_back({
        201,
        nlohmann::json{
            {"artifact_id",
                "01234567-89ab-cdef-0123-456789abcdef"},
            {"mime_type", "image/png"},
            {"byte_count",
                static_cast<std::uint64_t>(pngBytes.size())},
            {"sha256", Sha256Hex(pngBytes)},
            {"expires_at", "2030-07-31T14:00:00Z"},
        }.dump(),
        "",
    });
    auto httpUploader = CreateGobiiArtifactUploader(
        artifactHttp);
    assert(!httpUploader->Available());
    httpUploader->Configure(
        "https://gobii.ai/", "access-token");
    assert(httpUploader->Available());
    GobiiArtifactReference reference;
    const auto bytes = std::span(
        reinterpret_cast<const std::uint8_t*>(pngBytes.data()),
        pngBytes.size());
    assert(httpUploader->Upload(
        "request", bytes, "image/png", reference, error));
    assert(reference.id ==
        "01234567-89ab-cdef-0123-456789abcdef");
    assert(reference.mimeType == "image/png");
    assert(artifactHttp->requests.size() == 1);
    assert(artifactHttp->requests[0].url ==
        "https://gobii.ai/api/computer/v1/artifacts/");
    assert(artifactHttp->requests[0].headers.at(
        "Authorization") == "Bearer access-token");
    assert(artifactHttp->requests[0].headers.at(
        "Content-Type").find("multipart/form-data; boundary=") == 0);
    assert(artifactHttp->requests[0].body.find(pngBytes) !=
        std::string::npos);
    httpUploader->ClearAuthentication();
    assert(!httpUploader->Available());

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

void TestStatusPresentationAndDiagnostics() {
    GobiiConnectionStatus status;
    status.installedVersion = "0.24.0";

    auto view = PresentGobiiConnection(status, true, true);
    assert(view.title == "Not connected");
    assert(view.primaryAction == GobiiDialogAction::StartPairing);
    assert(view.permissionsSummary == "Permissions ready");
    assert(!view.showDisconnect);

    view = PresentGobiiConnection(status, false, true);
    assert(view.permissionsSummary == "Permissions needed");
    assert(view.permissionsTone == GobiiPresentationTone::Warning);

    status.deviceId = "device";
    view = PresentGobiiConnection(status, true, true);
    assert(view.title == "Ready to connect");
    assert(view.primaryAction == GobiiDialogAction::Connect);
    assert(view.showManage);
    assert(view.showDisconnect);
    assert(view.showAutoConnect);

    status.state = GobiiConnectionState::Pairing;
    view = PresentGobiiConnection(status, true, true);
    assert(view.busy);
    assert(view.secondaryAction == GobiiDialogAction::CancelPairing);
    assert(!view.showManage);
    assert(!view.showDisconnect);
    assert(!view.showAutoConnect);

    status.state = GobiiConnectionState::PairingPending;
    status.pairingCode = "ABCD-EFGH";
    view = PresentGobiiConnection(status, true, true);
    assert(view.showPairingCode);
    assert(view.primaryAction == GobiiDialogAction::ReopenPairing);
    assert(view.secondaryAction == GobiiDialogAction::CancelPairing);
    assert(!view.showManage);
    assert(!view.showDisconnect);

    status.state = GobiiConnectionState::Connecting;
    view = PresentGobiiConnection(status, true, true);
    assert(view.busy);
    assert(view.primaryAction == GobiiDialogAction::None);
    assert(!view.showManage);
    assert(!view.showDisconnect);

    status.state = GobiiConnectionState::Connected;
    status.deviceName.clear();
    const auto now = std::chrono::system_clock::now();
    status.lastHeartbeatAt = now - std::chrono::seconds(2);
    view = PresentGobiiConnection(status, true, true, now);
    assert(view.title == "Connected");
    assert(view.identityTitle == "This Mac");
    assert(view.identityDetail.find("Active just now") !=
        std::string::npos);
    assert(view.primaryAction == GobiiDialogAction::Pause);
    assert(view.permissionsSummary.empty());
    assert(view.showManage);
    assert(view.showDisconnect);

    status.state = GobiiConnectionState::Paused;
    view = PresentGobiiConnection(status, true, true);
    assert(view.tone == GobiiPresentationTone::Warning);
    assert(view.primaryAction == GobiiDialogAction::Resume);

    status.state = GobiiConnectionState::PermissionsRequired;
    view = PresentGobiiConnection(status, false, true);
    assert(view.primaryAction == GobiiDialogAction::ShowPermissions);
    assert(view.secondaryAction == GobiiDialogAction::Connect);
    assert(!view.showManage);
    assert(view.showDisconnect);

    status.deviceId.clear();
    view = PresentGobiiConnection(status, false, true);
    assert(view.secondaryAction == GobiiDialogAction::StartPairing);
    assert(!view.showDisconnect);
    status.deviceId = "device";

    status.state = GobiiConnectionState::AuthenticationExpired;
    view = PresentGobiiConnection(status, true, true);
    assert(view.primaryAction == GobiiDialogAction::StartPairing);
    assert(!view.showManage);
    assert(view.showDisconnect);

    status.state = GobiiConnectionState::UpdateRequired;
    status.requiredVersion = "0.25.0";
    view = PresentGobiiConnection(status, true, true);
    assert(view.description.find("0.25.0") != std::string::npos);
    assert(view.primaryAction == GobiiDialogAction::CheckForUpdates);
    assert(view.showManage);
    assert(view.showDisconnect);

    status.state = GobiiConnectionState::Error;
    status.lastError =
        "failed Authorization: Bearer secret-token "
        "refresh_token=refresh-secret";
    view = PresentGobiiConnection(status, true, false);
    assert(view.primaryAction == GobiiDialogAction::Connect);
    assert(view.secondaryAction == GobiiDialogAction::CopyDiagnostics);
    assert(!view.showManage);
    assert(view.showDisconnect);
    assert(view.description.find("secret-token") == std::string::npos);
    assert(view.description.find("refresh-secret") == std::string::npos);
    const std::string diagnostics = GobiiConnectionDiagnostics(
        status, true, false);
    assert(diagnostics.find("State: error") != std::string::npos);
    assert(diagnostics.find("Installed version: 0.24.0") !=
        std::string::npos);
    assert(diagnostics.find("Accessibility: granted") !=
        std::string::npos);
    assert(diagnostics.find("Screen Recording: missing") !=
        std::string::npos);
    assert(diagnostics.find("secret-token") == std::string::npos);
    assert(diagnostics.find("refresh-secret") == std::string::npos);
    const std::string jsonSecrets = SanitizeGobiiDiagnosticText(
        R"({"access_token":"access-secret", "device_code" : "device-secret"})");
    assert(jsonSecrets.find("access-secret") == std::string::npos);
    assert(jsonSecrets.find("device-secret") == std::string::npos);
    const std::string spacedSecret = SanitizeGobiiDiagnosticText(
        "refresh_token: refresh-secret-2");
    assert(spacedSecret.find("refresh-secret-2") == std::string::npos);

    status.deviceId.clear();
    view = PresentGobiiConnection(status, true, true);
    assert(view.primaryAction == GobiiDialogAction::StartPairing);
    assert(!view.showDisconnect);
}

void TestPairingPageLifetime() {
    AppConfig config = DefaultAppConfig();
    config.gobii.baseUrl = "https://gobii.ai";
    config.gobii.machineId = "machine";
    std::string configError;
    assert(SaveAppConfig(config, &configError));

    auto http = std::make_shared<FakeHttpTransport>();
    const GobiiHttpResponse pairingResponse{
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
            {"interval", 1},
        }.dump(),
        "",
    };
    http->responses.push_back(pairingResponse);
    FakeConfiguredServerController server;
    std::vector<std::string> opened;
    GobiiConnectionController controller(
        http,
        std::make_unique<FakeGobiiCredentialStore>(),
        CreateDisabledGobiiArtifactUploader(),
        server,
        [&opened](const std::string& url) {
            opened.push_back(url);
            return true;
        },
        [] { return true; });

    const auto waitForPairing = [&controller] {
        for (int attempt = 0; attempt < 200; ++attempt) {
            if (controller.Status().state ==
                GobiiConnectionState::PairingPending) {
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    };
    const auto waitForState = [&controller](
        GobiiConnectionState expected) {
        for (int attempt = 0; attempt < 200; ++attempt) {
            if (controller.Status().state == expected) {
                return;
            }
            std::this_thread::sleep_for(
                std::chrono::milliseconds(5));
        }
    };

    controller.StartPairing();
    waitForPairing();
    assert(controller.Status().state ==
        GobiiConnectionState::PairingPending);
    assert(controller.Status().pairingCode == "ABCD-EFGH");
    assert(opened.size() == 1);
    assert(controller.ReopenPairingPage());
    assert(opened.size() == 2);
    assert(opened[0] == opened[1]);

    controller.CancelPairing();
    waitForState(GobiiConnectionState::Disconnected);
    assert(controller.Status().pairingCode.empty());
    assert(!controller.ReopenPairingPage());

    http->responses.push_back(pairingResponse);
    controller.StartPairing();
    waitForPairing();
    controller.Disconnect();
    waitForState(GobiiConnectionState::Disconnected);
    assert(controller.Status().state == GobiiConnectionState::Disconnected);
    assert(controller.Status().pairingCode.empty());
    assert(!controller.ReopenPairingPage());

    http->responses.push_back(pairingResponse);
    controller.StartPairing();
    waitForPairing();
    controller.Shutdown();
    assert(controller.Status().pairingCode.empty());
    assert(!controller.ReopenPairingPage());
}

} // namespace

void RunGobiiTests() {
    TestConfigAndResources();
    TestPairingAndRefresh();
    TestPairingHttpError();
    TestPairingRejectsIncompleteCatalog();
    TestRefreshFailureClassification();
    TestRelayProtocolAndLedger();
    TestLoopbackMcpForwarding();
    TestArtifactGateAndStates();
    TestStatusPresentationAndDiagnostics();
    TestPairingPageLifetime();
}

} // namespace ComputerCpp::Tests
