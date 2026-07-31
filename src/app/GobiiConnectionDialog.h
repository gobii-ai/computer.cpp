#pragma once

#include "computer_cpp/GobiiStatusPresentation.h"

#include <functional>

#include <wx/collpane.h>
#include <wx/dialog.h>
#include <wx/timer.h>

class wxActivityIndicator;
class wxButton;
class wxCheckBox;
class wxCloseEvent;
class wxHyperlinkCtrl;
class wxHyperlinkEvent;
class wxStaticText;

namespace ComputerCpp {
class GobiiConnectionController;
}

namespace ComputerCpp::App {

struct GobiiConnectionDialogCallbacks {
    std::function<void()> showPermissions;
    std::function<void()> checkForUpdates;
};

class GobiiConnectionDialog final : public wxDialog {
public:
    GobiiConnectionDialog(
        GobiiConnectionController& controller,
        GobiiConnectionDialogCallbacks callbacks = {});

private:
    void RefreshStatus();
    void SavePreferences();
    void ApplyAction(GobiiDialogAction action);
    void OnTimer(wxTimerEvent&);
    void OnPrimary(wxCommandEvent&);
    void OnSecondary(wxCommandEvent&);
    void OnManage(wxCommandEvent&);
    void OnDisconnect(wxHyperlinkEvent&);
    void OnClose(wxCloseEvent&);

    GobiiConnectionController& controller_;
    GobiiConnectionDialogCallbacks callbacks_;
    GobiiDialogAction primaryAction_ = GobiiDialogAction::None;
    GobiiDialogAction secondaryAction_ = GobiiDialogAction::None;
    wxStaticText* statusBadge_ = nullptr;
    wxStaticText* heading_ = nullptr;
    wxStaticText* description_ = nullptr;
    wxStaticText* identityTitle_ = nullptr;
    wxStaticText* identityDetail_ = nullptr;
    wxStaticText* verificationCode_ = nullptr;
    wxActivityIndicator* activity_ = nullptr;
    wxStaticText* progress_ = nullptr;
    wxStaticText* permissionsReady_ = nullptr;
    wxCollapsiblePane* details_ = nullptr;
    wxStaticText* version_ = nullptr;
    wxStaticText* permissions_ = nullptr;
    wxStaticText* lastError_ = nullptr;
    wxCheckBox* autoConnect_ = nullptr;
    wxButton* primary_ = nullptr;
    wxButton* secondary_ = nullptr;
    wxButton* manage_ = nullptr;
    wxHyperlinkCtrl* disconnect_ = nullptr;
    wxTimer timer_;
};

} // namespace ComputerCpp::App
