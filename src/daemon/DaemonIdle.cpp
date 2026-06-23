#include "DaemonIdle.h"

#include "computer_cpp/ControlSession.h"
#include "computer_cpp/Platform.h"

#include "DaemonParsing.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <mutex>
#include <random>
#include <string>

namespace ComputerCpp {
namespace {

std::atomic_bool gShouldStop = false;
std::atomic<int> gActiveRequests = 0;
std::atomic<long long> gLastActivityMs = 0;
std::recursive_mutex gControlMutex;
std::mutex gStopNotifierMutex;
std::function<void()> gStopNotifier;

long long NowSteadyMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

bool EnvFlag(const char* name, bool defaultValue = false) {
    const char* raw = std::getenv(name);
    if (!raw) {
        return defaultValue;
    }
    std::string value = raw;
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value == "1" || value == "true" || value == "yes" || value == "on";
}

int EnvInt(const char* name, int defaultValue, int low, int high) {
    const char* raw = std::getenv(name);
    int value = defaultValue;
    if (raw) {
        if (auto parsed = ParseIntegerStrict<int>(raw)) {
            value = *parsed;
        }
    }
    return std::clamp(value, low, high);
}

std::mt19937& IdleRng() {
    static thread_local std::mt19937 rng(std::random_device{}());
    return rng;
}

int IdleRandomInt(int low, int high) {
    std::uniform_int_distribution<int> dist(low, high);
    return dist(IdleRng());
}

double IdleRandomDouble(double low, double high) {
    std::uniform_real_distribution<double> dist(low, high);
    return dist(IdleRng());
}

void IdleBehaviorLoop() {
    const int quietMs = EnvInt("COMPUTER_CPP_IDLE_QUIET_MS", 2600, 500, 60000);
    const int minGapMs = EnvInt("COMPUTER_CPP_IDLE_MIN_GAP_MS", 4500, 1000, 120000);
    const int maxGapMs = EnvInt("COMPUTER_CPP_IDLE_MAX_GAP_MS", 14000, minGapMs, 180000);
    long long nextAt = NowSteadyMs() + IdleRandomInt(minGapMs, maxGapMs);
    while (!DaemonShouldStop()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        long long now = NowSteadyMs();
        if (gActiveRequests.load() > 0 || now - gLastActivityMs.load() < quietMs || now < nextAt) {
            continue;
        }
        if (HasActiveControlSession(kDefaultControlScope)) {
            nextAt = NowSteadyMs() + IdleRandomInt(minGapMs, maxGapMs);
            continue;
        }
        std::unique_lock<std::recursive_mutex> lock(gControlMutex, std::try_to_lock);
        if (!lock.owns_lock()) {
            continue;
        }
        if (gActiveRequests.load() > 0 || NowSteadyMs() - gLastActivityMs.load() < quietMs ||
            HasActiveControlSession(kDefaultControlScope)) {
            continue;
        }

        double anchorX = 0.0;
        double anchorY = 0.0;
        Platform::GetCursorPosition(anchorX, anchorY);
        int screenW = 0;
        int screenH = 0;
        Platform::GetScreenSize(screenW, screenH);
        if (screenW <= 0 || screenH <= 0 || (anchorX <= 1.0 && anchorY <= 1.0)) {
            nextAt = NowSteadyMs() + IdleRandomInt(minGapMs, maxGapMs);
            continue;
        }

        double angle = IdleRandomDouble(0.0, 6.28318530717958647692);
        double radius = IdleRandomDouble(10.0, 52.0);
        double targetX = std::clamp(anchorX + std::cos(angle) * radius, 8.0, static_cast<double>(std::max(8, screenW - 8)));
        double targetY = std::clamp(anchorY + std::sin(angle) * radius, 8.0, static_cast<double>(std::max(8, screenH - 8)));
        int outMs = IdleRandomInt(240, 720);
        int backMs = IdleRandomInt(220, 680);
        int outSteps = IdleRandomInt(14, 34);
        int backSteps = IdleRandomInt(14, 34);
        Platform::MoveMouseSmooth(targetX, targetY, outMs, outSteps);
        std::this_thread::sleep_for(std::chrono::milliseconds(IdleRandomInt(250, 1600)));
        Platform::MoveMouseSmooth(anchorX, anchorY, backMs, backSteps);
        nextAt = NowSteadyMs() + IdleRandomInt(minGapMs, maxGapMs);
    }
}

} // namespace

class ScopedControlActivity::Impl {
public:
    Impl() : lock(gControlMutex) {
        gActiveRequests.fetch_add(1);
        MarkDaemonActivity();
    }

    ~Impl() {
        MarkDaemonActivity();
        gActiveRequests.fetch_sub(1);
    }

private:
    std::unique_lock<std::recursive_mutex> lock;
};

ScopedControlActivity::ScopedControlActivity() : impl_(std::make_unique<Impl>()) {}

ScopedControlActivity::~ScopedControlActivity() = default;

void ResetDaemonStopState() {
    gShouldStop = false;
    MarkDaemonActivity();
}

void RequestDaemonStop() {
    gShouldStop = true;
    std::function<void()> notifier;
    {
        std::lock_guard<std::mutex> lock(gStopNotifierMutex);
        notifier = gStopNotifier;
    }
    if (notifier) {
        notifier();
    }
}

bool DaemonShouldStop() {
    return gShouldStop.load();
}

void MarkDaemonActivity() {
    gLastActivityMs.store(NowSteadyMs());
}

void SetDaemonStopNotifier(std::function<void()> notifier) {
    std::lock_guard<std::mutex> lock(gStopNotifierMutex);
    gStopNotifier = std::move(notifier);
}

std::thread StartIdleBehaviorThreadIfEnabled() {
    if (!EnvFlag("COMPUTER_CPP_IDLE_BEHAVIOR", false)) {
        return {};
    }
    return std::thread(IdleBehaviorLoop);
}

} // namespace ComputerCpp
