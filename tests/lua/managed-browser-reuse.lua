local ac = require("computer_cpp")

local current_url = "about:blank"
local typed_url = ""
local bootstrap_calls = 0
local new_window_presses = 0
local native_window_requests = 0

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
    return { ok = true, data = {} }
  end
  if method == "press" then
    if type(params.keys) == "table" and params.keys[1] == "primary" and params.keys[2] == "n" then
      new_window_presses = new_window_presses + 1
    elseif params.keys == "enter" then
      current_url = typed_url
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
}
local first = ac.browser.managed.ensure(options)
local focused = ac.browser.managed.focus(options)

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
}
