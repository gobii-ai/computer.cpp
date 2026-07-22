#include "computer_cpp/Daemon.h"
#include "computer_cpp/Timeline.h"

#include "TestSupport.h"

#include <cassert>
#include <string>
#include <utility>
#include <vector>

namespace {

void TestDaemonDispatch() {
    auto response = ComputerCpp::HandleDaemonRequest("unit", {
        {"id", "ping"},
        {"method", "ping"},
        {"params", nlohmann::json::object()}
    });
    assert(response["ok"] == true);
    assert(response["id"] == "ping");
    assert(response["data"]["message"] == "pong");

    auto unknown = ComputerCpp::HandleDaemonRequest("unit", {
        {"id", "unknown-check"},
        {"method", "does_not_exist"},
        {"params", nlohmann::json::object()}
    });
    assert(unknown["ok"] == false);
    assert(unknown["id"] == "unknown-check");

    auto nonObjectRequest = ComputerCpp::HandleDaemonRequest("unit", nlohmann::json::array());
    assert(nonObjectRequest["ok"] == false);
    assert(nonObjectRequest["code"] == "bad_request");

    auto nonStringId = ComputerCpp::HandleDaemonRequest("unit", {
        {"id", true},
        {"method", "ping"},
        {"params", nlohmann::json::object()}
    });
    assert(nonStringId["ok"] == false);
    assert(nonStringId["code"] == "bad_request");

    auto emptyId = ComputerCpp::HandleDaemonRequest("unit", {
        {"id", ""},
        {"method", "ping"},
        {"params", nlohmann::json::object()}
    });
    assert(emptyId["ok"] == false);
    assert(emptyId["code"] == "bad_request");

    auto blankId = ComputerCpp::HandleDaemonRequest("unit", {
        {"id", "   "},
        {"method", "ping"},
        {"params", nlohmann::json::object()}
    });
    assert(blankId["ok"] == false);
    assert(blankId["code"] == "bad_request");

    auto nonStringMethod = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", true},
        {"params", nlohmann::json::object()}
    });
    assert(nonStringMethod["ok"] == false);
    assert(nonStringMethod["code"] == "bad_request");

    auto blankMethod = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "   "},
        {"params", nlohmann::json::object()}
    });
    assert(blankMethod["ok"] == false);
    assert(blankMethod["code"] == "bad_request");

    auto nonObjectParams = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "ping"},
        {"params", nlohmann::json::array()}
    });
    assert(nonObjectParams["ok"] == false);
    assert(nonObjectParams["code"] == "bad_request");

    auto unknownTopLevelRequestParam = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "ping"},
        {"params", nlohmann::json::object()},
        {"raw", true}
    });
    assert(unknownTopLevelRequestParam["ok"] == false);
    assert(unknownTopLevelRequestParam["code"] == "bad_request");

    auto topLevelControlScope = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "state"},
        {"controlScope", "desktop:ignored"},
        {"params", {
            {"controlSession", "missing-token"}
        }}
    });
    assert(topLevelControlScope["ok"] == false);
    assert(topLevelControlScope["code"] == "bad_request");

    auto topLevelBatchControlScope = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "batch"},
        {"controlScope", "desktop:ignored"},
        {"params", {
            {"steps", nlohmann::json::array({
                {{"method", "ping"}, {"params", nlohmann::json::object()}}
            })}
        }}
    });
    assert(topLevelBatchControlScope["ok"] == false);
    assert(topLevelBatchControlScope["code"] == "bad_request");

    auto capabilities = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "capabilities"},
        {"params", nlohmann::json::object()}
    });
    assert(capabilities["ok"] == true);
    assert(capabilities["data"]["commands"].is_array());
    auto commands = capabilities["data"]["commands"].dump();
    assert(commands.find("session.release-active") != std::string::npos);
    assert(commands.find("permissions") != std::string::npos);
    assert(commands.find("permissions.open-settings") != std::string::npos);
    assert(commands.find("app.active") != std::string::npos);
    assert(commands.find("app.launch") != std::string::npos);
    assert(commands.find("app.activate") != std::string::npos);
    assert(commands.find("app.activate-pid") != std::string::npos);
    assert(commands.find("snapshot.interactive") != std::string::npos);
    assert(commands.find("snapshot.bounds") != std::string::npos);
    assert(commands.find("snapshot.actions") != std::string::npos);
    assert(commands.find("screenshot.frontmost-window") != std::string::npos);
    assert(commands.find("screenshot.region") != std::string::npos);
    assert(commands.find("click.multi") != std::string::npos);
    assert(commands.find("click.motion") != std::string::npos);
    assert(commands.find("click.timing") != std::string::npos);
    assert(commands.find("click.hover-safe") != std::string::npos);
    assert(commands.find("click.park") != std::string::npos);
    assert(commands.find("mouse.click") != std::string::npos);
    assert(commands.find("mouse.click.motion") != std::string::npos);
    assert(commands.find("mouse.down") != std::string::npos);
    assert(commands.find("mouse.up") != std::string::npos);
    assert(commands.find("clipboard.read") != std::string::npos);
    assert(commands.find("clipboard.write") != std::string::npos);
    assert(commands.find("clipboard.paste") != std::string::npos);
    assert(commands.find("scroll.anchor") != std::string::npos);
    assert(commands.find("scroll.clustered") != std::string::npos);
    assert(commands.find("scroll.humanize") != std::string::npos);
    assert(commands.find("wait.frontmost") != std::string::npos);
    assert(commands.find("wait.stable-screen") != std::string::npos);
    assert(commands.find("llm.chat") != std::string::npos);
    assert(commands.find("browser.eval") != std::string::npos);
    assert(commands.find("canvas.") == std::string::npos);
    assert(commands.find("observe.text") == std::string::npos);
    assert(commands.find("observe.find") == std::string::npos);
    assert(commands.find("image.split") != std::string::npos);
    assert(commands.find("open.url") != std::string::npos);
    assert(capabilities["data"]["nativeDeps"]["curl"].is_string());
    assert(!capabilities["data"]["nativeDeps"]["curl"].get<std::string>().empty());
    assert(!capabilities["data"]["nativeDeps"].contains("opencv"));
    assert(capabilities["data"]["nativeDeps"].size() == 1);
    assert(capabilities["data"]["runtime"]["executablePath"].is_string());
    assert(!capabilities["data"]["runtime"]["executablePath"].get<std::string>().empty());
    assert(capabilities["data"]["runtime"]["executableFingerprint"].is_string());
    assert(capabilities["data"]["runtime"]["executableFingerprint"].get<std::string>().find("fnv1a64:") == 0);
    assert(capabilities["data"]["runtime"]["featureMapSchemaVersion"] == "2");
    assert(capabilities["data"]["featureMapSchemaVersion"] == "2");

    auto schema = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "schema"},
        {"params", nlohmann::json::object()}
    });
    assert(schema["ok"] == true);
    assert(schema["data"].contains("response"));
    assert(schema["data"]["request"]["id"] == "optional non-empty string");
    assert(schema["data"]["request"]["method"] == "non-empty string");
    auto controlSchema = schema["data"]["controlSession"].dump();
    assert(controlSchema.find("control_session_acquire/control_session_resume") != std::string::npos);
    assert(controlSchema.find("waitMs") != std::string::npos);
    assert(controlSchema.find("maxRuntimeMs") != std::string::npos);
    assert(controlSchema.find("required non-empty token") != std::string::npos);
    assert(controlSchema.find("token is omitted from status responses") != std::string::npos);
    assert(controlSchema.find("staleAfterMs") != std::string::npos);
    assert(controlSchema.find("redacted token prefix") != std::string::npos);
    assert(controlSchema.find("session release-active") != std::string::npos);
    assert(controlSchema.find("maxExpiresAtMs") != std::string::npos);
    assert(schema["data"]["batchResponse"]["requested"].is_string());
    assert(schema["data"]["batchResponse"]["executed"].is_string());
    assert(schema["data"]["batchResponse"]["failed"].is_string());
    assert(schema["data"]["batchResponse"]["stoppedOnError"].is_string());
    auto browserEvalSchema = schema["data"]["browserEval"].dump();
    assert(browserEvalSchema.find("read-only") != std::string::npos);
    assert(browserEvalSchema.find("exact Chrome DevTools page target id") != std::string::npos);
    assert(browserEvalSchema.find("native click/type/press/mouse") != std::string::npos);
    auto batchSchema = schema["data"]["batch"].dump();
    assert(batchSchema.find("CLI reads the array from stdin") != std::string::npos);
    assert(batchSchema.find("--continue-on-error") != std::string::npos);
    assert(batchSchema.find("invalid_batch_step") != std::string::npos);
    assert(batchSchema.find("controlSession/controlScope") != std::string::npos);
    assert(batchSchema.find("step-1") != std::string::npos);

    auto mutatingBrowserEval = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "browser_eval"},
        {"params", {
            {"script", "document.querySelector('button').click()"}
        }}
    });
    assert(mutatingBrowserEval["ok"] == false);
    assert(mutatingBrowserEval["code"] == "invalid_browser_eval");

    auto whitespaceMutatingBrowserEval = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "browser_eval"},
        {"params", {
            {"script", "document.querySelector('input').value \t = 'x'"}
        }}
    });
    assert(whitespaceMutatingBrowserEval["ok"] == false);
    assert(whitespaceMutatingBrowserEval["code"] == "invalid_browser_eval");

    auto browserEvalWithControlMetadata = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "browser_eval"},
        {"params", {
            {"script", "document.title"},
            {"launch", false},
            {"controlScope", "desktop:local"}
        }}
    });
    assert(browserEvalWithControlMetadata["code"] != "invalid_browser_eval");
    assert(batchSchema.find("requested, executed, failed") != std::string::npos);
    auto targetSchema = schema["data"]["target"].dump();
    assert(targetSchema.find("rect:left,top,right,bottom") != std::string::npos);
    assert(targetSchema.find("text:") == std::string::npos);
    assert(targetSchema.find("exact:") == std::string::npos);
    assert(targetSchema.find("field:") == std::string::npos);
    auto targetCommandsSchema = schema["data"]["targetCommands"].dump();
    assert(targetCommandsSchema.find("target_find") != std::string::npos);
    assert(targetCommandsSchema.find("positive integer limit default 20") != std::string::npos);
    assert(targetCommandsSchema.find("positive integer limit default 1") != std::string::npos);
    assert(targetCommandsSchema.find("role:<role>") != std::string::npos);
    assert(targetCommandsSchema.find("name, value, and role") != std::string::npos);
    assert(targetCommandsSchema.find("coordinate candidates") != std::string::npos);
    assert(targetCommandsSchema.find("unsupported_visual_target") != std::string::npos);
    assert(targetCommandsSchema.find("clickPoint") != std::string::npos);
    auto getSchema = schema["data"]["get"].dump();
    assert(getSchema.find("@e1") != std::string::npos);
    assert(getSchema.find("text, value, bounds, or all") != std::string::npos);
    assert(getSchema.find("full ref metadata") != std::string::npos);
    auto clickSchema = schema["data"]["click"].dump();
    assert(clickSchema.find("motion") != std::string::npos);
    assert(clickSchema.find("durationMs") != std::string::npos);
    assert(clickSchema.find("clickDurationMs") != std::string::npos);
    assert(clickSchema.find("clickHoldMs") != std::string::npos);
    assert(clickSchema.find("parkBeforeClick") != std::string::npos);
    assert(clickSchema.find("rectClickXFraction") != std::string::npos);
    auto mouseSchema = schema["data"]["mouse"].dump();
    assert(mouseSchema.find("mouse_move") != std::string::npos);
    assert(mouseSchema.find("mouse_drag") != std::string::npos);
    assert(mouseSchema.find("from/to targets") != std::string::npos);
    assert(mouseSchema.find("clickCount integer 1..5") != std::string::npos);
    assert(mouseSchema.find("timeline db") != std::string::npos);
    auto pressSchema = schema["data"]["press"].dump();
    assert(pressSchema.find("Cmd+L") != std::string::npos);
    assert(pressSchema.find("string array") != std::string::npos);
    assert(pressSchema.find("1..5000") != std::string::npos);
    auto typeSchema = schema["data"]["type"].dump();
    assert(typeSchema.find("non-empty text") != std::string::npos);
    assert(typeSchema.find("clicked before typing") != std::string::npos);
    assert(typeSchema.find("paste text path") != std::string::npos);
    assert(typeSchema.find("characters count") != std::string::npos);
    auto clipboardSchema = schema["data"]["clipboard"].dump();
    assert(clipboardSchema.find("clipboard_read") != std::string::npos);
    assert(clipboardSchema.find("available boolean") != std::string::npos);
    assert(clipboardSchema.find("written boolean") != std::string::npos);
    assert(clipboardSchema.find("Cmd+V") != std::string::npos);
    auto screenshotSchema = schema["data"]["screenshot"].dump();
    assert(screenshotSchema.find("frontmostWindowOnly") != std::string::npos);
    assert(screenshotSchema.find("maxDimension") != std::string::npos);
    assert(screenshotSchema.find("x/y/width/height") != std::string::npos);
    assert(screenshotSchema.find("timestamped artifact") != std::string::npos);
    auto imageSchema = schema["data"]["image"].dump();
    assert(imageSchema.find("outDir/outputDir") != std::string::npos);
    assert(imageSchema.find("chunkHeight") != std::string::npos);
    assert(imageSchema.find("overlap") != std::string::npos);
    assert(imageSchema.find("prefix") != std::string::npos);
    assert(imageSchema.find("path/index/x/y/width/height") != std::string::npos);
    auto llmSchema = schema["data"]["llm"].dump();
    assert(llmSchema.find("llm_chat") != std::string::npos);
    assert(llmSchema.find("config.toml") != std::string::npos);
    assert(llmSchema.find("config set-provider") != std::string::npos);
    assert(llmSchema.find("profile") != std::string::npos);
    assert(llmSchema.find("image_path") != std::string::npos);
    assert(llmSchema.find("imagePaths") != std::string::npos);
    assert(llmSchema.find("max_output_tokens") != std::string::npos);
    assert(llmSchema.find("max_completion_tokens") != std::string::npos);
    assert(llmSchema.find("response_format") != std::string::npos);
    assert(llmSchema.find("json_object") != std::string::npos);
    assert(llmSchema.find("remote base URLs require an API key") != std::string::npos);
    assert(llmSchema.find("1..900000") != std::string::npos);
    assert(llmSchema.find("raw provider JSON") != std::string::npos);
    auto openUrlSchema = schema["data"]["openUrl"].dump();
    assert(openUrlSchema.find("http or https URL") != std::string::npos);
    assert(openUrlSchema.find("default firefox") != std::string::npos);
    assert(openUrlSchema.find("newWindow") != std::string::npos);
    assert(openUrlSchema.find("newInstance") != std::string::npos);
    assert(openUrlSchema.find("opened window metadata") != std::string::npos);
    auto windowSchema = schema["data"]["window"].dump();
    assert(windowSchema.find("active window metadata") != std::string::npos);
    assert(windowSchema.find("windows array") != std::string::npos);
    assert(windowSchema.find("x/y/width/height") != std::string::npos);
    assert(windowSchema.find("frontmost boolean") != std::string::npos);
    assert(windowSchema.find("id, title, app, pid") != std::string::npos);
    auto appSchema = schema["data"]["app"].dump();
    assert(appSchema.find("frontmost app metadata") != std::string::npos);
    assert(appSchema.find("app.activate is an alias") != std::string::npos);
    assert(appSchema.find("positive integer pid") != std::string::npos);
    assert(appSchema.find("available, pid, name, and bundleId") != std::string::npos);
    assert(appSchema.find("launched boolean") != std::string::npos);
    auto permissionsSchema = schema["data"]["permissions"].dump();
    assert(permissionsSchema.find("accessibility and screenCapture") != std::string::npos);
    assert(permissionsSchema.find("screen-capture") != std::string::npos);
    assert(permissionsSchema.find("normalized pane accessibility or screen") != std::string::npos);
    auto stateSchema = schema["data"]["state"].dump();
    assert(stateSchema.find("frontmostApp") != std::string::npos);
    assert(stateSchema.find("focusedElement") != std::string::npos);
    assert(stateSchema.find("frontmostWindowBounds") != std::string::npos);
    assert(stateSchema.find("cursor") != std::string::npos);
    auto snapshotSchema = schema["data"]["snapshot"].dump();
    assert(snapshotSchema.find("interactive elements") != std::string::npos);
    assert(snapshotSchema.find("element bounds") != std::string::npos);
    assert(snapshotSchema.find("element actions") != std::string::npos);
    assert(snapshotSchema.find("default 8") != std::string::npos);
    assert(snapshotSchema.find("default 700") != std::string::npos);
    assert(snapshotSchema.find("refs array") != std::string::npos);
    assert(snapshotSchema.find("ref store") != std::string::npos);
    auto scrollSchema = schema["data"]["scroll"].dump();
    assert(scrollSchema.find("dy and dx integers") != std::string::npos);
    assert(scrollSchema.find("maxScrollGestureDelta") != std::string::npos);
    assert(scrollSchema.find("clustered human-like gestures") != std::string::npos);
    assert(scrollSchema.find("atOffsetX") != std::string::npos);
    assert(scrollSchema.find("focusApp") != std::string::npos);
    assert(scrollSchema.find("samples integer 1..6") != std::string::npos);
    auto waitSchema = schema["data"]["wait"].dump();
    assert(waitSchema.find("frontmost app") != std::string::npos);
    assert(waitSchema.find("stableScreenMs") != std::string::npos);
    assert(waitSchema.find("1..120000") != std::string::npos);
    assert(waitSchema.find("50..5000") != std::string::npos);
    assert(waitSchema.find("wait_timeout") != std::string::npos);
    auto errorSchema = schema["data"]["errors"].dump();
    assert(errorSchema.find("invalid_wait") != std::string::npos);
    assert(errorSchema.find("bad_request") != std::string::npos);
    assert(errorSchema.find("invalid_control_scope") != std::string::npos);
    assert(errorSchema.find("control_session_purpose_mismatch") != std::string::npos);
    assert(errorSchema.find("invalid_batch_step") != std::string::npos);
    assert(errorSchema.find("invalid_permissions") != std::string::npos);
    assert(errorSchema.find("invalid_window") != std::string::npos);
    assert(errorSchema.find("invalid_app") != std::string::npos);
    assert(errorSchema.find("invalid_url") != std::string::npos);
    assert(errorSchema.find("invalid_image_info") != std::string::npos);
    assert(errorSchema.find("invalid_screenshot") != std::string::npos);
    assert(errorSchema.find("invalid_snapshot") != std::string::npos);
    assert(errorSchema.find("invalid_mouse_down") != std::string::npos);
    assert(errorSchema.find("invalid_mouse_up") != std::string::npos);
    assert(errorSchema.find("unsupported_wait_text") == std::string::npos);

    auto invalidControlAcquire = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "control_session_acquire"},
        {"params", {
            {"scope", "desktop:invalid-control"},
            {"owner", "core-test"},
            {"purpose", "invalid-control"},
            {"ttlMs", "soon"}
        }}
    });
    assert(invalidControlAcquire["ok"] == false);
    assert(invalidControlAcquire["code"] == "invalid_control_session");

    auto unknownControlAcquireParam = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "control_session_acquire"},
        {"params", {
            {"scope", "desktop:invalid-control"},
            {"owner", "core-test"},
            {"purpose", "invalid-control"},
            {"raw", true}
        }}
    });
    assert(unknownControlAcquireParam["ok"] == false);
    assert(unknownControlAcquireParam["code"] == "invalid_control_session");

    auto invalidControlAcquireScopeType = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "control_session_acquire"},
        {"params", {
            {"scope", true},
            {"owner", "core-test"},
            {"purpose", "invalid-control"}
        }}
    });
    assert(invalidControlAcquireScopeType["ok"] == false);
    assert(invalidControlAcquireScopeType["code"] == "invalid_control_session");

    auto blankControlAcquireScope = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "control_session_acquire"},
        {"params", {
            {"scope", "   "},
            {"owner", "core-test"},
            {"purpose", "invalid-control"}
        }}
    });
    assert(blankControlAcquireScope["ok"] == false);
    assert(blankControlAcquireScope["code"] == "invalid_control_session");

    auto blankControlAcquireDaemonSession = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "control_session_acquire"},
        {"params", {
            {"scope", "desktop:invalid-control"},
            {"daemonSession", "   "},
            {"owner", "core-test"},
            {"purpose", "invalid-control"}
        }}
    });
    assert(blankControlAcquireDaemonSession["ok"] == false);
    assert(blankControlAcquireDaemonSession["code"] == "invalid_control_session");

    auto invalidControlAcquireOwnerType = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "control_session_acquire"},
        {"params", {
            {"scope", "desktop:invalid-control"},
            {"owner", true},
            {"purpose", "invalid-control"}
        }}
    });
    assert(invalidControlAcquireOwnerType["ok"] == false);
    assert(invalidControlAcquireOwnerType["code"] == "invalid_control_session");

    auto unknownControlRenewParam = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "control_session_renew"},
        {"params", {
            {"token", "missing-token"},
            {"raw", true}
        }}
    });
    assert(unknownControlRenewParam["ok"] == false);
    assert(unknownControlRenewParam["code"] == "invalid_control_session");

    auto invalidControlRenewTokenType = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "control_session_renew"},
        {"params", {
            {"token", true}
        }}
    });
    assert(invalidControlRenewTokenType["ok"] == false);
    assert(invalidControlRenewTokenType["code"] == "invalid_control_session");

    auto blankControlRenewToken = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "control_session_renew"},
        {"params", {
            {"token", "   "}
        }}
    });
    assert(blankControlRenewToken["ok"] == false);
    assert(blankControlRenewToken["code"] == "invalid_control_session");

    auto unknownControlReleaseParam = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "control_session_release"},
        {"params", {
            {"token", "missing-token"},
            {"raw", true}
        }}
    });
    assert(unknownControlReleaseParam["ok"] == false);
    assert(unknownControlReleaseParam["code"] == "invalid_control_session");

    auto invalidControlReleaseTokenType = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "control_session_release"},
        {"params", {
            {"token", true}
        }}
    });
    assert(invalidControlReleaseTokenType["ok"] == false);
    assert(invalidControlReleaseTokenType["code"] == "invalid_control_session");

    auto blankControlReleaseToken = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "control_session_release"},
        {"params", {
            {"token", "   "}
        }}
    });
    assert(blankControlReleaseToken["ok"] == false);
    assert(blankControlReleaseToken["code"] == "invalid_control_session");

    auto unknownControlStatusParam = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "control_session_status"},
        {"params", {
            {"scope", "desktop:invalid-control"},
            {"raw", true}
        }}
    });
    assert(unknownControlStatusParam["ok"] == false);
    assert(unknownControlStatusParam["code"] == "invalid_control_session");

    auto invalidControlStatusScopeType = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "control_session_status"},
        {"params", {
            {"scope", true}
        }}
    });
    assert(invalidControlStatusScopeType["ok"] == false);
    assert(invalidControlStatusScopeType["code"] == "invalid_control_session");

    auto blankControlStatusScope = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "control_session_status"},
        {"params", {
            {"scope", "   "}
        }}
    });
    assert(blankControlStatusScope["ok"] == false);
    assert(blankControlStatusScope["code"] == "invalid_control_session");

    auto blankControlStatusToken = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "control_session_status"},
        {"params", {
            {"token", "   "}
        }}
    });
    assert(blankControlStatusToken["ok"] == false);
    assert(blankControlStatusToken["code"] == "invalid_control_session");

    auto invalidControlMetrics = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "control_session_metrics"},
        {"params", {
            {"scope", "desktop:invalid-control"},
            {"eventLimit", 2.5}
        }}
    });
    assert(invalidControlMetrics["ok"] == false);
    assert(invalidControlMetrics["code"] == "invalid_control_session");

    auto invalidControlMetricsLimitRange = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "control_session_metrics"},
        {"params", {
            {"scope", "desktop:invalid-control"},
            {"eventLimit", 0}
        }}
    });
    assert(invalidControlMetricsLimitRange["ok"] == false);
    assert(invalidControlMetricsLimitRange["code"] == "invalid_control_session");

    auto invalidControlMetricsThresholdRange = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "control_session_metrics"},
        {"params", {
            {"scope", "desktop:invalid-control"},
            {"staleAfterMs", -1}
        }}
    });
    assert(invalidControlMetricsThresholdRange["ok"] == false);
    assert(invalidControlMetricsThresholdRange["code"] == "invalid_control_session");

    auto unknownControlMetricsParam = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "control_session_metrics"},
        {"params", {
            {"scope", "desktop:invalid-control"},
            {"raw", true}
        }}
    });
    assert(unknownControlMetricsParam["ok"] == false);
    assert(unknownControlMetricsParam["code"] == "invalid_control_session");

    auto invalidControlMetricsScopeType = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "control_session_metrics"},
        {"params", {
            {"scope", true}
        }}
    });
    assert(invalidControlMetricsScopeType["ok"] == false);
    assert(invalidControlMetricsScopeType["code"] == "invalid_control_session");

    auto blankControlMetricsScope = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "control_session_metrics"},
        {"params", {
            {"scope", "   "}
        }}
    });
    assert(blankControlMetricsScope["ok"] == false);
    assert(blankControlMetricsScope["code"] == "invalid_control_session");

    auto invalidControlEventsLimitRange = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "control_session_events"},
        {"params", {
            {"scope", "desktop:invalid-control"},
            {"limit", 0}
        }}
    });
    assert(invalidControlEventsLimitRange["ok"] == false);
    assert(invalidControlEventsLimitRange["code"] == "invalid_control_session");

    auto unknownControlEventsParam = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "control_session_events"},
        {"params", {
            {"scope", "desktop:invalid-control"},
            {"raw", true}
        }}
    });
    assert(unknownControlEventsParam["ok"] == false);
    assert(unknownControlEventsParam["code"] == "invalid_control_session");

    auto invalidControlEventsScopeType = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "control_session_events"},
        {"params", {
            {"scope", true}
        }}
    });
    assert(invalidControlEventsScopeType["ok"] == false);
    assert(invalidControlEventsScopeType["code"] == "invalid_control_session");

    auto blankControlEventsScope = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "control_session_events"},
        {"params", {
            {"scope", "   "}
        }}
    });
    assert(blankControlEventsScope["ok"] == false);
    assert(blankControlEventsScope["code"] == "invalid_control_session");

    auto unknownBatchParam = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "batch"},
        {"params", {
            {"steps", nlohmann::json::array()},
            {"raw", true}
        }}
    });
    assert(unknownBatchParam["ok"] == false);
    assert(unknownBatchParam["code"] == "invalid_batch");

    auto invalidBatchStopOnError = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "batch"},
        {"params", {
            {"steps", nlohmann::json::array()},
            {"stopOnError", "no"}
        }}
    });
    assert(invalidBatchStopOnError["ok"] == false);
    assert(invalidBatchStopOnError["code"] == "invalid_batch");

    auto unknownBatchStepParam = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "batch"},
        {"params", {
            {"steps", nlohmann::json::array({
                {{"id", "bad-raw"}, {"method", "ping"}, {"params", nlohmann::json::object()}, {"raw", true}},
                {{"method", "schema"}, {"params", nlohmann::json::object()}}
            })}
        }}
    });
    assert(unknownBatchStepParam["ok"] == true);
    assert(unknownBatchStepParam["data"]["requested"] == 2);
    assert(unknownBatchStepParam["data"]["executed"] == 1);
    assert(unknownBatchStepParam["data"]["failed"] == 1);
    assert(unknownBatchStepParam["data"]["stoppedOnError"] == true);
    assert(unknownBatchStepParam["data"]["results"].size() == 1);
    assert(unknownBatchStepParam["data"]["results"][0]["ok"] == false);
    assert(unknownBatchStepParam["data"]["results"][0]["code"] == "invalid_batch_step");
    assert(unknownBatchStepParam["data"]["results"][0]["id"] == "bad-raw");

    auto invalidBatchStepMethodType = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "batch"},
        {"params", {
            {"steps", nlohmann::json::array({
                {{"id", "bad-method"}, {"method", true}, {"params", nlohmann::json::object()}},
                {{"method", "schema"}, {"params", nlohmann::json::object()}}
            })}
        }}
    });
    assert(invalidBatchStepMethodType["ok"] == true);
    assert(invalidBatchStepMethodType["data"]["results"].size() == 1);
    assert(invalidBatchStepMethodType["data"]["results"][0]["ok"] == false);
    assert(invalidBatchStepMethodType["data"]["results"][0]["code"] == "invalid_batch_step");
    assert(invalidBatchStepMethodType["data"]["results"][0]["id"] == "bad-method");

    auto emptyBatchStepMethod = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "batch"},
        {"params", {
            {"steps", nlohmann::json::array({
                {{"method", ""}, {"params", nlohmann::json::object()}},
                {{"method", "schema"}, {"params", nlohmann::json::object()}}
            })},
            {"stopOnError", false}
        }}
    });
    assert(emptyBatchStepMethod["ok"] == true);
    assert(emptyBatchStepMethod["data"]["results"].size() == 2);
    assert(emptyBatchStepMethod["data"]["results"][0]["ok"] == false);
    assert(emptyBatchStepMethod["data"]["results"][0]["code"] == "invalid_batch_step");
    assert(emptyBatchStepMethod["data"]["results"][1]["ok"] == true);

    auto blankBatchStepMethod = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "batch"},
        {"params", {
            {"steps", nlohmann::json::array({
                {{"method", "   "}, {"params", nlohmann::json::object()}},
                {{"method", "schema"}, {"params", nlohmann::json::object()}}
            })},
            {"stopOnError", false}
        }}
    });
    assert(blankBatchStepMethod["ok"] == true);
    assert(blankBatchStepMethod["data"]["results"].size() == 2);
    assert(blankBatchStepMethod["data"]["results"][0]["ok"] == false);
    assert(blankBatchStepMethod["data"]["results"][0]["code"] == "invalid_batch_step");
    assert(blankBatchStepMethod["data"]["results"][1]["ok"] == true);

    auto emptyBatchStepId = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "batch"},
        {"params", {
            {"steps", nlohmann::json::array({
                {{"id", ""}, {"method", "ping"}, {"params", nlohmann::json::object()}},
                {{"method", "schema"}, {"params", nlohmann::json::object()}}
            })}
        }}
    });
    assert(emptyBatchStepId["ok"] == true);
    assert(emptyBatchStepId["data"]["results"].size() == 1);
    assert(emptyBatchStepId["data"]["results"][0]["ok"] == false);
    assert(emptyBatchStepId["data"]["results"][0]["code"] == "invalid_batch_step");

    auto blankBatchStepId = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "batch"},
        {"params", {
            {"steps", nlohmann::json::array({
                {{"id", "   "}, {"method", "ping"}, {"params", nlohmann::json::object()}},
                {{"method", "schema"}, {"params", nlohmann::json::object()}}
            })}
        }}
    });
    assert(blankBatchStepId["ok"] == true);
    assert(blankBatchStepId["data"]["results"].size() == 1);
    assert(blankBatchStepId["data"]["results"][0]["ok"] == false);
    assert(blankBatchStepId["data"]["results"][0]["code"] == "invalid_batch_step");

    auto batchControlSessionStatus = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "batch"},
        {"params", {
            {"controlScope", "desktop:invalid-control"},
            {"steps", nlohmann::json::array({
                {{"method", "control_session_status"}, {"params", nlohmann::json::object()}}
            })}
        }}
    });
    assert(batchControlSessionStatus["ok"] == true);
    assert(batchControlSessionStatus["data"]["results"].size() == 1);
    assert(batchControlSessionStatus["data"]["results"][0]["ok"] == true);

    auto batchBlankControlStatusToken = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "batch"},
        {"params", {
            {"controlScope", "desktop:invalid-control"},
            {"steps", nlohmann::json::array({
                {{"method", "control_session_status"}, {"params", {{"token", "   "}}}}
            })}
        }}
    });
    assert(batchBlankControlStatusToken["ok"] == true);
    assert(batchBlankControlStatusToken["data"]["results"].size() == 1);
    assert(batchBlankControlStatusToken["data"]["results"][0]["ok"] == false);
    assert(batchBlankControlStatusToken["data"]["results"][0]["code"] == "invalid_control_session");

    auto invalidBatchStepParamsType = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "batch"},
        {"params", {
            {"steps", nlohmann::json::array({
                {{"method", "ping"}, {"params", nlohmann::json::array()}},
                {{"method", "schema"}, {"params", nlohmann::json::object()}}
            })},
            {"stopOnError", false}
        }}
    });
    assert(invalidBatchStepParamsType["ok"] == true);
    assert(invalidBatchStepParamsType["data"]["requested"] == 2);
    assert(invalidBatchStepParamsType["data"]["executed"] == 2);
    assert(invalidBatchStepParamsType["data"]["failed"] == 1);
    assert(invalidBatchStepParamsType["data"]["stoppedOnError"] == false);
    assert(invalidBatchStepParamsType["data"]["results"].size() == 2);
    assert(invalidBatchStepParamsType["data"]["results"][0]["ok"] == false);
    assert(invalidBatchStepParamsType["data"]["results"][0]["code"] == "invalid_batch_step");
    assert(invalidBatchStepParamsType["data"]["results"][0]["id"] == "step-1");
    assert(invalidBatchStepParamsType["data"]["results"][1]["ok"] == true);
    assert(invalidBatchStepParamsType["data"]["results"][1]["id"] == "step-2");

    auto batch = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "batch"},
        {"params", {
            {"steps", nlohmann::json::array({
                {{"method", "ping"}, {"params", nlohmann::json::object()}},
                {{"id", "schema-check"}, {"method", "schema"}, {"params", nlohmann::json::object()}}
            })}
        }}
    });
    assert(batch["ok"] == true);
    assert(batch["data"]["requested"] == 2);
    assert(batch["data"]["executed"] == 2);
    assert(batch["data"]["failed"] == 0);
    assert(batch["data"]["stoppedOnError"] == false);
    assert(batch["data"]["results"].size() == 2);
    assert(batch["data"]["results"][0]["ok"] == true);
    assert(batch["data"]["results"][0]["id"] == "step-1");
    assert(batch["data"]["results"][1]["ok"] == true);
    assert(batch["data"]["results"][1]["id"] == "schema-check");
}

