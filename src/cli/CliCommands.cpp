#include "CliCommands.h"

#include "computer_cpp/StringUtils.h"

#include "CliCommandHelpers.h"

#include <nlohmann/json.hpp>
#include <array>
#include <string_view>
#include <utility>

using json = nlohmann::json;

namespace ComputerCpp::Cli {
namespace {

using CommandBuilder = CommandRequest (*)(const std::vector<std::string>&);

struct CommandRoute {
    std::string_view name;
    CommandBuilder build;
};

constexpr auto kCommandRoutes = std::to_array<CommandRoute>({
    {"app", BuildAppCommand},
    {"open", BuildOpenCommand},
    {"observe", BuildObserveCommand},
    {"target", BuildTargetCommand},
    {"get", BuildGetCommand},
    {"permissions", BuildPermissionsCommand},
    {"window", BuildWindowCommand},
    {"snapshot", BuildSnapshotCommand},
    {"screenshot", BuildScreenshotCommand},
    {"image", BuildImageCommand},
    {"click", BuildClickCommand},
    {"mouse", BuildMouseCommand},
    {"scroll", BuildScrollCommand},
    {"wait", BuildWaitCommand},
    {"press", BuildPressCommand},
    {"type", BuildTypeCommand},
    {"clipboard", BuildClipboardCommand},
});

bool IsPermissionsPane(const std::string& pane) {
    return pane == "accessibility" ||
        pane == "screen" ||
        pane == "screen-capture" ||
        pane == "screen-recording";
}

} // namespace

bool CommandRequest::ok() const {
    return error.empty();
}

CommandRequest BuildOpenCommand(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        return Error("open requires: url <http-url>");
    }
    if (args[1] != "url") {
        return Error("open requires: url <http-url>");
    }
    if (args.size() < 3) {
        return Error("open url requires http or https URL");
    }
    if (IsBlank(args[2])) {
        return Error("open url requires non-empty URL");
    }
    if (args[2].rfind("http://", 0) != 0 && args[2].rfind("https://", 0) != 0) {
        return Error("open url requires http or https URL");
    }
    json params = {{"url", args[2]}};
    for (size_t i = 3; i < args.size(); ++i) {
        if (args[i] == "--browser") {
            if (i + 1 >= args.size()) {
                return Error("open url --browser requires a value");
            }
            if (IsBlank(args[i + 1])) {
                return Error("open url --browser requires a non-empty value");
            }
            params["browser"] = args[++i];
        } else if (args[i] == "--new-window") {
            params["newWindow"] = true;
        } else if (args[i] == "--no-new-window") {
            params["newWindow"] = false;
        } else if (args[i] == "--new-instance") {
            params["newInstance"] = true;
        } else {
            return Error("unknown open url option: " + args[i]);
        }
    }
    return Ok("open_url", std::move(params));
}

CommandRequest BuildAppCommand(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        return Error("app requires subcommand");
    }
    if (args[1] == "active") {
        if (args.size() > 2) {
            return Error("unknown app active option: " + args[2]);
        }
        return Ok("app_active", json::object());
    }
    if (args[1] == "activate-pid") {
        if (args.size() < 3) {
            return Error("app activate-pid requires pid");
        }
        json params = json::object();
        if (!SetParsedInt(params, "pid", args[2])) {
            return Error("app activate-pid requires an integer pid");
        }
        if (params["pid"].get<int>() <= 0) {
            return Error("app activate-pid requires positive pid");
        }
        if (args.size() > 3) {
            return Error("unknown app activate-pid option: " + args[3]);
        }
        return Ok("app_activate_pid", std::move(params));
    }
    if (args[1] == "launch" || args[1] == "activate") {
        if (args.size() < 3) {
            return Error("app launch requires app name or bundle id");
        }
        if (args.size() > 3) {
            return Error("unknown app launch option: " + args[3]);
        }
        if (IsBlank(args[2])) {
            return Error("app launch requires non-empty app name or bundle id");
        }
        return Ok("app_launch", {{"query", args[2]}});
    }
    return Error("unknown app subcommand");
}

