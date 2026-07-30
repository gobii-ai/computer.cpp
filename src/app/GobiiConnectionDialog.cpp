#include "GobiiConnectionDialog.h"

#include "computer_cpp/AppConfig.h"
#include "computer_cpp/GobiiConnectionController.h"
#include "computer_cpp/GobiiStartupRegistration.h"
#include "computer_cpp/Platform.h"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/wx.h>

namespace ComputerCpp::App {
namespace {

wxString StatusLabel(const GobiiConnectionStatus& status) {
    wxString label = wxString::FromUTF8(
        GobiiConnectionStateName(status.state));
    label.Replace("_", " ");
    if (!label.empty()) {
        label[0] = wxToupper(label[0]);
    }
    return label;
}

} // namespace

GobiiConnectionDialog::GobiiConnectionDialog(
    GobiiConnectionController& controller
) : wxDialog(
        nullptr,
        wxID_ANY,
        "Gobii Connection",
        wxDefaultPosition,
        wxDefaultSize,
        wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
    controller_(controller),
    timer_(this) {
    auto* root = new wxBoxSizer(wxVERTICAL);
    auto* disclosure = new wxStaticText(
        this,
        wxID_ANY,
        "A connected Gobii agent may see the screen and control "
        "native mouse and keyboard input while access is enabled.");
    disclosure->Wrap(520);
    root->Add(disclosure, 0, wxEXPAND | wxALL, 14);

    auto* grid = new wxFlexGridSizer(2, 8, 16);
    grid->AddGrowableCol(1, 1);
    const auto row = [this, grid](
        const char* name,
        wxStaticText*& value) {
        grid->Add(
            new wxStaticText(this, wxID_ANY, name),
            0,
            wxALIGN_CENTER_VERTICAL);
        value = new wxStaticText(this, wxID_ANY, "—");
        grid->Add(value, 1, wxEXPAND);
    };
    row("Status", state_);
    row("Computer", computer_);
    row("Agent", agent_);
    row("Permissions", permissions_);
    row("Installed version", version_);
    row("Last error", lastError_);
    root->Add(grid, 0, wxEXPAND | wxLEFT | wxRIGHT, 14);

    autoConnect_ = new wxCheckBox(
        this, wxID_ANY, "Connect automatically");
    startAtLogin_ = new wxCheckBox(
        this, wxID_ANY, "Start ComputerCpp at login");
    root->Add(autoConnect_, 0, wxTOP | wxLEFT | wxRIGHT, 14);
    root->Add(startAtLogin_, 0, wxTOP | wxLEFT | wxRIGHT, 8);
    autoConnect_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
        SavePreferences();
    });
    startAtLogin_->Bind(
        wxEVT_CHECKBOX,
        [this](wxCommandEvent&) { SavePreferences(); });

    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    connect_ = new wxButton(this, wxID_ANY, "Connect…");
    pauseResume_ = new wxButton(this, wxID_ANY, "Pause");
    disconnect_ = new wxButton(this, wxID_ANY, "Disconnect");
    auto* manage = new wxButton(this, wxID_ANY, "Manage in Gobii…");
    auto* close = new wxButton(this, wxID_CLOSE, "Close");
    buttons->Add(connect_, 0, wxRIGHT, 8);
    buttons->Add(pauseResume_, 0, wxRIGHT, 8);
    buttons->Add(disconnect_, 0, wxRIGHT, 8);
    buttons->Add(manage, 0, wxRIGHT, 8);
    buttons->AddStretchSpacer();
    buttons->Add(close);
    root->Add(buttons, 0, wxEXPAND | wxALL, 14);
    connect_->Bind(
        wxEVT_BUTTON,
        &GobiiConnectionDialog::OnConnect,
        this);
    pauseResume_->Bind(
        wxEVT_BUTTON,
        &GobiiConnectionDialog::OnPauseResume,
        this);
    disconnect_->Bind(
        wxEVT_BUTTON,
        &GobiiConnectionDialog::OnDisconnect,
        this);
    manage->Bind(
        wxEVT_BUTTON,
        &GobiiConnectionDialog::OnManage,
        this);
    close->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        Close();
    });
    Bind(
        wxEVT_TIMER,
        &GobiiConnectionDialog::OnTimer,
        this);

    SetSizerAndFit(root);
    SetMinSize(wxSize(600, GetSize().GetHeight()));
    RefreshStatus();
    timer_.Start(1000);
}

