#pragma once

#include "UpdateFlow.h"

#include <memory>
#include <string>
#include <thread>
#include <wx/taskbar.h>

class wxDialog;
class wxProcess;
class wxProcessEvent;

namespace ComputerCpp::App {

class TrayIcon : public wxTaskBarIcon {
public:
    TrayIcon();
    ~TrayIcon() override;

    wxMenu* CreatePopupMenu() override;
    void SetUpPermissionsIfNeeded(bool notifyWhenGranted = true);

private:
    void OnPermissions(wxCommandEvent& event);
    void OnSettings(wxCommandEvent& event);
    void OnCheckForUpdates(wxCommandEvent& event);
    void OnStartServer(wxCommandEvent& event);
    void OnStopServer(wxCommandEvent& event);
    void OnServerProcessEnded(wxProcessEvent& event);
    void OnState(wxCommandEvent& event);
    void OnTestScreenshot(wxCommandEvent& event);
    void OnTestMouse(wxCommandEvent& event);
    void OnQuit(wxCommandEvent& event);
    void StartOwnedDaemon();
    void StopServerProcess();
    void ClearServerProcessState(bool deleteProcess);

    bool daemonStarted_ = false;
    wxDialog* permissionDialog_ = nullptr;
    wxDialog* settingsDialog_ = nullptr;
    std::unique_ptr<TrayUpdateFlow> updateFlow_;
    wxProcess* serverProcess_ = nullptr;
    long serverPid_ = 0;
    std::string serverUrl_;
    std::string serverAppDisplayName_;
    std::thread daemonThread_;

    wxDECLARE_EVENT_TABLE();
};

}
