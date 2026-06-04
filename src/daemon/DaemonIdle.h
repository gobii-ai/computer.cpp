#pragma once

#include <memory>
#include <thread>

namespace ComputerCpp {

class ScopedControlActivity {
public:
    ScopedControlActivity();
    ~ScopedControlActivity();

    ScopedControlActivity(const ScopedControlActivity&) = delete;
    ScopedControlActivity& operator=(const ScopedControlActivity&) = delete;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

void ResetDaemonStopState();
void RequestDaemonStop();
bool DaemonShouldStop();
void MarkDaemonActivity();
std::thread StartIdleBehaviorThreadIfEnabled();

} // namespace ComputerCpp
