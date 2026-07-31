#include "GobiiConnectionDialog.h"

#include "computer_cpp/AppConfig.h"
#include "computer_cpp/GobiiConnectionController.h"
#include "computer_cpp/Platform.h"

#include <algorithm>
#include <utility>

#include <wx/activityindicator.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/clipbrd.h>
#include <wx/collpane.h>
#include <wx/dataobj.h>
#include <wx/hyperlink.h>
#include <wx/msgdlg.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/wx.h>

namespace ComputerCpp::App {
namespace {

wxColour ToneColour(GobiiPresentationTone tone) {
    switch (tone) {
        case GobiiPresentationTone::Progress:
            return wxColour(49, 118, 230);
        case GobiiPresentationTone::Success:
            return wxColour(52, 168, 83);
        case GobiiPresentationTone::Warning:
            return wxColour(225, 157, 45);
        case GobiiPresentationTone::Error:
            return wxColour(215, 67, 67);
        case GobiiPresentationTone::Neutral:
            return wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT);
    }
    return wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT);
}

wxFont LargerFont(const wxFont& base, int points, bool bold = false) {
    wxFont font = base;
    font.SetPointSize(std::max(1, base.GetPointSize() + points));
    if (bold) font.SetWeight(wxFONTWEIGHT_BOLD);
    return font;
}

void SetVisible(wxWindow* window, bool visible) {
    if (window) window->Show(visible);
}

bool IsLoopbackHost(const std::string& host) {
    return host == "127.0.0.1" || host == "localhost" || host == "::1";
}

} // namespace