CommandRequest BuildGetCommand(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        return Error("get requires a ref");
    }
    if (args.size() > 3) {
        return Error("unknown get option: " + args[3]);
    }
    if (IsBlank(args[1])) {
        return Error("get requires non-empty ref");
    }
    std::string field = args.size() > 2 ? args[2] : "all";
    if (field != "text" && field != "value" && field != "bounds" && field != "all") {
        return Error("get field must be text, value, bounds, or all");
    }
    return Ok("get", {
        {"target", args[1]},
        {"field", std::move(field)},
    });
}

CommandRequest BuildPermissionsCommand(const std::vector<std::string>& args) {
    if (args.size() > 1 && args[1] == "open-settings") {
        json params = json::object();
        if (args.size() > 2) {
            if (IsBlank(args[2])) {
                return Error("permissions open-settings pane must be non-empty");
            }
            if (!IsPermissionsPane(args[2])) {
                return Error("permissions open-settings pane must be accessibility, screen, screen-capture, or screen-recording");
            }
            params["pane"] = args[2];
        }
        if (args.size() > 3) {
            return Error("unknown permissions open-settings option: " + args[3]);
        }
        return Ok("open_permissions", std::move(params));
    }
    json params = {{"request", false}};
    for (size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--request") {
            params["request"] = true;
        } else {
            return Error("unknown permissions option: " + args[i]);
        }
    }
    return Ok("permissions", std::move(params));
}

CommandRequest BuildImageCommand(const std::vector<std::string>& args) {
    if (args.size() < 3 || !IsOneOf(args[1], {"info", "split"})) {
        return Error("image requires: info <path> | split <path> [--out-dir dir|--output-dir dir] [--chunk-height n] [--overlap n] [--prefix p]");
    }
    if (IsBlank(args[2])) {
        return Error("image path must be non-empty");
    }

    json params = {{"path", args[2]}};
    if (args[1] == "info") {
        if (args.size() > 3) {
            return Error("unknown image info option: " + args[3]);
        }
        return Ok("image_info", std::move(params));
    }

    for (size_t i = 3; i < args.size(); ++i) {
        if (IsOneOf(args[i], {"--out-dir", "--output-dir"})) {
            const std::string option = args[i];
            if (!SetStringOption(params, "outDir", args, i, {"--out-dir", "--output-dir"})) {
                return Error("image split " + option + " requires a directory");
            }
            if (IsBlank(params["outDir"].get<std::string>())) {
                return Error("image split " + option + " must be non-empty");
            }
        } else if (args[i] == "--chunk-height") {
            if (i + 1 >= args.size()) {
                return Error("image split --chunk-height requires a value");
            }
            if (!SetIntOption(params, "chunkHeight", args, i, {"--chunk-height"})) {
                return Error("image split --chunk-height requires an integer");
            }
            if (params["chunkHeight"].get<int>() <= 0) {
                return Error("image split --chunk-height must be positive");
            }
        } else if (args[i] == "--overlap") {
            if (i + 1 >= args.size()) {
                return Error("image split --overlap requires a value");
            }
            if (!SetIntOption(params, "overlap", args, i, {"--overlap"})) {
                return Error("image split --overlap requires an integer");
            }
            if (params["overlap"].get<int>() < 0) {
                return Error("image split --overlap must be non-negative");
            }
        } else if (args[i] == "--prefix") {
            if (!SetStringOption(params, "prefix", args, i, {"--prefix"})) {
                return Error("image split --prefix requires a value");
            }
            if (IsBlank(params["prefix"].get<std::string>())) {
                return Error("image split --prefix must be non-empty");
            }
        } else {
            return Error("unknown image split option: " + args[i]);
        }
    }
    if (params.contains("chunkHeight") && params.contains("overlap") &&
        params["overlap"].get<int>() >= params["chunkHeight"].get<int>()) {
        return Error("image split --overlap must be smaller than --chunk-height");
    }
    return Ok("image_split", std::move(params));
}

