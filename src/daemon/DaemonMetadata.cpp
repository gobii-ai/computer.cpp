#include "DaemonMetadata.h"

#include "computer_cpp/ControlSession.h"
#include "computer_cpp/NativeDeps.h"
#include "computer_cpp/RuntimeProvenance.h"

using json = nlohmann::json;

namespace ComputerCpp {

json CapabilitiesJson() {
    auto nativeDeps = NativeDeps::GetVersions();
    return {
        {"apiVersion", "0.1"},
        {"protocol", "json-line-over-local-socket"},
        {"runtime", RuntimeProvenanceToJson(CurrentRuntimeProvenance())},
        {"featureMapSchemaVersion", kFeatureMapSchemaVersion},
        {"controlSessionRequired", true},
        {"controlSessionScope", kDefaultControlScope},
        {"commands", {
            "session.acquire", "session.resume", "session.renew", "session.release", "session.release-active", "session.status", "session.metrics", "session.events", "session.run", "session.exec",
            "state", "snapshot", "snapshot.interactive", "snapshot.bounds", "snapshot.actions", "screenshot", "screenshot.frontmost-window", "screenshot.region", "observe.events", "observe.frames",
            "permissions", "permissions.open-settings",
            "app.active", "app.launch", "app.activate", "app.activate-pid",
            "target.resolve",
            "target.find", "target.explain", "get", "click", "click.multi", "click.motion", "click.timing", "click.hover-safe", "click.park", "mouse.click", "mouse.click.motion", "mouse.move", "mouse.drag", "mouse.down", "mouse.up",
            "window.active", "window.list", "window.close", "window.bounds", "scroll", "scroll.anchor", "scroll.clustered", "scroll.humanize", "scroll.read",
            "image.info", "image.split", "llm.chat", "open.url", "press", "type",
            "browser.eval", "browser.eval.cdp", "browser.eval.read-only",
            "clipboard", "clipboard.read", "clipboard.write", "clipboard.paste",
            "wait", "wait.frontmost", "wait.stable-screen", "batch"
        }},
        {"platforms", {
#if defined(__APPLE__)
            {{"name", "macOS"}, {"implemented", true}}
#elif defined(_WIN32)
            {{"name", "Windows"}, {"implemented", false}}
#else
            {{"name", "Linux"}, {"implemented", false}}
#endif
        }},
        {"refKinds", {"app", "window", "element", "point", "region"}},
        {"artifactKinds", {"screenshot", "timeline_frame", "sqlite_timeline"}},
        {"nativeDeps", {
            {"curl", nativeDeps.curl}
        }}
    };
}

json SchemaJson() {
    return {
        {"request", {
            {"id", "optional non-empty string"},
            {"method", "non-empty string"},
            {"params", "object with controlSession/controlScope for protected methods"}
        }},
        {"response", {{"ok", "boolean"}, {"id", "echoed request id"}, {"data", "object on success"}, {"error", "string on failure"}, {"code", "machine-readable error code"}}},
        {"batchResponse", {
            {"results", "array of per-step responses with step id"},
            {"requested", "number of steps requested"},
            {"executed", "number of step results returned"},
            {"failed", "number of failed step results"},
            {"stoppedOnError", "true when execution stopped early after a failed step"}
        }},
        {"batch", {
            {"steps", "array of step objects; CLI reads the array from stdin"},
            {"stopOnError", "boolean default true; CLI --continue-on-error sends false"},
            {"step", "object with optional non-empty id, required non-empty method string, and optional params object"},
            {"blockedMethods", "nested batch and shutdown steps are rejected with invalid_batch_step"},
            {"control", "batch controlSession/controlScope is propagated to non-control-session steps that do not override it"},
            {"fallbackId", "steps without id are reported as step-1, step-2, and so on"},
            {"response", "results plus requested, executed, failed, and stoppedOnError counters"}
        }},
        {"controlSession", {
            {"scope", kDefaultControlScope},
            {"ttlMs", "lease duration; renew before expiresAtMs"},
            {"acquire", "control_session_acquire/control_session_resume params: optional scope, daemonSession, owner, purpose, ttlMs, waitMs, maxRuntimeMs"},
            {"renew", "control_session_renew params: required non-empty token and optional ttlMs"},
            {"release", "control_session_release params: required non-empty token"},
            {"status", "control_session_status params: optional scope and token; token is omitted from status responses"},
            {"metrics", "control_session_metrics params: optional scope, non-negative staleAfterMs and longRunningAfterMs, positive eventLimit"},
            {"events", "control_session_events params: optional scope and positive limit; response events include redacted token prefix, event, code, message, createdAtMs, and metadata"},
            {"releaseActive", "CLI session release-active debug/audit helper releases current active lease with optional owner/purpose match and reason"},
            {"record", "session record includes scope, daemonSession, owner, purpose, state, createdAtMs, acquiredAtMs, renewedAtMs, expiresAtMs, releasedAtMs, lastSeenAtMs, maxRuntimeMs, and maxExpiresAtMs"},
            {"protectedMethods", "all methods except ping, capabilities, schema, browser_eval, control_session_*, shutdown, metrics/events, and non-requesting permissions"}
        }},
        {"target", {"@ref", "point:x,y", "rect:left,top,right,bottom", "role:button[name=\"Save\"]"}},
        {"targetCommands", {
            {"find", "method target_find; params: query or target string plus positive integer limit default 20"},
            {"resolve", "method target_resolve; params: target string plus positive integer limit default 1; response includes selected target"},
            {"explain", "method target_explain; params: query or target string plus positive integer limit default 20; response includes explanation"},
            {"roleSelector", "role:<role>[name=\"text\"] with name, label, or text filter; role normalization removes ax prefix and maps textbox/textfield to textarea"},
            {"textQuery", "non-role target text is matched against latest snapshot ref name, value, and role"},
            {"coordinateTarget", "point:x,y and rect:left,top,right,bottom resolve to screen coordinate candidates when no ref matches"},
            {"removedVisualSelectors", "text:, exact:, field:, and color: visual selectors are rejected as unsupported_visual_target"},
            {"response", "query, strategy, candidates array, and optional target or explanation"},
            {"candidate", "accessibility candidates include source, target @ref, text, role, bounds, and confidence; coordinate candidates include clickPoint and coordinateSpace"}
        }},
        {"get", {
            {"target", "required latest snapshot ref such as @e1 or e1"},
            {"field", "optional text, value, bounds, or all; default all"},
            {"text", "response includes text plus full ref metadata"},
            {"value", "response includes value plus full ref metadata"},
            {"bounds", "response includes bounds plus full ref metadata"},
            {"all", "response includes full ref metadata"}
        }},
        {"click", {
            {"target", "@ref, point:x,y, rect:left,top,right,bottom, or role selector"},
            {"button", "mouse button string, default left"},
            {"motion", "natural, instant, hover_safe, or implementation-specific motion profile"},
            {"clickCount", "integer 1..5"},
            {"durationMs", "integer 0..5000; alias clickDurationMs"},
            {"steps", "integer 0..120; alias clickSteps"},
            {"clickHoldMs", "integer -1..5000"},
            {"preClickSettleMs", "integer -1..5000"},
            {"hoverSafe", "boolean"},
            {"parkBeforeClick", "boolean"},
            {"parkFractions", "parkXFraction/parkYFraction and rectClickXFraction/rectClickYFraction between 0.05 and 0.95"}
        }},
        {"mouse", {
            {"move", "method mouse_move; params: numeric x/y, non-negative durationMs and steps, optional observe boolean"},
            {"drag", "method mouse_drag; params: non-empty from/to targets, non-empty button default left, non-negative durationMs and steps, optional observe boolean"},
            {"down", "method mouse_down; params: non-empty button default left and clickCount integer 1..5"},
            {"up", "method mouse_up; params: non-empty button default left and clickCount integer 1..5"},
            {"observe", "mouse_move and mouse_drag may attach observed event metadata and timeline db when observe is true"},
            {"response", "move returns x/y; drag returns from/to points, button, durationMs, and steps"}
        }},
        {"press", {
            {"keys", "required key chord as string such as Cmd+L or string array of non-empty keys"},
            {"holdMs", "integer 1..5000, default 40"},
            {"response", "resolved keys array"}
        }},
        {"type", {
            {"text", "required non-empty text"},
            {"target", "optional target resolved and clicked before typing when found"},
            {"paste", "boolean default false; true uses paste text path instead of per-character typing"},
            {"holdMs", "integer 1..5000, default 20 for per-character typing"},
            {"response", "characters count"}
        }},
        {"clipboard", {
            {"read", "method clipboard_read; response includes available boolean and text"},
            {"write", "method clipboard_write; params: text string; response includes written boolean"},
            {"paste", "method clipboard_paste; response includes pasted boolean from Cmd+V hotkey"}
        }},
        {"screenshot", {
            {"path", "optional output path; empty or omitted creates a timestamped artifact"},
            {"frontmostWindowOnly", "boolean; capture frontmost window bounds instead of full screen"},
            {"maxDimension", "integer 0..8192; 0 disables scaling"},
            {"region", "optional x/y/width/height numbers; width and height must be positive"},
            {"response", "path plus optional maxDimension, frontmostWindowBounds, and region"}
        }},
        {"image", {
            {"info", "params: path; response: path, width, height"},
            {"split", "params: path, optional outDir/outputDir, chunkHeight, overlap, prefix"},
            {"chunkHeight", "positive integer; default 1500 and clamped to image height"},
            {"overlap", "non-negative integer smaller than chunkHeight; default 160"},
            {"prefix", "non-empty chunk filename prefix, default chunk"},
            {"response", "path, width, height, chunkHeight, overlap, and chunks with path/index/x/y/width/height"}
        }},
        {"llm", {
            {"chat", "method llm_chat; OpenAI-compatible /chat/completions request"},
            {"config", "profile defaults come from config.toml; use `computer.cpp config path`, `config set-provider`, and `config set-profile` to edit the canonical config"},
            {"profile", "optional profile name; defaults to config.toml default_profile; explicit request model/baseUrl/apiKey/provider remain compatibility overrides"},
            {"model", "model defaults from the selected profile; explicit model always wins"},
            {"openrouter", "provider type openrouter defaults to https://openrouter.ai/api/v1 and supports model openrouter/auto plus OpenRouter provider routing preferences"},
            {"openaiCompatible", "provider type openai-compatible uses the configured base_url and may omit an API key for localhost, loopback, and private-network endpoints"},
            {"messages", "messages array is required by the model request body; content items with type image_path or image_path string are converted to data image_url items"},
            {"imagePaths", "optional array of local image paths appended to the last message as image_url content"},
            {"forwardedOptions", "profile defaults and request overrides support max_output_tokens, max_tokens, max_completion_tokens, temperature, top_p, tools, tool_choice, chat_template_kwargs, frequency_penalty, presence_penalty, seed, response_format, and provider-specific options"},
            {"responseFormat", "response_format string json is normalized to {type: json_object}"},
            {"apiKeyPolicy", "remote base URLs require an API key; localhost, loopback, and private-network base URLs may omit it"},
            {"timeout", "timeoutMs default 180000 and clamped to 1..900000"},
            {"response", "provider, baseUrl, model, assistant content, assistant message, usage, and raw provider JSON"}
        }},
        {"openUrl", {
            {"url", "required non-empty http or https URL"},
            {"browser", "optional non-empty app name or bundle id; default firefox"},
            {"newWindow", "boolean; default true"},
            {"newInstance", "boolean; default false"},
            {"response", "url, browser, newWindow, newInstance, and opened window metadata when available"}
        }},
        {"browserEval", {
            {"method", "browser_eval"},
            {"script", "required non-empty read-only JavaScript expression for DOM/page inspection"},
            {"targetUrlPrefix", "optional URL prefix that the Chrome DevTools page target must match"},
            {"browserContextId", "optional Chrome DevTools browser context id filter"},
            {"browser", "optional browser app name for launch attempt; default Google Chrome"},
            {"host", "optional loopback Chrome DevTools host; default 127.0.0.1"},
            {"port", "optional Chrome DevTools port; default 9222"},
            {"launch", "boolean default true; attempts to start Chrome with --remote-debugging-port when endpoint is unavailable"},
            {"readOnly", "must be true; obvious DOM/input mutation snippets are rejected"},
            {"response", "backend cdp, value, JavaScript result type, host, port, targetUrlPrefix, and browserPid when querying without a prefix"},
            {"inputBoundary", "browser_eval is for inspection only; user-like input must use native click/type/press/mouse commands"}
        }},
        {"window", {
            {"active", "response: current active window metadata"},
            {"list", "params: optional non-empty app filter; response: windows array"},
            {"bounds", "params: x/y/width/height numbers and optional non-negative pid; width and height must be positive"},
            {"close", "params: optional id and frontmost boolean; response: found/closed plus window metadata when available"},
            {"responseWindow", "window metadata includes id, title, app, pid, active, available, and bounds"}
        }},
        {"app", {
            {"active", "response: current frontmost app metadata"},
            {"launch", "params: non-empty query app name or bundle id; app.activate is an alias"},
            {"activatePid", "params: positive integer pid"},
            {"responseApp", "app metadata includes available, pid, name, and bundleId"},
            {"launchResponse", "launched boolean plus app metadata and opened window metadata when available"}
        }},
        {"permissions", {
            {"check", "params: optional request boolean; response: accessibility and screenCapture booleans"},
            {"openSettings", "params: optional pane accessibility, screen, screen-capture, or screen-recording"},
            {"openResponse", "opened boolean and normalized pane accessibility or screen"}
        }},
        {"state", {
            {"response", "session, permissions, frontmostApp, focusedElement, frontmostWindowBounds, screen, and cursor"},
            {"screen", "width and height pixels"},
            {"cursor", "x and y coordinates"}
        }},
        {"snapshot", {
            {"interactive", "boolean default false; restrict snapshot to interactive elements when true"},
            {"bounds", "boolean default false; include element bounds when supported"},
            {"actions", "boolean default false; include element actions when supported"},
            {"maxDepth", "non-negative integer default 8"},
            {"maxNodes", "non-negative integer default 700"},
            {"response", "text, frontmostApp, frontmostWindowBounds, refs array, refStore path, and optional warning"},
            {"refs", "saved to the session ref store for target/get lookup"}
        }},
        {"scroll", {
            {"delta", "dy and dx integers"},
            {"timing", "optional non-negative durationMs, steps, jitter, and maxGestureDelta/maxScrollGestureDelta"},
            {"humanize", "boolean default true; large scrolls may be planned into clustered human-like gestures"},
            {"anchor", "optional at target with atOffsetX/atOffsetY plus anchor and centerAnchor booleans"},
            {"focusGuard", "optional non-empty focusApp checked against the frontmost app"},
            {"observe", "optional observe boolean and samples integer 1..6 for timeline frame capture"},
            {"response", "dy, dx, durationMs, steps, jitter, humanized, maxGestureDelta, optional clusters, anchor, focusApp/frontmost, observed event metadata, and db"}
        }},
        {"wait", {
            {"frontmost", "optional non-empty substring matched against the frontmost app, bundle id, window class, or active-window title"},
            {"stableScreenMs", "non-negative integer; require screenshot-size stability for at least this many ms"},
            {"delayMs", "non-negative integer up to 120000; platform-neutral delay, usable by itself or before other conditions; delay-only calls above 1000 require a control session"},
            {"timeoutMs", "integer 1..120000, default 10000"},
            {"pollMs", "integer 50..5000, default 250"},
            {"response", "matched boolean and evidence including delayMs, frontmostApp/window, and stableScreenMs when requested"},
            {"timeout", "wait_timeout error when conditions are not met before timeoutMs"}
        }},
        {"bounds", {{"available", "boolean"}, {"x", "number"}, {"y", "number"}, {"width", "number"}, {"height", "number"}}},
        {"errors", {
            "app_activate_pid_failed", "app_launch_failed", "artifact_write_failed", "bad_request",
            "control_session_busy", "control_session_expired", "control_session_not_active",
            "control_session_not_found", "control_session_not_holder", "control_session_owner_mismatch", "control_session_purpose_mismatch",
            "control_session_required", "control_session_scope_mismatch", "curl_failed", "curl_init_failed",
            "exception", "focus_guard_failed", "image_crop_failed", "image_read_failed",
            "inference_bad_json", "inference_http_error", "input_failed", "invalid_app", "invalid_batch", "invalid_control_scope",
            "invalid_batch_step", "invalid_click", "invalid_control_session", "invalid_event_ref",
            "invalid_browser_eval", "invalid_image_info", "invalid_image_split", "invalid_key", "invalid_limit",
            "invalid_llm_request", "invalid_mouse_down", "invalid_mouse_drag", "invalid_mouse_move",
            "invalid_mouse_up", "invalid_permissions", "invalid_screenshot", "invalid_screenshot_region",
            "invalid_scroll", "invalid_snapshot", "invalid_target", "invalid_type", "invalid_url",
            "invalid_wait", "invalid_window", "browser_debug_invalid_response", "browser_debug_unavailable",
            "browser_eval_failed", "browser_eval_timeout", "browser_target_not_found", "missing_api_key", "open_url_failed",
            "permission_or_capture_failed", "target_not_found", "unknown_method", "unsupported_visual_target",
            "wait_timeout", "window_close_failed"
        }}
    };
}

json PermissionToJson(const Platform::PermissionStatus& status) {
    return {
        {"accessibility", status.accessibility},
        {"screenCapture", status.screenCapture}
    };
}

} // namespace ComputerCpp
