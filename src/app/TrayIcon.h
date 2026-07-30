#pragma once

#include "UpdateFlow.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>
#include <wx/taskbar.h>

class wxDialog;
class wxProcess;
class wxProcessEvent;
class wxTimer;
class wxTimerEvent;

namespace ComputerCpp {
struct AppConfig;
struct ServerAppConfig;
struct ServerConfig;
}

namespace ComputerCpp::App {

class TrayIcon : public wxTaskBarIcon {
public:
    TrayIcon();
    ~TrayIcon() override;

    wxMenu* CreatePopupMenu() override;
    void SetUpPermissionsIfNeeded(bool notifyWhenGranted = true);

private:
    enum class ServerStatus {
        Stopped,
        Starting,
        Running,
        Stopping,
        Failed,
    };

    struct ManagedServer {
        std::string configName;
        std::string displayName;
        std::string appPath;
        std::string host;
        std::string url;
        std::string authToken;
        std::filesystem::path statePath;
        int port = 0;
        long pid = 0;
        wxProcess* process = nullptr;
        ServerStatus status = ServerStatus::Stopped;
        std::chrono::steady_clock::time_point deadline;
        int shutdownStage = 0;
        bool configured = false;
        bool batchMember = false;
        std::string failure;
        std::chrono::steady_clock::time_point nextHealthProbe;
    };

    enum class ServerBatchAction {
        None,
        Start,
        Stop,
    };

    void OnPermissions(wxCommandEvent& event);
    void OnSettings(wxCommandEvent& event);
    void OnRecordingToggle(wxCommandEvent& event);
    void OnShowLogs(wxCommandEvent& event);
    void OnCheckForUpdates(wxCommandEvent& event);
    void OnStartServer(wxCommandEvent& event);
    void OnStopServer(wxCommandEvent& event);
    void OnServerProcessEnded(wxProcessEvent& event);
    void OnServerTimer(wxTimerEvent& event);
    void OnState(wxCommandEvent& event);
    void OnTestScreenshot(wxCommandEvent& event);
    void OnTestMouse(wxCommandEvent& event);
    void OnTaskbarRightUp(wxTaskBarIconEvent& event);
    void OnQuit(wxCommandEvent& event);
    void StartOwnedDaemon();
    void RefreshConfiguredServers(bool force = false);
    void AdoptExistingServers(bool removeInvalidState);
    void ResolveServerConfigAsync(
        std::function<void(ComputerCpp::AppConfig, std::string)> completion);
    void StartAllServersWithConfig(ComputerCpp::AppConfig config);
    void StartConfiguredServerWithConfig(
        const std::string& configName,
        ComputerCpp::AppConfig config);
    std::set<int> OccupiedServerPorts(const std::string& excludedConfigName = {}) const;
    void ToggleServer(const std::string& configName);
    void StartOneServer(
        const ComputerCpp::ServerConfig& server,
        const ComputerCpp::ServerAppConfig& app,
        int port,
        bool batchMember);
    void StopOneServer(const std::string& configName, bool batchMember);
    void PollServers();
    void CompleteServerAction(const std::string& configName, bool success, const std::string& error = {});
    void FinishBatchIfReady();
    void QueueServerNotification(std::string message);
    void ShowPendingServerNotifications();
    void ReleaseServerProcess(ManagedServer& server);
    void StopAllServersBlocking();

    bool daemonStarted_ = false;
#ifdef __APPLE__
    void* nativeTrayIcon_ = nullptr;
#endif
    wxDialog* permissionDialog_ = nullptr;
    wxDialog* settingsDialog_ = nullptr;
    std::unique_ptr<TrayUpdateFlow> updateFlow_;
    std::unique_ptr<wxTimer> serverTimer_;
    std::map<std::string, ManagedServer> servers_;
    std::string serverAuthToken_;
    std::shared_ptr<std::atomic_bool> alive_ = std::make_shared<std::atomic_bool>(true);
    bool serverCredentialBusy_ = false;
    ServerBatchAction serverBatchAction_ = ServerBatchAction::None;
    std::set<std::string> serverBatchPending_;
    std::vector<std::string> serverBatchFailures_;
    std::vector<std::string> pendingServerNotifications_;
    bool serverNotificationScheduled_ = false;
    bool serverNotificationShowing_ = false;
    std::string lastHealthProbeConfigName_;
    std::thread daemonThread_;
    std::chrono::steady_clock::time_point configuredServersRefreshedAt_;
    size_t cachedActiveRecordingCount_ = 0;
    std::chrono::steady_clock::time_point activeRecordingCountRefreshedAt_;

    wxDECLARE_EVENT_TABLE();
};

}
