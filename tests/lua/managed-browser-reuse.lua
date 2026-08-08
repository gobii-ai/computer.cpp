local ac = require("computer_cpp")

local current_url = "about:blank"
local typed_url = ""
local bootstrap_calls = 0
local new_window_presses = 0
local native_window_requests = 0
local navigation_mode = "success"
local last_type_used_paste = false
local navigation_type_modes = {}

local function browser_data(extra)
  local data = {
    browser = "chrome",
    profile = "default",
    managed = true,
    browserPid = 4242,
    targetId = "target-1",
    targetUrl = current_url,
    value = current_url,
    proxyConfigured = false,
  }
  for key, value in pairs(extra or {}) do data[key] = value end
  return { ok = true, data = data }
end

ac.request = function(method, params)
  params = params or {}
  if method == "browser_eval" then
    if params.nativeWindow == true then
      native_window_requests = native_window_requests + 1
    end
    if params.script == "document.hasFocus()" then
      return browser_data({ value = true })
    end
    if params.targetFocused == true then
      local prefix = tostring(params.targetUrlPrefix or "")
      if prefix ~= "" and current_url:sub(1, #prefix) ~= prefix then
        return { ok = false, code = "browser_target_not_found", error = "no focused matching target" }
      end
      return browser_data()
    end
    if tostring(params.targetId or "") == "" then
      bootstrap_calls = bootstrap_calls + 1
      return browser_data({ launched = bootstrap_calls == 1 })
    end
    return browser_data()
  end
  if method == "app_activate_pid" or method == "window_activate" or method == "wait" then
    return { ok = true, data = {} }
  end
  if method == "window_active" then
    -- Some macOS/Chrome combinations do not expose AXWindowNumber, so the
    -- native window identity falls back to the browser PID.
    return { ok = true, data = { window = { available = true, id = "4242", pid = 4242 } } }
  end
  if method == "snapshot" then
    return { ok = true, data = { frontmostApp = { pid = 4242 }, refs = {} } }
  end
  if method == "type" then
    typed_url = tostring(params.text or "")
    last_type_used_paste = params.paste == true
    table.insert(navigation_type_modes, last_type_used_paste and "paste" or "direct")
    return { ok = true, data = {} }
  end
  if method == "press" then
    if type(params.keys) == "table" and params.keys[1] == "primary" and params.keys[2] == "n" then
      new_window_presses = new_window_presses + 1
    elseif params.keys == "enter" then
      if navigation_mode == "success" or
          (navigation_mode == "retry" and not last_type_used_paste) then
        current_url = typed_url
      elseif navigation_mode == "unrelated" then
        current_url = "https://example.test/unrelated"
      end
    end
    return { ok = true, data = {} }
  end
  return { ok = false, code = "unexpected_method", error = method }
end

local options = {
  name = "test.surface",
  startUrl = "https://example.test/start",
  startUrlPrefix = "https://example.test/",
  launch = true,
  navigationAttemptTimeoutMs = 0,
}
local first = ac.browser.managed.ensure(options)
local focused = ac.browser.managed.focus(options)

navigation_type_modes = {}
navigation_mode = "success"
local first_navigation = ac.browser.managed.navigate(
  "https://example.test/first", options)
local first_navigation_modes = table.concat(navigation_type_modes, ",")

navigation_type_modes = {}
navigation_mode = "retry"
local retry_navigation = ac.browser.managed.navigate(
  "https://example.test/retry", options)
local retry_navigation_modes = table.concat(navigation_type_modes, ",")

navigation_type_modes = {}
navigation_mode = "ignored"
local failed_navigation = ac.browser.managed.navigate(
  "https://example.test/ignored", options)
local failed_navigation_modes = table.concat(navigation_type_modes, ",")

current_url = "https://example.test/canonical/"
navigation_type_modes = {}
local canonical_navigation = ac.browser.managed.navigate(
  "https://EXAMPLE.test:443/canonical#section", options)
local canonical_navigation_modes = table.concat(navigation_type_modes, ",")

current_url = "https://example.test/before-unrelated"
navigation_type_modes = {}
navigation_mode = "unrelated"
local unrelated_navigation = ac.browser.managed.navigate(
  "https://example.test/expected", options)
local unrelated_navigation_modes = table.concat(navigation_type_modes, ",")

current_url = "https://example.test/before-accepted-redirect"
navigation_type_modes = {}
local redirect_options = {}
for key, value in pairs(options) do redirect_options[key] = value end
redirect_options.navigationUrlMatches = function(observed)
  return observed == "https://example.test/unrelated"
end
local redirect_navigation = ac.browser.managed.navigate(
  "https://example.test/redirecting", redirect_options)
local redirect_navigation_modes = table.concat(navigation_type_modes, ",")

local root = os.getenv("COMPUTER_CPP_HOME")
local separator = package.config:sub(1, 1)
local state_file = io.open(root .. separator .. "managed-browser-surfaces.json", "r")
local state_exists = state_file ~= nil
if state_file then state_file:close() end

return {
  first_ok = first and first.ok == true,
  focus_ok = focused and focused.ok == true,
  focus_reused = focused and focused.data and focused.data.reused == true,
  bootstrap_calls = bootstrap_calls,
  new_window_presses = new_window_presses,
  native_window_requests = native_window_requests,
  state_exists = state_exists,
  first_navigation_ok = first_navigation and first_navigation.ok == true,
  first_navigation_attempts = first_navigation and first_navigation.data and first_navigation.data.attempts,
  first_navigation_modes = first_navigation_modes,
  first_navigation_target = first_navigation and first_navigation.data and first_navigation.data.targetId,
  first_navigation_window = first_navigation and first_navigation.data and first_navigation.data.windowId,
  retry_navigation_ok = retry_navigation and retry_navigation.ok == true,
  retry_navigation_attempts = retry_navigation and retry_navigation.data and retry_navigation.data.attempts,
  retry_navigation_modes = retry_navigation_modes,
  retry_navigation_target = retry_navigation and retry_navigation.data and retry_navigation.data.targetId,
  retry_navigation_window = retry_navigation and retry_navigation.data and retry_navigation.data.windowId,
  failed_navigation_ok = failed_navigation and failed_navigation.ok == true,
  failed_navigation_code = failed_navigation and failed_navigation.code,
  failed_navigation_attempts = failed_navigation and failed_navigation.data and failed_navigation.data.attempts,
  failed_navigation_url = failed_navigation and failed_navigation.data and failed_navigation.data.currentUrl,
  failed_navigation_modes = failed_navigation_modes,
  failed_navigation_target = failed_navigation and failed_navigation.data and failed_navigation.data.targetId,
  failed_navigation_window = failed_navigation and failed_navigation.data and failed_navigation.data.windowId,
  canonical_navigation_ok = canonical_navigation and canonical_navigation.ok == true,
  canonical_navigation_attempts = canonical_navigation and canonical_navigation.data and canonical_navigation.data.attempts,
  canonical_navigation_modes = canonical_navigation_modes,
  unrelated_navigation_ok = unrelated_navigation and unrelated_navigation.ok == true,
  unrelated_navigation_code = unrelated_navigation and unrelated_navigation.code,
  unrelated_navigation_url = unrelated_navigation and unrelated_navigation.data and unrelated_navigation.data.currentUrl,
  unrelated_navigation_modes = unrelated_navigation_modes,
  redirect_navigation_ok = redirect_navigation and redirect_navigation.ok == true,
  redirect_navigation_attempts = redirect_navigation and redirect_navigation.data and redirect_navigation.data.attempts,
  redirect_navigation_url = redirect_navigation and redirect_navigation.data and redirect_navigation.data.currentUrl,
  redirect_navigation_modes = redirect_navigation_modes,
}
