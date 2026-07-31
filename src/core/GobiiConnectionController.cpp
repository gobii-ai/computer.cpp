#include "computer_cpp/GobiiConnectionController.h"

#include "computer_cpp/AppConfig.h"
#include "computer_cpp/GobiiLocalMcpClient.h"
#include "computer_cpp/Platform.h"

#include <chrono>
#include <condition_variable>
#include <nlohmann/json.hpp>
#include <random>
#include <set>
#include <thread>

using json = nlohmann::json;

namespace ComputerCpp {
namespace {

std::string PlatformName() {
#if defined(__APPLE__)
    return "macos";
#elif defined(_WIN32)
    return "windows";
#else
    return "linux";
#endif
}

std::string ArchitectureName() {
#if defined(__aarch64__) || defined(_M_ARM64)
    return "arm64";
#elif defined(__x86_64__) || defined(_M_X64)
    return "x64";
#else
    return "unknown";
#endif
}

void InterruptibleWait(
    std::stop_token stop,
    std::chrono::seconds duration
) {
    std::mutex mutex;
    std::condition_variable_any condition;
    std::unique_lock lock(mutex);
    condition.wait_for(lock, stop, duration, [] { return false; });
}

} // namespace

GobiiConnectionController::GobiiConnectionController(
    std::shared_ptr<GobiiHttpTransport> http,
    std::unique_ptr<GobiiCredentialStore> credentials,
    std::unique_ptr<GobiiArtifactUploader> artifacts,
    ConfiguredServerController& server,
    BrowserLauncher browserLauncher,
    PermissionReady permissionReady
) : http_(std::move(http)),
    credentials_(std::move(credentials)),
    artifacts_(std::move(artifacts)),
    server_(server),
    browserLauncher_(std::move(browserLauncher)),
    permissionReady_(std::move(permissionReady)) {
#ifdef COMPUTER_CPP_PROJECT_VERSION
    status_.installedVersion = COMPUTER_CPP_PROJECT_VERSION;
#endif
}

GobiiConnectionController::~GobiiConnectionController() {
    Shutdown();
}

void GobiiConnectionController::SetObserver(StatusObserver observer) {
    {
        std::lock_guard lock(mutex_);
        observer_ = std::move(observer);
    }
    Publish();
}

GobiiConnectionStatus GobiiConnectionController::Status() const {
    std::lock_guard lock(mutex_);
    return status_;
}

void GobiiConnectionController::Publish() {
    StatusObserver observer;
    GobiiConnectionStatus snapshot;
    {
        std::lock_guard lock(mutex_);
        observer = observer_;
        snapshot = status_;
    }
    if (observer) {
        observer(snapshot);
    }
}

void GobiiConnectionController::SetState(
    GobiiConnectionState state,
    std::string error
) {
    {
        std::lock_guard lock(mutex_);
        status_.state = state;
        status_.lastError = std::move(error);
        if (state != GobiiConnectionState::PairingPending) {
            status_.pairingCode.clear();
            pairingVerificationUrl_.clear();
        }
    }
    Publish();
}

void GobiiConnectionController::Initialize() {
    std::string error;
    AppConfig config = LoadAppConfig(&error);
    if (!error.empty()) {
        SetState(GobiiConnectionState::Error, error);
        return;
    }
    {
        std::lock_guard lock(mutex_);
        status_.deviceId = config.gobii.deviceId;
        status_.deviceName = config.gobii.deviceName;
        status_.agentId = config.gobii.assignedAgentId;
        status_.agentName = config.gobii.assignedAgentName;
        status_.requiredVersion = config.gobii.requiredVersion;
    }
    if (!config.gobii.updateRequiredInstalledVersion.empty()) {
        if (config.gobii.updateRequiredInstalledVersion ==
            status_.installedVersion) {
            SetState(
                GobiiConnectionState::UpdateRequired,
                "ComputerCpp must be updated before reconnecting");
            return;
        }
        config.gobii.requiredVersion.clear();
        config.gobii.updateRequiredInstalledVersion.clear();
        if (!SaveAppConfig(config, &error)) {
            SetState(GobiiConnectionState::Error, error);
            return;
        }
        {
            std::lock_guard lock(mutex_);
            status_.requiredVersion.clear();
        }
    }
    if (config.gobii.deviceId.empty()) {
        SetState(GobiiConnectionState::Disconnected);
        return;
    }
    auto token = credentials_->LoadRefreshToken(
        config.gobii.deviceId, &error);
    if (!token || token->empty()) {
        SetState(
            GobiiConnectionState::AuthenticationExpired,
            error.empty() ? "Gobii credential is missing" : error);
        return;
    }
    if (config.gobii.paused) {
        SetState(GobiiConnectionState::Paused);
        return;
    }
    if (config.gobii.autoConnect) {
        Connect();
    } else {
        SetState(GobiiConnectionState::Disconnected);
    }
}

void GobiiConnectionController::StartPairing() {
    if (shuttingDown_) return;
    CancelPairing();
    SetState(GobiiConnectionState::Pairing);
    lifecycleThread_ = std::jthread(
        [this](std::stop_token stop) {
            PairingWorker(stop);
        });
}

void GobiiConnectionController::CancelPairing() {
    if (lifecycleThread_.joinable()) {
        lifecycleThread_.request_stop();
        lifecycleThread_.join();
    }
    const auto state = Status().state;
    if (state == GobiiConnectionState::Pairing ||
        state == GobiiConnectionState::PairingPending) {
        SetState(GobiiConnectionState::Disconnected);
    }
}

bool GobiiConnectionController::SaveTokenAndConfig(
    const GobiiTokenResponse& token,
    std::string& error
) {
    if (!credentials_->SaveRefreshToken(
            token.deviceId,
            token.deviceRefreshToken,
            &error)) {
        return false;
    }
    AppConfig config = LoadAppConfig(&error);
    if (!error.empty()) return false;
    config.gobii.deviceId = token.deviceId;
    config.gobii.assignedAgentId = token.agentId;
    config.gobii.assignedAgentName = token.agentName;
    config.gobii.requiredVersion.clear();
    config.gobii.updateRequiredInstalledVersion.clear();
    config.gobii.paused = false;
    if (!SaveAppConfig(config, &error)) {
        credentials_->DeleteRefreshToken(token.deviceId, nullptr);
        return false;
    }
    {
        std::lock_guard lock(mutex_);
        status_.deviceId = token.deviceId;
        status_.deviceName = config.gobii.deviceName;
        status_.agentId = token.agentId;
        status_.agentName = token.agentName;
    }
    return true;
}

void GobiiConnectionController::PairingWorker(
    std::stop_token stop
) {
    std::string error;
    AppConfig config = LoadAppConfig(&error);
    if (!error.empty()) {
        SetState(GobiiConnectionState::Error, error);
        return;
    }
    if (config.gobii.updateRequiredInstalledVersion ==
        status_.installedVersion) {
        {
            std::lock_guard lock(mutex_);
            status_.requiredVersion = config.gobii.requiredVersion;
        }
        SetState(
            GobiiConnectionState::UpdateRequired,
            "ComputerCpp must be updated before reconnecting");
        return;
    }
    const bool loopback =
        config.server.host == "127.0.0.1" ||
        config.server.host == "localhost" ||
        config.server.host == "::1";
    if (!loopback && config.server.authToken.empty()) {
        SetState(
            GobiiConnectionState::Error,
            "Gobii relay requires bearer authentication for a "
            "non-loopback configured server");
        return;
    }
    if (!server_.EnsureRunning(error)) {
        SetState(GobiiConnectionState::Error, error);
        return;
    }
    const ConfiguredServerInfo server = server_.Status();
    if (!ConfiguredServerCatalogReady(server)) {
        SetState(
            GobiiConnectionState::Error,
            server.error.empty()
                ? "local configured MCP server catalog is not ready"
                : server.error);
        return;
    }
    if (config.gobii.machineId.empty()) {
        config.gobii.machineId = GenerateServerAuthToken();
        if (!SaveAppConfig(config, &error)) {
            SetState(GobiiConnectionState::Error, error);
            return;
        }
    }
    GobiiPairingClient pairing(config.gobii.baseUrl, http_);
    GobiiPairingRequest request;
    request.machineId = config.gobii.machineId;
    request.deviceName = config.gobii.deviceName.empty()
        ? "ComputerCpp Desktop"
        : config.gobii.deviceName;
    request.platform = PlatformName();
    request.architecture = ArchitectureName();
    request.clientVersion = status_.installedVersion;
    for (const auto& [name, catalogValue] : server.apps) {
        const size_t separator = catalogValue.find('\n');
        const std::string displayName =
            catalogValue.substr(0, separator);
        const std::string schemaSha =
            separator == std::string::npos
            ? ""
            : catalogValue.substr(separator + 1);
        request.apps.push_back({
            name,
            displayName.empty() ? name : displayName,
            schemaSha,
            name == "gobii-desktop" ? "bundled" : "custom",
        });
    }
    GobiiPairingSession session;
    if (!pairing.CreatePairing(request, session, error)) {
        SetState(GobiiConnectionState::Error, error);
        return;
    }
    {
        std::lock_guard lock(mutex_);
        status_.pairingCode = session.userCode;
        pairingVerificationUrl_ = session.verificationUriComplete;
    }
    Publish();
    if (stop.stop_requested()) return;
    if (!ReopenPairingPage()) {
        return;
    }
    if (stop.stop_requested()) return;
    SetState(GobiiConnectionState::PairingPending);
    int interval = session.intervalSeconds;
    const auto expires =
        std::chrono::steady_clock::now() +
        std::chrono::seconds(session.expiresInSeconds);
    while (!stop.stop_requested() &&
           std::chrono::steady_clock::now() < expires) {
        InterruptibleWait(stop, std::chrono::seconds(interval));
        if (stop.stop_requested()) return;
        auto result = pairing.Poll(session);
        if (result.state == GobiiPairingPollState::Pending) {
            continue;
        }
        if (result.state == GobiiPairingPollState::SlowDown) {
            interval =
                result.intervalSeconds > 0 &&
                result.intervalSeconds <= 60
                ? result.intervalSeconds
                : std::min(interval + 5, 60);
            continue;
        }
        if (result.state == GobiiPairingPollState::Approved) {
            if (!SaveTokenAndConfig(result.token, error)) {
                SetState(GobiiConnectionState::Error, error);
                return;
            }
            ConnectWorker(stop, std::move(result.token));
            return;
        }
        const char* message =
            result.state == GobiiPairingPollState::AccessDenied
            ? "Gobii pairing was denied"
            : result.state == GobiiPairingPollState::Expired
                ? "Gobii pairing expired"
                : "Gobii pairing failed";
        SetState(
            GobiiConnectionState::Disconnected,
            result.error.empty() ? message : result.error);
        return;
    }
    if (!stop.stop_requested()) {
        SetState(
            GobiiConnectionState::Disconnected,
            "Gobii pairing expired");
    }
}

bool GobiiConnectionController::ReopenPairingPage() {
    std::string url;
    {
        std::lock_guard lock(mutex_);
        url = pairingVerificationUrl_;
    }
    if (url.empty()) {
        return false;
    }
    if (!browserLauncher_ || !browserLauncher_(url)) {
        SetState(
            GobiiConnectionState::Error,
            "could not open the Gobii pairing page");
        return false;
    }
    return true;
}

void GobiiConnectionController::Connect() {
    if (shuttingDown_) return;
    if (refreshThread_.joinable()) {
        refreshThread_.request_stop();
        refreshThread_.join();
    }
    CancelPairing();
    lifecycleThread_ = std::jthread(
        [this](std::stop_token stop) {
            ConnectWorker(stop);
        });
}

void GobiiConnectionController::ConnectWorker(
    std::stop_token stop,
    std::optional<GobiiTokenResponse> initialToken
) {
    artifacts_->ClearAuthentication();
    SetState(GobiiConnectionState::Connecting);
    if (permissionReady_ && !permissionReady_()) {
        SetState(GobiiConnectionState::PermissionsRequired);
        return;
    }
    std::string error;
    AppConfig config = LoadAppConfig(&error);
    if (!error.empty()) {
        SetState(GobiiConnectionState::Error, error);
        return;
    }
    if (config.gobii.updateRequiredInstalledVersion ==
        status_.installedVersion) {
        {
            std::lock_guard lock(mutex_);
            status_.requiredVersion = config.gobii.requiredVersion;
        }
        SetState(
            GobiiConnectionState::UpdateRequired,
            "ComputerCpp must be updated before reconnecting");
        return;
    }
    const bool loopback =
        config.server.host == "127.0.0.1" ||
        config.server.host == "localhost" ||
        config.server.host == "::1";
    if (!loopback && config.server.authToken.empty()) {
        SetState(
            GobiiConnectionState::Error,
            "Gobii relay requires bearer authentication for a "
            "non-loopback configured server");
        return;
    }
    GobiiTokenResponse token;
    if (initialToken) {
        token = std::move(*initialToken);
    } else {
        auto refresh = credentials_->LoadRefreshToken(
            config.gobii.deviceId, &error);
        if (!refresh || refresh->empty()) {
            SetState(
                error.empty()
                    ? GobiiConnectionState::AuthenticationExpired
                    : GobiiConnectionState::Error,
                error.empty()
                    ? "Gobii credential is missing"
                    : error);
            return;
        }
        GobiiPairingClient pairing(config.gobii.baseUrl, http_);
        GobiiRefreshFailure refreshFailure =
            GobiiRefreshFailure::None;
        if (!pairing.Refresh(
                *refresh,
                status_.installedVersion,
                token,
                error,
                &refreshFailure)) {
            SetState(
                refreshFailure == GobiiRefreshFailure::Authentication
                    ? GobiiConnectionState::AuthenticationExpired
                    : refreshFailure == GobiiRefreshFailure::UpdateRequired
                        ? GobiiConnectionState::UpdateRequired
                        : GobiiConnectionState::Error,
                error);
            return;
        }
    }
    if (token.deviceId != config.gobii.deviceId) {
        SetState(
            GobiiConnectionState::AuthenticationExpired,
            "Gobii returned a different device identity");
        return;
    }
    if (!initialToken) {
        if (!credentials_->SaveRefreshToken(
                token.deviceId,
                token.deviceRefreshToken,
                &error)) {
            SetState(GobiiConnectionState::Error, error);
            return;
        }
    }
    if (stop.stop_requested()) return;
    if (!server_.EnsureRunning(error)) {
        SetState(GobiiConnectionState::Error, error);
        return;
    }
    const ConfiguredServerInfo server = server_.Status();
    if (!ConfiguredServerCatalogReady(server)) {
        SetState(
            GobiiConnectionState::Error,
            server.error.empty()
                ? "local configured MCP server catalog is not ready"
                : server.error);
        return;
    }
    GobiiRelayConnectOptions options;
    options.url = token.relayUrl;
    options.accessToken = token.relayAccessToken;
    options.userAgent =
        "computer.cpp/" + status_.installedVersion;
    if (!relay_.Start(
            std::move(options),
            [this](const std::string& message) {
                HandleRelayMessage(message);
            },
            [this](const std::string& disconnectError) {
                if (!shuttingDown_) {
                    ScheduleReconnect(disconnectError);
                }
            },
            [this] {
                {
                    std::lock_guard lock(mutex_);
                    status_.lastHeartbeatAt =
                        std::chrono::system_clock::now();
                }
                Publish();
            },
            error)) {
        SetState(GobiiConnectionState::Error, error);
        return;
    }
    artifacts_->Configure(
        config.gobii.baseUrl,
        token.relayAccessToken);
    json apps = json::array();
    for (const auto& [name, catalogValue] : server.apps) {
        const size_t separator = catalogValue.find('\n');
        const std::string displayName =
            catalogValue.substr(0, separator);
        const std::string schemaSha =
            separator == std::string::npos
            ? ""
            : catalogValue.substr(separator + 1);
        apps.push_back({
            {"key", name},
            {"display_name", displayName},
            {"schema_sha256", schemaSha},
            {"type", name == "gobii-desktop"
                ? "bundled"
                : "custom"},
        });
    }
    const auto permissions = Platform::CheckPermissions(false);
    const json hello = {
        {"type", "hello"},
        {"protocol_version", 1},
        {"device_id", token.deviceId},
        {"client_version", status_.installedVersion},
        {"platform", PlatformName()},
        {"architecture", ArchitectureName()},
        {"permissions", {
            {"screen_capture", permissions.screenCapture},
            {"accessibility", permissions.accessibility},
            {"input", permissions.accessibility},
        }},
        {"paused", false},
        {"apps", std::move(apps)},
    };
    if (!relay_.Send(hello.dump())) {
        artifacts_->ClearAuthentication();
        relay_.Stop();
        SetState(
            GobiiConnectionState::Error,
            "could not send relay hello");
        return;
    }
    {
        std::lock_guard lock(mutex_);
        status_.state = GobiiConnectionState::Connected;
        status_.lastError.clear();
        status_.lastConnectedAt =
            std::chrono::system_clock::now();
        status_.lastHeartbeatAt = status_.lastConnectedAt;
    }
    Publish();
    const auto now = std::chrono::system_clock::now();
    const auto lifetime =
        token.relayAccessTokenExpiresAt - now;
    const auto refreshAt = lifetime <= std::chrono::seconds(300)
        ? now + lifetime * 4 / 5
        : token.relayAccessTokenExpiresAt -
            std::chrono::seconds(60);
    refreshThread_ = std::jthread([
        this,
        refreshAt
    ](std::stop_token stop) {
        const auto stableAt =
            std::chrono::steady_clock::now() +
            std::chrono::minutes(2);
        auto nextHeartbeat =
            std::chrono::steady_clock::now() +
            std::chrono::seconds(
                heartbeatIntervalSeconds_.load());
        bool stableReset = false;
        while (!stop.stop_requested() &&
               relay_.Running()) {
            if (!stableReset &&
                std::chrono::steady_clock::now() >= stableAt) {
                {
                    std::lock_guard lock(mutex_);
                    status_.reconnectAttempt = 0;
                }
                stableReset = true;
                Publish();
            }
            if (std::chrono::steady_clock::now() >=
                nextHeartbeat) {
                if (!relay_.Send(
                        json{{"type", "heartbeat"}}.dump())) {
                    return;
                }
                nextHeartbeat =
                    std::chrono::steady_clock::now() +
                    std::chrono::seconds(
                        heartbeatIntervalSeconds_.load());
            }
            if (std::chrono::system_clock::now() >= refreshAt) {
                relay_.Stop();
                if (!stop.stop_requested()) {
                    lifecycleThread_ = std::jthread(
                        [this](std::stop_token nextStop) {
                            ConnectWorker(nextStop);
                        });
                }
                return;
            }
            std::this_thread::sleep_for(
                std::chrono::milliseconds(250));
        }
    });
}

void GobiiConnectionController::ScheduleReconnect(
    const std::string& error
) {
    int attempt = 0;
    {
        std::lock_guard lock(mutex_);
        status_.state = GobiiConnectionState::Connecting;
        status_.lastError = error;
        attempt = ++status_.reconnectAttempt;
    }
    Publish();
    reconnectThread_ = std::jthread([
        this,
        attempt
    ](std::stop_token stop) {
        const int capSeconds =
            std::min(30, 1 << std::min(attempt - 1, 5));
        std::mt19937 random(std::random_device{}());
        std::uniform_int_distribution<int> delay(
            std::max(1, capSeconds / 2),
            capSeconds);
        const auto deadline =
            std::chrono::steady_clock::now() +
            std::chrono::seconds(delay(random));
        while (!stop.stop_requested() &&
               std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(100));
        }
        if (!stop.stop_requested() && !shuttingDown_) {
            Connect();
        }
    });
}

