#include "CliCommands.h"

#include "computer_cpp/StringUtils.h"

#include "CliCommandHelpers.h"

#include <nlohmann/json.hpp>

#include <utility>

using json = nlohmann::json;

namespace ComputerCpp::Cli {
CommandRequest BuildObserveCommand(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        return Error("observe requires subcommand");
    }

    json params = json::object();
    if (args[1] == "events") {
        if (args.size() > 2) {
            if (args[2].rfind("--", 0) == 0) {
                return Error("unknown observe events option: " + args[2]);
            }
            if (!SetParsedInt(params, "limit", args[2])) {
                return Error("observe events limit must be an integer");
            }
            if (params["limit"].get<int>() <= 0) {
                return Error("observe events limit must be positive");
            }
        }
        if (args.size() > 3) {
            return Error("unknown observe events option: " + args[3]);
        }
        return Ok("observe_events", std::move(params));
    }

    if (args[1] == "frames") {
        params["event"] = args.size() > 2 ? args[2] : "last";
        if (IsBlank(params["event"].get<std::string>())) {
            return Error("observe frames event must be non-empty");
        }
        if (args.size() > 3) {
            if (args[3].rfind("--", 0) == 0) {
                return Error("unknown observe frames option: " + args[3]);
            }
            if (!SetParsedInt(params, "limit", args[3])) {
                return Error("observe frames limit must be an integer");
            }
            if (params["limit"].get<int>() <= 0) {
                return Error("observe frames limit must be positive");
            }
        }
        if (args.size() > 4) {
            return Error("unknown observe frames option: " + args[4]);
        }
        return Ok("observe_frames", std::move(params));
    }

    return Error("unknown observe subcommand");
}

CommandRequest BuildTargetCommand(const std::vector<std::string>& args) {
    if (args.size() < 3) {
        return Error("target requires subcommand and query");
    }

    json params = json::object();
    if (args[1] == "resolve") {
        if (args.size() > 3) {
            return Error("unknown target resolve option: " + args[3]);
        }
        if (IsBlank(args[2])) {
            return Error("target resolve target must be non-empty");
        }
        params["target"] = args[2];
        return Ok("target_resolve", std::move(params));
    }
    if (args[1] == "explain") {
        if (args.size() > 3) {
            return Error("unknown target explain option: " + args[3]);
        }
        if (IsBlank(args[2])) {
            return Error("target explain target must be non-empty");
        }
        params["target"] = args[2];
        return Ok("target_explain", std::move(params));
    }
    if (args[1] != "find") {
        return Error("unknown target subcommand");
    }

    if (args[2] == "role") {
        if (args.size() < 4) {
            return Error("target find role requires role");
        }
        if (IsBlank(args[3])) {
            return Error("target find role must be non-empty");
        }
        std::string target = "role:" + args[3];
        size_t next = 4;
        if (args.size() > 4 && args[4].rfind("--", 0) != 0) {
            if (IsBlank(args[4])) {
                return Error("target find role name must be non-empty");
            }
            target += "[name=\"" + args[4] + "\"]";
            next = 5;
        }
        params["query"] = target;
        if (next < args.size()) {
            if (args[next].rfind("--", 0) == 0) {
                return Error("unknown target find option: " + args[next]);
            }
            if (!SetParsedInt(params, "limit", args[next])) {
                return Error("target find limit must be an integer");
            }
            if (params["limit"].get<int>() <= 0) {
                return Error("target find limit must be positive");
            }
            ++next;
        }
        if (next < args.size()) {
            return Error("unknown target find option: " + args[next]);
        }
        return Ok("target_find", std::move(params));
    }

    return Error("target find only supports role targets");
}

} // namespace ComputerCpp::Cli