CommandRequest BuildWindowCommand(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        return Error("window requires: bounds|active|list|close");
    }

    if (args[1] == "active") {
        if (args.size() > 2) {
            return Error("unknown window active option: " + args[2]);
        }
        return Ok("window_active", json::object());
    }

    if (args[1] == "list") {
        json params = json::object();
        if (args.size() > 2) {
            if (IsBlank(args[2])) {
                return Error("window list app must be non-empty");
            }
            params["app"] = args[2];
        }
        if (args.size() > 3) {
            return Error("unknown window list option: " + args[3]);
        }
        return Ok("window_list", std::move(params));
    }

    if (args[1] == "close") {
        json params = json::object();
        if (args.size() == 2) {
            params["frontmost"] = true;
        } else if (args[2] == "--frontmost") {
            params["frontmost"] = true;
            if (args.size() > 3) {
                return Error("unknown window close option: " + args[3]);
            }
        } else {
            if (IsBlank(args[2])) {
                return Error("window close id must be non-empty");
            }
            params["id"] = args[2];
            params["frontmost"] = false;
            if (args.size() > 3) {
                return Error("unknown window close option: " + args[3]);
            }
        }
        return Ok("window_close", std::move(params));
    }

    if (args[1] != "bounds") {
        return Error("window requires: bounds|active|list|close");
    }
    if (args.size() < 6) {
        return Error("window bounds requires x y width height");
    }

    json params = json::object();
    if (!SetParsedDouble(params, "x", args[2]) ||
        !SetParsedDouble(params, "y", args[3]) ||
        !SetParsedDouble(params, "width", args[4]) ||
        !SetParsedDouble(params, "height", args[5])) {
        return Error("window bounds requires numeric x y width height");
    }
    if (params["width"].get<double>() <= 0.0 || params["height"].get<double>() <= 0.0) {
        return Error("window bounds requires positive width and height");
    }
    for (size_t i = 6; i < args.size(); ++i) {
        if (args[i] == "--pid") {
            if (i + 1 >= args.size()) {
                return Error("window bounds --pid requires a value");
            }
            if (!SetParsedInt(params, "pid", args[i + 1])) {
                return Error("window bounds --pid requires an integer");
            }
            if (params["pid"].get<int>() < 0) {
                return Error("window bounds --pid must be non-negative");
            }
            ++i;
        } else {
            return Error("unknown window bounds option: " + args[i]);
        }
    }
    return Ok("window_bounds", std::move(params));
}

CommandRequest BuildSnapshotCommand(const std::vector<std::string>& args) {
    json params = {
        {"interactive", false},
        {"bounds", false},
        {"actions", false},
    };
    for (size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--max-depth") {
            if (i + 1 >= args.size()) {
                return Error("snapshot --max-depth requires a value");
            }
            if (!SetParsedInt(params, "maxDepth", args[i + 1])) {
                return Error("snapshot --max-depth requires an integer");
            }
            if (params["maxDepth"].get<int>() < 0) {
                return Error("snapshot --max-depth must be non-negative");
            }
            ++i;
        } else if (args[i] == "--max-nodes") {
            if (i + 1 >= args.size()) {
                return Error("snapshot --max-nodes requires a value");
            }
            if (!SetParsedInt(params, "maxNodes", args[i + 1])) {
                return Error("snapshot --max-nodes requires an integer");
            }
            if (params["maxNodes"].get<int>() < 0) {
                return Error("snapshot --max-nodes must be non-negative");
            }
            ++i;
        } else if (args[i] == "-i" || args[i] == "--interactive") {
            params["interactive"] = true;
        } else if (args[i] == "--with-bounds") {
            params["bounds"] = true;
        } else if (args[i] == "--with-actions") {
            params["actions"] = true;
        } else {
            return Error("unknown snapshot option: " + args[i]);
        }
    }
    return Ok("snapshot", std::move(params));
}

