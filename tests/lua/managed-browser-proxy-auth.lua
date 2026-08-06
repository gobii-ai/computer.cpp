local ac = require("computer_cpp")

local current_url = "about:blank"
local typed_url = ""
local prompt_visible = false
local proxy_authenticated = false
local proxy_sign_in_clicks = 0

local function browser_data(extra)
  local data = {
    browser = "chrome",
    profile = "chrome-cdp-profile",
    managed = true,
    browserPid = 4242,
    targetId = proxy_authenticated and "target-1" or "bootstrap-target",
    targetUrl = current_url,
    value = current_url,
    proxyConfigured = true,
  }
  for key, value in pairs(extra or {}) do data[key] = value end
  return { ok = true, data = data }
end

local function bounds(x, y, width, height)
  return { available = true, x = x, y = y, width = width, height = height }
end

local function proxy_snapshot()
  if not prompt_visible or proxy_authenticated then
    return { ok = true, data = { frontmostApp = { pid = 4242 }, text = "", refs = {} } }
  end
  return {
    ok = true,
    data = {
      frontmostApp = { pid = 4242 },
      -- Deliberately omit AXDialog/AXSheet. Chrome can flatten this native
      -- prompt into the focused window, which is the recorded regression.
      text = "The proxy https://proxy.example:10080 requires a username and password.",
      refs = {
        { ref = "e1", displayRef = "@e1", role = "AXStaticText", name = "Sign in", value = "", bounds = bounds(700, 260, 80, 24) },
        { ref = "e2", displayRef = "@e2", role = "AXStaticText", name = "The proxy https://proxy.example:10080 requires a username and password.", value = "", bounds = bounds(700, 292, 420, 24) },
        { ref = "e3", displayRef = "@e3", role = "AXTextField", name = "Username", value = "filled-user", bounds = bounds(700, 330, 420, 28) },
        { ref = "e4", displayRef = "@e4", role = "AXSecureTextField", name = "Password", value = "••••••••", bounds = bounds(700, 366, 420, 28) },
        { ref = "e5", displayRef = "@e5", role = "AXButton", name = "Cancel", value = "", bounds = bounds(920, 410, 90, 30) },
        { ref = "e6", displayRef = "@e6", role = "AXButton", name = "Sign in", value = "", bounds = bounds(1020, 410, 100, 30) },
      },
    },
  }
end

ac.request = function(method, params)
  params = params or {}
  if method == "browser_eval" then
    if params.targetFocused == true and not proxy_authenticated then
      return { ok = false, code = "browser_target_not_found", error = "no focused page target" }
    end
    return browser_data({ launched = params.nativeWindow == true })
  end
  if method == "app_activate_pid" or method == "window_activate" or method == "wait" then
    return { ok = true, data = {} }
  end
  if method == "window_active" then
    return { ok = true, data = { window = { available = true, id = "4242", pid = 4242 } } }
  end
  if method == "snapshot" then return proxy_snapshot() end
  if method == "type" then
    typed_url = tostring(params.text or "")
    return { ok = true, data = {} }
  end
  if method == "press" then
    if params.keys == "enter" then prompt_visible = true end
    return { ok = true, data = {} }
  end
  if method == "click" and params.target == "@e6" then
    proxy_sign_in_clicks = proxy_sign_in_clicks + 1
    proxy_authenticated = true
    current_url = typed_url
    return { ok = true, data = {} }
  end
  return { ok = false, code = "unexpected_method", error = method }
end

local result = ac.browser.managed.ensure({
  name = "test.proxy-auth",
  startUrl = "https://example.test/start",
  startUrlPrefix = "https://example.test/",
  launch = true,
})

return {
  ok = result and result.ok == true,
  proxy_authenticated = proxy_authenticated,
  proxy_sign_in_clicks = proxy_sign_in_clicks,
  current_url = current_url,
  target_id = result and result.data and result.data.targetId or "",
}
