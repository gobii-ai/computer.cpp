#include "DaemonTextInput.h"

#include "computer_cpp/AppPaths.h"
#include "computer_cpp/Platform.h"
#include "computer_cpp/StringUtils.h"

#include "DaemonJson.h"
#include "DaemonParsing.h"
#include "DaemonProtocol.h"
#include "DaemonTargetResolve.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <set>
#include <string>
#include <thread>

namespace ComputerCpp {
namespace {

using json = nlohmann::json;

} // namespace

std::vector<std::string> KeyChordFromParams(const json& params) {
    std::vector<std::string> keys;
    if (params.contains("keys") && params["keys"].is_array()) {
        for (const auto& key : params["keys"]) {
            keys.push_back(key.get<std::string>());
        }
        return keys;
    }
    return SplitKeyChord(params.value("keys", ""));
}

json RunWaitCommand(const json& params) {
    static const std::set<std::string> allowedParams = {
        "controlSession", "controlSessionToken", "controlScope",
        "frontmost", "timeoutMs", "pollMs", "stableScreenMs"
    };
    for (const auto& item : params.items()) {
        if (allowedParams.count(item.key()) == 0) {
            return Error("unsupported wait parameter: " + item.key(), "invalid_wait");
        }
    }
    auto timeoutMsParam = IntParam(params, "timeoutMs", 10000);
    auto pollMsParam = IntParam(params, "pollMs", 250);
    auto stableMsParam = IntParam(params, "stableScreenMs", 0);
    if (!timeoutMsParam || !pollMsParam || !stableMsParam) {
        return Error("wait requires integer timeoutMs, pollMs, and stableScreenMs", "invalid_wait");
    }
    if (*timeoutMsParam < 1 || *timeoutMsParam > 120000) {
        return Error("wait timeoutMs must be between 1 and 120000", "invalid_wait");
    }
    if (*pollMsParam < 50 || *pollMsParam > 5000) {
        return Error("wait pollMs must be between 50 and 5000", "invalid_wait");
    }
    if (*stableMsParam < 0) {
        return Error("wait stableScreenMs must be non-negative", "invalid_wait");
    }
    int timeoutMs = *timeoutMsParam;
    int pollMs = *pollMsParam;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    auto frontmostParam = StringParam(params, "frontmost", "");
    if (!frontmostParam) {
        return Error("wait frontmost must be a string", "invalid_wait");
    }
    std::string frontmost = *frontmostParam;
    if (params.contains("frontmost") && IsBlank(frontmost)) {
        return Error("wait frontmost must be non-empty when provided", "invalid_wait");
    }
    int stableMs = *stableMsParam;
    std::string lastSignature;
    auto stableSince = std::chrono::steady_clock::now();

    while (std::chrono::steady_clock::now() <= deadline) {
        bool matched = true;
        json evidence = json::object();
        if (!frontmost.empty()) {
            auto app = Platform::GetFrontmostApp();
            matched = matched && ContainsCaseInsensitive(app.name + " " + app.bundleId, frontmost);
            evidence["frontmostApp"] = AppToJson(app);
        }
        if (matched && stableMs > 0) {
            auto path = DefaultArtifactDir() / ("stable-" + std::to_string(NowMs()) + ".png");
            if (Platform::SaveScreenshot(path.string())) {
                auto fileSize = std::filesystem::file_size(path);
                std::string signature = std::to_string(fileSize);
                if (signature != lastSignature) {
                    lastSignature = signature;
                    stableSince = std::chrono::steady_clock::now();
                }
                auto elapsed =
                    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - stableSince).count();
                matched = elapsed >= stableMs;
                evidence["stableScreenMs"] = elapsed;
            } else {
                matched = false;
            }
        }
        if (matched) {
            return Ok({{"matched", true}, {"evidence", evidence}});
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(pollMs));
    }
    return Error("wait timed out", "wait_timeout");
}

json RunPressCommand(const json& params) {
    if (auto unknown = UnknownParam(params, {"keys", "holdMs", "controlSession", "controlSessionToken", "controlScope"})) {
        return Error("unknown press parameter: " + *unknown, "invalid_key");
    }
    if (params.contains("keys")) {
        if (params["keys"].is_array()) {
            if (params["keys"].empty()) {
                return Error("press keys array must be non-empty", "invalid_key");
            }
            for (const auto& key : params["keys"]) {
                if (!key.is_string()) {
                    return Error("press keys array must contain strings", "invalid_key");
                }
                if (IsBlank(key.get<std::string>())) {
                    return Error("press keys array must contain non-empty strings", "invalid_key");
                }
            }
        } else if (!params["keys"].is_string()) {
            return Error("press keys must be a string or string array", "invalid_key");
        }
    }
    auto keys = KeyChordFromParams(params);
    if (keys.empty()) {
        return Error("press requires non-empty key chord", "invalid_key");
    }
    auto holdMs = IntParam(params, "holdMs", 40);
    if (!holdMs) {
        return Error("press requires integer holdMs", "invalid_key");
    }
    if (*holdMs < 1 || *holdMs > 5000) {
        return Error("press holdMs must be between 1 and 5000", "invalid_key");
    }
    bool ok = Platform::SendHotkey(keys, *holdMs);
    if (!ok) {
        return Error("could not resolve key chord", "invalid_key");
    }
    return Ok({{"keys", keys}});
}

json RunTypeCommand(const std::string& session, const json& params) {
    if (auto unknown = UnknownParam(params, {"text", "target", "paste", "holdMs", "controlSession", "controlSessionToken", "controlScope"})) {
        return Error("unknown type parameter: " + *unknown, "invalid_type");
    }
    auto textParam = StringParam(params, "text", "");
    auto targetParam = StringParam(params, "target", "");
    auto paste = BoolParam(params, "paste", false);
    if (!textParam || !targetParam || !paste) {
        return Error("type requires string text/target and boolean paste", "invalid_type");
    }
    std::string text = *textParam;
    if (text.empty()) {
        return Error("type text must be non-empty", "invalid_type");
    }
    if (params.contains("target")) {
        auto point = PointFromTarget(session, params);
        if (point.has_value()) {
            Platform::Click(point->first, point->second, "left", 1);
            std::this_thread::sleep_for(std::chrono::milliseconds(80));
        }
    }
    auto holdMs = IntParam(params, "holdMs", 20);
    if (!holdMs) {
        return Error("type requires integer holdMs", "invalid_type");
    }
    if (*holdMs < 1 || *holdMs > 5000) {
        return Error("type holdMs must be between 1 and 5000", "invalid_type");
    }
    bool ok = *paste
        ? Platform::PasteText(text)
        : Platform::TypeText(text, *holdMs);
    if (!ok) {
        return Error("text input failed", "input_failed");
    }
    return Ok({{"characters", text.size()}});
}

json RunClipboardReadCommand() {
    std::string text;
    bool ok = Platform::ReadClipboardText(text);
    return Ok({{"available", ok}, {"text", text}});
}

json RunClipboardWriteCommand(const json& params) {
    if (auto unknown = UnknownParam(params, {"text", "controlSession", "controlSessionToken", "controlScope"})) {
        return Error("unknown clipboard write parameter: " + *unknown, "invalid_type");
    }
    auto text = StringParam(params, "text", "");
    if (!text) {
        return Error("clipboard write text must be a string", "invalid_type");
    }
    bool ok = Platform::WriteClipboardText(*text);
    return Ok({{"written", ok}});
}

json RunClipboardPasteCommand() {
    bool pasted = Platform::SendHotkey({"command", "v"}, 55);
    return Ok({{"pasted", pasted}});
}

} // namespace ComputerCpp