CommandRequest BuildScreenshotCommand(const std::vector<std::string>& args) {
    json params = json::object();
    params["frontmostWindowOnly"] = false;
    bool pathSet = false;
    auto setPath = [&](const std::string& path) -> std::optional<std::string> {
        if (pathSet) {
            return std::string("screenshot accepts at most one path");
        }
        if (!path.empty() && IsBlank(path)) {
            return std::string("screenshot path must be non-empty");
        }
        params["path"] = path;
        pathSet = true;
        return std::nullopt;
    };
    for (size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--max-dim") {
            if (i + 1 >= args.size()) {
                return Error("screenshot --max-dim requires a value");
            }
            if (!SetParsedInt(params, "maxDimension", args[i + 1])) {
                return Error("screenshot --max-dim requires an integer");
            }
            if (params["maxDimension"].get<int>() < 0 || params["maxDimension"].get<int>() > 8192) {
                return Error("screenshot --max-dim must be between 0 and 8192");
            }
            ++i;
        } else if (args[i] == "--region") {
            if (i + 4 >= args.size()) {
                return Error("screenshot --region requires x y width height");
            }
            if (!SetParsedDouble(params, "x", args[i + 1]) ||
                !SetParsedDouble(params, "y", args[i + 2]) ||
                !SetParsedDouble(params, "width", args[i + 3]) ||
                !SetParsedDouble(params, "height", args[i + 4])) {
                return Error("screenshot --region requires numeric x y width height");
            }
            if (params["width"].get<double>() <= 0.0 || params["height"].get<double>() <= 0.0) {
                return Error("screenshot --region requires positive width and height");
            }
            i += 4;
        } else if (args[i] == "--frontmost-window") {
            params["frontmostWindowOnly"] = true;
        } else if (args[i] == "--output" || args[i] == "--path") {
            if (i + 1 >= args.size()) {
                return Error("screenshot " + args[i] + " requires a path");
            }
            if (auto error = setPath(args[i + 1])) {
                return Error(*error);
            }
            ++i;
        } else if (args[i].rfind("--", 0) == 0) {
            return Error("unknown screenshot option: " + args[i]);
        } else {
            if (auto error = setPath(args[i])) {
                return Error(*error);
            }
        }
    }
    return Ok("screenshot", std::move(params));
}

CommandRequest BuildDaemonCommand(const std::vector<std::string>& args, const std::string& batchInput) {
    if (args.empty()) {
        return Error("missing command");
    }

    const std::string& cmd = args[0];
    auto noArgs = [&](const std::string& method) -> CommandRequest {
        if (args.size() > 1) {
            return Error("unknown " + cmd + " option: " + args[1]);
        }
        return Ok(method, json::object());
    };
    if (cmd == "ping" || cmd == "capabilities" || cmd == "schema") {
        return noArgs(cmd);
    }
    if (cmd == "doctor" || cmd == "state") {
        return noArgs("state");
    }
    if (cmd == "batch") {
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] != "--continue-on-error") {
                return Error("unknown batch option: " + args[i]);
            }
        }
        if (batchInput.empty()) {
            return Error("batch requires a JSON array on stdin");
        }
        auto parsed = json::parse(batchInput, nullptr, false);
        if (!parsed.is_array()) {
            return Error("batch stdin must be a JSON array");
        }
        return Ok("batch", {
            {"steps", std::move(parsed)},
            {"stopOnError", !HasFlag(args, "--continue-on-error")},
        });
    }
    for (const auto& route : kCommandRoutes) {
        if (cmd == route.name) {
            return route.build(args);
        }
    }
    return Error("unknown command: " + cmd);
}

} // namespace ComputerCpp::Cli
