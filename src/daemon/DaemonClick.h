#pragma once

#include <limits>
#include <nlohmann/json.hpp>
#include <string>

namespace ComputerCpp {

struct ClickActivationOptions {
    bool valid = true;
    std::string error;
    std::string mode = "natural";
    bool hoverSafe = false;
    int durationMs = 0;
    int steps = 0;
    int clickCount = 1;
    int preClickSettleMs = -1;
    int clickHoldMs = -1;
    bool parkBeforeClick = false;
    double parkX = std::numeric_limits<double>::quiet_NaN();
    double parkY = std::numeric_limits<double>::quiet_NaN();
    int parkDurationMs = 130;
    int parkSteps = 6;
};

ClickActivationOptions ClickActivationOptionsFromParams(const nlohmann::json& params);
bool PerformClickActivation(double screenX,
                            double screenY,
                            const std::string& button,
                            const ClickActivationOptions& options);

} // namespace ComputerCpp