void TestDaemonGateCoverageForProtectedMethods() {
    std::vector<std::pair<std::string, nlohmann::json>> protectedMethods = {
        {"permissions", {{"request", true}}},
        {"open_permissions", nlohmann::json::object()},
        {"state", nlohmann::json::object()},
        {"snapshot", nlohmann::json::object()},
        {"screenshot", nlohmann::json::object()},
        {"window_list", nlohmann::json::object()},
        {"window_active", nlohmann::json::object()},
        {"window_bounds", {{"x", 0}, {"y", 0}, {"width", 100}, {"height", 100}}},
        {"window_close", {{"id", "window-1"}}},
        {"app_active", nlohmann::json::object()},
        {"app_launch", {{"query", "Firefox"}}},
        {"app_activate_pid", {{"pid", 1}}},
        {"open_url", {{"url", "https://example.invalid"}}},
        {"observe_events", nlohmann::json::object()},
        {"observe_frames", nlohmann::json::object()},
        {"target_find", {{"query", "role:button[name=\"Continue\"]"}}},
        {"target_resolve", {{"target", "role:button[name=\"Continue\"]"}}},
        {"target_explain", {{"query", "role:button[name=\"Continue\"]"}}},
        {"get", {{"target", "@e1"}}},
        {"image_info", {{"path", "/tmp/missing.png"}}},
        {"image_split", {{"path", "/tmp/missing.png"}}},
        {"llm_chat", nlohmann::json::object()},
        {"click", {{"target", "point:1,1"}}},
        {"mouse_move", {{"x", 1}, {"y", 1}}},
        {"mouse_drag", nlohmann::json::object()},
        {"mouse_down", {{"button", "left"}}},
        {"mouse_up", {{"button", "left"}}},
        {"scroll", {{"dy", 1}}},
        {"wait", {{"timeoutMs", 1}}},
        {"press", {{"keys", "Escape"}}},
        {"type", {{"text", "hello"}}},
        {"clipboard_read", nlohmann::json::object()},
        {"clipboard_write", {{"text", "hello"}}},
        {"clipboard_paste", nlohmann::json::object()}
    };

    for (const auto& [method, params] : protectedMethods) {
        auto blocked = ComputerCpp::HandleDaemonRequest("unit", {
            {"method", method},
            {"params", params}
        });
        assert(blocked["ok"] == false);
        assert(blocked["code"] == "control_session_required");
    }

    auto passivePermissions = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "permissions"},
        {"params", nlohmann::json::object()}
    });
    assert(passivePermissions["code"] != "control_session_required");
}

