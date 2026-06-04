#pragma once

#include <string>

namespace ComputerCpp::Cli {

class ScopedControlSessionLease {
public:
    explicit ScopedControlSessionLease(std::string token, bool releaseOnDestroy = true);

    ScopedControlSessionLease(const ScopedControlSessionLease&) = delete;
    ScopedControlSessionLease& operator=(const ScopedControlSessionLease&) = delete;

    ~ScopedControlSessionLease();

    void SetReleaseOnDestroy(bool enabled);
    bool ReleaseNow();

private:
    std::string token_;
    bool releaseOnDestroy_ = true;
    bool released_ = false;
};

} // namespace ComputerCpp::Cli
