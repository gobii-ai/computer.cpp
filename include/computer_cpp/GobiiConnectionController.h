#pragma once

#include "computer_cpp/ConfiguredServerController.h"
#include "computer_cpp/GobiiArtifactUploader.h"
#include "computer_cpp/GobiiCredentialStore.h"
#include "computer_cpp/GobiiPairingClient.h"
#include "computer_cpp/GobiiRelayClient.h"
#include "computer_cpp/GobiiRelayProtocol.h"
#include "computer_cpp/GobiiTypes.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>

namespace ComputerCpp {

class GobiiConnectionController {
public:
    using StatusObserver =
        std::function<void(const GobiiConnectionStatus&)>;
    using BrowserLauncher =
        std::function<bool(const std::string&)>;
    using PermissionReady = std::function<bool()>;

    GobiiConnectionController(
        std::shared_ptr<GobiiHttpTransport> http,
        std::unique_ptr<GobiiCredentialStore> credentials,
        std::unique_ptr<GobiiArtifactUploader> artifacts,
        ConfiguredServerController& server,
        BrowserLauncher browserLauncher,
        PermissionReady permissionReady);
    ~GobiiConnectionController();

    void SetObserver(StatusObserver observer);
    GobiiConnectionStatus Status() const;
    void Initialize();
    void StartPairing();
    bool ReopenPairingPage();
    void CancelPairing();
    void Connect();
    void Pause();
    void Resume();
    void Disconnect();
    void Shutdown();

private:
    enum class Command {
        StartPairing,
        CancelPairing,
        Connect,
        Pause,
        Resume,
        Disconnect,
    };

    void Enqueue(Command command);
    void CommandWorker(std::stop_token stop);
    void DoStartPairing();
    void DoCancelPairing();
    void DoConnect();
    void DoPause();
    void DoResume();
    void DoDisconnect();
    void ReplaceThread(std::jthread& slot, std::jthread next);
    void StopThread(std::jthread& slot);
    void RequestThreadStop(std::jthread& slot);
    void PairingWorker(std::stop_token stop);
    void ConnectWorker(
        std::stop_token stop,
        std::optional<GobiiTokenResponse> initialToken =
            std::nullopt);
    void HandleRelayMessage(const std::string& message);
    void ExecuteRelayRequest(GobiiRelayRequest request);
    void ScheduleReconnect(const std::string& error);
    void Publish();
    void SetState(
        GobiiConnectionState state,
        std::string error = {});
    bool SaveTokenAndConfig(
        const GobiiTokenResponse& token,
        std::string& error);

    std::shared_ptr<GobiiHttpTransport> http_;
    std::unique_ptr<GobiiCredentialStore> credentials_;
    std::unique_ptr<GobiiArtifactUploader> artifacts_;
    ConfiguredServerController& server_;
    BrowserLauncher browserLauncher_;
    PermissionReady permissionReady_;
    mutable std::mutex mutex_;
    StatusObserver observer_;
    GobiiConnectionStatus status_;
    std::string pairingVerificationUrl_;
    GobiiRequestLedger ledger_;
    GobiiRelayClient relay_;
    std::mutex commandMutex_;
    std::condition_variable_any commandCondition_;
    std::deque<Command> commands_;
    std::jthread commandThread_;
    std::mutex threadMutex_;
    std::jthread lifecycleThread_;
    std::jthread operationThread_;
    std::jthread refreshThread_;
    std::jthread reconnectThread_;
    std::atomic<bool> shuttingDown_{false};
    std::atomic<bool> operationRunning_{false};
    std::atomic<int> heartbeatIntervalSeconds_{20};
};

} // namespace ComputerCpp
