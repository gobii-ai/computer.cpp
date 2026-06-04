#pragma once

#include <thread>
#include <wx/taskbar.h>

class wxDialog;

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
    void OnState(wxCommandEvent& event);
    void OnTestScreenshot(wxCommandEvent& event);
    void OnTestMouse(wxCommandEvent& event);
    void OnQuit(wxCommandEvent& event);
    void StartOwnedDaemon();

    bool daemonStarted_ = false;
    wxDialog* permissionDialog_ = nullptr;
    wxDialog* settingsDialog_ = nullptr;
    std::thread daemonThread_;

    wxDECLARE_EVENT_TABLE();
};

}
