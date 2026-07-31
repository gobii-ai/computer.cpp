#include "computer_cpp/GobiiStatusPresentation.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string_view>

namespace ComputerCpp {
namespace {

std::string ActiveDescription(
    const GobiiConnectionStatus& status,
    std::chrono::system_clock::time_point now
) {
    if (!status.lastHeartbeatAt) {
        return "Connected to This Mac";
    }
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - *status.lastHeartbeatAt);
    if (elapsed < std::chrono::seconds::zero()) {
        elapsed = std::chrono::seconds::zero();
    }
    if (elapsed < std::chrono::seconds(10)) {
        return "Connected to This Mac · Active just now";
    }
    if (elapsed < std::chrono::minutes(1)) {
        return "Connected to This Mac · Active " +
            std::to_string(elapsed.count()) + " seconds ago";
    }
    const auto minutes =
        std::chrono::duration_cast<std::chrono::minutes>(elapsed).count();
    return "Connected to This Mac · Active " +
        std::to_string(minutes) +
        (minutes == 1 ? " minute ago" : " minutes ago");
}

void SetPairedActions(
    GobiiConnectionPresentation& presentation,
    const GobiiConnectionStatus& status,
    bool showManage
) {
    const bool paired = !status.deviceId.empty();
    presentation.showManage = paired && showManage;
    presentation.showDisconnect = paired;
    presentation.showAutoConnect = paired;
}

std::string Lower(std::string value) {
    std::transform(
        value.begin(), value.end(), value.begin(),
        [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
    return value;
}

void RedactAfter(
    std::string& value,
    const std::string& lowerNeedle,
    size_t prefixLength
) {
    std::string lower = Lower(value);
    size_t searchFrom = 0;
    while (true) {
        const size_t position = lower.find(lowerNeedle, searchFrom);
        if (position == std::string::npos) return;
        const size_t secretStart = position + prefixLength;
        size_t secretEnd = secretStart;
        while (secretEnd < value.size()) {
            const unsigned char ch =
                static_cast<unsigned char>(value[secretEnd]);
            if (std::isspace(ch) || ch == '&' || ch == ',' ||
                ch == ';' || ch == '\'' || ch == '"') {
                break;
            }
            ++secretEnd;
        }
        value.replace(secretStart, secretEnd - secretStart, "<redacted>");
        lower = Lower(value);
        searchFrom = secretStart + std::string_view("<redacted>").size();
    }
}

void RedactNamedSecret(
    std::string& value,
    const std::string& name
) {
    std::string lower = Lower(value);
    size_t searchFrom = 0;
    while (true) {
        const size_t position = lower.find(name, searchFrom);
        if (position == std::string::npos) return;
        size_t cursor = position + name.size();
        if (cursor < value.size() &&
            (value[cursor] == '\'' || value[cursor] == '"')) {
            ++cursor;
        }
        while (cursor < value.size() &&
               std::isspace(static_cast<unsigned char>(value[cursor]))) {
            ++cursor;
        }
        if (cursor >= value.size() ||
            (value[cursor] != '=' && value[cursor] != ':')) {
            searchFrom = position + name.size();
            continue;
        }
        ++cursor;
        while (cursor < value.size() &&
               std::isspace(static_cast<unsigned char>(value[cursor]))) {
            ++cursor;
        }
        char quote = '\0';
        if (cursor < value.size() &&
            (value[cursor] == '\'' || value[cursor] == '"')) {
            quote = value[cursor];
            ++cursor;
        }
        const size_t secretStart = cursor;
        while (cursor < value.size()) {
            const unsigned char ch =
                static_cast<unsigned char>(value[cursor]);
            if ((quote != '\0' && value[cursor] == quote) ||
                (quote == '\0' &&
                 (std::isspace(ch) || ch == '&' || ch == ',' ||
                  ch == ';' || ch == '\'' || ch == '"'))) {
                break;
            }
            ++cursor;
        }
        if (cursor == secretStart) {
            searchFrom = secretStart;
            continue;
        }
        value.replace(
            secretStart,
            cursor - secretStart,
            "<redacted>");
        lower = Lower(value);
        searchFrom = secretStart + std::string_view("<redacted>").size();
    }
}

} // namespace

GobiiConnectionPresentation PresentGobiiConnection(
    const GobiiConnectionStatus& status,
    bool accessibilityGranted,
    bool screenCaptureGranted,
    std::chrono::system_clock::time_point now
) {
    GobiiConnectionPresentation presentation;
    const bool permissionsReady =
        accessibilityGranted && screenCaptureGranted;

    switch (status.state) {
        case GobiiConnectionState::Disconnected:
            presentation.permissionsSummary = permissionsReady
                ? "Permissions ready"
                : "Permissions needed";
            presentation.permissionsTone = permissionsReady
                ? GobiiPresentationTone::Success
                : GobiiPresentationTone::Warning;
            if (status.deviceId.empty()) {
                presentation.title = "Not connected";
                presentation.description =
                    "Connect this Mac to let a Gobii agent securely view "
                    "and control it.";
                presentation.primaryLabel = "Connect to Gobii…";
                presentation.primaryAction =
                    GobiiDialogAction::StartPairing;
            } else {
                SetPairedActions(presentation, status, true);
                presentation.title = "Ready to connect";
                presentation.description =
                    "This Mac is paired with Gobii but is not connected.";
                presentation.primaryLabel = "Connect";
                presentation.primaryAction = GobiiDialogAction::Connect;
            }
            break;
        case GobiiConnectionState::Pairing:
            presentation.tone = GobiiPresentationTone::Progress;
            presentation.title = "Starting secure connection…";
            presentation.description =
                "Creating a one-time verification code.";
            presentation.progressText = "Contacting Gobii…";
            presentation.busy = true;
            presentation.secondaryLabel = "Cancel";
            presentation.secondaryAction =
                GobiiDialogAction::CancelPairing;
            break;
        case GobiiConnectionState::PairingPending:
            presentation.tone = GobiiPresentationTone::Progress;
            presentation.title = "Finish connecting in your browser";
            presentation.description =
                "Confirm that this code matches the one shown by Gobii:";
            presentation.progressText = "Waiting for approval…";
            presentation.busy = true;
            presentation.showPairingCode = true;
            presentation.primaryLabel = "Open Gobii again";
            presentation.primaryAction =
                GobiiDialogAction::ReopenPairing;
            presentation.secondaryLabel = "Cancel";
            presentation.secondaryAction =
                GobiiDialogAction::CancelPairing;
            break;
        case GobiiConnectionState::Connecting:
            presentation.tone = GobiiPresentationTone::Progress;
            presentation.title = "Connecting to Gobii…";
            presentation.description =
                "Establishing a secure connection for this Mac.";
            presentation.progressText = "Connecting…";
            presentation.busy = true;
            break;
        case GobiiConnectionState::Connected:
            SetPairedActions(presentation, status, true);
            presentation.tone = GobiiPresentationTone::Success;
            presentation.title = "Connected";
            presentation.description =
                "Gobii can currently view and control this Mac.";
            presentation.identityTitle = status.deviceName.empty()
                ? "This Mac"
                : status.deviceName;
            presentation.identityDetail = ActiveDescription(status, now);
            presentation.primaryLabel = "Pause agent access";
            presentation.primaryAction = GobiiDialogAction::Pause;
            break;
        case GobiiConnectionState::Paused:
            SetPairedActions(presentation, status, true);
            presentation.tone = GobiiPresentationTone::Warning;
            presentation.title = "Agent access is paused";
            presentation.description =
                "Gobii cannot view or control this Mac until you resume "
                "access.";
            presentation.identityTitle = status.deviceName.empty()
                ? "This Mac"
                : status.deviceName;
            presentation.identityDetail = "Paired with Gobii";
            presentation.primaryLabel = "Resume agent access";
            presentation.primaryAction = GobiiDialogAction::Resume;
            break;
        case GobiiConnectionState::PermissionsRequired:
            SetPairedActions(presentation, status, false);
            presentation.tone = GobiiPresentationTone::Warning;
            presentation.title = "Permissions needed";
            presentation.description =
                "Grant Accessibility and Screen Recording access before "
                "Gobii can connect.";
            presentation.primaryLabel = "Review permissions";
            presentation.primaryAction =
                GobiiDialogAction::ShowPermissions;
            presentation.secondaryLabel = "Try again";
            presentation.secondaryAction = status.deviceId.empty()
                ? GobiiDialogAction::StartPairing
                : GobiiDialogAction::Connect;
            break;
        case GobiiConnectionState::AuthenticationExpired:
            SetPairedActions(presentation, status, false);
            presentation.tone = GobiiPresentationTone::Warning;
            presentation.title = "Sign in again";
            presentation.description =
                "This Mac’s Gobii authorization has expired.";
            presentation.primaryLabel = "Connect again";
            presentation.primaryAction =
                GobiiDialogAction::StartPairing;
            break;
        case GobiiConnectionState::UpdateRequired:
            SetPairedActions(presentation, status, true);
            presentation.tone = GobiiPresentationTone::Warning;
            presentation.title = "Update required";
            presentation.description = status.requiredVersion.empty()
                ? "Update ComputerCpp before reconnecting to Gobii."
                : "Update ComputerCpp to version " +
                    status.requiredVersion + " before reconnecting.";
            presentation.primaryLabel = "Check for updates";
            presentation.primaryAction =
                GobiiDialogAction::CheckForUpdates;
            break;
        case GobiiConnectionState::Error:
            SetPairedActions(presentation, status, false);
            presentation.tone = GobiiPresentationTone::Error;
            presentation.title = "Couldn’t connect to Gobii";
            presentation.description = status.lastError.empty()
                ? "An unexpected connection error occurred."
                : SanitizeGobiiDiagnosticText(status.lastError);
            presentation.primaryLabel = "Try again";
            presentation.primaryAction = status.deviceId.empty()
                ? GobiiDialogAction::StartPairing
                : GobiiDialogAction::Connect;
            presentation.secondaryLabel = "Copy diagnostics";
            presentation.secondaryAction =
                GobiiDialogAction::CopyDiagnostics;
            break;
    }
    return presentation;
}

std::string SanitizeGobiiDiagnosticText(std::string value) {
    for (char& ch : value) {
        const unsigned char byte = static_cast<unsigned char>(ch);
        if ((byte < 0x20 && ch != '\t') || byte == 0x7f) {
            ch = ' ';
        }
    }
    RedactAfter(value, "bearer ", 7);
    for (const char* name : {
        "access_token", "refresh_token", "device_code"
    }) {
        RedactNamedSecret(value, name);
    }
    if (value.size() > 1000) {
        value.resize(997);
        value += "…";
    }
    return value;
}

std::string GobiiConnectionDiagnostics(
    const GobiiConnectionStatus& status,
    bool accessibilityGranted,
    bool screenCaptureGranted
) {
    std::ostringstream out;
    out << "Gobii connection diagnostics\n";
    out << "State: " << GobiiConnectionStateName(status.state) << "\n";
    out << "Installed version: "
        << (status.installedVersion.empty() ? "unknown" : status.installedVersion)
        << "\n";
    out << "Accessibility: "
        << (accessibilityGranted ? "granted" : "missing") << "\n";
    out << "Screen Recording: "
        << (screenCaptureGranted ? "granted" : "missing") << "\n";
    out << "Last error: "
        << (status.lastError.empty()
            ? "none"
            : SanitizeGobiiDiagnosticText(status.lastError))
        << "\n";
    return out.str();
}

} // namespace ComputerCpp
