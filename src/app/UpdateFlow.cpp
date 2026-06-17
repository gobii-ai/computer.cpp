#include "UpdateFlow.h"

#include <algorithm>
#include <utility>

#include <wx/msgdlg.h>
#include <wx/progdlg.h>
#include <wx/utils.h>
#include <wx/wx.h>

#if defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#endif

namespace ComputerCpp::App {

TrayUpdateFlow::TrayUpdateFlow(std::function<void()> quitForInstall)
    : quitForInstall_(std::move(quitForInstall)) {}

TrayUpdateFlow::~TrayUpdateFlow() {
    Shutdown();
}

void TrayUpdateFlow::Shutdown() {
    CloseUpdateProgress();
    if (updateThread_.joinable()) {
        updateThread_.join();
    }
}

void TrayUpdateFlow::CloseUpdateProgress() {
    if (updateProgressDialog_) {
        updateProgressDialog_->Destroy();
        updateProgressDialog_ = nullptr;
    }
}

void TrayUpdateFlow::CheckForUpdates() {
    bool expected = false;
    if (!updateInProgress_.compare_exchange_strong(expected, true)) {
        wxMessageBox("An update check is already in progress.",
                     "ComputerCpp Update",
                     wxOK | wxICON_INFORMATION);
        return;
    }

    if (updateThread_.joinable()) {
        updateThread_.join();
    }

    updateThread_ = std::thread([this] {
        ComputerCpp::Updater::CheckResult result = ComputerCpp::Updater::CheckForUpdate();
        wxTheApp->CallAfter([this, result] {
            if (result.status != ComputerCpp::Updater::CheckStatus::UpdateAvailable) {
                updateInProgress_ = false;
            }

            switch (result.status) {
            case ComputerCpp::Updater::CheckStatus::UpToDate:
            {
                wxString message;
                message << "ComputerCpp is up to date.\n\n";
                message << "Current version: " << result.currentVersion << "\n";
                message << "Latest version: " << result.latestVersion;
                wxMessageBox(message, "ComputerCpp Update", wxOK | wxICON_INFORMATION);
                return;
            }
            case ComputerCpp::Updater::CheckStatus::UpdateAvailable: {
                wxString message;
                message << "ComputerCpp " << result.latestVersion << " is available.\n\n";
                message << "Current version: " << result.currentVersion << "\n";
                message << "Release: " << result.release.htmlUrl << "\n\n";
                message << "Install it now?";
                wxMessageDialog dialog(nullptr,
                                       message,
                                       "ComputerCpp Update",
                                       wxYES_NO | wxCANCEL | wxICON_INFORMATION);
                dialog.SetYesNoCancelLabels("Install Update", "View Release", "Cancel");
                int answer = dialog.ShowModal();
                if (answer == wxID_YES) {
                    StartUpdateInstall(result.release);
                    return;
                }
                updateInProgress_ = false;
                if (answer == wxID_NO && !result.release.htmlUrl.empty()) {
                    wxLaunchDefaultBrowser(result.release.htmlUrl);
                }
                return;
            }
            case ComputerCpp::Updater::CheckStatus::NoCompatibleAsset:
            case ComputerCpp::Updater::CheckStatus::UnsupportedPlatform:
            case ComputerCpp::Updater::CheckStatus::NetworkError:
            case ComputerCpp::Updater::CheckStatus::InvalidResponse:
                wxMessageBox(result.message,
                             "ComputerCpp Update",
                             wxOK | wxICON_ERROR);
                return;
            }
        });
    });
}

void TrayUpdateFlow::StartUpdateInstall(const ComputerCpp::Updater::ReleaseInfo& release) {
    if (updateThread_.joinable()) {
        updateThread_.join();
    }

    CloseUpdateProgress();
    updateProgressDialog_ = new wxProgressDialog(
        "ComputerCpp Update",
        "Downloading update...",
        100,
        nullptr,
        wxPD_APP_MODAL | wxPD_ELAPSED_TIME | wxPD_REMAINING_TIME);
    updateProgressDialog_->Pulse("Downloading update...");

    updateThread_ = std::thread([this, release] {
        auto postProgress = [this](const wxString& message, int percent = -1) {
            wxTheApp->CallAfter([this, message, percent] {
                if (!updateProgressDialog_) {
                    return;
                }
                if (percent >= 0) {
                    updateProgressDialog_->Update(std::clamp(percent, 0, 100), message);
                } else {
                    updateProgressDialog_->Pulse(message);
                }
            });
        };

        auto download = ComputerCpp::Updater::DownloadReleaseAsset(
            release,
            [postProgress](int64_t downloaded, int64_t total) {
                if (total > 0) {
                    int percent = static_cast<int>((downloaded * 100) / total);
                    postProgress("Downloading update...", percent);
                } else {
                    postProgress("Downloading update...");
                }
                return true;
            });

        if (!download.ok) {
            wxTheApp->CallAfter([this, error = download.error] {
                CloseUpdateProgress();
                updateInProgress_ = false;
                wxMessageBox("Download failed:\n\n" + error,
                             "ComputerCpp Update",
                             wxOK | wxICON_ERROR);
            });
            return;
        }

        postProgress("Validating update...");
        auto staged = ComputerCpp::Updater::StageDownloadedUpdate(release, download.zipPath);
        if (!staged.ok) {
            wxTheApp->CallAfter([this, error = staged.error] {
                CloseUpdateProgress();
                updateInProgress_ = false;
                wxMessageBox("Update validation failed:\n\n" + error,
                             "ComputerCpp Update",
                             wxOK | wxICON_ERROR);
            });
            return;
        }

        postProgress("Preparing installer...");
        auto install = ComputerCpp::Updater::LaunchInstallAndRelaunch(staged, static_cast<int>(getpid()));
        wxTheApp->CallAfter([this, install] {
            CloseUpdateProgress();
            if (install.manualInstallRequired) {
                updateInProgress_ = false;
                if (!install.zipPath.empty()) {
                    ComputerCpp::Updater::RevealInFinder(install.zipPath);
                }
                wxMessageBox(install.error + "\n\nThe downloaded update has been revealed in Finder for manual installation.",
                             "ComputerCpp Update",
                             wxOK | wxICON_INFORMATION);
                return;
            }
            if (!install.ok) {
                updateInProgress_ = false;
                wxMessageBox("Could not start the update installer:\n\n" + install.error,
                             "ComputerCpp Update",
                             wxOK | wxICON_ERROR);
                return;
            }
            wxMessageBox("ComputerCpp will quit, install the update, and relaunch.",
                         "ComputerCpp Update",
                         wxOK | wxICON_INFORMATION);
            if (quitForInstall_) {
                quitForInstall_();
            }
        });
    });
}

} // namespace ComputerCpp::App
