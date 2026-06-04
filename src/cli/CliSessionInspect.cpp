#include "CliSessionInspect.h"

#include "CliSessionParsing.h"

#include "computer_cpp/ControlSession.h"
#include "computer_cpp/StringUtils.h"

#include <iostream>
#include <nlohmann/json.hpp>
#include <string_view>

namespace ComputerCpp::Cli {
namespace {

using json = nlohmann::json;

int ErrorExit(const std::string& message, int code = 1) {
    std::cerr << "Error: " << message << std::endl;
    return code;
}

bool LooksNegative(std::string_view value) {
    return !value.empty() && value.front() == '-';
}

} // namespace

int HandleSessionMetrics(const CliOptions& options, const std::vector<std::string>& args) {
    std::string scope = options.controlScope;
    bool prometheus = false;
    int64_t staleAfterMs = 60 * 1000;
    int64_t longRunningAfterMs = 30 * 60 * 1000;
    int eventLimit = 20;

    for (size_t i = 2; i < args.size(); ++i) {
        if (args[i] == "--scope") {
            if (i + 1 >= args.size()) {
                return ErrorExit("session metrics --scope requires a value", 2);
            }
            if (IsBlank(args[i + 1])) {
                return ErrorExit("session metrics --scope requires a non-empty value", 2);
            }
            scope = args[++i];
        } else if (args[i] == "--prometheus") {
            prometheus = true;
        } else if (args[i] == "--stale-after" || args[i] == "--stale-after-ms") {
            if (i + 1 >= args.size()) {
                return ErrorExit("session metrics --stale-after requires a value", 2);
            }
            if (LooksNegative(args[i + 1])) {
                return ErrorExit("session metrics --stale-after must be non-negative", 2);
            }
            auto parsed = ParseDurationOption(args[i + 1], args[i] == "--stale-after-ms");
            if (!parsed) {
                return ErrorExit("session metrics --stale-after requires a valid duration", 2);
            }
            staleAfterMs = *parsed;
            ++i;
        } else if (args[i] == "--long-running-after" || args[i] == "--long-running-after-ms") {
            if (i + 1 >= args.size()) {
                return ErrorExit("session metrics --long-running-after requires a value", 2);
            }
            if (LooksNegative(args[i + 1])) {
                return ErrorExit("session metrics --long-running-after must be non-negative", 2);
            }
            auto parsed = ParseDurationOption(args[i + 1], args[i] == "--long-running-after-ms");
            if (!parsed) {
                return ErrorExit("session metrics --long-running-after requires a valid duration", 2);
            }
            longRunningAfterMs = *parsed;
            ++i;
        } else if (args[i] == "--events") {
            if (i + 1 >= args.size()) {
                return ErrorExit("session metrics --events requires a value", 2);
            }
            auto parsed = ParseInt(args[i + 1]);
            if (!parsed) {
                return ErrorExit("session metrics --events requires an integer", 2);
            }
            if (*parsed <= 0) {
                return ErrorExit("session metrics --events must be positive", 2);
            }
            eventLimit = *parsed;
            ++i;
        } else {
            return ErrorExit("unknown session metrics option: " + args[i], 2);
        }
    }

    if (prometheus) {
        std::cout << ControlSessionPrometheus(scope, staleAfterMs, longRunningAfterMs);
        return 0;
    }
    auto metrics = ControlSessionMetricsJson(scope, staleAfterMs, longRunningAfterMs, eventLimit);
    if (options.jsonOutput) {
        std::cout << json({{"ok", true}, {"data", metrics}}).dump(2) << "\n";
    } else {
        std::cout << metrics.dump(2) << "\n";
    }
    return 0;
}

int HandleSessionEvents(const CliOptions& options, const std::vector<std::string>& args) {
    std::string scope = options.controlScope;
    int limit = 20;

    for (size_t i = 2; i < args.size(); ++i) {
        if (args[i] == "--scope") {
            if (i + 1 >= args.size()) {
                return ErrorExit("session events --scope requires a value", 2);
            }
            if (IsBlank(args[i + 1])) {
                return ErrorExit("session events --scope requires a non-empty value", 2);
            }
            scope = args[++i];
        } else if (args[i] == "--limit") {
            if (i + 1 >= args.size()) {
                return ErrorExit("session events --limit requires a value", 2);
            }
            auto parsed = ParseInt(args[i + 1]);
            if (!parsed) {
                return ErrorExit("session events --limit requires an integer", 2);
            }
            if (*parsed <= 0) {
                return ErrorExit("session events --limit must be positive", 2);
            }
            limit = *parsed;
            ++i;
        } else {
            return ErrorExit("unknown session events option: " + args[i], 2);
        }
    }

    json events = json::array();
    for (const auto& event : RecentControlSessionEvents(scope, limit)) {
        events.push_back(ControlSessionEventToJson(event));
    }
    json response = {{"ok", true}, {"data", {{"events", events}}}};
    std::cout << response.dump(options.jsonOutput ? 2 : -1) << "\n";
    return 0;
}

} // namespace ComputerCpp::Cli
