#include "CliCommands.h"

#include "computer_cpp/StringUtils.h"

#include "CliCommandHelpers.h"

#include <nlohmann/json.hpp>
#include <optional>
#include <utility>

using json = nlohmann::json;

namespace ComputerCpp::Cli {

CommandRequest BuildPressCommand(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        return Error("press requires key chord");
    }
    if (IsBlank(args[1])) {
        return Error("press requires non-empty key chord");
    }
    json params = {{"keys", args[1]}};
    for (size_t i = 2; i < args.size(); ++i) {
        if (args[i] == "--hold-ms") {
            if (i + 1 >= args.size()) {
                return Error("press --hold-ms requires a value");
            }
            if (!SetParsedInt(params, "holdMs", args[i + 1])) {
                return Error("press --hold-ms requires an integer");
            }
            if (params["holdMs"].get<int>() < 1 || params["holdMs"].get<int>() > 5000) {
                return Error("press --hold-ms must be between 1 and 5000");
            }
            ++i;
        } else {
            return Error("unknown press option: " + args[i]);
        }
    }
    return Ok("press", std::move(params));
}

CommandRequest BuildTypeCommand(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        return Error("type requires text");
    }
    if (args[1].empty()) {
        return Error("type text must be non-empty");
    }
    json params = {{"text", args[1]}, {"paste", false}};
    for (size_t i = 2; i < args.size(); ++i) {
        if (args[i] == "--paste") {
            params["paste"] = true;
        } else if (args[i] == "--target") {
            if (i + 1 >= args.size()) {
                return Error("type --target requires a target");
            }
            if (IsBlank(args[i + 1])) {
                return Error("type --target requires a non-empty target");
            }
            params["target"] = args[++i];
        } else if (args[i] == "--hold-ms") {
            if (i + 1 >= args.size()) {
                return Error("type --hold-ms requires a value");
            }
            if (!SetParsedInt(params, "holdMs", args[i + 1])) {
                return Error("type --hold-ms requires an integer");
            }
            if (params["holdMs"].get<int>() < 1 || params["holdMs"].get<int>() > 5000) {
                return Error("type --hold-ms must be between 1 and 5000");
            }
            ++i;
        } else {
            return Error("unknown type option: " + args[i]);
        }
    }
    return Ok("type", std::move(params));
}

