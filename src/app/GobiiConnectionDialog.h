#pragma once

#include <wx/dialog.h>
#include <wx/timer.h>

class wxButton;
class wxCheckBox;
class wxStaticText;

namespace ComputerCpp {
class GobiiConnectionController;
}

namespace ComputerCpp::App {

class GobiiConnectionDialog final : public wxDialog {
public:
    explicit GobiiConnectionDialog(
        GobiiConnectionController& controller);

private:
    void RefreshStatus();
    void SavePreferences();
    void OnTimer(wxTimerEvent&);
    void OnConnect(wxCommandEvent&);
    void OnPauseResume(wxCommandEvent&);
    void OnDisconnect(wxCommandEvent&);
    void OnManage(wxCommandEvent&);

    GobiiConnectionController& controller_;
    wxStaticText* state_ = nullptr;
    wxStaticText* computer_ = nullptr;
    wxStaticText* agent_ = nullptr;
    wxStaticText* verificationCode_ = nullptr;
    wxStaticText* permissions_ = nullptr;
    wxStaticText* version_ = nullptr;
    wxStaticText* lastError_ = nullptr;
    wxCheckBox* autoConnect_ = nullptr;
    wxButton* connect_ = nullptr;
    wxButton* pauseResume_ = nullptr;
    wxButton* disconnect_ = nullptr;
    wxTimer timer_;
};

} // namespace ComputerCpp::App
