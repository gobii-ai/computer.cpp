#pragma once

#include "computer_cpp/Updater.h"

#include <atomic>
#include <functional>
#include <memory>
#include <thread>

class wxProgressDialog;

namespace ComputerCpp::App {

class TrayUpdateFlow {
public:
    explicit TrayUpdateFlow(std::function<void()> quitForInstall);
    ~TrayUpdateFlow();

    void CheckForUpdates();
    void Shutdown();

private:
    void StartUpdateInstall(const ComputerCpp::Updater::ReleaseInfo& release);
    void CloseUpdateProgress();

    std::function<void()> quitForInstall_;
    std::atomic_bool updateInProgress_ = false;
    wxProgressDialog* updateProgressDialog_ = nullptr;
    std::thread updateThread_;
    std::shared_ptr<std::atomic_bool> alive_ = std::make_shared<std::atomic_bool>(true);
};

} // namespace ComputerCpp::App