void GobiiConnectionDialog::RefreshStatus() {
    const GobiiConnectionStatus status = controller_.Status();
    state_->SetLabel(StatusLabel(status));
    computer_->SetLabel(
        status.deviceName.empty() ? "—" : status.deviceName);
    agent_->SetLabel(
        status.agentName.empty() ? "—" : status.agentName);
    const auto permissions = Platform::CheckPermissions(false);
    permissions_->SetLabel(
        std::string("Accessibility ") +
        (permissions.accessibility ? "granted" : "missing") +
        ", Screen Recording " +
        (permissions.screenCapture ? "granted" : "missing"));
    version_->SetLabel(status.installedVersion);
    lastError_->SetLabel(
        status.lastError.empty() ? "—" : status.lastError);
    connect_->Enable(
        status.state == GobiiConnectionState::Disconnected ||
        status.state == GobiiConnectionState::AuthenticationExpired ||
        status.state == GobiiConnectionState::Error);
    pauseResume_->Enable(
        status.state == GobiiConnectionState::Connected ||
        status.state == GobiiConnectionState::Paused);
    pauseResume_->SetLabel(
        status.state == GobiiConnectionState::Paused
        ? "Resume"
        : "Pause");
    disconnect_->Enable(!status.deviceId.empty());

    std::string error;
    const AppConfig config = LoadAppConfig(&error);
    if (error.empty()) {
        autoConnect_->SetValue(config.gobii.autoConnect);
        startAtLogin_->SetValue(config.gobii.startAtLogin);
    }
}

void GobiiConnectionDialog::SavePreferences() {
    std::string error;
    AppConfig config = LoadAppConfig(&error);
    if (!error.empty()) {
        wxMessageBox(error, "Gobii Connection", wxOK | wxICON_ERROR);
        return;
    }
    const bool startupChanged =
        config.gobii.startAtLogin != startAtLogin_->GetValue();
    config.gobii.autoConnect = autoConnect_->GetValue();
    config.gobii.startAtLogin = startAtLogin_->GetValue();
    if (startupChanged &&
        !GobiiStartupRegistration::SetEnabled(
            config.gobii.startAtLogin, &error)) {
        startAtLogin_->SetValue(!config.gobii.startAtLogin);
        wxMessageBox(error, "Gobii Connection", wxOK | wxICON_ERROR);
        return;
    }
    if (!SaveAppConfig(config, &error)) {
        wxMessageBox(error, "Gobii Connection", wxOK | wxICON_ERROR);
    }
}

void GobiiConnectionDialog::OnTimer(wxTimerEvent&) {
    RefreshStatus();
}

void GobiiConnectionDialog::OnConnect(wxCommandEvent&) {
    controller_.StartPairing();
}

void GobiiConnectionDialog::OnPauseResume(wxCommandEvent&) {
    if (controller_.Status().state == GobiiConnectionState::Paused) {
        controller_.Resume();
    } else {
        controller_.Pause();
    }
}

void GobiiConnectionDialog::OnDisconnect(wxCommandEvent&) {
    if (wxMessageBox(
            "Disconnect this computer and delete its local Gobii "
            "credential?",
            "Disconnect from Gobii",
            wxYES_NO | wxNO_DEFAULT | wxICON_WARNING) == wxYES) {
        controller_.Disconnect();
    }
}

void GobiiConnectionDialog::OnManage(wxCommandEvent&) {
    std::string error;
    const AppConfig config = LoadAppConfig(&error);
    if (error.empty()) {
        wxLaunchDefaultBrowser(wxString::FromUTF8(
            config.gobii.baseUrl +
            "/app/integrations/computer"));
    }
}

} // namespace ComputerCpp::App