void GobiiConnectionController::HandleRelayMessage(
    const std::string& message
) {
    const json value = json::parse(message, nullptr, false);
    if (!value.is_discarded() && value.is_object()) {
        const std::string type = value.value("type", "");
        if (type == "pause") {
            Pause();
            return;
        }
        if (type == "relay.state") {
            if (value.contains("paused") &&
                value["paused"].is_boolean() &&
                value["paused"].get<bool>()) {
                Pause();
            }
            return;
        }
        if (type == "revoke") {
            Disconnect();
            return;
        }
        if (type == "relay.close") {
            Disconnect();
            return;
        }
        if (type == "update_required") {
            if (!value.contains("required_version") ||
                !value["required_version"].is_string() ||
                value["required_version"].get_ref<
                    const std::string&>().empty()) {
                relay_.Send(GobiiRelayError(
                    "",
                    "invalid_request",
                    "update_required requires required_version").dump());
                return;
            }
            const std::string requiredVersion =
                value["required_version"].get<std::string>();
            {
                std::lock_guard lock(mutex_);
                status_.requiredVersion = requiredVersion;
            }
            std::string configError;
            AppConfig config = LoadAppConfig(&configError);
            if (configError.empty()) {
                config.gobii.requiredVersion = requiredVersion;
                config.gobii.updateRequiredInstalledVersion =
                    status_.installedVersion;
                SaveAppConfig(config, &configError);
            }
            relay_.Stop();
            SetState(
                GobiiConnectionState::UpdateRequired,
                configError.empty()
                    ? "ComputerCpp must be updated before reconnecting"
                    : configError);
            return;
        }
        if (type == "hello.ack") {
            const auto status = Status();
            if (!value.contains("device_id") ||
                !value["device_id"].is_string() ||
                value["device_id"].get<std::string>() !=
                    status.deviceId ||
                !value.contains("heartbeat_interval") ||
                !value["heartbeat_interval"].is_number_integer() ||
                value["heartbeat_interval"].get<int>() <= 0 ||
                value["heartbeat_interval"].get<int>() > 60 ||
                !value.contains("max_frame_bytes") ||
                !value["max_frame_bytes"].is_number_integer() ||
                value["max_frame_bytes"].get<size_t>() >
                    kGobiiRelayFrameLimit) {
                relay_.Stop();
                SetState(
                    GobiiConnectionState::Error,
                    "Gobii relay hello acknowledgement is invalid");
                return;
            }
            heartbeatIntervalSeconds_ =
                value["heartbeat_interval"].get<int>();
            return;
        }
        if (type == "heartbeat.ack") {
            return;
        }
    }
    GobiiRelayRequest request;
    std::string code;
    std::string error;
    if (!ParseGobiiRelayRequest(
            message, request, code, error)) {
        const std::string requestId =
            value.is_object()
            ? value.value("request_id", "")
            : "";
        relay_.Send(
            GobiiRelayError(requestId, code, error).dump());
        return;
    }
    std::string cached;
    const auto start = ledger_.Start(
        request.requestId, &cached);
    if (start ==
        GobiiRequestLedger::StartResult::RunningDuplicate) {
        relay_.Send(GobiiRelayError(
            request.requestId,
            "request_in_progress",
            "request is already running").dump());
        return;
    }
    if (start ==
        GobiiRequestLedger::StartResult::CompletedDuplicate) {
        relay_.Send(std::move(cached));
        return;
    }
    if (operationRunning_.exchange(true)) {
        const std::string response = GobiiRelayError(
            request.requestId,
            "busy",
            "another desktop operation is running").dump();
        ledger_.Complete(request.requestId, response, true);
        relay_.Send(response);
        return;
    }
    operationThread_ = std::jthread(
        [this, request = std::move(request)](
            std::stop_token) mutable {
            ExecuteRelayRequest(std::move(request));
        });
}