void TestBatchDoesNotBypassControlSessionGate() {
    auto blankDirectControlSession = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "state"},
        {"params", {
            {"controlSession", "   "}
        }}
    });
    assert(blankDirectControlSession["ok"] == false);
    assert(blankDirectControlSession["code"] == "invalid_control_session");

    auto invalidDirectControlSessionType = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "state"},
        {"params", {
            {"controlSession", true}
        }}
    });
    assert(invalidDirectControlSessionType["ok"] == false);
    assert(invalidDirectControlSessionType["code"] == "invalid_control_session");

    auto blankRequestControlSession = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "state"},
        {"controlSession", "   "},
        {"params", nlohmann::json::object()}
    });
    assert(blankRequestControlSession["ok"] == false);
    assert(blankRequestControlSession["code"] == "invalid_control_session");

    auto invalidRequestControlSessionType = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "state"},
        {"controlSession", true},
        {"params", nlohmann::json::object()}
    });
    assert(invalidRequestControlSessionType["ok"] == false);
    assert(invalidRequestControlSessionType["code"] == "invalid_control_session");

    auto invalidPingControlSessionType = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "ping"},
        {"controlSession", true},
        {"params", nlohmann::json::object()}
    });
    assert(invalidPingControlSessionType["ok"] == false);
    assert(invalidPingControlSessionType["code"] == "invalid_control_session");

    auto blankPingControlScope = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "ping"},
        {"params", {
            {"controlScope", "   "}
        }}
    });
    assert(blankPingControlScope["ok"] == false);
    assert(blankPingControlScope["code"] == "invalid_control_session");

    auto blankDirectControlScope = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "state"},
        {"params", {
            {"controlSession", "missing-token"},
            {"controlScope", "   "}
        }}
    });
    assert(blankDirectControlScope["ok"] == false);
    assert(blankDirectControlScope["code"] == "invalid_control_session");

    auto invalidDirectControlScopeType = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "state"},
        {"params", {
            {"controlSession", "missing-token"},
            {"controlScope", true}
        }}
    });
    assert(invalidDirectControlScopeType["ok"] == false);
    assert(invalidDirectControlScopeType["code"] == "invalid_control_session");

    auto blockedBatch = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "batch"},
        {"params", {
            {"steps", nlohmann::json::array({
                {{"method", "state"}, {"params", nlohmann::json::object()}}
            })}
        }}
    });
    assert(blockedBatch["ok"] == true);
    assert(blockedBatch["data"]["results"].size() == 1);
    assert(blockedBatch["data"]["results"][0]["ok"] == false);
    assert(blockedBatch["data"]["results"][0]["code"] == "control_session_required");

    auto invalidEnvelopeBatchControlSession = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "batch"},
        {"params", {
            {"controlSession", true},
            {"steps", nlohmann::json::array({
                {{"method", "ping"}, {"params", nlohmann::json::object()}}
            })}
        }}
    });
    assert(invalidEnvelopeBatchControlSession["ok"] == false);
    assert(invalidEnvelopeBatchControlSession["code"] == "invalid_control_session");

    auto blankBatchControlSession = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "batch"},
        {"params", {
            {"steps", nlohmann::json::array({
                {{"method", "state"}, {"params", {{"controlSession", "   "}}}}
            })}
        }}
    });
    assert(blankBatchControlSession["ok"] == true);
    assert(blankBatchControlSession["data"]["results"].size() == 1);
    assert(blankBatchControlSession["data"]["results"][0]["ok"] == false);
    assert(blankBatchControlSession["data"]["results"][0]["code"] == "invalid_control_session");

    auto invalidBatchControlSessionType = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "batch"},
        {"params", {
            {"steps", nlohmann::json::array({
                {{"method", "state"}, {"params", {{"controlSession", true}}}}
            })}
        }}
    });
    assert(invalidBatchControlSessionType["ok"] == true);
    assert(invalidBatchControlSessionType["data"]["results"].size() == 1);
    assert(invalidBatchControlSessionType["data"]["results"][0]["ok"] == false);
    assert(invalidBatchControlSessionType["data"]["results"][0]["code"] == "invalid_control_session");

    auto blankBatchControlScope = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "batch"},
        {"params", {
            {"steps", nlohmann::json::array({
                {{"method", "state"}, {"params", {{"controlSession", "missing-token"}, {"controlScope", "   "}}}}
            })}
        }}
    });
    assert(blankBatchControlScope["ok"] == true);
    assert(blankBatchControlScope["data"]["results"].size() == 1);
    assert(blankBatchControlScope["data"]["results"][0]["ok"] == false);
    assert(blankBatchControlScope["data"]["results"][0]["code"] == "invalid_control_session");

    auto invalidBatchControlScopeType = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "batch"},
        {"params", {
            {"steps", nlohmann::json::array({
                {{"method", "state"}, {"params", {{"controlSession", "missing-token"}, {"controlScope", true}}}}
            })}
        }}
    });
    assert(invalidBatchControlScopeType["ok"] == true);
    assert(invalidBatchControlScopeType["data"]["results"].size() == 1);
    assert(invalidBatchControlScopeType["data"]["results"][0]["ok"] == false);
    assert(invalidBatchControlScopeType["data"]["results"][0]["code"] == "invalid_control_session");
}