CommandRequest BuildClickCommand(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        return Error("click requires a target");
    }
    if (IsBlank(args[1])) {
        return Error("click target must be non-empty");
    }

    json params = {{"target", args[1]}};
    auto setIntRange = [&](const std::string& option, const std::string& key, const std::string& value, int min, int max) -> std::optional<std::string> {
        if (!SetParsedInt(params, key, value)) {
            return "click " + option + " requires an integer";
        }
        if (params[key].get<int>() < min || params[key].get<int>() > max) {
            return "click " + option + " must be between " + std::to_string(min) + " and " + std::to_string(max);
        }
        return std::nullopt;
    };
    auto setFraction = [&](const std::string& option, const std::string& key, const std::string& value) -> std::optional<std::string> {
        if (!SetParsedDouble(params, key, value)) {
            return "click " + option + " requires a number";
        }
        if (params[key].get<double>() < 0.05 || params[key].get<double>() > 0.95) {
            return "click " + option + " must be between 0.05 and 0.95";
        }
        return std::nullopt;
    };
    for (size_t i = 2; i < args.size(); ++i) {
        if (args[i] == "--button") {
            if (i + 1 >= args.size()) {
                return Error("click --button requires a value");
            }
            if (IsBlank(args[i + 1])) {
                return Error("click --button requires a non-empty value");
            }
            params["button"] = args[++i];
        } else if (args[i] == "--count") {
            if (i + 1 >= args.size()) {
                return Error("click --count requires a value");
            }
            if (!SetParsedInt(params, "clickCount", args[i + 1])) {
                return Error("click --count requires an integer");
            }
            if (params["clickCount"].get<int>() < 1 || params["clickCount"].get<int>() > 5) {
                return Error("click --count must be between 1 and 5");
            }
            ++i;
        } else if (args[i] == "--duration-ms") {
            if (i + 1 >= args.size()) {
                return Error("click --duration-ms requires a value");
            }
            if (!SetParsedInt(params, "durationMs", args[i + 1])) {
                return Error("click --duration-ms requires an integer");
            }
            if (params["durationMs"].get<int>() < 0 || params["durationMs"].get<int>() > 5000) {
                return Error("click --duration-ms must be between 0 and 5000");
            }
            ++i;
        } else if (args[i] == "--steps") {
            if (i + 1 >= args.size()) {
                return Error("click --steps requires a value");
            }
            if (!SetParsedInt(params, "steps", args[i + 1])) {
                return Error("click --steps requires an integer");
            }
            if (params["steps"].get<int>() < 0 || params["steps"].get<int>() > 120) {
                return Error("click --steps must be between 0 and 120");
            }
            ++i;
        } else if (args[i] == "--motion") {
            if (i + 1 >= args.size()) {
                return Error("click --motion requires a value");
            }
            if (IsBlank(args[i + 1])) {
                return Error("click --motion requires a non-empty value");
            }
            params["motion"] = args[++i];
        } else if (args[i] == "--click-hold-ms") {
            if (i + 1 >= args.size()) {
                return Error("click --click-hold-ms requires a value");
            }
            if (!SetParsedInt(params, "clickHoldMs", args[i + 1])) {
                return Error("click --click-hold-ms requires an integer");
            }
            if (params["clickHoldMs"].get<int>() < -1 || params["clickHoldMs"].get<int>() > 5000) {
                return Error("click --click-hold-ms must be between -1 and 5000");
            }
            ++i;
        } else if (args[i] == "--pre-click-settle-ms") {
            if (i + 1 >= args.size()) {
                return Error("click --pre-click-settle-ms requires a value");
            }
            if (!SetParsedInt(params, "preClickSettleMs", args[i + 1])) {
                return Error("click --pre-click-settle-ms requires an integer");
            }
            if (params["preClickSettleMs"].get<int>() < -1 || params["preClickSettleMs"].get<int>() > 5000) {
                return Error("click --pre-click-settle-ms must be between -1 and 5000");
            }
            ++i;
        } else if (args[i] == "--hover-safe") {
            params["hoverSafe"] = true;
            params["motion"] = "hover_safe";
        } else if (args[i] == "--park-before-click") {
            params["parkBeforeClick"] = true;
        } else if (args[i] == "--no-park-before-click") {
            params["parkBeforeClick"] = false;
        } else if (args[i] == "--park-duration-ms") {
            if (i + 1 >= args.size()) {
                return Error("click --park-duration-ms requires a value");
            }
            if (auto error = setIntRange("--park-duration-ms", "parkDurationMs", args[i + 1], 0, 1200)) {
                return Error(*error);
            }
            ++i;
        } else if (args[i] == "--park-steps") {
            if (i + 1 >= args.size()) {
                return Error("click --park-steps requires a value");
            }
            if (auto error = setIntRange("--park-steps", "parkSteps", args[i + 1], 0, 80)) {
                return Error(*error);
            }
            ++i;
        } else if (args[i] == "--park-x-fraction") {
            if (i + 1 >= args.size()) {
                return Error("click --park-x-fraction requires a value");
            }
            if (auto error = setFraction("--park-x-fraction", "parkXFraction", args[i + 1])) {
                return Error(*error);
            }
            ++i;
        } else if (args[i] == "--park-y-fraction") {
            if (i + 1 >= args.size()) {
                return Error("click --park-y-fraction requires a value");
            }
            if (auto error = setFraction("--park-y-fraction", "parkYFraction", args[i + 1])) {
                return Error(*error);
            }
            ++i;
        } else if (args[i] == "--rect-click-x-fraction") {
            if (i + 1 >= args.size()) {
                return Error("click --rect-click-x-fraction requires a value");
            }
            if (auto error = setFraction("--rect-click-x-fraction", "rectClickXFraction", args[i + 1])) {
                return Error(*error);
            }
            ++i;
        } else if (args[i] == "--rect-click-y-fraction") {
            if (i + 1 >= args.size()) {
                return Error("click --rect-click-y-fraction requires a value");
            }
            if (auto error = setFraction("--rect-click-y-fraction", "rectClickYFraction", args[i + 1])) {
                return Error(*error);
            }
            ++i;
        } else if (args[i] == "--instant") {
            params["motion"] = "instant";
        } else {
            return Error("unknown click option: " + args[i]);
        }
    }
    return Ok("click", std::move(params));
}