void GobiiConnectionController::ExecuteRelayRequest(
    GobiiRelayRequest request
) {
    {
        std::lock_guard lock(mutex_);
        status_.currentRequestId = request.requestId;
        status_.currentOperationName =
            request.payload.value("method", "");
    }
    Publish();
    std::string response;
    bool failed = false;
    const ConfiguredServerInfo server = server_.Status();
    std::set<std::string> apps;
    for (const auto& [name, _] : server.apps) {
        apps.insert(name);
    }
    GobiiLocalMcpClient local(
        server.port,
        server.bearerToken,
        std::move(apps),
        http_);
    GobiiLocalMcpResult result =
        local.Forward(
            request.app,
            request.payload,
            request.deadline);
    if (!result.ok) {
        failed = true;
        json details = json::object();
        if (result.code == "deadline_exceeded") {
            details["completion"] = "unknown";
        }
        response = GobiiRelayError(
            request.requestId,
            result.code,
            result.error,
            std::move(details)).dump();
    } else {
        json payload =
            json::parse(result.response, nullptr, false);
        std::string artifactError;
        if (payload.is_discarded() ||
            !PrepareGobiiMcpImages(
                payload,
                request.requestId,
                *artifacts_,
                artifactError)) {
            failed = true;
            response = GobiiRelayError(
                request.requestId,
                artifactError == "artifact_upload_unavailable"
                    ? artifactError
                    : "internal_error",
                artifactError).dump();
        } else {
            response = GobiiRelayResponse(
                request.requestId,
                std::move(payload)).dump();
        }
    }
    ledger_.Complete(request.requestId, response, failed);
    relay_.Send(response);
    {
        std::lock_guard lock(mutex_);
        status_.currentRequestId.clear();
        status_.currentOperationName.clear();
    }
    operationRunning_ = false;
    Publish();
}

