#include "CliSessionLease.h"

#include "computer_cpp/ControlSession.h"

#include <iostream>
#include <utility>

namespace ComputerCpp::Cli {

ScopedControlSessionLease::ScopedControlSessionLease(std::string token, bool releaseOnDestroy)
    : token_(std::move(token)), releaseOnDestroy_(releaseOnDestroy) {}

ScopedControlSessionLease::~ScopedControlSessionLease() {
    ReleaseNow();
}

void ScopedControlSessionLease::SetReleaseOnDestroy(bool enabled) {
    releaseOnDestroy_ = enabled;
}

bool ScopedControlSessionLease::ReleaseNow() {
    if (!releaseOnDestroy_ || released_ || token_.empty()) {
        return true;
    }
    released_ = true;
    auto releaseResult = ReleaseControlSession(token_);
    if (!releaseResult.ok && !releaseResult.error.empty()) {
        std::cerr << "Warning: control session release failed: " << releaseResult.error << "\n";
        return false;
    }
    return releaseResult.ok;
}

} // namespace ComputerCpp::Cli