CommandRequest BuildMouseCommand(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        return Error("mouse requires subcommand");
    }

    json params = json::object();
    if (args[1] == "move") {
        if (args.size() < 4) {
            return Error("mouse move requires x y");
        }
        if (!SetParsedDouble(params, "x", args[2]) ||
            !SetParsedDouble(params, "y", args[3])) {
            return Error("mouse move requires numeric x y");
        }
        for (size_t i = 4; i < args.size(); ++i) {
            if (args[i] == "--duration-ms") {
                if (i + 1 >= args.size()) {
                    return Error("mouse move --duration-ms requires a value");
                }
                if (!SetParsedInt(params, "durationMs", args[i + 1])) {
                    return Error("mouse move --duration-ms requires an integer");
                }
                if (params["durationMs"].get<int>() < 0) {
                    return Error("mouse move --duration-ms must be non-negative");
                }
                ++i;
            } else if (args[i] == "--steps") {
                if (i + 1 >= args.size()) {
                    return Error("mouse move --steps requires a value");
                }
                if (!SetParsedInt(params, "steps", args[i + 1])) {
                    return Error("mouse move --steps requires an integer");
                }
                if (params["steps"].get<int>() < 0) {
                    return Error("mouse move --steps must be non-negative");
                }
                ++i;
            } else if (args[i] == "--observe") {
                params["observe"] = true;
            } else {
                return Error("unknown mouse move option: " + args[i]);
            }
        }
        return Ok("mouse_move", std::move(params));
    }

    if (args[1] == "drag") {
        if (args.size() < 6) {
            return Error("mouse drag requires from-x from-y to-x to-y");
        }
        if (!ParseDouble(args[2]) || !ParseDouble(args[3]) ||
            !ParseDouble(args[4]) || !ParseDouble(args[5])) {
            return Error("mouse drag requires numeric from-x from-y to-x to-y");
        }
        params["from"] = "point:" + args[2] + "," + args[3];
        params["to"] = "point:" + args[4] + "," + args[5];
        for (size_t i = 6; i < args.size(); ++i) {
            if (args[i] == "--button") {
                if (i + 1 >= args.size()) {
                    return Error("mouse drag --button requires a value");
                }
                if (IsBlank(args[i + 1])) {
                    return Error("mouse drag --button requires a non-empty value");
                }
                params["button"] = args[++i];
            } else if (args[i] == "--duration-ms") {
                if (i + 1 >= args.size()) {
                    return Error("mouse drag --duration-ms requires a value");
                }
                if (!SetParsedInt(params, "durationMs", args[i + 1])) {
                    return Error("mouse drag --duration-ms requires an integer");
                }
                if (params["durationMs"].get<int>() < 0) {
                    return Error("mouse drag --duration-ms must be non-negative");
                }
                ++i;
            } else if (args[i] == "--steps") {
                if (i + 1 >= args.size()) {
                    return Error("mouse drag --steps requires a value");
                }
                if (!SetParsedInt(params, "steps", args[i + 1])) {
                    return Error("mouse drag --steps requires an integer");
                }
                if (params["steps"].get<int>() < 0) {
                    return Error("mouse drag --steps must be non-negative");
                }
                ++i;
            } else if (args[i] == "--observe") {
                params["observe"] = true;
            } else {
                return Error("unknown mouse drag option: " + args[i]);
            }
        }
        return Ok("mouse_drag", std::move(params));
    }

    if (args[1] == "click") {
        if (args.size() < 4) {
            return Error("mouse click requires x y");
        }
        if (!ParseDouble(args[2]) || !ParseDouble(args[3])) {
            return Error("mouse click requires numeric x y");
        }
        params["target"] = "point:" + args[2] + "," + args[3];
        for (size_t i = 4; i < args.size(); ++i) {
            if (args[i] == "--button") {
                if (i + 1 >= args.size()) {
                    return Error("mouse click --button requires a value");
                }
                if (IsBlank(args[i + 1])) {
                    return Error("mouse click --button requires a non-empty value");
                }
                params["button"] = args[++i];
            } else if (args[i] == "--count") {
                if (i + 1 >= args.size()) {
                    return Error("mouse click --count requires a value");
                }
                if (!SetParsedInt(params, "clickCount", args[i + 1])) {
                    return Error("mouse click --count requires an integer");
                }
                if (params["clickCount"].get<int>() < 1 || params["clickCount"].get<int>() > 5) {
                    return Error("mouse click --count must be between 1 and 5");
                }
                ++i;
            } else if (args[i] == "--duration-ms") {
                if (i + 1 >= args.size()) {
                    return Error("mouse click --duration-ms requires a value");
                }
                if (!SetParsedInt(params, "durationMs", args[i + 1])) {
                    return Error("mouse click --duration-ms requires an integer");
                }
                if (params["durationMs"].get<int>() < 0 || params["durationMs"].get<int>() > 5000) {
                    return Error("mouse click --duration-ms must be between 0 and 5000");
                }
                ++i;
            } else if (args[i] == "--steps") {
                if (i + 1 >= args.size()) {
                    return Error("mouse click --steps requires a value");
                }
                if (!SetParsedInt(params, "steps", args[i + 1])) {
                    return Error("mouse click --steps requires an integer");
                }
                if (params["steps"].get<int>() < 0 || params["steps"].get<int>() > 120) {
                    return Error("mouse click --steps must be between 0 and 120");
                }
                ++i;
            } else if (args[i] == "--motion") {
                if (i + 1 >= args.size()) {
                    return Error("mouse click --motion requires a value");
                }
                if (IsBlank(args[i + 1])) {
                    return Error("mouse click --motion requires a non-empty value");
                }
                params["motion"] = args[++i];
            } else if (args[i] == "--instant") {
                params["motion"] = "instant";
            } else {
                return Error("unknown mouse click option: " + args[i]);
            }
        }
        return Ok("click", std::move(params));
    }

    if (args[1] == "down") {
        params["button"] = "left";
        size_t next = 2;
        if (next < args.size() && args[next].rfind("--", 0) != 0) {
            if (IsBlank(args[next])) {
                return Error("mouse down button must be non-empty");
            }
            params["button"] = args[next++];
        }
        for (size_t i = next; i < args.size(); ++i) {
            if (args[i] == "--count") {
                if (i + 1 >= args.size()) {
                    return Error("mouse down --count requires a value");
                }
                if (!SetParsedInt(params, "clickCount", args[i + 1])) {
                    return Error("mouse down --count requires an integer");
                }
                if (params["clickCount"].get<int>() < 1 || params["clickCount"].get<int>() > 5) {
                    return Error("mouse down --count must be between 1 and 5");
                }
                ++i;
            } else {
                return Error("unknown mouse down option: " + args[i]);
            }
        }
        return Ok("mouse_down", std::move(params));
    }

    if (args[1] == "up") {
        params["button"] = "left";
        size_t next = 2;
        if (next < args.size() && args[next].rfind("--", 0) != 0) {
            if (IsBlank(args[next])) {
                return Error("mouse up button must be non-empty");
            }
            params["button"] = args[next++];
        }
        for (size_t i = next; i < args.size(); ++i) {
            if (args[i] == "--count") {
                if (i + 1 >= args.size()) {
                    return Error("mouse up --count requires a value");
                }
                if (!SetParsedInt(params, "clickCount", args[i + 1])) {
                    return Error("mouse up --count requires an integer");
                }
                if (params["clickCount"].get<int>() < 1 || params["clickCount"].get<int>() > 5) {
                    return Error("mouse up --count must be between 1 and 5");
                }
                ++i;
            } else {
                return Error("unknown mouse up option: " + args[i]);
            }
        }
        return Ok("mouse_up", std::move(params));
    }

    return Error("unknown mouse subcommand");
}