void GobiiConnectionController::Pause() {
    if (refreshThread_.joinable()) {
        refreshThread_.request_stop();
        refreshThread_.join();
    }
    if (reconnectThread_.joinable()) {
        reconnectThread_.request_stop();
        reconnectThread_.join();
    }
    relay_.Send(json{{"type", "pause"}}.dump());
    relay_.Stop();
    artifacts_->ClearAuthentication();
    std::string error;
    AppConfig config = LoadAppConfig(&error);
    if (error.empty()) {
        config.gobii.paused = true;
        SaveAppConfig(config, &error);
    }
    SetState(
        error.empty()
            ? GobiiConnectionState::Paused
            : GobiiConnectionState::Error,
        error);
}

void GobiiConnectionController::Resume() {
    std::string error;
    AppConfig config = LoadAppConfig(&error);
    if (!error.empty()) {
        SetState(GobiiConnectionState::Error, error);
        return;
    }
    config.gobii.paused = false;
    if (!SaveAppConfig(config, &error)) {
        SetState(GobiiConnectionState::Error, error);
        return;
    }
    Connect();
}

void GobiiConnectionController::Disconnect() {
    CancelPairing();
    if (refreshThread_.joinable()) {
        refreshThread_.request_stop();
        refreshThread_.join();
    }
    if (reconnectThread_.joinable()) {
        reconnectThread_.request_stop();
        reconnectThread_.join();
    }
    relay_.Stop();
    artifacts_->ClearAuthentication();
    std::string error;
    AppConfig config = LoadAppConfig(&error);
    if (error.empty() && !config.gobii.deviceId.empty()) {
        auto token = credentials_->LoadRefreshToken(
            config.gobii.deviceId, nullptr);
        if (token && !token->empty()) {
            GobiiPairingClient pairing(
                config.gobii.baseUrl, http_);
            std::string revokeError;
            pairing.Revoke(*token, revokeError);
        }
        credentials_->DeleteRefreshToken(
            config.gobii.deviceId, nullptr);
        config.gobii.deviceId.clear();
        config.gobii.assignedAgentId.clear();
        config.gobii.assignedAgentName.clear();
        config.gobii.requiredVersion.clear();
        config.gobii.updateRequiredInstalledVersion.clear();
        config.gobii.paused = false;
        SaveAppConfig(config, &error);
    }
    {
        std::lock_guard lock(mutex_);
        status_.deviceId.clear();
        status_.agentId.clear();
        status_.agentName.clear();
        status_.requiredVersion.clear();
    }
    SetState(
        error.empty()
            ? GobiiConnectionState::Disconnected
            : GobiiConnectionState::Error,
        error);
}

void GobiiConnectionController::Shutdown() {
    if (shuttingDown_.exchange(true)) return;
    {
        std::lock_guard lock(mutex_);
        status_.pairingCode.clear();
        pairingVerificationUrl_.clear();
    }
    relay_.Stop();
    artifacts_->ClearAuthentication();
    if (lifecycleThread_.joinable()) {
        lifecycleThread_.request_stop();
        lifecycleThread_.join();
    }
    if (operationThread_.joinable()) {
        operationThread_.request_stop();
        operationThread_.join();
    }
    if (refreshThread_.joinable()) {
        refreshThread_.request_stop();
        refreshThread_.join();
    }
    if (reconnectThread_.joinable()) {
        reconnectThread_.request_stop();
        reconnectThread_.join();
    }
}

} // namespace ComputerCpp
