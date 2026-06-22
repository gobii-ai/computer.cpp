#include "UpdateFlow.h"

#include <algorithm>
#include <chrono>
#include <future>
#include <utility>

#include <wx/msgdlg.h>
#include <wx/progdlg.h>
#include <wx/utils.h>
#include <wx/wx.h>

#if defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

namespace ComputerCpp::App {
namespace {

int CurrentProcessIdForUpdate() {
#if defined(_WIN32)
    return static_cast<int>(GetCurrentProcessId());
#elif defined(__unix__) || defined(__APPLE__)
    return static_cast<int>(getpid());
#else
    return 0;
#endif
}

}

TrayUpdateFlow::TrayUpdateFlow(std::function<void()> quitForInstall)
    : quitForInstall_(std::move(quitForInstall)) {}

TrayUpdateFlow::~TrayUpdateFlow() {
    if (alive_) {
        alive_->store(false);
    }
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

    auto alive = alive_;
    updateThread_ = std::thread([this, alive] {
        ComputerCpp::Updater::CheckResult result = ComputerCpp::Updater::CheckForUpdate();
        wxTheApp->CallAfter([this, alive, result] {
            if (!alive->load()) {
                return;
            }
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
        wxPD_ELAPSED_TIME | wxPD_REMAINING_TIME);
    updateProgressDialog_->Pulse("Downloading update...");

    auto alive = alive_;
    updateThread_ = std::thread([this, alive, release] {
        auto postProgress = [this, alive](const wxString& message, int percent = -1) {
            wxTheApp->CallAfter([this, alive, message, percent] {
                if (!alive->load()) {
                    return;
                }
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
            [alive, postProgress](int64_t downloaded, int64_t total) {
                if (!alive->load()) {
                    return false;
                }
                if (total > 0) {
                    int percent = static_cast<int>((downloaded * 100) / total);
                    postProgress("Downloading update...", percent);
                } else {
                    postProgress("Downloading update...");
                }
                return true;
            });

        if (!download.ok) {
            wxTheApp->CallAfter([this, alive, error = download.error] {
                if (!alive->load()) {
                    return;
                }
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
            wxTheApp->CallAfter([this, alive, error = staged.error] {
                if (!alive->load()) {
                    return;
                }
                CloseUpdateProgress();
                updateInProgress_ = false;
                wxMessageBox("Update validation failed:\n\n" + error,
                             "ComputerCpp Update",
                             wxOK | wxICON_ERROR);
            });
            return;
        }

        auto progressClosed = std::make_shared<std::promise<void>>();
        auto progressClosedFuture = progressClosed->get_future();
        wxTheApp->CallAfter([this, alive, progressClosed] {
            if (!alive->load()) {
                progressClosed->set_value();
                return;
            }
            CloseUpdateProgress();
            progressClosed->set_value();
        });
        progressClosedFuture.wait_for(std::chrono::seconds(5));

        auto install = ComputerCpp::Updater::LaunchInstallAndRelaunch(staged, CurrentProcessIdForUpdate());
        wxTheApp->CallAfter([this, alive, install] {
            if (!alive->load()) {
                return;
            }
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
            updateInProgress_ = false;
            if (quitForInstall_) {
                quitForInstall_();
            }
        });
    });
}

} // namespace ComputerCpp::App