CommandRequest BuildWaitCommand(const std::vector<std::string>& args) {
    json params = json::object();
    for (size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--text") {
            return Error("wait --text was removed");
        } else if (args[i] == "--frontmost") {
            if (i + 1 >= args.size()) {
                return Error("wait --frontmost requires an app name");
            }
            if (IsBlank(args[i + 1])) {
                return Error("wait --frontmost requires a non-empty app name");
            }
            params["frontmost"] = args[++i];
        } else if (args[i] == "--frontmost-window") {
            return Error("wait --frontmost-window was removed with text waits");
        } else if (args[i] == "--stable-screen") {
            if (i + 1 >= args.size()) {
                return Error("wait --stable-screen requires a value");
            }
            if (!SetParsedInt(params, "stableScreenMs", args[i + 1])) {
                return Error("wait --stable-screen requires an integer");
            }
            if (params["stableScreenMs"].get<int>() < 0) {
                return Error("wait --stable-screen must be non-negative");
            }
            ++i;
        } else if (args[i] == "--timeout-ms") {
            if (i + 1 >= args.size()) {
                return Error("wait --timeout-ms requires a value");
            }
            if (!SetParsedInt(params, "timeoutMs", args[i + 1])) {
                return Error("wait --timeout-ms requires an integer");
            }
            if (params["timeoutMs"].get<int>() < 1 || params["timeoutMs"].get<int>() > 120000) {
                return Error("wait --timeout-ms must be between 1 and 120000");
            }
            ++i;
        } else if (args[i] == "--poll-ms") {
            if (i + 1 >= args.size()) {
                return Error("wait --poll-ms requires a value");
            }
            if (!SetParsedInt(params, "pollMs", args[i + 1])) {
                return Error("wait --poll-ms requires an integer");
            }
            if (params["pollMs"].get<int>() < 50 || params["pollMs"].get<int>() > 5000) {
                return Error("wait --poll-ms must be between 50 and 5000");
            }
            ++i;
        } else {
            return Error("unknown wait option: " + args[i]);
        }
    }
    return Ok("wait", std::move(params));
}