void TestScrollObserveDoesNotCreateEventWhenAnchorFails() {
    const std::string session = "scroll-anchor-failure-unit";
    auto acquired = ComputerCpp::HandleDaemonRequest(session, {
        {"method", "control_session_acquire"},
        {"params", {
            {"scope", "desktop:local"},
            {"owner", "core-test"},
            {"purpose", "scroll-anchor-failure"},
            {"ttlMs", 5000}
        }}
    });
    assert(acquired["ok"] == true);
    std::string token = acquired["data"]["session"]["token"];

    auto failedScroll = ComputerCpp::HandleDaemonRequest(session, {
        {"method", "scroll"},
        {"params", {
            {"controlSession", token},
            {"dy", -100},
            {"at", "@missing-anchor"},
            {"observe", true}
        }}
    });
    assert(failedScroll["ok"] == false);
    assert(failedScroll["code"] == "target_not_found");
    assert(ComputerCpp::RecentTimelineEvents(session, 5).empty());

    auto released = ComputerCpp::HandleDaemonRequest(session, {
        {"method", "control_session_release"},
        {"params", {{"token", token}}}
    });
    assert(released["ok"] == true);
}

void TestDaemonRequiresControlSessionForProtectedMethods() {
    nlohmann::json llmParams = {
        {"baseUrl", "https://inference.example.test/v1"},
        {"model", "qwen36-27b"},
        {"apiKey", ""},
        {"messages", nlohmann::json::array({
            {{"role", "user"}, {"content", "ping"}}
        })}
    };
    auto blocked = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "llm_chat"},
        {"params", llmParams}
    });
    assert(blocked["ok"] == false);
    assert(blocked["code"] == "control_session_required");

    auto acquired = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "control_session_acquire"},
        {"params", {
            {"scope", "desktop:local"},
            {"owner", "core-test"},
            {"purpose", "daemon-gate"},
            {"ttlMs", 60000}
        }}
    });
    assert(acquired["ok"] == true);
    std::string token = acquired["data"]["session"]["token"];

    auto status = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "control_session_status"},
        {"params", {{"scope", "desktop:local"}}}
    });
    assert(status["ok"] == true);
    assert(status["data"]["session"]["owner"] == "core-test");
    assert(!status["data"]["session"].contains("token"));

    auto tokenStatus = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "control_session_status"},
        {"params", {{"token", token}}}
    });
    assert(tokenStatus["ok"] == true);
    assert(!tokenStatus["data"]["session"].contains("token"));

    auto unknownPermissionsParam = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "permissions"},
        {"params", {
            {"controlSession", token},
            {"request", true},
            {"raw", true}
        }}
    });
    assert(unknownPermissionsParam["ok"] == false);
    assert(unknownPermissionsParam["code"] == "invalid_permissions");

    auto invalidPermissionsRequest = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "permissions"},
        {"params", {
            {"controlSession", token},
            {"request", "yes"}
        }}
    });
    assert(invalidPermissionsRequest["ok"] == false);
    assert(invalidPermissionsRequest["code"] == "invalid_permissions");

    auto unknownOpenPermissionsParam = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "open_permissions"},
        {"params", {
            {"controlSession", token},
            {"pane", "screen"},
            {"raw", true}
        }}
    });
    assert(unknownOpenPermissionsParam["ok"] == false);
    assert(unknownOpenPermissionsParam["code"] == "invalid_permissions");

    auto invalidOpenPermissionsPane = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "open_permissions"},
        {"params", {
            {"controlSession", token},
            {"pane", true}
        }}
    });
    assert(invalidOpenPermissionsPane["ok"] == false);
    assert(invalidOpenPermissionsPane["code"] == "invalid_permissions");

    auto emptyOpenPermissionsPane = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "open_permissions"},
        {"params", {
            {"controlSession", token},
            {"pane", ""}
        }}
    });
    assert(emptyOpenPermissionsPane["ok"] == false);
    assert(emptyOpenPermissionsPane["code"] == "invalid_permissions");

    auto blankOpenPermissionsPane = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "open_permissions"},
        {"params", {
            {"controlSession", token},
            {"pane", "   "}
        }}
    });
    assert(blankOpenPermissionsPane["ok"] == false);
    assert(blankOpenPermissionsPane["code"] == "invalid_permissions");

    auto unsupportedOpenPermissionsPane = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "open_permissions"},
        {"params", {
            {"controlSession", token},
            {"pane", "camera"}
        }}
    });
    assert(unsupportedOpenPermissionsPane["ok"] == false);
    assert(unsupportedOpenPermissionsPane["code"] == "invalid_permissions");

    auto unknownWindowBoundsParam = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "window_bounds"},
        {"params", {
            {"controlSession", token},
            {"x", 0},
            {"y", 0},
            {"width", 100},
            {"height", 100},
            {"raw", true}
        }}
    });
    assert(unknownWindowBoundsParam["ok"] == false);
    assert(unknownWindowBoundsParam["code"] == "invalid_window");

    auto invalidWindowBoundsParam = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "window_bounds"},
        {"params", {
            {"controlSession", token},
            {"x", "left"},
            {"y", 0},
            {"width", 100},
            {"height", 100}
        }}
    });
    assert(invalidWindowBoundsParam["ok"] == false);
    assert(invalidWindowBoundsParam["code"] == "invalid_window");

    auto invalidWindowBoundsSize = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "window_bounds"},
        {"params", {
            {"controlSession", token},
            {"x", 0},
            {"y", 0},
            {"width", 0},
            {"height", 100}
        }}
    });
    assert(invalidWindowBoundsSize["ok"] == false);
    assert(invalidWindowBoundsSize["code"] == "invalid_window");

    auto invalidWindowBoundsPid = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "window_bounds"},
        {"params", {
            {"controlSession", token},
            {"x", 0},
            {"y", 0},
            {"width", 100},
            {"height", 100},
            {"pid", -1}
        }}
    });
    assert(invalidWindowBoundsPid["ok"] == false);
    assert(invalidWindowBoundsPid["code"] == "invalid_window");

    auto unknownWindowListParam = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "window_list"},
        {"params", {
            {"controlSession", token},
            {"app", "Finder"},
            {"raw", true}
        }}
    });
    assert(unknownWindowListParam["ok"] == false);
    assert(unknownWindowListParam["code"] == "invalid_window");

    auto invalidWindowListParam = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "window_list"},
        {"params", {
            {"controlSession", token},
            {"app", true}
        }}
    });
    assert(invalidWindowListParam["ok"] == false);
    assert(invalidWindowListParam["code"] == "invalid_window");

    auto emptyWindowListApp = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "window_list"},
        {"params", {
            {"controlSession", token},
            {"app", ""}
        }}
    });
    assert(emptyWindowListApp["ok"] == false);
    assert(emptyWindowListApp["code"] == "invalid_window");

    auto blankWindowListApp = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "window_list"},
        {"params", {
            {"controlSession", token},
            {"app", "   "}
        }}
    });
    assert(blankWindowListApp["ok"] == false);
    assert(blankWindowListApp["code"] == "invalid_window");

    auto unknownWindowCloseParam = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "window_close"},
        {"params", {
            {"controlSession", token},
            {"id", "missing-window"},
            {"raw", true}
        }}
    });
    assert(unknownWindowCloseParam["ok"] == false);
    assert(unknownWindowCloseParam["code"] == "invalid_window");

    auto invalidWindowCloseParam = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "window_close"},
        {"params", {
            {"controlSession", token},
            {"frontmost", "yes"}
        }}
    });
    assert(invalidWindowCloseParam["ok"] == false);
    assert(invalidWindowCloseParam["code"] == "invalid_window");

    auto emptyWindowCloseId = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "window_close"},
        {"params", {
            {"controlSession", token},
            {"id", ""}
        }}
    });
    assert(emptyWindowCloseId["ok"] == false);
    assert(emptyWindowCloseId["code"] == "invalid_window");

    auto blankWindowCloseId = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "window_close"},
        {"params", {
            {"controlSession", token},
            {"id", "   "}
        }}
    });
    assert(blankWindowCloseId["ok"] == false);
    assert(blankWindowCloseId["code"] == "invalid_window");

    auto unknownAppLaunchParam = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "app_launch"},
        {"params", {
            {"controlSession", token},
            {"query", "Finder"},
            {"raw", true}
        }}
    });
    assert(unknownAppLaunchParam["ok"] == false);
    assert(unknownAppLaunchParam["code"] == "invalid_app");

    auto invalidAppLaunchParam = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "app_launch"},
        {"params", {
            {"controlSession", token},
            {"query", true}
        }}
    });
    assert(invalidAppLaunchParam["ok"] == false);
    assert(invalidAppLaunchParam["code"] == "invalid_app");

    auto emptyAppLaunchParam = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "app_launch"},
        {"params", {
            {"controlSession", token},
            {"query", ""}
        }}
    });
    assert(emptyAppLaunchParam["ok"] == false);
    assert(emptyAppLaunchParam["code"] == "invalid_app");

    auto blankAppLaunchParam = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "app_launch"},
        {"params", {
            {"controlSession", token},
            {"query", "   "}
        }}
    });
    assert(blankAppLaunchParam["ok"] == false);
    assert(blankAppLaunchParam["code"] == "invalid_app");

    auto unknownAppActivatePidParam = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "app_activate_pid"},
        {"params", {
            {"controlSession", token},
            {"pid", 1},
            {"raw", true}
        }}
    });
    assert(unknownAppActivatePidParam["ok"] == false);
    assert(unknownAppActivatePidParam["code"] == "invalid_app");

    auto invalidAppActivatePidParam = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "app_activate_pid"},
        {"params", {
            {"controlSession", token},
            {"pid", "finder"}
        }}
    });
    assert(invalidAppActivatePidParam["ok"] == false);
    assert(invalidAppActivatePidParam["code"] == "invalid_app");

    auto invalidAppActivatePidRange = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "app_activate_pid"},
        {"params", {
            {"controlSession", token},
            {"pid", 0}
        }}
    });
    assert(invalidAppActivatePidRange["ok"] == false);
    assert(invalidAppActivatePidRange["code"] == "invalid_app");

    auto unknownOpenUrlParam = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "open_url"},
        {"params", {
            {"controlSession", token},
            {"url", "https://example.invalid"},
            {"raw", true}
        }}
    });
    assert(unknownOpenUrlParam["ok"] == false);
    assert(unknownOpenUrlParam["code"] == "invalid_url");

    auto invalidOpenUrlParam = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "open_url"},
        {"params", {
            {"controlSession", token},
            {"url", "https://example.invalid"},
            {"newWindow", "yes"}
        }}
    });
    assert(invalidOpenUrlParam["ok"] == false);
    assert(invalidOpenUrlParam["code"] == "invalid_url");

    auto invalidOpenUrlScheme = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "open_url"},
        {"params", {
            {"controlSession", token},
            {"url", "file:///tmp/example.html"}
        }}
    });
    assert(invalidOpenUrlScheme["ok"] == false);
    assert(invalidOpenUrlScheme["code"] == "invalid_url");

    auto blankOpenUrl = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "open_url"},
        {"params", {
            {"controlSession", token},
            {"url", "   "}
        }}
    });
    assert(blankOpenUrl["ok"] == false);
    assert(blankOpenUrl["code"] == "invalid_url");

    auto emptyOpenUrlBrowser = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "open_url"},
        {"params", {
            {"controlSession", token},
            {"url", "https://example.invalid"},
            {"browser", ""}
        }}
    });
    assert(emptyOpenUrlBrowser["ok"] == false);
    assert(emptyOpenUrlBrowser["code"] == "invalid_url");

    auto blankOpenUrlBrowser = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "open_url"},
        {"params", {
            {"controlSession", token},
            {"url", "https://example.invalid"},
            {"browser", "   "}
        }}
    });
    assert(blankOpenUrlBrowser["ok"] == false);
    assert(blankOpenUrlBrowser["code"] == "invalid_url");

    llmParams["controlSession"] = token;
    auto allowed = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "llm_chat"},
        {"params", llmParams}
    });
    assert(allowed["ok"] == false);
    assert(allowed["code"] == "missing_api_key");

    auto invalidObserveLimit = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "observe_events"},
        {"params", {
            {"controlSession", token},
            {"limit", "many"}
        }}
    });
    assert(invalidObserveLimit["ok"] == false);
    assert(invalidObserveLimit["code"] == "invalid_limit");

    auto invalidObserveLimitRange = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "observe_events"},
        {"params", {
            {"controlSession", token},
            {"limit", 0}
        }}
    });
    assert(invalidObserveLimitRange["ok"] == false);
    assert(invalidObserveLimitRange["code"] == "invalid_limit");

    auto unknownObserveParam = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "observe_events"},
        {"params", {
            {"controlSession", token},
            {"limit", 5},
            {"raw", true}
        }}
    });
    assert(unknownObserveParam["ok"] == false);
    assert(unknownObserveParam["code"] == "invalid_limit");

    auto invalidObserveEvent = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "observe_frames"},
        {"params", {
            {"controlSession", token},
            {"event", "@evnope"}
        }}
    });
    assert(invalidObserveEvent["ok"] == false);
    assert(invalidObserveEvent["code"] == "invalid_event_ref");

    auto unknownObserveFramesParam = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "observe_frames"},
        {"params", {
            {"controlSession", token},
            {"event", "last"},
            {"raw", true}
        }}
    });
    assert(unknownObserveFramesParam["ok"] == false);
    assert(unknownObserveFramesParam["code"] == "invalid_event_ref");

    auto invalidObserveFramesEventType = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "observe_frames"},
        {"params", {
            {"controlSession", token},
            {"event", true}
        }}
    });
    assert(invalidObserveFramesEventType["ok"] == false);
    assert(invalidObserveFramesEventType["code"] == "invalid_event_ref");

    auto blankObserveFramesEvent = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "observe_frames"},
        {"params", {
            {"controlSession", token},
            {"event", "   "}
        }}
    });
    assert(blankObserveFramesEvent["ok"] == false);
    assert(blankObserveFramesEvent["code"] == "invalid_event_ref");

    auto invalidObserveFramesLimitRange = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "observe_frames"},
        {"params", {
            {"controlSession", token},
            {"event", "@ev1"},
            {"limit", 0}
        }}
    });
    assert(invalidObserveFramesLimitRange["ok"] == false);
    assert(invalidObserveFramesLimitRange["code"] == "invalid_limit");

    auto invalidTargetLimit = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "target_find"},
        {"params", {
            {"controlSession", token},
            {"query", "Continue"},
            {"limit", 2.5}
        }}
    });
    assert(invalidTargetLimit["ok"] == false);
    assert(invalidTargetLimit["code"] == "invalid_limit");

    auto invalidTargetLimitRange = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "target_find"},
        {"params", {
            {"controlSession", token},
            {"query", "Continue"},
            {"limit", 0}
        }}
    });
    assert(invalidTargetLimitRange["ok"] == false);
    assert(invalidTargetLimitRange["code"] == "invalid_limit");

    auto unknownTargetParam = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "target_find"},
        {"params", {
            {"controlSession", token},
            {"query", "Continue"},
            {"raw", true}
        }}
    });
    assert(unknownTargetParam["ok"] == false);
    assert(unknownTargetParam["code"] == "invalid_target");

    auto invalidTargetQueryType = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "target_find"},
        {"params", {
            {"controlSession", token},
            {"query", true}
        }}
    });
    assert(invalidTargetQueryType["ok"] == false);
    assert(invalidTargetQueryType["code"] == "invalid_target");

    auto blankTargetQuery = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "target_find"},
        {"params", {
            {"controlSession", token},
            {"query", "   "}
        }}
    });
    assert(blankTargetQuery["ok"] == false);
    assert(blankTargetQuery["code"] == "invalid_target");

    auto blankQueryWithTarget = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "target_find"},
        {"params", {
            {"controlSession", token},
            {"query", "   "},
            {"target", "role:button"}
        }}
    });
    assert(blankQueryWithTarget["ok"] == true);
    assert(blankQueryWithTarget["data"]["query"] == "role:button");

    auto emptyRoleTargetName = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "target_find"},
        {"params", {
            {"controlSession", token},
            {"query", "role:button[name=\"\"]"}
        }}
    });
    assert(emptyRoleTargetName["ok"] == false);
    assert(emptyRoleTargetName["code"] == "invalid_target");

    auto unknownImageInfoParam = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "image_info"},
        {"params", {
            {"controlSession", token},
            {"path", "/tmp/missing.png"},
            {"raw", true}
        }}
    });
    assert(unknownImageInfoParam["ok"] == false);
    assert(unknownImageInfoParam["code"] == "invalid_image_info");

    auto invalidImageInfoPath = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "image_info"},
        {"params", {
            {"controlSession", token},
            {"path", true}
        }}
    });
    assert(invalidImageInfoPath["ok"] == false);
    assert(invalidImageInfoPath["code"] == "invalid_image_info");

    auto blankImageInfoPath = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "image_info"},
        {"params", {
            {"controlSession", token},
            {"path", "   "}
        }}
    });
    assert(blankImageInfoPath["ok"] == false);
    assert(blankImageInfoPath["code"] == "invalid_image_info");

    auto unknownImageSplitParam = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "image_split"},
        {"params", {
            {"controlSession", token},
            {"path", "/tmp/missing.png"},
            {"raw", true}
        }}
    });
    assert(unknownImageSplitParam["ok"] == false);
    assert(unknownImageSplitParam["code"] == "invalid_image_split");

    auto invalidImageSplitPath = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "image_split"},
        {"params", {
            {"controlSession", token},
            {"path", true}
        }}
    });
    assert(invalidImageSplitPath["ok"] == false);
    assert(invalidImageSplitPath["code"] == "invalid_image_split");

    auto blankImageSplitPath = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "image_split"},
        {"params", {
            {"controlSession", token},
            {"path", "   "}
        }}
    });
    assert(blankImageSplitPath["ok"] == false);
    assert(blankImageSplitPath["code"] == "invalid_image_split");

    auto blankImageSplitOutDir = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "image_split"},
        {"params", {
            {"controlSession", token},
            {"path", "/tmp/missing.png"},
            {"outDir", "   "}
        }}
    });
    assert(blankImageSplitOutDir["ok"] == false);
    assert(blankImageSplitOutDir["code"] == "invalid_image_split");

    auto blankImageSplitOutputDir = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "image_split"},
        {"params", {
            {"controlSession", token},
            {"path", "/tmp/missing.png"},
            {"outputDir", "   "}
        }}
    });
    assert(blankImageSplitOutputDir["ok"] == false);
    assert(blankImageSplitOutputDir["code"] == "invalid_image_split");

    auto invalidImageSplitChunkHeight = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "image_split"},
        {"params", {
            {"controlSession", token},
            {"path", "/tmp/missing.png"},
            {"chunkHeight", 0}
        }}
    });
    assert(invalidImageSplitChunkHeight["ok"] == false);
    assert(invalidImageSplitChunkHeight["code"] == "invalid_image_split");

    auto invalidImageSplitOverlap = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "image_split"},
        {"params", {
            {"controlSession", token},
            {"path", "/tmp/missing.png"},
            {"overlap", -1}
        }}
    });
    assert(invalidImageSplitOverlap["ok"] == false);
    assert(invalidImageSplitOverlap["code"] == "invalid_image_split");

    auto invalidImageSplitSelfOverlap = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "image_split"},
        {"params", {
            {"controlSession", token},
            {"path", "/tmp/missing.png"},
            {"chunkHeight", 64},
            {"overlap", 64}
        }}
    });
    assert(invalidImageSplitSelfOverlap["ok"] == false);
    assert(invalidImageSplitSelfOverlap["code"] == "invalid_image_split");

    auto invalidImageSplitPrefix = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "image_split"},
        {"params", {
            {"controlSession", token},
            {"path", "/tmp/missing.png"},
            {"prefix", ""}
        }}
    });
    assert(invalidImageSplitPrefix["ok"] == false);
    assert(invalidImageSplitPrefix["code"] == "invalid_image_split");

    auto blankImageSplitPrefix = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "image_split"},
        {"params", {
            {"controlSession", token},
            {"path", "/tmp/missing.png"},
            {"prefix", "   "}
        }}
    });
    assert(blankImageSplitPrefix["ok"] == false);
    assert(blankImageSplitPrefix["code"] == "invalid_image_split");

    auto invalidClickTarget = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "click"},
        {"params", {
            {"controlSession", token},
            {"x", "45px"},
            {"y", 72},
            {"motion", "instant"}
        }}
    });
    assert(invalidClickTarget["ok"] == false);
    assert(invalidClickTarget["code"] == "target_not_found");

    auto invalidClickTargetType = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "click"},
        {"params", {
            {"controlSession", token},
            {"target", true}
        }}
    });
    assert(invalidClickTargetType["ok"] == false);
    assert(invalidClickTargetType["code"] == "invalid_click");

    auto invalidClickTiming = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "click"},
        {"params", {
            {"controlSession", token},
            {"target", "point:45,72"},
            {"durationMs", "soon"}
        }}
    });
    assert(invalidClickTiming["ok"] == false);
    assert(invalidClickTiming["code"] == "invalid_click");

    auto invalidInstantClickCount = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "click"},
        {"params", {
            {"controlSession", token},
            {"target", "point:45,72"},
            {"motion", "instant"},
            {"clickCount", 0}
        }}
    });
    assert(invalidInstantClickCount["ok"] == false);
    assert(invalidInstantClickCount["code"] == "invalid_click");

    auto invalidNaturalClickCount = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "click"},
        {"params", {
            {"controlSession", token},
            {"target", "point:45,72"},
            {"clickCount", 6}
        }}
    });
    assert(invalidNaturalClickCount["ok"] == false);
    assert(invalidNaturalClickCount["code"] == "invalid_click");

    auto unknownClickParam = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "click"},
        {"params", {
            {"controlSession", token},
            {"target", "point:45,72"},
            {"raw", true}
        }}
    });
    assert(unknownClickParam["ok"] == false);
    assert(unknownClickParam["code"] == "invalid_click");

    auto emptyClickTarget = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "click"},
        {"params", {
            {"controlSession", token},
            {"target", ""}
        }}
    });
    assert(emptyClickTarget["ok"] == false);
    assert(emptyClickTarget["code"] == "invalid_click");

    auto blankClickTarget = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "click"},
        {"params", {
            {"controlSession", token},
            {"target", "   "}
        }}
    });
    assert(blankClickTarget["ok"] == false);
    assert(blankClickTarget["code"] == "invalid_click");

    auto invalidClickMotion = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "click"},
        {"params", {
            {"controlSession", token},
            {"target", "point:45,72"},
            {"motion", true}
        }}
    });
    assert(invalidClickMotion["ok"] == false);
    assert(invalidClickMotion["code"] == "invalid_click");

    auto emptyClickButton = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "click"},
        {"params", {
            {"controlSession", token},
            {"target", "point:45,72"},
            {"button", ""}
        }}
    });
    assert(emptyClickButton["ok"] == false);
    assert(emptyClickButton["code"] == "invalid_click");

    auto blankClickButton = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "click"},
        {"params", {
            {"controlSession", token},
            {"target", "point:45,72"},
            {"button", "   "}
        }}
    });
    assert(blankClickButton["ok"] == false);
    assert(blankClickButton["code"] == "invalid_click");

    auto emptyClickMotion = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "click"},
        {"params", {
            {"controlSession", token},
            {"target", "point:45,72"},
            {"motion", ""}
        }}
    });
    assert(emptyClickMotion["ok"] == false);
    assert(emptyClickMotion["code"] == "invalid_click");

    auto blankClickMotion = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "click"},
        {"params", {
            {"controlSession", token},
            {"target", "point:45,72"},
            {"motion", "   "}
        }}
    });
    assert(blankClickMotion["ok"] == false);
    assert(blankClickMotion["code"] == "invalid_click");

    auto emptyClickMode = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "click"},
        {"params", {
            {"controlSession", token},
            {"target", "point:45,72"},
            {"clickMode", ""}
        }}
    });
    assert(emptyClickMode["ok"] == false);
    assert(emptyClickMode["code"] == "invalid_click");

    auto blankClickMode = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "click"},
        {"params", {
            {"controlSession", token},
            {"target", "point:45,72"},
            {"clickMode", "   "}
        }}
    });
    assert(blankClickMode["ok"] == false);
    assert(blankClickMode["code"] == "invalid_click");
    assert(blankClickMode["error"].get<std::string>().find("non-empty motion/clickMode") != std::string::npos);

    auto invalidClickHoverSafe = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "click"},
        {"params", {
            {"controlSession", token},
            {"target", "point:45,72"},
            {"hoverSafe", "yes"}
        }}
    });
    assert(invalidClickHoverSafe["ok"] == false);
    assert(invalidClickHoverSafe["code"] == "invalid_click");

    auto invalidClickNegativeDuration = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "click"},
        {"params", {
            {"controlSession", token},
            {"target", "point:45,72"},
            {"durationMs", -1}
        }}
    });
    assert(invalidClickNegativeDuration["ok"] == false);
    assert(invalidClickNegativeDuration["code"] == "invalid_click");

    auto invalidClickStepsRange = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "click"},
        {"params", {
            {"controlSession", token},
            {"target", "point:45,72"},
            {"steps", 121}
        }}
    });
    assert(invalidClickStepsRange["ok"] == false);
    assert(invalidClickStepsRange["code"] == "invalid_click");

    auto invalidClickHoldRange = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "click"},
        {"params", {
            {"controlSession", token},
            {"target", "point:45,72"},
            {"clickHoldMs", -2}
        }}
    });
    assert(invalidClickHoldRange["ok"] == false);
    assert(invalidClickHoldRange["code"] == "invalid_click");

    auto invalidClickParkDurationRange = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "click"},
        {"params", {
            {"controlSession", token},
            {"target", "point:45,72"},
            {"parkDurationMs", 1201}
        }}
    });
    assert(invalidClickParkDurationRange["ok"] == false);
    assert(invalidClickParkDurationRange["code"] == "invalid_click");

    auto invalidClickParkStepsRange = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "click"},
        {"params", {
            {"controlSession", token},
            {"target", "point:45,72"},
            {"parkSteps", -1}
        }}
    });
    assert(invalidClickParkStepsRange["ok"] == false);
    assert(invalidClickParkStepsRange["code"] == "invalid_click");

    auto invalidClickParkFraction = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "click"},
        {"params", {
            {"controlSession", token},
            {"target", "point:45,72"},
            {"hoverSafe", true},
            {"parkBeforeClick", true},
            {"parkXFraction", "right"}
        }}
    });
    assert(invalidClickParkFraction["ok"] == false);
    assert(invalidClickParkFraction["code"] == "invalid_click");

    auto invalidClickParkFractionRange = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "click"},
        {"params", {
            {"controlSession", token},
            {"target", "point:45,72"},
            {"hoverSafe", true},
            {"parkBeforeClick", true},
            {"parkXFraction", 0.01}
        }}
    });
    assert(invalidClickParkFractionRange["ok"] == false);
    assert(invalidClickParkFractionRange["code"] == "invalid_click");

    auto invalidClickRectFraction = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "click"},
        {"params", {
            {"controlSession", token},
            {"target", "rect:10,20,50,80"},
            {"rectClickXFraction", "right"}
        }}
    });
    assert(invalidClickRectFraction["ok"] == false);
    assert(invalidClickRectFraction["code"] == "invalid_click");

    auto invalidClickRectFractionRange = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "click"},
        {"params", {
            {"controlSession", token},
            {"target", "rect:10,20,50,80"},
            {"rectClickYFraction", 1.0}
        }}
    });
    assert(invalidClickRectFractionRange["ok"] == false);
    assert(invalidClickRectFractionRange["code"] == "invalid_click");

    auto invalidMouseMove = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "mouse_move"},
        {"params", {
            {"controlSession", token},
            {"x", "left"},
            {"y", 72}
        }}
    });
    assert(invalidMouseMove["ok"] == false);
    assert(invalidMouseMove["code"] == "invalid_mouse_move");

    auto invalidMouseMoveTiming = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "mouse_move"},
        {"params", {
            {"controlSession", token},
            {"x", 45},
            {"y", 72},
            {"durationMs", "soon"}
        }}
    });
    assert(invalidMouseMoveTiming["ok"] == false);
    assert(invalidMouseMoveTiming["code"] == "invalid_mouse_move");

    auto invalidMouseMoveNegativeTiming = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "mouse_move"},
        {"params", {
            {"controlSession", token},
            {"x", 45},
            {"y", 72},
            {"durationMs", -1}
        }}
    });
    assert(invalidMouseMoveNegativeTiming["ok"] == false);
    assert(invalidMouseMoveNegativeTiming["code"] == "invalid_mouse_move");

    auto unknownMouseMoveParam = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "mouse_move"},
        {"params", {
            {"controlSession", token},
            {"x", 45},
            {"y", 72},
            {"raw", true}
        }}
    });
    assert(unknownMouseMoveParam["ok"] == false);
    assert(unknownMouseMoveParam["code"] == "invalid_mouse_move");

    auto invalidMouseMoveObserve = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "mouse_move"},
        {"params", {
            {"controlSession", token},
            {"x", 45},
            {"y", 72},
            {"observe", "yes"}
        }}
    });
    assert(invalidMouseMoveObserve["ok"] == false);
    assert(invalidMouseMoveObserve["code"] == "invalid_mouse_move");

    auto invalidMouseDragTiming = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "mouse_drag"},
        {"params", {
            {"controlSession", token},
            {"from", "point:1,2"},
            {"to", "point:3,4"},
            {"steps", "many"}
        }}
    });
    assert(invalidMouseDragTiming["ok"] == false);
    assert(invalidMouseDragTiming["code"] == "invalid_mouse_drag");

    auto invalidMouseDragNegativeTiming = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "mouse_drag"},
        {"params", {
            {"controlSession", token},
            {"from", "point:1,2"},
            {"to", "point:3,4"},
            {"steps", -1}
        }}
    });
    assert(invalidMouseDragNegativeTiming["ok"] == false);
    assert(invalidMouseDragNegativeTiming["code"] == "invalid_mouse_drag");

    auto unknownMouseDragParam = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "mouse_drag"},
        {"params", {
            {"controlSession", token},
            {"from", "point:1,2"},
            {"to", "point:3,4"},
            {"raw", true}
        }}
    });
    assert(unknownMouseDragParam["ok"] == false);
    assert(unknownMouseDragParam["code"] == "invalid_mouse_drag");

    auto invalidMouseDragFrom = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "mouse_drag"},
        {"params", {
            {"controlSession", token},
            {"from", true},
            {"to", "point:3,4"}
        }}
    });
    assert(invalidMouseDragFrom["ok"] == false);
    assert(invalidMouseDragFrom["code"] == "invalid_mouse_drag");

    auto emptyMouseDragFrom = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "mouse_drag"},
        {"params", {
            {"controlSession", token},
            {"from", ""},
            {"to", "point:3,4"}
        }}
    });
    assert(emptyMouseDragFrom["ok"] == false);
    assert(emptyMouseDragFrom["code"] == "invalid_mouse_drag");

    auto blankMouseDragTo = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "mouse_drag"},
        {"params", {
            {"controlSession", token},
            {"from", "point:1,2"},
            {"to", "   "}
        }}
    });
    assert(blankMouseDragTo["ok"] == false);
    assert(blankMouseDragTo["code"] == "invalid_mouse_drag");

    auto invalidMouseDragObserve = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "mouse_drag"},
        {"params", {
            {"controlSession", token},
            {"from", "point:1,2"},
            {"to", "point:3,4"},
            {"observe", "yes"}
        }}
    });
    assert(invalidMouseDragObserve["ok"] == false);
    assert(invalidMouseDragObserve["code"] == "invalid_mouse_drag");

    auto emptyMouseDragButton = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "mouse_drag"},
        {"params", {
            {"controlSession", token},
            {"from", "point:1,2"},
            {"to", "point:3,4"},
            {"button", ""}
        }}
    });
    assert(emptyMouseDragButton["ok"] == false);
    assert(emptyMouseDragButton["code"] == "invalid_mouse_drag");

    auto blankMouseDragButton = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "mouse_drag"},
        {"params", {
            {"controlSession", token},
            {"from", "point:1,2"},
            {"to", "point:3,4"},
            {"button", "   "}
        }}
    });
    assert(blankMouseDragButton["ok"] == false);
    assert(blankMouseDragButton["code"] == "invalid_mouse_drag");

    auto invalidMouseDownCount = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "mouse_down"},
        {"params", {
            {"controlSession", token},
            {"clickCount", "two"}
        }}
    });
    assert(invalidMouseDownCount["ok"] == false);
    assert(invalidMouseDownCount["code"] == "invalid_mouse_down");

    auto invalidMouseDownCountRange = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "mouse_down"},
        {"params", {
            {"controlSession", token},
            {"clickCount", 0}
        }}
    });
    assert(invalidMouseDownCountRange["ok"] == false);
    assert(invalidMouseDownCountRange["code"] == "invalid_mouse_down");

    auto invalidMouseDownButton = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "mouse_down"},
        {"params", {
            {"controlSession", token},
            {"button", true}
        }}
    });
    assert(invalidMouseDownButton["ok"] == false);
    assert(invalidMouseDownButton["code"] == "invalid_mouse_down");

    auto emptyMouseDownButton = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "mouse_down"},
        {"params", {
            {"controlSession", token},
            {"button", ""}
        }}
    });
    assert(emptyMouseDownButton["ok"] == false);
    assert(emptyMouseDownButton["code"] == "invalid_mouse_down");

    auto blankMouseDownButton = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "mouse_down"},
        {"params", {
            {"controlSession", token},
            {"button", "   "}
        }}
    });
    assert(blankMouseDownButton["ok"] == false);
    assert(blankMouseDownButton["code"] == "invalid_mouse_down");

    auto unknownMouseDownParam = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "mouse_down"},
        {"params", {
            {"controlSession", token},
            {"button", "left"},
            {"raw", true}
        }}
    });
    assert(unknownMouseDownParam["ok"] == false);
    assert(unknownMouseDownParam["code"] == "invalid_mouse_down");

    auto invalidMouseUpCount = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "mouse_up"},
        {"params", {
            {"controlSession", token},
            {"clickCount", "two"}
        }}
    });
    assert(invalidMouseUpCount["ok"] == false);
    assert(invalidMouseUpCount["code"] == "invalid_mouse_up");

    auto invalidMouseUpCountRange = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "mouse_up"},
        {"params", {
            {"controlSession", token},
            {"clickCount", 6}
        }}
    });
    assert(invalidMouseUpCountRange["ok"] == false);
    assert(invalidMouseUpCountRange["code"] == "invalid_mouse_up");

    auto invalidMouseUpButton = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "mouse_up"},
        {"params", {
            {"controlSession", token},
            {"button", true}
        }}
    });
    assert(invalidMouseUpButton["ok"] == false);
    assert(invalidMouseUpButton["code"] == "invalid_mouse_up");

    auto emptyMouseUpButton = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "mouse_up"},
        {"params", {
            {"controlSession", token},
            {"button", ""}
        }}
    });
    assert(emptyMouseUpButton["ok"] == false);
    assert(emptyMouseUpButton["code"] == "invalid_mouse_up");

    auto blankMouseUpButton = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "mouse_up"},
        {"params", {
            {"controlSession", token},
            {"button", "   "}
        }}
    });
    assert(blankMouseUpButton["ok"] == false);
    assert(blankMouseUpButton["code"] == "invalid_mouse_up");

    auto unknownMouseUpParam = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "mouse_up"},
        {"params", {
            {"controlSession", token},
            {"button", "left"},
            {"raw", true}
        }}
    });
    assert(unknownMouseUpParam["ok"] == false);
    assert(unknownMouseUpParam["code"] == "invalid_mouse_up");

    auto invalidScrollDelta = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "scroll"},
        {"params", {
            {"controlSession", token},
            {"dy", "down"},
            {"dx", 0}
        }}
    });
    assert(invalidScrollDelta["ok"] == false);
    assert(invalidScrollDelta["code"] == "invalid_scroll");

    auto invalidScrollTiming = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "scroll"},
        {"params", {
            {"controlSession", token},
            {"dy", 1},
            {"steps", 2.5}
        }}
    });
    assert(invalidScrollTiming["ok"] == false);
    assert(invalidScrollTiming["code"] == "invalid_scroll");

    auto invalidScrollNegativeDuration = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "scroll"},
        {"params", {
            {"controlSession", token},
            {"dy", 1},
            {"dx", 0},
            {"durationMs", -1}
        }}
    });
    assert(invalidScrollNegativeDuration["ok"] == false);
    assert(invalidScrollNegativeDuration["code"] == "invalid_scroll");

    auto invalidScrollNegativeSteps = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "scroll"},
        {"params", {
            {"controlSession", token},
            {"dy", 1},
            {"dx", 0},
            {"steps", -1}
        }}
    });
    assert(invalidScrollNegativeSteps["ok"] == false);
    assert(invalidScrollNegativeSteps["code"] == "invalid_scroll");

    auto invalidScrollNegativeJitter = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "scroll"},
        {"params", {
            {"controlSession", token},
            {"dy", 1},
            {"dx", 0},
            {"jitter", -0.1}
        }}
    });
    assert(invalidScrollNegativeJitter["ok"] == false);
    assert(invalidScrollNegativeJitter["code"] == "invalid_scroll");

    auto invalidScrollNegativeGestureDelta = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "scroll"},
        {"params", {
            {"controlSession", token},
            {"dy", 1},
            {"dx", 0},
            {"maxGestureDelta", -1}
        }}
    });
    assert(invalidScrollNegativeGestureDelta["ok"] == false);
    assert(invalidScrollNegativeGestureDelta["code"] == "invalid_scroll");

    auto invalidScrollSamplesLow = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "scroll"},
        {"params", {
            {"controlSession", token},
            {"dy", 1},
            {"dx", 0},
            {"samples", 0}
        }}
    });
    assert(invalidScrollSamplesLow["ok"] == false);
    assert(invalidScrollSamplesLow["code"] == "invalid_scroll");

    auto invalidScrollSamplesHigh = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "scroll"},
        {"params", {
            {"controlSession", token},
            {"dy", 1},
            {"dx", 0},
            {"samples", 7}
        }}
    });
    assert(invalidScrollSamplesHigh["ok"] == false);
    assert(invalidScrollSamplesHigh["code"] == "invalid_scroll");

    auto unknownScrollParam = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "scroll"},
        {"params", {
            {"controlSession", token},
            {"dy", 1},
            {"dx", 0},
            {"raw", true}
        }}
    });
    assert(unknownScrollParam["ok"] == false);
    assert(unknownScrollParam["code"] == "invalid_scroll");

    auto invalidScrollHumanize = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "scroll"},
        {"params", {
            {"controlSession", token},
            {"dy", 1},
            {"dx", 0},
            {"humanize", "yes"}
        }}
    });
    assert(invalidScrollHumanize["ok"] == false);
    assert(invalidScrollHumanize["code"] == "invalid_scroll");

    auto invalidScrollAt = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "scroll"},
        {"params", {
            {"controlSession", token},
            {"dy", 1},
            {"dx", 0},
            {"at", true}
        }}
    });
    assert(invalidScrollAt["ok"] == false);
    assert(invalidScrollAt["code"] == "invalid_scroll");

    auto emptyScrollAt = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "scroll"},
        {"params", {
            {"controlSession", token},
            {"dy", 1},
            {"dx", 0},
            {"at", ""}
        }}
    });
    assert(emptyScrollAt["ok"] == false);
    assert(emptyScrollAt["code"] == "invalid_scroll");

    auto blankScrollAt = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "scroll"},
        {"params", {
            {"controlSession", token},
            {"dy", 1},
            {"dx", 0},
            {"at", "   "}
        }}
    });
    assert(blankScrollAt["ok"] == false);
    assert(blankScrollAt["code"] == "invalid_scroll");

    auto emptyScrollFocus = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "scroll"},
        {"params", {
            {"controlSession", token},
            {"dy", 1},
            {"dx", 0},
            {"focusApp", ""}
        }}
    });
    assert(emptyScrollFocus["ok"] == false);
    assert(emptyScrollFocus["code"] == "invalid_scroll");

    auto blankScrollFocus = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "scroll"},
        {"params", {
            {"controlSession", token},
            {"dy", 1},
            {"dx", 0},
            {"focusApp", "   "}
        }}
    });
    assert(blankScrollFocus["ok"] == false);
    assert(blankScrollFocus["code"] == "invalid_scroll");

    auto invalidWaitTiming = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "wait"},
        {"params", {
            {"controlSession", token},
            {"timeoutMs", "soon"}
        }}
    });
    assert(invalidWaitTiming["ok"] == false);
    assert(invalidWaitTiming["code"] == "invalid_wait");

    auto invalidWaitTimeoutRange = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "wait"},
        {"params", {
            {"controlSession", token},
            {"timeoutMs", 0}
        }}
    });
    assert(invalidWaitTimeoutRange["ok"] == false);
    assert(invalidWaitTimeoutRange["code"] == "invalid_wait");

    auto invalidWaitNoPredicate = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "wait"},
        {"params", {
            {"controlSession", token},
            {"timeoutMs", 1}
        }}
    });
    assert(invalidWaitNoPredicate["ok"] == false);
    assert(invalidWaitNoPredicate["code"] == "invalid_wait");

    auto invalidWaitPollRange = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "wait"},
        {"params", {
            {"controlSession", token},
            {"pollMs", 25}
        }}
    });
    assert(invalidWaitPollRange["ok"] == false);
    assert(invalidWaitPollRange["code"] == "invalid_wait");

    auto invalidWaitStableRange = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "wait"},
        {"params", {
            {"controlSession", token},
            {"stableScreenMs", -1}
        }}
    });
    assert(invalidWaitStableRange["ok"] == false);
    assert(invalidWaitStableRange["code"] == "invalid_wait");

    auto validWaitDelay = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "wait"},
        {"params", {
            {"controlSession", token},
            {"delayMs", 1},
            {"timeoutMs", 1}
        }}
    });
    assert(validWaitDelay["ok"] == true);
    assert(validWaitDelay["data"]["matched"] == true);
    assert(validWaitDelay["data"]["evidence"]["delayMs"] == 1);

    auto validUnleasedShortDelay = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "wait"},
        {"params", {
            {"delayMs", 1},
            {"timeoutMs", 1}
        }}
    });
    assert(validUnleasedShortDelay["ok"] == true);
    assert(validUnleasedShortDelay["data"]["matched"] == true);

    auto rejectedUnleasedLongDelay = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "wait"},
        {"params", {
            {"delayMs", 1001},
            {"timeoutMs", 1001}
        }}
    });
    assert(rejectedUnleasedLongDelay["ok"] == false);
    assert(rejectedUnleasedLongDelay["code"] == "control_session_required");

    auto rejectedUnleasedConditionalDelay = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "wait"},
        {"params", {
            {"delayMs", 1},
            {"frontmost", "test"}
        }}
    });
    assert(rejectedUnleasedConditionalDelay["ok"] == false);
    assert(rejectedUnleasedConditionalDelay["code"] == "control_session_required");

    auto invalidWaitDelayRange = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "wait"},
        {"params", {
            {"controlSession", token},
            {"delayMs", -1}
        }}
    });
    assert(invalidWaitDelayRange["ok"] == false);
    assert(invalidWaitDelayRange["code"] == "invalid_wait");

    auto removedWaitText = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "wait"},
        {"params", {
            {"controlSession", token},
            {"text", "Ready"}
        }}
    });
    assert(removedWaitText["ok"] == false);
    assert(removedWaitText["code"] == "invalid_wait");

    auto invalidWaitParam = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "wait"},
        {"params", {
            {"controlSession", token},
            {"frontmostWindowOnly", true}
        }}
    });
    assert(invalidWaitParam["ok"] == false);
    assert(invalidWaitParam["code"] == "invalid_wait");

    auto invalidWaitFrontmost = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "wait"},
        {"params", {
            {"controlSession", token},
            {"frontmost", true}
        }}
    });
    assert(invalidWaitFrontmost["ok"] == false);
    assert(invalidWaitFrontmost["code"] == "invalid_wait");

    auto emptyWaitFrontmost = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "wait"},
        {"params", {
            {"controlSession", token},
            {"frontmost", ""}
        }}
    });
    assert(emptyWaitFrontmost["ok"] == false);
    assert(emptyWaitFrontmost["code"] == "invalid_wait");

    auto blankWaitFrontmost = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "wait"},
        {"params", {
            {"controlSession", token},
            {"frontmost", "   "}
        }}
    });
    assert(blankWaitFrontmost["ok"] == false);
    assert(blankWaitFrontmost["code"] == "invalid_wait");

    auto invalidPressHold = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "press"},
        {"params", {
            {"controlSession", token},
            {"keys", "Escape"},
            {"holdMs", "long"}
        }}
    });
    assert(invalidPressHold["ok"] == false);
    assert(invalidPressHold["code"] == "invalid_key");

    auto invalidPressHoldRange = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "press"},
        {"params", {
            {"controlSession", token},
            {"keys", "Escape"},
            {"holdMs", 0}
        }}
    });
    assert(invalidPressHoldRange["ok"] == false);
    assert(invalidPressHoldRange["code"] == "invalid_key");

    auto invalidPressHoldHigh = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "press"},
        {"params", {
            {"controlSession", token},
            {"keys", "Escape"},
            {"holdMs", 5001}
        }}
    });
    assert(invalidPressHoldHigh["ok"] == false);
    assert(invalidPressHoldHigh["code"] == "invalid_key");

    auto unknownPressParam = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "press"},
        {"params", {
            {"controlSession", token},
            {"keys", "Escape"},
            {"raw", true}
        }}
    });
    assert(unknownPressParam["ok"] == false);
    assert(unknownPressParam["code"] == "invalid_key");

    auto invalidPressKeys = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "press"},
        {"params", {
            {"controlSession", token},
            {"keys", {true}}
        }}
    });
    assert(invalidPressKeys["ok"] == false);
    assert(invalidPressKeys["code"] == "invalid_key");

    auto emptyPressChord = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "press"},
        {"params", {
            {"controlSession", token},
            {"keys", ""}
        }}
    });
    assert(emptyPressChord["ok"] == false);
    assert(emptyPressChord["code"] == "invalid_key");

    auto blankPressChord = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "press"},
        {"params", {
            {"controlSession", token},
            {"keys", "   "}
        }}
    });
    assert(blankPressChord["ok"] == false);
    assert(blankPressChord["code"] == "invalid_key");

    auto emptyPressKeys = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "press"},
        {"params", {
            {"controlSession", token},
            {"keys", nlohmann::json::array()}
        }}
    });
    assert(emptyPressKeys["ok"] == false);
    assert(emptyPressKeys["code"] == "invalid_key");

    auto emptyPressKey = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "press"},
        {"params", {
            {"controlSession", token},
            {"keys", nlohmann::json::array({""})}
        }}
    });
    assert(emptyPressKey["ok"] == false);
    assert(emptyPressKey["code"] == "invalid_key");

    auto blankPressKey = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "press"},
        {"params", {
            {"controlSession", token},
            {"keys", nlohmann::json::array({"   "})}
        }}
    });
    assert(blankPressKey["ok"] == false);
    assert(blankPressKey["code"] == "invalid_key");

    auto invalidTypeHold = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "type"},
        {"params", {
            {"controlSession", token},
            {"text", "hello"},
            {"holdMs", 12.5}
        }}
    });
    assert(invalidTypeHold["ok"] == false);
    assert(invalidTypeHold["code"] == "invalid_type");

    auto invalidTypeHoldRange = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "type"},
        {"params", {
            {"controlSession", token},
            {"text", "hello"},
            {"holdMs", 0}
        }}
    });
    assert(invalidTypeHoldRange["ok"] == false);
    assert(invalidTypeHoldRange["code"] == "invalid_type");

    auto invalidTypeHoldHigh = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "type"},
        {"params", {
            {"controlSession", token},
            {"text", "hello"},
            {"holdMs", 5001}
        }}
    });
    assert(invalidTypeHoldHigh["ok"] == false);
    assert(invalidTypeHoldHigh["code"] == "invalid_type");

    auto unknownTypeParam = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "type"},
        {"params", {
            {"controlSession", token},
            {"text", "hello"},
            {"raw", true}
        }}
    });
    assert(unknownTypeParam["ok"] == false);
    assert(unknownTypeParam["code"] == "invalid_type");

    auto invalidTypePaste = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "type"},
        {"params", {
            {"controlSession", token},
            {"text", "hello"},
            {"paste", "yes"}
        }}
    });
    assert(invalidTypePaste["ok"] == false);
    assert(invalidTypePaste["code"] == "invalid_type");

    auto invalidTypeText = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "type"},
        {"params", {
            {"controlSession", token},
            {"text", true}
        }}
    });
    assert(invalidTypeText["ok"] == false);
    assert(invalidTypeText["code"] == "invalid_type");

    auto emptyTypeText = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "type"},
        {"params", {
            {"controlSession", token},
            {"text", ""}
        }}
    });
    assert(emptyTypeText["ok"] == false);
    assert(emptyTypeText["code"] == "invalid_type");

    auto unknownClipboardWriteParam = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "clipboard_write"},
        {"params", {
            {"controlSession", token},
            {"text", "hello"},
            {"raw", true}
        }}
    });
    assert(unknownClipboardWriteParam["ok"] == false);
    assert(unknownClipboardWriteParam["code"] == "invalid_type");

    auto invalidClipboardWriteText = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "clipboard_write"},
        {"params", {
            {"controlSession", token},
            {"text", true}
        }}
    });
    assert(invalidClipboardWriteText["ok"] == false);
    assert(invalidClipboardWriteText["code"] == "invalid_type");

    auto invalidScreenshotRegion = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "screenshot"},
        {"params", {
            {"controlSession", token},
            {"x", "10px"},
            {"y", 20},
            {"width", 100},
            {"height", 80}
        }}
    });
    assert(invalidScreenshotRegion["ok"] == false);
    assert(invalidScreenshotRegion["code"] == "invalid_screenshot_region");

    auto invalidScreenshotMaxDimension = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "screenshot"},
        {"params", {
            {"controlSession", token},
            {"maxDimension", "large"}
        }}
    });
    assert(invalidScreenshotMaxDimension["ok"] == false);
    assert(invalidScreenshotMaxDimension["code"] == "invalid_screenshot_region");

    auto invalidScreenshotMaxDimensionLow = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "screenshot"},
        {"params", {
            {"controlSession", token},
            {"maxDimension", -1}
        }}
    });
    assert(invalidScreenshotMaxDimensionLow["ok"] == false);
    assert(invalidScreenshotMaxDimensionLow["code"] == "invalid_screenshot_region");

    auto invalidScreenshotMaxDimensionHigh = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "screenshot"},
        {"params", {
            {"controlSession", token},
            {"maxDimension", 8193}
        }}
    });
    assert(invalidScreenshotMaxDimensionHigh["ok"] == false);
    assert(invalidScreenshotMaxDimensionHigh["code"] == "invalid_screenshot_region");

    auto invalidScreenshotFrontmost = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "screenshot"},
        {"params", {
            {"controlSession", token},
            {"frontmostWindowOnly", "yes"}
        }}
    });
    assert(invalidScreenshotFrontmost["ok"] == false);
    assert(invalidScreenshotFrontmost["code"] == "invalid_screenshot");

    auto invalidScreenshotPath = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "screenshot"},
        {"params", {
            {"controlSession", token},
            {"path", true}
        }}
    });
    assert(invalidScreenshotPath["ok"] == false);
    assert(invalidScreenshotPath["code"] == "invalid_screenshot");

    auto blankScreenshotPath = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "screenshot"},
        {"params", {
            {"controlSession", token},
            {"path", "   "}
        }}
    });
    assert(blankScreenshotPath["ok"] == false);
    assert(blankScreenshotPath["code"] == "invalid_screenshot");

    auto unknownScreenshotParam = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "screenshot"},
        {"params", {
            {"controlSession", token},
            {"path", "/tmp/computer.cpp-unknown-param.png"},
            {"raw", true}
        }}
    });
    assert(unknownScreenshotParam["ok"] == false);
    assert(unknownScreenshotParam["code"] == "invalid_screenshot");

    auto invalidSnapshotInteractive = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "snapshot"},
        {"params", {
            {"controlSession", token},
            {"interactive", "yes"}
        }}
    });
    assert(invalidSnapshotInteractive["ok"] == false);
    assert(invalidSnapshotInteractive["code"] == "invalid_snapshot");

    auto invalidSnapshotMaxDepth = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "snapshot"},
        {"params", {
            {"controlSession", token},
            {"maxDepth", 2.5}
        }}
    });
    assert(invalidSnapshotMaxDepth["ok"] == false);
    assert(invalidSnapshotMaxDepth["code"] == "invalid_snapshot");

    auto negativeSnapshotMaxDepth = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "snapshot"},
        {"params", {
            {"controlSession", token},
            {"maxDepth", -1}
        }}
    });
    assert(negativeSnapshotMaxDepth["ok"] == false);
    assert(negativeSnapshotMaxDepth["code"] == "invalid_snapshot");

    auto negativeSnapshotMaxNodes = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "snapshot"},
        {"params", {
            {"controlSession", token},
            {"maxNodes", -1}
        }}
    });
    assert(negativeSnapshotMaxNodes["ok"] == false);
    assert(negativeSnapshotMaxNodes["code"] == "invalid_snapshot");

    auto unknownSnapshotParam = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "snapshot"},
        {"params", {
            {"controlSession", token},
            {"interactive", true},
            {"raw", true}
        }}
    });
    assert(unknownSnapshotParam["ok"] == false);
    assert(unknownSnapshotParam["code"] == "invalid_snapshot");

    auto unknownGetParam = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "get"},
        {"params", {
            {"controlSession", token},
            {"target", "@missing"},
            {"raw", true}
        }}
    });
    assert(unknownGetParam["ok"] == false);
    assert(unknownGetParam["code"] == "invalid_target");

    auto invalidGetTargetType = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "get"},
        {"params", {
            {"controlSession", token},
            {"target", true}
        }}
    });
    assert(invalidGetTargetType["ok"] == false);
    assert(invalidGetTargetType["code"] == "invalid_target");

    auto emptyGetTarget = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "get"},
        {"params", {
            {"controlSession", token},
            {"target", ""}
        }}
    });
    assert(emptyGetTarget["ok"] == false);
    assert(emptyGetTarget["code"] == "invalid_target");

    auto blankGetTarget = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "get"},
        {"params", {
            {"controlSession", token},
            {"target", "   "}
        }}
    });
    assert(blankGetTarget["ok"] == false);
    assert(blankGetTarget["code"] == "invalid_target");

    auto invalidGetField = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "get"},
        {"params", {
            {"controlSession", token},
            {"target", "@missing"},
            {"field", "label"}
        }}
    });
    assert(invalidGetField["ok"] == false);
    assert(invalidGetField["code"] == "invalid_target");

    auto batch = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "batch"},
        {"params", {
            {"controlSession", token},
            {"steps", nlohmann::json::array({
                {{"method", "schema"}, {"params", nlohmann::json::object()}},
                {{"method", "llm_chat"}, {"params", {
                    {"baseUrl", "https://inference.example.test/v1"},
                    {"model", "qwen36-27b"},
                    {"apiKey", ""},
                    {"messages", nlohmann::json::array({
                        {{"role", "user"}, {"content", "ping"}}
                    })}
                }}}
            })},
            {"stopOnError", false}
        }}
    });
    assert(batch["ok"] == true);
    assert(batch["data"]["results"][0]["ok"] == true);
    assert(batch["data"]["results"][1]["code"] == "missing_api_key");

    auto released = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "control_session_release"},
        {"params", {{"token", token}}}
    });
    assert(released["ok"] == true);
    auto afterRelease = ComputerCpp::HandleDaemonRequest("unit", {
        {"method", "llm_chat"},
        {"params", llmParams}
    });
    assert(afterRelease["ok"] == false);
    assert(afterRelease["code"] == "control_session_not_active");
}

} // namespace

namespace ComputerCpp::Tests {

void RunDaemonDispatchTests() {
    TestDaemonDispatch();
    TestDaemonGateCoverageForProtectedMethods();
    TestBatchDoesNotBypassControlSessionGate();
    TestScrollObserveDoesNotCreateEventWhenAnchorFails();
    TestDaemonRequiresControlSessionForProtectedMethods();
}

} // namespace ComputerCpp::Tests