GobiiConnectionDialog::GobiiConnectionDialog(
    GobiiConnectionController& controller,
    GobiiConnectionDialogCallbacks callbacks
) : wxDialog(
        nullptr,
        wxID_ANY,
        "Gobii Connection",
        wxDefaultPosition,
        wxDefaultSize,
        wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
    controller_(controller),
    callbacks_(std::move(callbacks)),
    timer_(this) {
    auto* root = new wxBoxSizer(wxVERTICAL);

    auto* header = new wxBoxSizer(wxHORIZONTAL);
    statusBadge_ = new wxStaticText(this, wxID_ANY, "●");
    statusBadge_->SetFont(LargerFont(statusBadge_->GetFont(), 13));
    header->Add(statusBadge_, 0, wxRIGHT | wxALIGN_TOP, 14);

    auto* headerText = new wxBoxSizer(wxVERTICAL);
    heading_ = new wxStaticText(this, wxID_ANY, "");
    heading_->SetFont(LargerFont(heading_->GetFont(), 7, true));
    description_ = new wxStaticText(this, wxID_ANY, "");
    description_->SetForegroundColour(
        wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
    headerText->Add(heading_, 0, wxEXPAND);
    headerText->Add(description_, 0, wxTOP | wxEXPAND, 8);
    header->Add(headerText, 1, wxEXPAND);
    root->Add(header, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 24);

    auto* body = new wxBoxSizer(wxVERTICAL);
    body->SetMinSize(wxSize(-1, 64));

    identityTitle_ = new wxStaticText(this, wxID_ANY, "");
    identityTitle_->SetFont(LargerFont(identityTitle_->GetFont(), 3, true));
    identityDetail_ = new wxStaticText(this, wxID_ANY, "");
    identityDetail_->SetForegroundColour(
        wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
    body->Add(identityTitle_, 0, wxEXPAND);
    body->Add(identityDetail_, 0, wxTOP | wxEXPAND, 5);

    verificationCode_ = new wxStaticText(
        this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
        wxALIGN_CENTER_HORIZONTAL);
    wxFont codeFont = LargerFont(verificationCode_->GetFont(), 10, true);
    codeFont.SetFamily(wxFONTFAMILY_TELETYPE);
    verificationCode_->SetFont(codeFont);
    body->Add(verificationCode_, 0, wxTOP | wxEXPAND, 8);

    auto* progressRow = new wxBoxSizer(wxHORIZONTAL);
    activity_ = new wxActivityIndicator(this);
    progress_ = new wxStaticText(this, wxID_ANY, "");
    progress_->SetForegroundColour(
        wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
    progressRow->AddStretchSpacer();
    progressRow->Add(activity_, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, 8);
    progressRow->Add(progress_, 0, wxALIGN_CENTER_VERTICAL);
    progressRow->AddStretchSpacer();
    body->Add(progressRow, 0, wxTOP | wxEXPAND, 14);

    permissionsReady_ = new wxStaticText(
        this, wxID_ANY, "✓  Permissions ready");
    permissionsReady_->SetForegroundColour(
        ToneColour(GobiiPresentationTone::Success));
    body->Add(permissionsReady_, 0, wxTOP | wxEXPAND, 12);
    root->Add(body, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 20);

    details_ = new wxCollapsiblePane(
        this,
        wxID_ANY,
        "Connection details",
        wxDefaultPosition,
        wxDefaultSize,
        wxCP_NO_TLW_RESIZE);
    auto* detailsGrid = new wxFlexGridSizer(2, 5, 14);
    detailsGrid->AddGrowableCol(1, 1);
    const auto addDetail = [this, detailsGrid](
        const char* label,
        wxStaticText*& value) {
        auto* name = new wxStaticText(
            details_->GetPane(), wxID_ANY, label);
        name->SetForegroundColour(
            wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
        detailsGrid->Add(name, 0, wxALIGN_TOP);
        value = new wxStaticText(details_->GetPane(), wxID_ANY, "—");
        detailsGrid->Add(value, 1, wxEXPAND);
    };
    addDetail("Installed version", version_);
    addDetail("Permissions", permissions_);
    addDetail("Last error", lastError_);
    details_->GetPane()->SetSizer(detailsGrid);
    root->Add(details_, 0, wxEXPAND | wxLEFT | wxRIGHT, 24);

    autoConnect_ = new wxCheckBox(
        this,
        wxID_ANY,
        "Connect automatically when ComputerCpp starts");
    root->Add(autoConnect_, 0, wxTOP | wxLEFT | wxRIGHT, 12);
    root->AddStretchSpacer();

    auto* actions = new wxBoxSizer(wxHORIZONTAL);
    primary_ = new wxButton(this, wxID_ANY, "");
    secondary_ = new wxButton(this, wxID_ANY, "");
    manage_ = new wxButton(this, wxID_ANY, "Manage in Gobii");
    disconnect_ = new wxHyperlinkCtrl(
        this,
        wxID_ANY,
        "Disconnect this computer…",
        "disconnect");
    const wxColour destructive(205, 67, 67);
    disconnect_->SetNormalColour(destructive);
    disconnect_->SetHoverColour(destructive.ChangeLightness(115));
    disconnect_->SetVisitedColour(destructive);
    actions->Add(primary_, 0, wxRIGHT, 8);
    actions->Add(secondary_, 0, wxRIGHT, 8);
    actions->Add(manage_, 0, wxRIGHT, 8);
    actions->AddStretchSpacer();
    actions->Add(disconnect_, 0, wxALIGN_CENTER_VERTICAL);
    root->Add(actions, 0, wxEXPAND | wxALL, 24);

    primary_->Bind(
        wxEVT_BUTTON,
        &GobiiConnectionDialog::OnPrimary,
        this);
    secondary_->Bind(
        wxEVT_BUTTON,
        &GobiiConnectionDialog::OnSecondary,
        this);
    manage_->Bind(
        wxEVT_BUTTON,
        &GobiiConnectionDialog::OnManage,
        this);
    disconnect_->Bind(
        wxEVT_HYPERLINK,
        &GobiiConnectionDialog::OnDisconnect,
        this);
    autoConnect_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
        SavePreferences();
    });
    Bind(wxEVT_TIMER, &GobiiConnectionDialog::OnTimer, this);
    Bind(wxEVT_CLOSE_WINDOW, &GobiiConnectionDialog::OnClose, this);

    SetSizer(root);
    const wxSize dialogSize = FromDIP(wxSize(640, 390));
    SetClientSize(dialogSize);
    SetMinClientSize(dialogSize);
    CentreOnScreen();
    RefreshStatus();
    timer_.Start(250);
}

void GobiiConnectionDialog::RefreshStatus() {
    const GobiiConnectionStatus status = controller_.Status();
    const auto permissionStatus = Platform::CheckPermissions(false);
    const auto presentation = PresentGobiiConnection(
        status,
        permissionStatus.accessibility,
        permissionStatus.screenCapture);

    statusBadge_->SetForegroundColour(ToneColour(presentation.tone));
    heading_->SetLabel(wxString::FromUTF8(presentation.title));
    description_->SetLabel(wxString::FromUTF8(presentation.description));
    description_->Wrap(550);
    identityTitle_->SetLabel(
        wxString::FromUTF8(presentation.identityTitle));
    identityDetail_->SetLabel(
        wxString::FromUTF8(presentation.identityDetail));
    const bool showIdentity = !presentation.identityTitle.empty();
    SetVisible(identityTitle_, showIdentity);
    SetVisible(identityDetail_, showIdentity);

    verificationCode_->SetLabel(
        wxString::FromUTF8(status.pairingCode));
    SetVisible(verificationCode_, presentation.showPairingCode);

    progress_->SetLabel(wxString::FromUTF8(presentation.progressText));
    SetVisible(progress_, presentation.busy);
    SetVisible(activity_, presentation.busy);
    if (presentation.busy) {
        activity_->Start();
    } else {
        activity_->Stop();
    }
    permissionsReady_->SetLabel(wxString::FromUTF8(
        (presentation.permissionsTone == GobiiPresentationTone::Success
            ? "✓  "
            : "!  ") + presentation.permissionsSummary));
    permissionsReady_->SetForegroundColour(
        ToneColour(presentation.permissionsTone));
    SetVisible(
        permissionsReady_,
        !presentation.permissionsSummary.empty());

    primaryAction_ = presentation.primaryAction;
    secondaryAction_ = presentation.secondaryAction;
    primary_->SetLabel(wxString::FromUTF8(presentation.primaryLabel));
    secondary_->SetLabel(wxString::FromUTF8(presentation.secondaryLabel));
    SetVisible(primary_, primaryAction_ != GobiiDialogAction::None);
    SetVisible(secondary_, secondaryAction_ != GobiiDialogAction::None);
    SetVisible(manage_, presentation.showManage);
    SetVisible(disconnect_, presentation.showDisconnect);
    SetVisible(autoConnect_, presentation.showAutoConnect);
    SetDefaultItem(
        primaryAction_ == GobiiDialogAction::None
            ? nullptr
            : primary_);

    version_->SetLabel(status.installedVersion.empty()
        ? wxString("Unknown")
        : wxString::FromUTF8(status.installedVersion));
    permissions_->SetLabel(
        std::string("Accessibility ") +
        (permissionStatus.accessibility ? "granted" : "missing") +
        ", Screen Recording " +
        (permissionStatus.screenCapture ? "granted" : "missing"));
    lastError_->SetLabel(status.lastError.empty()
        ? wxString("None")
        : wxString::FromUTF8(
            SanitizeGobiiDiagnosticText(status.lastError)));
    lastError_->Wrap(390);

    std::string configError;
    const AppConfig config = LoadAppConfig(&configError);
    if (configError.empty()) {
        autoConnect_->SetValue(config.gobii.autoConnect);
    }

    Layout();
}

void GobiiConnectionDialog::SavePreferences() {
    std::string error;
    AppConfig config = LoadAppConfig(&error);
    if (!error.empty()) {
        wxMessageBox(error, "Gobii Connection", wxOK | wxICON_ERROR);
        return;
    }
    config.gobii.autoConnect = autoConnect_->GetValue();
    if (!SaveAppConfig(config, &error)) {
        wxMessageBox(error, "Gobii Connection", wxOK | wxICON_ERROR);
    }
}

void GobiiConnectionDialog::ApplyAction(GobiiDialogAction action) {
    switch (action) {
        case GobiiDialogAction::StartPairing: {
            std::string configError;
            const AppConfig config = LoadAppConfig(&configError);
            if (!configError.empty()) {
                wxMessageBox(
                    wxString::FromUTF8(configError),
                    "Connect to Gobii",
                    wxOK | wxICON_ERROR);
                break;
            }
            const bool nonLoopback = !IsLoopbackHost(config.server.host);
            if (nonLoopback && config.server.authToken.empty()) {
                wxMessageBox(
                    "Gobii relay access requires bearer authentication "
                    "when the configured server binds outside loopback.",
                    "Connect to Gobii",
                    wxOK | wxICON_ERROR);
                break;
            }
            std::string disclosure =
                "A connected Gobii agent may see your screen and control "
                "native mouse and keyboard input while access is "
                "enabled.\n\nAuthentication and agent selection will "
                "open in your normal browser.";
            if (nonLoopback) {
                disclosure +=
                    "\n\nYour server currently binds outside loopback. "
                    "Gobii does not require a public listener; changing "
                    "the server host to 127.0.0.1 is recommended.";
            }
            if (wxMessageBox(
                    wxString::FromUTF8(disclosure),
                    "Connect to Gobii",
                    wxOK | wxCANCEL | wxICON_WARNING) == wxOK) {
                controller_.StartPairing();
            }
            break;
        }
        case GobiiDialogAction::Connect:
            controller_.Connect();
            break;
        case GobiiDialogAction::ReopenPairing:
            controller_.ReopenPairingPage();
            break;
        case GobiiDialogAction::CancelPairing:
            controller_.CancelPairing();
            break;
        case GobiiDialogAction::Pause:
            controller_.Pause();
            break;
        case GobiiDialogAction::Resume:
            controller_.Resume();
            break;
        case GobiiDialogAction::ShowPermissions:
            if (callbacks_.showPermissions) callbacks_.showPermissions();
            break;
        case GobiiDialogAction::CheckForUpdates:
            if (callbacks_.checkForUpdates) callbacks_.checkForUpdates();
            break;
        case GobiiDialogAction::CopyDiagnostics: {
            const auto permissions = Platform::CheckPermissions(false);
            const std::string diagnostics = GobiiConnectionDiagnostics(
                controller_.Status(),
                permissions.accessibility,
                permissions.screenCapture);
            if (wxTheClipboard && wxTheClipboard->Open()) {
                wxTheClipboard->SetData(
                    new wxTextDataObject(wxString::FromUTF8(diagnostics)));
                wxTheClipboard->Close();
            }
            break;
        }
        case GobiiDialogAction::None:
            break;
    }
    RefreshStatus();
}

void GobiiConnectionDialog::OnTimer(wxTimerEvent&) {
    RefreshStatus();
}

void GobiiConnectionDialog::OnPrimary(wxCommandEvent&) {
    ApplyAction(primaryAction_);
}

void GobiiConnectionDialog::OnSecondary(wxCommandEvent&) {
    ApplyAction(secondaryAction_);
}

void GobiiConnectionDialog::OnManage(wxCommandEvent&) {
    std::string error;
    const AppConfig config = LoadAppConfig(&error);
    if (error.empty()) {
        wxLaunchDefaultBrowser(wxString::FromUTF8(
            config.gobii.baseUrl + "/app/integrations"));
    }
}

void GobiiConnectionDialog::OnDisconnect(wxHyperlinkEvent&) {
    if (wxMessageBox(
            "Disconnect this computer and delete its local Gobii "
            "credential?",
            "Disconnect from Gobii",
            wxYES_NO | wxNO_DEFAULT | wxICON_WARNING) == wxYES) {
        controller_.Disconnect();
        RefreshStatus();
    }
}

void GobiiConnectionDialog::OnClose(wxCloseEvent&) {
    timer_.Stop();
    Destroy();
}

} // namespace ComputerCpp::App