CommandRequest BuildClipboardCommand(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        return Error("clipboard requires subcommand");
    }
    if (args[1] == "read") {
        if (args.size() > 2) {
            return Error("unknown clipboard read option: " + args[2]);
        }
        return Ok("clipboard_read", json::object());
    }
    if (args[1] == "write") {
        if (args.size() < 3) {
            return Error("clipboard write requires text");
        }
        if (args.size() > 3) {
            return Error("unknown clipboard write option: " + args[3]);
        }
        return Ok("clipboard_write", {{"text", args[2]}});
    }
    if (args[1] == "paste") {
        if (args.size() > 2) {
            return Error("unknown clipboard paste option: " + args[2]);
        }
        return Ok("clipboard_paste", json::object());
    }
    return Error("unknown clipboard subcommand");
}

CommandRequest BuildScrollCommand(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        return Error("scroll requires dy");
    }

    json params = json::object();
    size_t optionStart = 2;
    if (args[1] == "read") {
        const bool hasDirection = args.size() > 2 && args[2].rfind("--", 0) != 0;
        std::string direction = hasDirection ? args[2] : "down";
        if (direction != "down" && direction != "up") {
            return Error("scroll read direction must be down or up");
        }
        params["dy"] = direction == "up" ? 200 : -200;
        params["dx"] = 0;
        params["durationMs"] = 1000;
        params["steps"] = 40;
        params["jitter"] = 0.08;
        params["samples"] = 4;
        params["centerAnchor"] = true;
        optionStart = hasDirection ? 3 : 2;
    } else {
        if (!SetParsedInt(params, "dy", args[1])) {
            return Error("scroll dy must be an integer");
        }
        const bool hasDx = args.size() > 2 && args[2].rfind("--", 0) != 0;
        if (hasDx) {
            if (!SetParsedInt(params, "dx", args[2])) {
                return Error("scroll dx must be an integer");
            }
        } else {
            params["dx"] = 0;
        }
        optionStart = hasDx ? 3 : 2;
    }

    for (size_t i = optionStart; i < args.size(); ++i) {
        if (args[i] == "--duration-ms") {
            if (i + 1 >= args.size()) {
                return Error("scroll --duration-ms requires a value");
            }
            if (!SetParsedInt(params, "durationMs", args[i + 1])) {
                return Error("scroll --duration-ms requires an integer");
            }
            if (params["durationMs"].get<int>() < 0) {
                return Error("scroll --duration-ms must be non-negative");
            }
            ++i;
        } else if (args[i] == "--steps") {
            if (i + 1 >= args.size()) {
                return Error("scroll --steps requires a value");
            }
            if (!SetParsedInt(params, "steps", args[i + 1])) {
                return Error("scroll --steps requires an integer");
            }
            if (params["steps"].get<int>() < 0) {
                return Error("scroll --steps must be non-negative");
            }
            ++i;
        } else if (args[i] == "--jitter") {
            if (i + 1 >= args.size()) {
                return Error("scroll --jitter requires a value");
            }
            if (!SetParsedDouble(params, "jitter", args[i + 1])) {
                return Error("scroll --jitter requires a number");
            }
            if (params["jitter"].get<double>() < 0.0) {
                return Error("scroll --jitter must be non-negative");
            }
            ++i;
        } else if (args[i] == "--samples") {
            if (i + 1 >= args.size()) {
                return Error("scroll --samples requires a value");
            }
            if (!SetParsedInt(params, "samples", args[i + 1])) {
                return Error("scroll --samples requires an integer");
            }
            if (params["samples"].get<int>() < 1 || params["samples"].get<int>() > 6) {
                return Error("scroll --samples must be between 1 and 6");
            }
            ++i;
        } else if (args[i] == "--max-gesture-delta" || args[i] == "--max-scroll-gesture-delta") {
            if (i + 1 >= args.size()) {
                return Error("scroll " + args[i] + " requires a value");
            }
            if (!SetParsedInt(params, "maxGestureDelta", args[i + 1])) {
                return Error("scroll " + args[i] + " requires an integer");
            }
            if (params["maxGestureDelta"].get<int>() < 0) {
                return Error("scroll " + args[i] + " must be non-negative");
            }
            ++i;
        } else if (args[i] == "--humanize") {
            params["humanize"] = true;
        } else if (args[i] == "--no-humanize") {
            params["humanize"] = false;
        } else if (args[i] == "--at") {
            if (i + 1 >= args.size()) {
                return Error("scroll --at requires a target");
            }
            if (IsBlank(args[i + 1])) {
                return Error("scroll --at requires a non-empty target");
            }
            params["at"] = args[++i];
        } else if (args[i] == "--at-offset") {
            if (i + 2 >= args.size()) {
                return Error("scroll --at-offset requires integer dx dy");
            }
            if (!SetParsedInt(params, "atOffsetX", args[i + 1]) ||
                !SetParsedInt(params, "atOffsetY", args[i + 2])) {
                return Error("scroll --at-offset requires integer dx dy");
            }
            i += 2;
        } else if (args[i] == "--focus") {
            if (i + 1 >= args.size()) {
                return Error("scroll --focus requires an app name");
            }
            if (IsBlank(args[i + 1])) {
                return Error("scroll --focus requires a non-empty app name");
            }
            params["focusApp"] = args[++i];
        } else if (args[i] == "--no-anchor") {
            params["anchor"] = false;
        } else if (args[i] == "--center-anchor") {
            params["centerAnchor"] = true;
        } else if (args[i] == "--no-center-anchor") {
            params["centerAnchor"] = false;
        } else if (args[i] == "--observe") {
            params["observe"] = true;
        } else {
            return Error("unknown scroll option: " + args[i]);
        }
    }
    if (!params.contains("anchor")) {
        params["anchor"] = true;
    }
    if (!params.contains("observe")) {
        params["observe"] = false;
    }
    return Ok("scroll", std::move(params));
}

} // namespace ComputerCpp::Cli
