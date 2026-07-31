#pragma once

#include "computer_cpp/GobiiTypes.h"

#include <chrono>
#include <string>

namespace ComputerCpp {

enum class GobiiPresentationTone {
    Neutral,
    Progress,
    Success,
    Warning,
    Error,
};

enum class GobiiDialogAction {
    None,
    StartPairing,
    Connect,
    ReopenPairing,
    CancelPairing,
    Pause,
    Resume,
    ShowPermissions,
    CheckForUpdates,
    CopyDiagnostics,
};

struct GobiiConnectionPresentation {
    GobiiPresentationTone tone = GobiiPresentationTone::Neutral;
    std::string title;
    std::string description;
    std::string identityTitle;
    std::string identityDetail;
    std::string progressText;
    std::string permissionsSummary;
    GobiiPresentationTone permissionsTone = GobiiPresentationTone::Neutral;
    std::string primaryLabel;
    GobiiDialogAction primaryAction = GobiiDialogAction::None;
    std::string secondaryLabel;
    GobiiDialogAction secondaryAction = GobiiDialogAction::None;
    bool busy = false;
    bool showPairingCode = false;
    bool showManage = false;
    bool showDisconnect = false;
    bool showAutoConnect = false;
};

GobiiConnectionPresentation PresentGobiiConnection(
    const GobiiConnectionStatus& status,
    bool accessibilityGranted,
    bool screenCaptureGranted,
    std::chrono::system_clock::time_point now =
        std::chrono::system_clock::now());

std::string SanitizeGobiiDiagnosticText(std::string value);
std::string GobiiConnectionDiagnostics(
    const GobiiConnectionStatus& status,
    bool accessibilityGranted,
    bool screenCaptureGranted);

} // namespace ComputerCpp
