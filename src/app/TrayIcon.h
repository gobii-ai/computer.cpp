#pragma once

#include "UpdateFlow.h"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <map>
#include <memory>
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
class GobiiConnectionController;
class ConfiguredServerController;
struct TrayAppServerState;
}

namespace ComputerCpp::App {

class TrayIcon : public wxTaskBarIcon {
public:
    TrayIcon();
    ~TrayIcon() override;

    wxMenu* CreatePopupMenu() override;
    void SetUpPermissionsIfNeeded(bool notifyWhenGranted = true);

private:
    friend class TrayConfiguredServerController;
    enum class ServerStatus {
        Stopped,
        Starting,
        Running,
        Stopping,
        Failed,
    };

    struct ManagedServer {
        std::string host;
        std::string url;
        std::filesystem::path statePath;
        int port = 0;
        long pid = 0;
        wxProcess* process = nullptr;
        ServerStatus status = ServerStatus::Stopped;
        std::chrono::steady_clock::time_point deadline;
        int shutdownStage = 0;
        std::string failure;
        std::string configSignature;
        std::chrono::steady_clock::time_point nextHealthProbe;
    };

    struct ConfiguredAppStatus {
        std::string displayName;
        std::string path;
        std::string status = "configured";
        std::string error;
        std::string schemaSha256;
    };

    void OnPermissions(wxCommandEvent& event);
    void OnSettings(wxCommandEvent& event);
    void OnRecordingToggle(wxCommandEvent& event);
    void OnShowLogs(wxCommandEvent& event);
    void OnCheckForUpdates(wxCommandEvent& event);
    void OnStartServer(wxCommandEvent& event);
    void OnStopServer(wxCommandEvent& event);
    void OnGobiiConnect(wxCommandEvent& event);
    void OnGobiiStatus(wxCommandEvent& event);
    void OnGobiiPauseResume(wxCommandEvent& event);
    void OnGobiiDisconnect(wxCommandEvent& event);
    void OnGobiiManage(wxCommandEvent& event);
    void OnServerProcessEnded(wxProcessEvent& event);
    void OnServerTimer(wxTimerEvent& event);
    void OnState(wxCommandEvent& event);
    void OnTestScreenshot(wxCommandEvent& event);
    void OnTestMouse(wxCommandEvent& event);
    void OnTaskbarRightUp(wxTaskBarIconEvent& event);
    void OnQuit(wxCommandEvent& event);
    void StartOwnedDaemon();
    void RefreshConfiguredServer(bool force = false);
    void AdoptExistingServer(bool removeInvalidState);
    void StartConfiguredServer();
    void StopConfiguredServer();
    void PollServer();
    void ApplyServerHealth(const std::string& responseBody);
    void CleanupLegacyServers();
    void QueueServerNotification(std::string message);
    void ShowPendingServerNotifications();
    void ReleaseServerProcess(ManagedServer& server);
    TrayAppServerState CurrentServerState() const;
    void ClearServerProcess();
    void StopServerBlocking();

    bool daemonStarted_ = false;
#ifdef __APPLE__
    void* nativeTrayIcon_ = nullptr;
#endif
    wxDialog* permissionDialog_ = nullptr;
    wxDialog* settingsDialog_ = nullptr;
    wxDialog* gobiiDialog_ = nullptr;
    std::unique_ptr<TrayUpdateFlow> updateFlow_;
    std::unique_ptr<ConfiguredServerController>
        configuredServerController_;
    std::unique_ptr<GobiiConnectionController>
        gobiiController_;
    std::unique_ptr<wxTimer> serverTimer_;
    ManagedServer server_;
    std::map<std::string, ConfiguredAppStatus> configuredApps_;
    std::string serverAuthToken_;
    std::string serverInternalControlToken_;
    bool serverRestartRequired_ = false;
    std::vector<std::string> pendingServerNotifications_;
    bool serverNotificationScheduled_ = false;
    bool serverNotificationShowing_ = false;
    std::thread daemonThread_;
    std::chrono::steady_clock::time_point configuredServerRefreshedAt_;
    size_t cachedActiveRecordingCount_ = 0;
    std::chrono::steady_clock::time_point activeRecordingCountRefreshedAt_;

    wxDECLARE_EVENT_TABLE();
};

}
