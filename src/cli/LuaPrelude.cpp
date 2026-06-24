#include "LuaPrelude.h"

#include <nlohmann/json.hpp>

#include <sstream>

using json = nlohmann::json;

namespace ComputerCpp {

namespace {

std::string LuaStringLiteral(const std::string& value) {
    std::ostringstream out;
    out << '"';
    for (unsigned char ch : value) {
        switch (ch) {
            case '\\': out << "\\\\"; break;
            case '"': out << "\\\""; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (ch < 0x20) {
                    out << "\\x";
                    const char* hex = "0123456789ABCDEF";
                    out << hex[(ch >> 4) & 0x0f] << hex[ch & 0x0f];
                } else {
                    out << static_cast<char>(ch);
                }
        }
    }
    out << '"';
    return out.str();
}

} // namespace

std::string LuaPreludeSource(const LuaRunOptions& options) {
    json context = {
        {"executable", options.executablePath.string()},
        {"session", options.session},
        {"control_session", options.controlSessionToken},
        {"control_scope", options.controlScope},
        {"vars", options.vars},
        {"dry_run", options.dryRun},
        {"agent_stdio", options.agentStdio},
        {"json_output", options.jsonOutput}
    };

    std::ostringstream out;
    out << "local __ac_context_json = " << LuaStringLiteral(context.dump()) << "\n";
    out << R"LUA(
local json = {}
local json_array_mt = { __ac_json_array = true }

function json.array(items)
  return setmetatable(items or {}, json_array_mt)
end

local function encode_string(value)
  local replacements = {
    ['"'] = '\\"',
    ['\\'] = '\\\\',
    ['\b'] = '\\b',
    ['\f'] = '\\f',
    ['\n'] = '\\n',
    ['\r'] = '\\r',
    ['\t'] = '\\t',
  }
  return '"' .. tostring(value):gsub('[%z\1-\31\\"]', function(ch)
    return replacements[ch] or string.format('\\u%04x', ch:byte())
  end) .. '"'
end

local function is_array(value)
  if type(value) ~= "table" then return false end
  if getmetatable(value) == json_array_mt then return true end
  local count, max = 0, 0
  for key, _ in pairs(value) do
    if type(key) ~= "number" or key < 1 or key % 1 ~= 0 then
      return false
    end
    count = count + 1
    if key > max then max = key end
  end
  return count > 0 and count == max
end

function json.encode(value)
  local value_type = type(value)
  if value == nil then
    return "null"
  elseif value_type == "boolean" or value_type == "number" then
    return tostring(value)
  elseif value_type == "string" then
    return encode_string(value)
  elseif value_type == "table" then
    if is_array(value) then
      local parts = {}
      for i = 1, #value do
        parts[#parts + 1] = json.encode(value[i])
      end
      return "[" .. table.concat(parts, ",") .. "]"
    end
    local keys = {}
    for key, _ in pairs(value) do
      keys[#keys + 1] = key
    end
    table.sort(keys, function(a, b) return tostring(a) < tostring(b) end)
    local parts = {}
    for _, key in ipairs(keys) do
      parts[#parts + 1] = encode_string(key) .. ":" .. json.encode(value[key])
    end
    return "{" .. table.concat(parts, ",") .. "}"
  end
  error("cannot JSON encode value of type " .. value_type, 2)
end

function json.decode(input)
  local text = tostring(input)
  local index = 1

  local function fail(message)
    error("JSON decode error at byte " .. index .. ": " .. message, 3)
  end

  local function peek()
    return text:sub(index, index)
  end

  local function skip_ws()
    while true do
      local ch = peek()
      if ch == " " or ch == "\n" or ch == "\r" or ch == "\t" then
        index = index + 1
      else
        return
      end
    end
  end

  local parse_value

  local function parse_string()
    if peek() ~= '"' then fail("expected string") end
    index = index + 1
    local parts = {}
    while index <= #text do
      local ch = peek()
      if ch == '"' then
        index = index + 1
        return table.concat(parts)
      elseif ch == "\\" then
        index = index + 1
        local esc = peek()
        local map = { ['"'] = '"', ['\\'] = '\\', ['/'] = '/', b = '\b', f = '\f', n = '\n', r = '\r', t = '\t' }
        if map[esc] then
          parts[#parts + 1] = map[esc]
          index = index + 1
        elseif esc == "u" then
          local hex = text:sub(index + 1, index + 4)
          if not hex:match("^%x%x%x%x$") then fail("bad unicode escape") end
          local code = tonumber(hex, 16)
          if code < 128 then
            parts[#parts + 1] = string.char(code)
          else
            parts[#parts + 1] = "?"
          end
          index = index + 5
        else
          fail("bad escape")
        end
      else
        parts[#parts + 1] = ch
        index = index + 1
      end
    end
    fail("unterminated string")
  end

  local function parse_number()
    local start = index
    local ch = peek()
    if ch == "-" then index = index + 1 end
    while peek():match("%d") do index = index + 1 end
    if peek() == "." then
      index = index + 1
      while peek():match("%d") do index = index + 1 end
    end
    ch = peek()
    if ch == "e" or ch == "E" then
      index = index + 1
      ch = peek()
      if ch == "+" or ch == "-" then index = index + 1 end
      while peek():match("%d") do index = index + 1 end
    end
    local number = tonumber(text:sub(start, index - 1))
    if number == nil then fail("bad number") end
    return number
  end

  local function parse_array()
    index = index + 1
    local result = json.array({})
    skip_ws()
    if peek() == "]" then
      index = index + 1
      return result
    end
    while true do
      result[#result + 1] = parse_value()
      skip_ws()
      local ch = peek()
      if ch == "]" then
        index = index + 1
        return result
      elseif ch == "," then
        index = index + 1
      else
        fail("expected ',' or ']'")
      end
    end
  end

  local function parse_object()
    index = index + 1
    local result = {}
    skip_ws()
    if peek() == "}" then
      index = index + 1
      return result
    end
    while true do
      skip_ws()
      local key = parse_string()
      skip_ws()
      if peek() ~= ":" then fail("expected ':'") end
      index = index + 1
      result[key] = parse_value()
      skip_ws()
      local ch = peek()
      if ch == "}" then
        index = index + 1
        return result
      elseif ch == "," then
        index = index + 1
      else
        fail("expected ',' or '}'")
      end
    end
  end

  function parse_value()
    skip_ws()
    local ch = peek()
    if ch == '"' then return parse_string() end
    if ch == "{" then return parse_object() end
    if ch == "[" then return parse_array() end
    if ch == "-" or ch:match("%d") then return parse_number() end
    if text:sub(index, index + 3) == "true" then index = index + 4; return true end
    if text:sub(index, index + 4) == "false" then index = index + 5; return false end
    if text:sub(index, index + 3) == "null" then index = index + 4; return nil end
    fail("unexpected token")
  end

  local result = parse_value()
  skip_ws()
  if index <= #text then fail("trailing input") end
  return result
end

local context = json.decode(__ac_context_json)
local key_alias = {
  timeout_ms = "timeoutMs",
  base_url = "baseUrl",
  api_key = "apiKey",
  poll_ms = "pollMs",
  frontmost_window = "frontmostWindowOnly",
  stable_screen_ms = "stableScreenMs",
  max_dimension = "maxDimension",
  max_dim = "maxDimension",
  output_path = "path",
  output = "path",
  click_count = "clickCount",
  hover_safe = "hoverSafe",
  duration_ms = "durationMs",
  hold_ms = "holdMs",
  focus_app = "focusApp",
  at_offset_x = "atOffsetX",
  at_offset_y = "atOffsetY",
  center_anchor = "centerAnchor",
  frontmost_window_only = "frontmostWindowOnly",
  max_depth = "maxDepth",
  max_nodes = "maxNodes",
  max_gesture_delta = "maxGestureDelta",
  max_scroll_gesture_delta = "maxScrollGestureDelta",
  park_before_click = "parkBeforeClick",
  park_x_fraction = "parkXFraction",
  park_y_fraction = "parkYFraction",
  click_hold_ms = "clickHoldMs",
  pre_click_settle_ms = "preClickSettleMs",
  out_dir = "outDir",
  output_dir = "outputDir",
  chunk_height = "chunkHeight",
  new_window = "newWindow",
  new_instance = "newInstance",
}

local function normalize(value)
  if type(value) ~= "table" then return value end
  local out = {}
  for key, item in pairs(value) do
    if key == "no_anchor" then
      out.anchor = not item
    elseif key == "no_humanize" then
      out.humanize = not item
    elseif key == "no_new_window" then
      out.newWindow = not item
    elseif key == "no_new_instance" then
      out.newInstance = not item
    elseif key == "no_park_before_click" then
      out.parkBeforeClick = not item
    elseif key == "instant" then
      if item then out.motion = "instant" end
    else
      local normalized_key = type(key) == "string" and (key_alias[key] or key) or key
      out[normalized_key] = normalize(item)
    end
  end
  return out
end

local function merge(base, opts)
  local out = normalize(base or {})
  opts = normalize(opts or {})
  for key, value in pairs(opts) do
    out[key] = value
  end
  return out
end

local function option_value(opts, name, alias, default)
  if type(opts) ~= "table" then return default end
  local value = opts[name]
  if value == nil and alias ~= nil then
    value = opts[alias]
  end
  if value == nil then return default end
  return value
end

local function should_redact_key(key)
  local lowered = tostring(key):lower()
  return lowered == "apikey"
    or lowered == "api_key"
    or lowered == "authorization"
    or lowered == "baseurl"
    or lowered == "base_url"
    or lowered == "token"
    or lowered == "api_token"
    or lowered == "access_token"
    or lowered == "refresh_token"
    or lowered == "controlsession"
    or lowered == "control_session"
    or lowered:find("secret", 1, true) ~= nil
end

local function redacted(value)
  if type(value) ~= "table" then return value end
  local out = {}
  for key, item in pairs(value) do
    if should_redact_key(key) then
      out[key] = "<redacted>"
    else
      out[key] = redacted(item)
    end
  end
  return out
end

local function logging_enabled()
  local value = os.getenv("COMPUTER_CPP_LOG")
  if value == nil or value == "" then return true end
  value = tostring(value):lower()
  return value ~= "0" and value ~= "false" and value ~= "off" and value ~= "quiet"
end

local log_enabled = logging_enabled()

local function short_string(value, limit)
  limit = limit or 160
  local text = tostring(value or "")
  text = text:gsub("\n", "\\n"):gsub("\r", "\\r")
  if #text > limit then
    return text:sub(1, limit - 3) .. "..."
  end
  return text
end

local function table_count(value)
  if type(value) ~= "table" then return 0 end
  local count = 0
  for _, _ in pairs(value) do
    count = count + 1
  end
  return count
end

local function compact_value(value, depth)
  depth = depth or 2
  local value_type = type(value)
  if value == nil then return "nil" end
  if value_type == "boolean" or value_type == "number" then return tostring(value) end
  if value_type == "string" then return '"' .. short_string(value, 120) .. '"' end
  if value_type ~= "table" then return tostring(value) end
  if depth <= 0 then return "{...}" end
  if type(value.__ac_image_path) == "string" then
    return "{image=" .. compact_value(value.__ac_image_path, 0) .. "}"
  end

  local parts = {}
  local keys = {}
  for key, _ in pairs(value) do
    keys[#keys + 1] = key
  end
  table.sort(keys, function(a, b) return tostring(a) < tostring(b) end)
  for _, key in ipairs(keys) do
    if #parts >= 6 then
      parts[#parts + 1] = "..."
      break
    end
    local item = value[key]
    if type(item) ~= "function" then
      parts[#parts + 1] = tostring(key) .. "=" .. compact_value(item, depth - 1)
    end
  end
  return "{" .. table.concat(parts, ", ") .. "}"
end

local function log_line(kind, message, fields)
  if not log_enabled then return end
  local parts = {}
  if type(fields) == "table" then
    local keys = {}
    for key, _ in pairs(fields) do
      keys[#keys + 1] = key
    end
    table.sort(keys, function(a, b) return tostring(a) < tostring(b) end)
    for _, key in ipairs(keys) do
      local value = fields[key]
      if value ~= nil and value ~= "" then
        if should_redact_key(key) then
          value = "<redacted>"
        else
          value = redacted(value)
        end
        parts[#parts + 1] = tostring(key) .. "=" .. compact_value(value, 2)
      end
    end
  end
  local suffix = #parts > 0 and ("  " .. table.concat(parts, " ")) or ""
  io.stderr:write(string.format("[%s] computer.cpp %-9s %s%s\n", os.date("%H:%M:%S"), tostring(kind), tostring(message), suffix))
  io.stderr:flush()
end

local function usage_fields(usage)
  if type(usage) ~= "table" then return {} end
  return {
    prompt_tokens = usage.prompt_tokens or usage.input_tokens,
    completion_tokens = usage.completion_tokens or usage.output_tokens,
    total_tokens = usage.total_tokens,
  }
end

local function count_tool_calls(message)
  local calls = type(message) == "table" and message.tool_calls or nil
  return type(calls) == "table" and #calls or 0
end

local function content_char_count(message)
  if type(message) ~= "table" then return 0 end
  local content = message.content
  if type(content) == "string" then return #content end
  if type(content) ~= "table" then return 0 end
  local count = 0
  for _, item in ipairs(content) do
    if type(item) == "table" and type(item.text) == "string" then
      count = count + #item.text
    elseif type(item) == "string" then
      count = count + #item
    end
  end
  return count
end

local function reasoning_char_count(message)
  if type(message) ~= "table" then return 0 end
  if type(message.reasoning_content) == "string" then return #message.reasoning_content end
  if type(message.reasoningContent) == "string" then return #message.reasoningContent end
  if type(message.reasoning) == "string" then return #message.reasoning end
  return 0
end

local function summarize_action_params(method, params)
  params = params or {}
  if method == "llm_chat" then
    return {
      provider = params.provider,
      baseUrl = params.baseUrl,
      model = params.model,
      messages = type(params.messages) == "table" and #params.messages or nil,
      tools = type(params.tools) == "table" and #params.tools or nil,
      tool_choice = params.tool_choice,
      temperature = params.temperature,
      max_tokens = params.max_tokens or params.max_completion_tokens,
    }
  elseif method == "click" then
    return {
      target = params.target,
      rect = params.rect,
      button = params.button,
      count = params.clickCount,
    }
  elseif method == "screenshot" then
    return {
      maxDimension = params.maxDimension,
      frontmostWindowOnly = params.frontmostWindowOnly,
      path = params.path,
      region = params.x and { x = params.x, y = params.y, width = params.width, height = params.height } or nil,
    }
  elseif method == "type" then
    return {
      chars = type(params.text) == "string" and #params.text or nil,
      paste = params.paste,
      target = params.target,
    }
  elseif method == "press" then
    return { keys = params.keys, holdMs = params.holdMs }
  elseif method == "scroll" then
    return { dy = params.dy, dx = params.dx, target = params.target or params.at }
  elseif method == "wait" then
    return {
      frontmost = params.frontmost,
      stableScreenMs = params.stableScreenMs,
      timeoutMs = params.timeoutMs,
      pollMs = params.pollMs,
    }
  elseif method == "app_launch" then
    return { query = params.query }
  end
  return redacted(params)
end

local function shell_quote(value)
  return "'" .. tostring(value):gsub("'", "'\\''") .. "'"
end

local function capture(command, allow_nonzero)
  local handle = assert(io.popen(command .. " 2>&1", "r"))
  local output = handle:read("*a")
  local ok, why, code = handle:close()
  if ok == true or ok == 0 then
    return output
  end
  if allow_nonzero then
    return output
  end
  error("command failed (" .. tostring(code or why or ok) .. "): " .. command .. "\n" .. output, 3)
end

local trace = {}
local ac = {
  vars = context.vars or {},
  trace = trace,
  json = json,
}
local deferred = {}

function ac.defer(fn)
  if type(fn) ~= "function" then
    error("ac.defer requires a function", 2)
  end
  deferred[#deferred + 1] = fn
  return fn
end

function ac.finally(fn)
  return ac.defer(fn)
end

local function run_deferred()
  local errors = {}
  for i = #deferred, 1, -1 do
    local fn = deferred[i]
    deferred[i] = nil
    local ok, err = xpcall(fn, debug.traceback)
    if not ok then
      errors[#errors + 1] = tostring(err)
    end
  end
  return #errors == 0, errors
end

local function dry_run_batch(steps)
  local results = {}
  for i, step in ipairs(steps) do
    local data = {
      dryRun = true,
      method = step.method,
      params = step.params or {},
    }
    if step.method == "screenshot" then
      data.path = data.params.path or "/tmp/computer.cpp-dry-run-screenshot.png"
      data.width = tonumber(data.params.maxDimension) or 1000
      data.height = math.floor(data.width * 0.75)
      data.frontmostWindowBounds = {
        available = true,
        x = 0,
        y = 0,
        width = data.width,
        height = data.height,
      }
    end
    results[i] = {
      ok = true,
      id = step.id or ("step-" .. tostring(i)),
      data = data,
    }
  end
  return {
    ok = true,
    data = {
      results = results,
      requested = #steps,
      executed = #results,
      failed = 0,
      stoppedOnError = false,
    },
  }
end

function ac.batch(steps, opts)
  opts = opts or {}
  local normalized_steps = {}
  for i, step in ipairs(steps) do
    normalized_steps[i] = {
      id = step.id or ("step-" .. tostring(i)),
      method = assert(step.method, "batch step missing method"),
      params = normalize(step.params or {}),
    }
  end

  for _, step in ipairs(normalized_steps) do
    log_line("action", step.method, {
      id = step.id,
      params = summarize_action_params(step.method, step.params),
    })
  end

  local response
  if context.dry_run then
    response = dry_run_batch(normalized_steps)
  else
    local tmp = os.tmpname()
    local output
    local file
    local ok, err = pcall(function()
      file = assert(io.open(tmp, "w"))
      file:write(json.encode(normalized_steps))
      file:close()
      file = nil
      local command_parts = {
        shell_quote(context.executable),
        "--session", shell_quote(context.session),
      }
      if context.control_session and context.control_session ~= "" then
        command_parts[#command_parts + 1] = "--control-session"
        command_parts[#command_parts + 1] = shell_quote(context.control_session)
      end
      command_parts[#command_parts + 1] = "--control-scope"
      command_parts[#command_parts + 1] = shell_quote(context.control_scope or "desktop:local")
      if not opts.allow_start then
        command_parts[#command_parts + 1] = "--no-start"
      end
      command_parts[#command_parts + 1] = "--json"
      command_parts[#command_parts + 1] = "batch"
      command_parts[#command_parts + 1] = "<"
      command_parts[#command_parts + 1] = shell_quote(tmp)
      local command = table.concat(command_parts, " ")
      output = capture(command, true)
    end)
    if file then
      pcall(function() file:close() end)
    end
    os.remove(tmp)
    if not ok then
      error(err, 0)
    end
    local decoded
    ok, err = pcall(function()
      decoded = json.decode(output)
    end)
    if not ok then
      error("computer.cpp batch returned invalid JSON: " .. tostring(err), 2)
    end
    response = decoded
  end

  trace[#trace + 1] = { kind = "batch", steps = redacted(normalized_steps), response = response }
  local results = response.data and response.data.results or {}
  for i, result in ipairs(results) do
    local step = normalized_steps[i] or {}
    log_line("action", result.ok and "ok" or "failed", {
      id = result.id or step.id,
      method = step.method,
      code = result.code,
      error = result.error,
    })
  end
  if not opts.allow_error then
    if not response.ok then
      log_line("action", "batch failed", { error = response.error })
      error(response.error or "computer.cpp batch failed", 2)
    end
    for i, result in ipairs(response.data and response.data.results or {}) do
      if not result.ok then
        local step_id = result.id or ("step-" .. tostring(i))
        error("computer.cpp step " .. tostring(step_id) .. " failed: " .. tostring(result.error or result.code), 2)
      end
    end
  end
  return response
end

function ac.batch_result(response, id)
  for _, result in ipairs(response.data and response.data.results or {}) do
    if result.id == id then
      return result.data or result
    end
  end
  return {}
end

ac.value = {}
function ac.value.string(value, default)
  if value == nil then return default or "" end
  return tostring(value)
end
function ac.value.boolean(value, default)
  if value == nil and default ~= nil then return default == true end
  return value == true
end
function ac.value.array(items)
  return json.array(items or {})
end

ac.text = {}
function ac.text.trim(value)
  return tostring(value or ""):match("^%s*(.-)%s*$")
end
function ac.text.normalized(value)
  return ac.text.trim(value):gsub("%s+", " "):lower()
end
function ac.text.present(value)
  return ac.text.trim(value) ~= ""
end
function ac.text.equals(a, b)
  return ac.text.normalized(a) == ac.text.normalized(b)
end

ac.response = {}
function ac.response.data(response)
  return response and (response.data or response) or {}
end

ac.rect = {}
function ac.rect.normalize(rect)
  if type(rect) ~= "table" then return rect end
  if rect.left ~= nil or rect.top ~= nil or rect.right ~= nil or rect.bottom ~= nil then
    return rect
  end
  if rect[1] ~= nil and rect[2] ~= nil and rect[3] ~= nil and rect[4] ~= nil then
    return {
      left = tonumber(rect[1]),
      top = tonumber(rect[2]),
      right = tonumber(rect[3]),
      bottom = tonumber(rect[4]),
    }
  end
  return rect
end
function ac.rect.center(rect)
  rect = ac.rect.normalize(rect)
  local left = tonumber(rect and rect.left)
  local top = tonumber(rect and rect.top)
  local right = tonumber(rect and rect.right)
  local bottom = tonumber(rect and rect.bottom)
  if not left or not top or not right or not bottom or right <= left or bottom <= top then
    return nil
  end
  return {
    x = (left + right) / 2,
    y = (top + bottom) / 2,
    width = right - left,
    height = bottom - top,
  }
end
function ac.rect.valid(rect)
  return ac.rect.center(rect) ~= nil
end
function ac.rect.vertical_distance(a, b)
  local ca = ac.rect.center(a)
  local cb = ac.rect.center(b)
  if not ca or not cb then return nil end
  return math.abs(ca.y - cb.y)
end
function ac.rect.horizontal_gap(left_rect, right_rect)
  left_rect = ac.rect.normalize(left_rect)
  right_rect = ac.rect.normalize(right_rect)
  local right_edge = tonumber(left_rect and left_rect.right)
  local left_edge = tonumber(right_rect and right_rect.left)
  if not right_edge or not left_edge then return nil end
  return left_edge - right_edge
end

function ac.request(method, params, opts)
  opts = opts or {}
  local response = ac.batch({ { method = method, params = params or {} } }, { allow_error = true, allow_start = opts.allow_start })
  local result = response.data and response.data.results and response.data.results[1] or response
  if not opts.allow_error and not result.ok then
    error("computer.cpp " .. tostring(method) .. " failed: " .. tostring(result.error or result.code), 2)
  end
  return result
end

	function ac.ping() return ac.request("ping") end
	function ac.state() return ac.request("state") end
	ac.permissions = setmetatable({
	  open_settings = function(pane)
	    return ac.request("open_permissions", pane and { pane = pane } or {})
	  end,
	}, {
	  __call = function(_, opts) return ac.request("permissions", opts or {}) end,
	})
	function ac.capabilities() return ac.request("capabilities") end
	function ac.schema() return ac.request("schema") end

	ac.session = {}
	function ac.session.token() return context.control_session or "" end
	function ac.session.scope() return context.control_scope or "desktop:local" end
	function ac.session.acquire(opts)
	  opts = merge({
	    scope = context.control_scope or "desktop:local",
	    daemonSession = context.session or "default",
	    owner = "lua-app",
	    purpose = "desktop automation",
	    ttlMs = 60000,
	    waitMs = 0,
	    maxRuntimeMs = 0,
	  }, opts or {})
	  local result = ac.request("control_session_acquire", opts, { allow_error = true, allow_start = true })
	  if result.ok and result.data and result.data.session and type(result.data.session.token) == "string" then
	    context.control_session = result.data.session.token
	    context.control_scope = opts.scope
	  end
	  return result
	end
	function ac.session.resume(opts)
	  opts = merge({
	    scope = context.control_scope or "desktop:local",
	    daemonSession = context.session or "default",
	    owner = "lua-app",
	    purpose = "desktop automation",
	    ttlMs = 60000,
	    waitMs = 0,
	    maxRuntimeMs = 0,
	  }, opts or {})
	  local result = ac.request("control_session_resume", opts, { allow_error = true, allow_start = true })
	  if result.ok and result.data and result.data.session and type(result.data.session.token) == "string" then
	    context.control_session = result.data.session.token
	    context.control_scope = opts.scope
	  end
	  return result
	end
	function ac.session.renew(ttl_ms)
	  return ac.request("control_session_renew", { token = context.control_session or "", ttlMs = tonumber(ttl_ms) or nil }, { allow_error = true })
	end
	function ac.session.release()
	  local result = ac.request("control_session_release", { token = context.control_session or "" }, { allow_error = true })
	  if result.ok then
	    context.control_session = ""
	  end
	  return result
	end
	function ac.session.with_desktop_control(opts, fn)
	  if type(opts) == "function" and fn == nil then
	    fn = opts
	    opts = {}
	  end
	  if type(fn) ~= "function" then
	    error("ac.session.with_desktop_control requires a function", 2)
	  end
	  opts = merge({
	    scope = context.control_scope or "desktop:local",
	    daemonSession = context.session or "default",
	    owner = "lua-app",
	    purpose = "desktop automation",
	    ttlMs = 60000,
	    waitMs = 0,
	    maxRuntimeMs = 0,
	  }, opts or {})

	  local acquired = false
	  if context.control_session == nil or context.control_session == "" then
	    local lease = ac.session.resume(opts)
	    if not lease.ok then
	      error("could not acquire desktop control session: " .. tostring(lease.error or lease.code), 2)
	    end
	    acquired = lease.data and lease.data.session and type(lease.data.session.token) == "string"
	    if not acquired and not context.dry_run then
	      error("could not acquire desktop control session: missing session token", 2)
	    end
	  else
	    ac.session.renew(opts.ttlMs)
	  end

	  local ok, result = xpcall(fn, debug.traceback)
	  if acquired then
	    ac.session.release()
	  end
	  if not ok then
	    error(result, 0)
	  end
	  return result
	end

local function jsonable_copy(value)
  local value_type = type(value)
  if value_type == "nil" or value_type == "boolean" or value_type == "number" or value_type == "string" then
    return value
  end
  if value_type ~= "table" then
    return nil
  end
  local out = {}
  for key, item in pairs(value) do
    if type(item) ~= "function" then
      local copied = jsonable_copy(item)
      if copied ~= nil then
        out[key] = copied
      end
    end
  end
  return out
end

local function has_schema_type(value)
  return type(value) == "table" and type(value.type) == "string"
end

local function field_map_to_schema(fields, include_required)
  local schema = {
    type = "object",
    properties = {},
    additionalProperties = false,
  }
  local required = {}
  for name, field in pairs(fields or {}) do
    if type(field) == "table" then
      local property = jsonable_copy(field) or {}
      if property.required then
        required[#required + 1] = name
      end
      property.required = nil
      schema.properties[name] = property
    end
  end
  table.sort(required)
  if include_required and #required > 0 then
    schema.required = required
  end
  return schema
end

local function normalize_schema(value, include_required)
  if has_schema_type(value) then
    return jsonable_copy(value)
  end
  return field_map_to_schema(value or {}, include_required)
end

ac.schemas = {}
function ac.schemas.rect()
  return {
    type = "object",
    description = "Pixel rectangle in the latest screenshot image.",
    properties = {
      left = { type = "number", minimum = 0, description = "Left edge in screenshot pixels." },
      top = { type = "number", minimum = 0, description = "Top edge in screenshot pixels." },
      right = { type = "number", minimum = 0, description = "Right edge in screenshot pixels." },
      bottom = { type = "number", minimum = 0, description = "Bottom edge in screenshot pixels." },
    },
    required = { "bottom", "left", "right", "top" },
    additionalProperties = false,
  }
end
function ac.schemas.rect_like()
  return {
    type = "object",
    description = "Rectangle as {left, top, right, bottom}; arrays [left, top, right, bottom] are accepted.",
  }
end
function ac.schemas.array(items)
  return {
    type = "array",
    items = jsonable_copy(items or {}),
  }
end
function ac.schemas.object(fields, opts)
  opts = opts or {}
  return normalize_schema(fields or {}, opts.required ~= false)
end

local AppDefinition = {}
AppDefinition.__index = AppDefinition

function AppDefinition:command(name, spec)
  if type(name) ~= "string" or name == "" then
    error("app:command requires a non-empty command name", 2)
  end
  if type(spec) ~= "table" then
    error("app:command requires a command spec table", 2)
  end
  if type(spec.handler) ~= "function" then
    error("app:command " .. name .. " requires a handler function", 2)
  end
  if self.commands[name] == nil then
    self.command_order[#self.command_order + 1] = name
  end
  self.commands[name] = spec
  return self
end

local function app_schema(app)
  local commands = {}
  for _, name in ipairs(app.command_order or {}) do
    local spec = app.commands[name] or {}
    commands[name] = {
      description = spec.description or "",
      input = normalize_schema(spec.input or {}, true),
      output = normalize_schema(spec.output or {}, false),
    }
  end
  local schema = {
    name = app.name or "",
    title = app.title or app.name or "",
    version = app.version or "",
    commands = commands,
  }
  if app.description then
    schema.description = app.description
  end
  return schema
end

local function schema_required_set(schema)
  local out = {}
  if type(schema) ~= "table" or type(schema.required) ~= "table" then
    return out
  end
  for _, name in ipairs(schema.required) do
    out[name] = true
  end
  return out
end

local function validate_value(schema, value, path, apply_defaults)
  if type(schema) ~= "table" then
    return true, value
  end
  local expected = schema.type
  if value == nil then
    return true, value
  end
  if expected == "string" then
    if type(value) ~= "string" then return false, path .. " must be a string" end
    return true, value
  elseif expected == "integer" then
    if type(value) ~= "number" or value % 1 ~= 0 then return false, path .. " must be an integer" end
    if schema.minimum ~= nil and value < schema.minimum then return false, path .. " must be at least " .. tostring(schema.minimum) end
    if schema.maximum ~= nil and value > schema.maximum then return false, path .. " must be at most " .. tostring(schema.maximum) end
    return true, value
  elseif expected == "number" then
    if type(value) ~= "number" then return false, path .. " must be a number" end
    if schema.minimum ~= nil and value < schema.minimum then return false, path .. " must be at least " .. tostring(schema.minimum) end
    if schema.maximum ~= nil and value > schema.maximum then return false, path .. " must be at most " .. tostring(schema.maximum) end
    return true, value
  elseif expected == "boolean" then
    if type(value) ~= "boolean" then return false, path .. " must be a boolean" end
    return true, value
  elseif expected == "array" then
    if type(value) ~= "table" or not is_array(value) then return false, path .. " must be an array" end
    if schema.items then
      for i, item in ipairs(value) do
        local ok, err = validate_value(schema.items, item, path .. "[" .. tostring(i) .. "]", apply_defaults)
        if not ok then return false, err end
      end
    end
    return true, value
  elseif expected == "object" then
    if type(value) ~= "table" then return false, path .. " must be an object" end
    local properties = schema.properties or {}
    local required = schema_required_set(schema)
    for name, _ in pairs(required) do
      if value[name] == nil then
        return false, path .. "." .. name .. " is required"
      end
    end
    if apply_defaults then
      for name, property in pairs(properties) do
        if value[name] == nil and type(property) == "table" and property.default ~= nil then
          value[name] = property.default
        end
      end
    end
    if schema.additionalProperties == false then
      for name, _ in pairs(value) do
        if properties[name] == nil then
          return false, path .. "." .. tostring(name) .. " is not allowed"
        end
      end
    end
    for name, property in pairs(properties) do
      if value[name] ~= nil then
        local ok, err = validate_value(property, value[name], path .. "." .. name, apply_defaults)
        if not ok then return false, err end
      end
    end
    return true, value
  end
  return true, value
end

local function write_json_file(path, value)
  local file = assert(io.open(path, "w"))
  file:write(json.encode(value))
  file:write("\n")
  file:close()
end

local function read_json_file(path)
  local file = io.open(path, "r")
  if not file then return nil end
  local text = file:read("*a")
  file:close()
  if text == nil or text == "" then return nil end
  local ok, decoded = pcall(function() return json.decode(text) end)
  if not ok then return nil end
  return decoded
end

local function make_app_context(command_name, input)
  local operation_dir = context.vars and context.vars.__ac_operation_dir or nil
  local ctx = {
    command = command_name,
    input = input,
    progress_history = {},
    state_table = {},
  }
  function ctx:progress(value)
    self.progress_history[#self.progress_history + 1] = value
    trace[#trace + 1] = { kind = "progress", value = redacted(value) }
    log_line("progress", value and value.step or "update", redacted(value))
    if operation_dir and operation_dir ~= "" then
      pcall(write_json_file, operation_dir .. "/progress.json", value)
    end
    return value
  end
  function ctx:trace(name, value)
    trace[#trace + 1] = { kind = "trace", name = tostring(name), value = redacted(value) }
    return value
  end
  function ctx:artifact(path_or_bytes, metadata)
    local entry = { path = tostring(path_or_bytes or ""), metadata = metadata or {} }
    trace[#trace + 1] = { kind = "artifact", value = redacted(entry) }
    return entry
  end
  function ctx:screenshot(options)
    return ac.screenshot(nil, options or {})
  end
  function ctx:cancelled()
    if not operation_dir or operation_dir == "" then return false end
    local operation = read_json_file(operation_dir .. "/operation.json")
    return operation and operation.cancel_requested == true
  end
  function ctx:deadline()
    return context.vars and context.vars.__ac_deadline or nil
  end
  function ctx:logger()
    return { log = function(_, message) ac.log(message) end }
  end
  function ctx:state()
    return self.state_table
  end
  return ctx
end

function ac.cancelled()
  return { __ac_status = "cancelled" }
end

local function handle_app_mode(app)
  if type(app) ~= "table" or getmetatable(app) ~= AppDefinition then
    return { ok = false, code = "invalid_app", error = "Lua app file must return an ac.app.define app object" }
  end
  local mode = context.vars and context.vars.__ac_app_mode or ""
  if mode == "schema" then
    return { ok = true, data = app_schema(app) }
  end
  if mode ~= "run" then
    return { ok = false, code = "invalid_app_mode", error = "unknown app mode: " .. tostring(mode) }
  end

  local function app_error_message(raw_error)
    if type(raw_error) == "table" then
      return tostring(raw_error.message or raw_error.error or raw_error.code or "operation failed")
    end
    local text = tostring(raw_error or "")
    text = text:gsub("\r\n", "\n")
    local stack_start = text:find("\nstack traceback:", 1, true)
    if stack_start then
      text = text:sub(1, stack_start - 1)
    end
    local line = text:match("([^\n]*)") or text
    while true do
      local stripped = line:match("^.-%.lua:%d+:%s*(.+)$") or line:match("^%[string.-%]:%d+:%s*(.+)$")
      if not stripped or stripped == line then
        break
      end
      line = stripped
    end
    line = line:gsub("^%s+", ""):gsub("%s+$", "")
    if line == "" then
      return "operation failed"
    end
    return line
  end

  local command_name = tostring(context.vars.__ac_app_command or "")
  local command = app.commands[command_name]
  if not command then
    return { ok = false, code = "unknown_command", error = "unknown command: " .. command_name }
  end
  local input = {}
  local input_json = context.vars.__ac_app_input_json or "{}"
  local decode_ok, decoded = pcall(function() return json.decode(input_json) end)
  if not decode_ok or type(decoded) ~= "table" then
    return { ok = false, code = "invalid_input", error = "command input must be a JSON object" }
  end
  input = decoded
  local input_schema = normalize_schema(command.input or {}, true)
  local input_ok, input_err = validate_value(input_schema, input, "input", true)
  if not input_ok then
    return { ok = false, code = "invalid_input", error = input_err }
  end

  local ctx = make_app_context(command_name, input)
  log_line("app", "start", {
    command = command_name,
    input = redacted(input),
  })
  ctx:trace("input", input)
  local started_at = os.clock()
  local run_ok, result = xpcall(function()
    return command.handler(ctx, input)
  end, debug.traceback)
  if not run_ok then
    local message = app_error_message(result)
    log_line("app", "failed", {
      command = command_name,
      elapsed_ms = math.floor((os.clock() - started_at) * 1000),
      error = message,
    })
    return {
      ok = false,
      code = "operation_failed",
      error = message,
      data = { trace = trace, progress = ctx.progress_history, error = { message = message, raw = tostring(result) } },
    }
  end
  if type(result) == "table" and result.__ac_status == "cancelled" then
    log_line("app", "cancelled", {
      command = command_name,
      elapsed_ms = math.floor((os.clock() - started_at) * 1000),
    })
    return {
      ok = false,
      code = "operation_cancelled",
      error = "operation cancelled",
      data = { trace = trace, progress = ctx.progress_history },
    }
  end

  local output_schema = normalize_schema(command.output or {}, false)
  local output_ok, output_err = validate_value(output_schema, result, "result", false)
  if not output_ok then
    log_line("app", "invalid output", {
      command = command_name,
      elapsed_ms = math.floor((os.clock() - started_at) * 1000),
      error = output_err,
    })
    return {
      ok = false,
      code = "invalid_output",
      error = output_err,
      data = { trace = trace, progress = ctx.progress_history },
    }
  end
  ctx:trace("result", result)
  log_line("app", "done", {
    command = command_name,
    elapsed_ms = math.floor((os.clock() - started_at) * 1000),
    result = redacted(result),
  })
  return {
    ok = true,
    data = {
      result = result,
      trace = trace,
      progress = ctx.progress_history,
    }
  }
end

	ac.app = {}
function ac.app.define(spec)
  spec = spec or {}
  if type(spec) ~= "table" then
    error("ac.app.define requires a table", 2)
  end
  if type(spec.name) ~= "string" or spec.name == "" then
    error("ac.app.define requires a non-empty name", 2)
  end
  return setmetatable({
    name = spec.name,
    title = spec.title or spec.name,
    version = spec.version or "",
    description = spec.description,
    commands = {},
    command_order = {},
  }, AppDefinition)
end
function ac.app.active() return ac.request("app_active") end
function ac.app.launch(query) return ac.request("app_launch", { query = query }) end
function ac.app.activate_pid(pid) return ac.request("app_activate_pid", { pid = tonumber(pid) or 0 }) end
ac.app.activate = ac.app.launch

ac.window = {}
function ac.window.active() return ac.request("window_active") end
function ac.window.list(opts) return ac.request("window_list", opts or {}) end
function ac.window.close(id, opts)
  opts = opts or {}
  if id ~= nil and id ~= "" then opts.id = id end
  return ac.request("window_close", opts)
end

ac.browser = {}
function ac.browser.open(url, opts)
  return ac.request("open_url", merge({ url = url, browser = "firefox", newWindow = true }, opts or {}))
end

function ac.wait(opts, request_opts) return ac.request("wait", opts or {}, request_opts or {}) end
function ac.wait_frontmost(app, opts) return ac.wait(merge({ frontmost = app }, opts)) end
function ac.wait_stable_screen(ms, opts) return ac.wait(merge({ stable_screen_ms = ms }, opts)) end

function ac.snapshot(opts) return ac.request("snapshot", opts or {}) end
function ac.screenshot(path, opts)
  local params = merge(opts or {}, path and { path = path } or {})
  return ac.request("screenshot", params)
end
function ac.screenshot_region(path, x, y, width, height, opts)
  return ac.screenshot(path, merge({ x = x, y = y, width = width, height = height }, opts or {}))
end

ac.desktop = {}

local function response_data(response)
  return ac.response.data(response)
end

local function focus_app_name(app, opts)
  if type(app) == "string" and app ~= "" then return app end
  if type(opts) ~= "table" then return nil end
  return opts.focusApp or opts.focus_app or opts.app
end

function ac.desktop.focus_app(app, opts)
  opts = opts or {}
  local name = focus_app_name(app, opts)
  if type(name) ~= "string" or name == "" then
    return { ok = true, data = { focused = false } }
  end
  return ac.batch({
    { id = "focus-launch", method = "app_launch", params = { query = name } },
    {
      id = "focus-frontmost",
      method = "wait",
      params = {
        frontmost = name,
        timeoutMs = option_value(opts, "timeoutMs", "timeout_ms", 10000),
        pollMs = option_value(opts, "pollMs", "poll_ms", 200),
      },
    },
  }, {
    allow_error = option_value(opts, "allowError", "allow_error", false) == true,
    allow_start = option_value(opts, "allowStart", "allow_start", nil),
  })
end

function ac.desktop.with_app(ctx, app, opts, fn)
  opts = opts or {}
  if type(fn) ~= "function" then
    error("ac.desktop.with_app requires a function", 2)
  end

  local purpose = option_value(opts, "purpose", nil, "desktop automation")
  local control_step = option_value(opts, "controlStep", "control_step", "desktop-control")
  if control_step ~= false and ctx and ctx.progress then
    ctx:progress({ step = control_step, purpose = purpose })
  end

  return ac.session.with_desktop_control({
    scope = option_value(opts, "scope", nil, nil),
    daemonSession = option_value(opts, "daemonSession", "daemon_session", nil),
    owner = option_value(opts, "owner", nil, "lua-app"),
    purpose = purpose,
    ttlMs = option_value(opts, "ttlMs", "ttl_ms", 60000),
    waitMs = option_value(opts, "waitMs", "wait_ms", 0),
    maxRuntimeMs = option_value(opts, "maxRuntimeMs", "max_runtime_ms", 0),
  }, function()
    local launch_step = option_value(opts, "launchStep", "launch_step", "launch-app")
    if launch_step ~= false and ctx and ctx.progress then
      ctx:progress({ step = launch_step })
    end
    ac.desktop.focus_app(app, {
      timeoutMs = option_value(opts, "focusTimeoutMs", "focus_timeout_ms", option_value(opts, "timeoutMs", "timeout_ms", 10000)),
      pollMs = option_value(opts, "focusPollMs", "focus_poll_ms", option_value(opts, "pollMs", "poll_ms", 200)),
      allowError = option_value(opts, "focusAllowError", "focus_allow_error", false),
      allowStart = option_value(opts, "allowStart", "allow_start", nil),
    })
    return fn()
  end)
end

function ac.desktop.batch_for_app(app, steps, opts)
  opts = opts or {}
  local name = focus_app_name(app, opts)
  if type(name) == "string" and name ~= "" then
    ac.desktop.focus_app(name, {
      timeoutMs = option_value(opts, "focusTimeoutMs", "focus_timeout_ms", option_value(opts, "timeoutMs", "timeout_ms", 10000)),
      pollMs = option_value(opts, "focusPollMs", "focus_poll_ms", option_value(opts, "pollMs", "poll_ms", 200)),
      allowError = option_value(opts, "focusAllowError", "focus_allow_error", false),
      allowStart = option_value(opts, "allowStart", "allow_start", nil),
    })
  end
  return ac.batch(steps or {}, {
    allow_error = option_value(opts, "allowError", "allow_error", false) == true,
    allow_start = option_value(opts, "allowStart", "allow_start", nil),
  })
end

function ac.desktop.step_for_app(app, id, method, params, opts)
  return ac.batch_result(ac.desktop.batch_for_app(app, {
    { id = id, method = method, params = params or {} },
  }, opts or {}), id)
end

function ac.desktop.remember_screenshot(store, data)
  data = data or {}
  if type(data.path) == "string" and data.path ~= "" and (data.width == nil or data.height == nil) then
    local ok, info = pcall(function()
      return ac.request("image_info", { path = data.path }, { allow_error = true })
    end)
    if ok and info and info.ok and type(info.data) == "table" then
      data.width = data.width or info.data.width
      data.height = data.height or info.data.height
    end
  end
  if type(store) == "table" then
    store.last_screenshot_bounds = data.frontmostWindowBounds
    store.last_screenshot_image_width = data.width
    store.last_screenshot_image_height = data.height
  end
  if type(data.path) == "string" and data.path ~= "" then
    data.__ac_image_path = data.path
  end
  return data
end

function ac.desktop.capture_screenshot(store, opts)
  opts = opts or {}
  local params = {
    maxDimension = option_value(opts, "maxDimension", "max_dimension", 1200),
    frontmostWindowOnly = option_value(opts, "frontmostWindowOnly", "frontmost_window_only", false),
  }
  local path = option_value(opts, "path", "outputPath", nil)
  if path ~= nil then params.path = path end
  return ac.desktop.remember_screenshot(store, response_data(ac.screenshot(nil, params)))
end

function ac.desktop.capture_required_screenshot(ctx, store, opts)
  opts = opts or {}
  local step = option_value(opts, "progressStep", "progress_step", nil)
  if step ~= nil and ctx and ctx.progress then
    ctx:progress({ step = step })
  end
  local screenshot = ac.desktop.capture_screenshot(store, opts)
  local path = screenshot.__ac_image_path or screenshot.path or screenshot.image
  if type(path) ~= "string" or path == "" then
    error(option_value(opts, "errorMessage", "error_message", "screenshot did not return an image path"), 2)
  end
  screenshot.__ac_image_path = path
  return screenshot
end

function ac.desktop.screenshot_result(store, opts)
  opts = opts or {}
  local screenshot = ac.desktop.capture_required_screenshot(nil, store, opts)
  local result = {
    screenshot = screenshot,
    image = screenshot.__ac_image_path,
    __ac_image_path = screenshot.__ac_image_path,
  }
  local reason = option_value(opts, "reason", nil, nil)
  if ac.text.present(reason) then
    result.reason = reason
    result.ignored = option_value(opts, "ignored", nil, true)
  end
  return ac.tool_result.ok(result)
end

function ac.desktop.screenshot_state(screenshot, opts)
  opts = opts or {}
  local coordinate_space = option_value(opts, "coordinateSpace", "coordinate_space", "model_1000")
  local coordinate_rule = option_value(opts, "coordinateRule", "coordinate_rule", nil)
  if coordinate_rule == nil then
    if coordinate_space == "model_1000" or coordinate_space == "image_1000" then
      coordinate_rule = "x and y both range from 0 to 1000 across the screenshot"
    elseif coordinate_space == "screenshot_pixels" or coordinate_space == "image_pixels" then
      coordinate_rule = "rect coordinates are pixels in the screenshot image"
    elseif coordinate_space == "normalized" then
      coordinate_rule = "x and y both range from 0 to 1 across the screenshot"
    else
      coordinate_rule = "rect coordinates use " .. tostring(coordinate_space)
    end
  end
  return {
    coordinate_space = coordinate_space,
    coordinate_rule = coordinate_rule,
    width = screenshot and screenshot.width or nil,
    height = screenshot and screenshot.height or nil,
  }
end

function ac.desktop.vision_task(ctx, spec)
  spec = spec or {}
  local store = spec.store or {}
  local coordinate_space = option_value(spec, "coordinateSpace", "coordinate_space", "model_1000")

  local function capture_and_prepare()
    local screenshot = ac.desktop.capture_required_screenshot(ctx, store, merge({
      progressStep = option_value(spec, "screenshotStep", "screenshot_step", "screenshot"),
      maxDimension = option_value(spec, "maxDimension", "max_dimension", 1200),
      frontmostWindowOnly = option_value(spec, "frontmostWindowOnly", "frontmost_window_only", true),
    }, spec.screenshot or {}))

    local data = merge({}, spec.data or {})
    data.screenshot = ac.desktop.screenshot_state(screenshot, {
      coordinateSpace = coordinate_space,
      coordinateRule = option_value(spec, "coordinateRule", "coordinate_rule", nil),
    })
    local user_content = ac.micro_agent.user_content(data, screenshot)
    local out = {
      store = store,
      screenshot = screenshot,
      data = data,
      user_content = user_content,
    }

    if type(spec.agent) == "table" then
      local agent = merge({}, spec.agent)
      agent.user_content = agent.user_content or user_content
      if agent.state == nil then
        agent.state = data
      end
      out.result = ac.micro_agent.run(ctx, agent)
    end

    return out
  end

  local app = focus_app_name(spec.app, spec)
  if type(app) == "string" and app ~= "" then
    return ac.desktop.with_app(ctx, app, spec, capture_and_prepare)
  end
  return capture_and_prepare()
end

function ac.desktop.screenshot_for_app(app, store, opts)
  opts = opts or {}
  local name = focus_app_name(app, opts)
  if type(name) == "string" and name ~= "" then
    ac.desktop.focus_app(name, {
      timeoutMs = option_value(opts, "focusTimeoutMs", "focus_timeout_ms", option_value(opts, "timeoutMs", "timeout_ms", 10000)),
      pollMs = option_value(opts, "focusPollMs", "focus_poll_ms", option_value(opts, "pollMs", "poll_ms", 200)),
      allowError = option_value(opts, "focusAllowError", "focus_allow_error", false),
      allowStart = option_value(opts, "allowStart", "allow_start", nil),
    })
  end
  return ac.desktop.capture_screenshot(store, opts)
end

function ac.desktop.latest_screenshot_bounds(store)
  local bounds = type(store) == "table" and store.last_screenshot_bounds or nil
  if type(bounds) ~= "table" or bounds.available == false then
    return nil
  end
  local width = tonumber(bounds.width) or 0
  local height = tonumber(bounds.height) or 0
  if width <= 0 or height <= 0 then
    return nil
  end
  return {
    x = tonumber(bounds.x) or 0,
    y = tonumber(bounds.y) or 0,
    width = width,
    height = height,
  }
end

function ac.desktop.latest_screenshot_frame(store)
  local bounds = ac.desktop.latest_screenshot_bounds(store)
  if not bounds then return nil end
  local image_width = tonumber(type(store) == "table" and store.last_screenshot_image_width or nil) or 0
  local image_height = tonumber(type(store) == "table" and store.last_screenshot_image_height or nil) or 0
  if image_width <= 0 or image_height <= 0 then
    return nil
  end
  bounds.image_width = image_width
  bounds.image_height = image_height
  return bounds
end

function ac.desktop.rect_to_screen(store, rect, normalized, opts)
  opts = opts or {}
  local left = tonumber(rect and rect.left) or 0
  local top = tonumber(rect and rect.top) or 0
  local right = tonumber(rect and rect.right) or 0
  local bottom = tonumber(rect and rect.bottom) or 0

  local coordinate_space = option_value(opts, "coordinateSpace", "coordinate_space", nil)
  if coordinate_space == nil then
    if normalized == false then
      coordinate_space = "screen_pixels"
    elseif normalized == true then
      coordinate_space = "normalized"
    else
      coordinate_space = "screenshot_pixels"
    end
  end

  if coordinate_space == "screen_pixels" then
    return {
      left = math.floor(left),
      top = math.floor(top),
      right = math.floor(right),
      bottom = math.floor(bottom),
    }
  end

  local bounds = ac.desktop.latest_screenshot_bounds(store)
  if not bounds then
    if option_value(opts, "requireScreenshot", "require_screenshot", false) then
      return nil, "requires a prior screenshot so rects map to the latest visible window"
    end
    local state = ac.state()
    local screen = state and state.data and state.data.screen or {}
    bounds = {
      x = 0,
      y = 0,
      width = tonumber(screen.width) or 1,
      height = tonumber(screen.height) or 1,
    }
  end

  if coordinate_space == "screenshot_pixels" or coordinate_space == "image_pixels" then
    local frame = ac.desktop.latest_screenshot_frame(store)
    if not frame then
      return nil, "requires a prior screenshot with image dimensions so pixel rects map to the latest visible window"
    end
    left = left / frame.image_width
    right = right / frame.image_width
    top = top / frame.image_height
    bottom = bottom / frame.image_height
    bounds = frame
  elseif coordinate_space == "model_1000" or coordinate_space == "image_1000" then
    local frame = ac.desktop.latest_screenshot_frame(store)
    if not frame then
      return nil, "requires a prior screenshot with image dimensions so 0-1000 rects map to the latest visible window"
    end
    left = left / 1000
    right = right / 1000
    top = top / 1000
    bottom = bottom / 1000
    bounds = frame
  elseif coordinate_space ~= "normalized" then
    return nil, "unknown rectangle coordinate space: " .. tostring(coordinate_space)
  end

  return {
    left = math.floor(bounds.x + left * bounds.width),
    top = math.floor(bounds.y + top * bounds.height),
    right = math.floor(bounds.x + right * bounds.width),
    bottom = math.floor(bounds.y + bottom * bounds.height),
  }
end

function ac.desktop.click_rect(rect, opts)
  opts = opts or {}
  local screen_rect, err = ac.desktop.rect_to_screen(opts.store, rect, option_value(opts, "normalized", nil, nil), opts)
  if not screen_rect then
    return nil, err
  end
  local params = {
    rect = screen_rect,
    rectClickXFraction = option_value(opts, "rectClickXFraction", "rect_click_x_fraction", 0.5),
    rectClickYFraction = option_value(opts, "rectClickYFraction", "rect_click_y_fraction", 0.5),
  }
  local name = focus_app_name(nil, opts)
  if type(name) == "string" and name ~= "" then
    return ac.desktop.step_for_app(name, option_value(opts, "id", nil, "click"), "click", params, opts)
  end
  return response_data(ac.request("click", params))
end

function ac.desktop.click_rect_and_screenshot(store, rect, opts)
  opts = opts or {}
  local click, err = ac.desktop.click_rect(rect, {
    store = store,
    normalized = option_value(opts, "normalized", nil, nil),
    coordinateSpace = option_value(opts, "coordinateSpace", "coordinate_space", nil),
    requireScreenshot = option_value(opts, "requireScreenshot", "require_screenshot", false),
    focusApp = option_value(opts, "focusApp", "focus_app", nil),
    focusTimeoutMs = option_value(opts, "focusTimeoutMs", "focus_timeout_ms", 10000),
    focusPollMs = option_value(opts, "focusPollMs", "focus_poll_ms", 200),
    focusAllowError = option_value(opts, "focusAllowError", "focus_allow_error", false),
    rectClickXFraction = option_value(opts, "rectClickXFraction", "rect_click_x_fraction", 0.5),
    rectClickYFraction = option_value(opts, "rectClickYFraction", "rect_click_y_fraction", 0.5),
  })
  if not click then
    return ac.tool_result.error({
      code = "invalid_input",
      message = tostring(option_value(opts, "name", nil, "click_rect")) .. " " .. tostring(err),
    })
  end

  local screenshot = ac.desktop.capture_required_screenshot(nil, store, opts)
  return ac.tool_result.ok({
    clicked = true,
    click = click,
    screenshot = screenshot,
    image = screenshot.__ac_image_path,
    __ac_image_path = screenshot.__ac_image_path,
  })
end

function ac.desktop.type_and_screenshot(text, store, opts)
  opts = opts or {}
  local typed = response_data(ac.request("type", {
    text = ac.value.string(text),
    paste = option_value(opts, "paste", nil, true) ~= false,
  }))
  local screenshot = ac.desktop.capture_required_screenshot(nil, store, opts)
  return ac.tool_result.ok({
    typed = typed,
    screenshot = screenshot,
    image = screenshot.__ac_image_path,
    __ac_image_path = screenshot.__ac_image_path,
  })
end

function ac.desktop.press_and_screenshot(keys, store, opts)
  opts = opts or {}
  local pressed = response_data(ac.request("press", {
    keys = keys,
    holdMs = option_value(opts, "holdMs", "hold_ms", nil),
  }, {
    allow_error = option_value(opts, "allowError", "allow_error", false) == true,
  }))
  local screenshot = ac.desktop.capture_required_screenshot(nil, store, opts)
  return ac.tool_result.ok({
    pressed = pressed,
    screenshot = screenshot,
    image = screenshot.__ac_image_path,
    __ac_image_path = screenshot.__ac_image_path,
  })
end

ac.image = {}
function ac.image.info(path)
  return ac.request("image_info", { path = path })
end
function ac.image.split(path, opts)
  return ac.request("image_split", merge({ path = path }, opts or {}))
end

)LUA";
    out << R"LUA(
ac.llm = {}
function ac.llm.chat(spec)
  spec = spec or {}
  log_line("inference", "request", summarize_action_params("llm_chat", spec))
  local started_at = os.clock()
  local result = ac.request("llm_chat", spec, { allow_error = true })
  local elapsed_ms = math.floor((os.clock() - started_at) * 1000)
  if not result.ok then
    log_line("inference", "failed", {
      elapsed_ms = elapsed_ms,
      code = result.code,
      error = result.error,
      provider = spec.provider,
      model = spec.model,
    })
    error("computer.cpp llm_chat failed: " .. tostring(result.error or result.code), 2)
  end

  local data = result.data or result
  local message = data.message or {}
  if type(message) == "table" and message.finish_reason == nil then
    message.finish_reason = data.finishReason
  end
  local usage = usage_fields(data.usage)
  log_line("inference", "response", {
    elapsed_ms = elapsed_ms,
    provider = data.provider or spec.provider,
    model = data.model or spec.model,
    content_chars = content_char_count(message),
    reasoning_chars = reasoning_char_count(message),
    finish_reason = data.finishReason,
    tool_calls = count_tool_calls(message),
    prompt_tokens = usage.prompt_tokens,
    completion_tokens = usage.completion_tokens,
    total_tokens = usage.total_tokens,
  })
  return data
end

ac.models = {}
function ac.models.main_fabric(model_or_opts)
  if type(model_or_opts) == "string" then return model_or_opts end
  local opts = type(model_or_opts) == "table" and model_or_opts or {}
  if opts.model ~= nil then return opts.model end
  return nil
end
function ac.models.openrouter(model)
  return model
end
function ac.models.openai_compatible(model)
  return model
end

local Agent = {}
Agent.__index = Agent

function Agent:system(text)
  self.system_prompt = tostring(text or "")
  return self
end

function Agent:tool(name, spec)
  self.tools[name] = spec or {}
  return self
end

function Agent:run(input)
  if self.run_fn then
    return self.run_fn(input or {}, self)
  end
  local messages = {}
  if self.system_prompt and self.system_prompt ~= "" then
    messages[#messages + 1] = { role = "system", content = self.system_prompt }
  elseif self.spec and self.spec.purpose then
    messages[#messages + 1] = { role = "system", content = tostring(self.spec.purpose) }
  end
  messages[#messages + 1] = {
    role = "user",
    content = {
      { type = "text", text = json.encode(input or {}) },
    },
  }
  return ac.llm.chat({
    model = self.spec and self.spec.model or nil,
    temperature = self.spec and self.spec.temperature or 0,
    max_tokens = self.spec and self.spec.max_tokens or 1200,
    messages = messages,
  })
end

ac.agent = {}
function ac.agent.define(name, spec)
  local agent = setmetatable({
    name = name,
    spec = spec or {},
    tools = {},
    system_prompt = "",
    run_fn = spec and spec.run or nil,
  }, Agent)
  return agent
end

ac.tool_result = {}
function ac.tool_result.ok(result)
  return { ok = true, result = result or {} }
end
function ac.tool_result.done(result)
  return { ok = true, done = true, result = result or {} }
end
function ac.tool_result.error(error_value)
  error_value = error_value or {}
  return {
    ok = false,
    error = {
      code = error_value.code or "tool_error",
      message = error_value.message or "tool failed",
      details = error_value.details,
    },
  }
end
function ac.tool_result.invalid(message, code)
  return ac.tool_result.error({ code = code or "invalid_input", message = message or "invalid input" })
end

ac.tool = {}
function ac.tool.define(name, spec)
  if type(name) ~= "string" or name == "" then
    error("ac.tool.define requires a non-empty name", 2)
  end
  spec = spec or {}
  local input_schema = normalize_schema(spec.input or {}, true)
  return {
    __ac_tool = true,
    name = name,
    description = spec.description or "",
    input = input_schema,
    handler = spec.handler,
    model_tool = {
      type = "function",
      ["function"] = {
        name = name,
        description = spec.description or "",
        parameters = input_schema,
      },
    },
  }
end
function ac.tool.inputless(name, description, handler)
  return ac.tool.define(name, {
    description = description or "",
    input = {
      type = "object",
      properties = {},
      additionalProperties = false,
    },
    handler = handler,
  })
end

local function standard_tool(name, spec, handler)
  spec = spec or {}
  spec.handler = handler
  local tool = ac.tool.define(name, spec)
  tool.standard = true
  return tool
end

ac.tools = {}
function ac.tools.screenshot(spec)
  spec = spec or {}
  local focus_app = focus_app_name(nil, spec)
  local store = spec.store
  local default_max_dimension = option_value(spec, "maxDimension", "max_dimension", 1200)
  local default_frontmost_window_only = option_value(spec, "frontmostWindowOnly", "frontmost_window_only", false)
  return standard_tool("screenshot", {
    description = "Capture a bounded screenshot for visual desktop state. The output path is generated by computer.cpp.",
    input = {
      maxDimension = { type = "integer", default = default_max_dimension, minimum = 1, maximum = 1600 },
      frontmostWindowOnly = { type = "boolean", default = default_frontmost_window_only },
    },
  }, function(ctx, args)
    args.path = nil
    local params = {
      maxDimension = args.maxDimension or default_max_dimension,
      frontmostWindowOnly = args.frontmostWindowOnly,
    }
    if params.frontmostWindowOnly == nil then
      params.frontmostWindowOnly = default_frontmost_window_only
    end
    if type(focus_app) == "string" and focus_app ~= "" then
      return ac.tool_result.ok(ac.desktop.screenshot_for_app(focus_app, store, {
        maxDimension = params.maxDimension,
        frontmostWindowOnly = params.frontmostWindowOnly,
        focusTimeoutMs = option_value(spec, "focusTimeoutMs", "focus_timeout_ms", 10000),
        focusPollMs = option_value(spec, "focusPollMs", "focus_poll_ms", 200),
        focusAllowError = option_value(spec, "focusAllowError", "focus_allow_error", false),
      }))
    end
    local result = ctx and ctx.screenshot and ctx:screenshot(params) or ac.screenshot(nil, params)
    return ac.tool_result.ok(ac.desktop.remember_screenshot(store, response_data(result)))
  end)
end
local function rect_tool_schema()
  return {
    type = "object",
    properties = {
      rect = ac.schemas.rect_like(),
    },
    required = { "rect" },
    additionalProperties = false,
  }
end

function ac.tools.click_box(spec)
  spec = spec or {}
  local focus_app = focus_app_name(nil, spec)
  local store = spec.store
  return standard_tool("click_box", {
    description = "Click the center of a visible rectangle. Requires rect, never x/y points. Rect values are pixels in the latest screenshot image.",
    input = rect_tool_schema(),
  }, function(_, args)
    local result, err = ac.desktop.click_rect(ac.rect.normalize(args.rect), {
      store = store,
      normalized = args.normalized,
      coordinateSpace = option_value(spec, "coordinateSpace", "coordinate_space", nil),
      requireScreenshot = option_value(spec, "requireScreenshot", "require_screenshot", false),
      focusApp = focus_app,
      focusTimeoutMs = option_value(spec, "focusTimeoutMs", "focus_timeout_ms", 10000),
      focusPollMs = option_value(spec, "focusPollMs", "focus_poll_ms", 200),
      focusAllowError = option_value(spec, "focusAllowError", "focus_allow_error", false),
      rectClickXFraction = 0.5,
      rectClickYFraction = 0.5,
    })
    if not result then
      return ac.tool_result.error({ code = "invalid_input", message = "click_box " .. tostring(err) })
    end
    return ac.tool_result.ok(result)
  end)
end
function ac.tools.scroll_down(spec)
  spec = spec or {}
  local focus_app = focus_app_name(nil, spec)
  return standard_tool("scroll_down", {
    description = "Scroll down by a bounded amount",
    input = {
      pixels = { type = "integer", default = 600, minimum = 1, maximum = 3000 },
    },
  }, function(_, args)
    local pixels = tonumber(args.pixels) or 600
    local params = { dy = -pixels, dx = 0 }
    local result = focus_app and ac.desktop.step_for_app(focus_app, "scroll", "scroll", params, spec) or response_data(ac.request("scroll", params))
    return ac.tool_result.ok({ amount = pixels, result = result })
  end)
end
function ac.tools.scroll_up(spec)
  spec = spec or {}
  local focus_app = focus_app_name(nil, spec)
  return standard_tool("scroll_up", {
    description = "Scroll up by a bounded amount",
    input = {
      pixels = { type = "integer", default = 600, minimum = 1, maximum = 3000 },
    },
  }, function(_, args)
    local pixels = tonumber(args.pixels) or 600
    local params = { dy = pixels, dx = 0 }
    local result = focus_app and ac.desktop.step_for_app(focus_app, "scroll", "scroll", params, spec) or response_data(ac.request("scroll", params))
    return ac.tool_result.ok({ amount = pixels, result = result })
  end)
end
function ac.tools.press_key(spec)
  spec = spec or {}
  local focus_app = focus_app_name(nil, spec)
  return standard_tool("press_key", {
    description = "Press a key or key chord",
    input = {
      keys = { type = "string", required = true },
      holdMs = { type = "integer", minimum = 1, maximum = 5000 },
    },
  }, function(_, args)
    local params = { keys = args.keys, holdMs = args.holdMs }
    local result = focus_app and ac.desktop.step_for_app(focus_app, "press", "press", params, spec) or response_data(ac.request("press", params))
    return ac.tool_result.ok(result)
  end)
end
function ac.tools.type_text(spec)
  spec = spec or {}
  local focus_app = focus_app_name(nil, spec)
  return standard_tool("type_text", {
    description = "Type text into the focused field",
    input = {
      text = { type = "string", required = true },
      paste = { type = "boolean", default = true },
    },
  }, function(_, args)
    local params = { text = args.text, paste = args.paste ~= false }
    local result = focus_app and ac.desktop.step_for_app(focus_app, "type", "type", params, spec) or response_data(ac.request("type", params))
    return ac.tool_result.ok(result)
  end)
end
function ac.tools.desktop_app(app, opts)
  opts = opts or {}
  local store = opts.store or {}
  local focus_opts = {
    focusApp = app,
    focusTimeoutMs = option_value(opts, "focusTimeoutMs", "focus_timeout_ms", 10000),
    focusPollMs = option_value(opts, "focusPollMs", "focus_poll_ms", 200),
    focusAllowError = option_value(opts, "focusAllowError", "focus_allow_error", false),
  }
  return {
    ac.tools.screenshot({
      focusApp = app,
      store = store,
      frontmostWindowOnly = option_value(opts, "frontmostWindowOnly", "frontmost_window_only", false),
      maxDimension = option_value(opts, "maxDimension", "max_dimension", 1200),
      focusTimeoutMs = focus_opts.focusTimeoutMs,
      focusPollMs = focus_opts.focusPollMs,
      focusAllowError = focus_opts.focusAllowError,
    }),
    ac.tools.click_box({
      focusApp = app,
      store = store,
      requireScreenshot = option_value(opts, "requireScreenshot", "require_screenshot", false),
      coordinateSpace = option_value(opts, "coordinateSpace", "coordinate_space", nil),
      focusTimeoutMs = focus_opts.focusTimeoutMs,
      focusPollMs = focus_opts.focusPollMs,
      focusAllowError = focus_opts.focusAllowError,
    }),
    ac.tools.scroll_down(focus_opts),
    ac.tools.scroll_up(focus_opts),
    ac.tools.press_key(focus_opts),
    ac.tools.type_text(focus_opts),
  }
end
function ac.tools.wait()
  return standard_tool("wait", {
    description = "Wait for a desktop condition",
    input = {
      frontmost = { type = "string" },
      timeoutMs = { type = "integer", minimum = 1, maximum = 120000 },
      pollMs = { type = "integer", minimum = 50, maximum = 5000 },
    },
  }, function(_, args)
    local result = ac.wait(args)
    return ac.tool_result.ok(result.data or result)
  end)
end
function ac.tools.wait_stable()
  return standard_tool("wait_stable", {
    description = "Wait for the screen to become stable",
    input = {
      stableMs = { type = "integer", default = 750, minimum = 0, maximum = 10000 },
      timeoutMs = { type = "integer", default = 5000, minimum = 1, maximum = 120000 },
    },
  }, function(_, args)
    local result = ac.wait_stable_screen(args.stableMs or 750, { timeoutMs = args.timeoutMs or 5000 })
    return ac.tool_result.ok({ stableMs = args.stableMs or 750, result = result.data or result })
  end)
end
function ac.tools.done()
  return standard_tool("done", {
    description = "Mark the micro-agent task complete",
    input = {
      reason = { type = "string" },
    },
  }, function(_, args)
    return { ok = true, done = true, result = { reason = args.reason or "done" } }
  end)
end
function ac.tools.blocked()
  return standard_tool("blocked", {
    description = "Report that the screen or task is blocked",
    input = {
      reason = { type = "string" },
      code = { type = "string" },
    },
  }, function(_, args)
    return ac.tool_result.error({
      code = args.code or "screen_blocked",
      message = args.reason or "The screen is blocked",
    })
  end)
end

local MicroAgent = {}
MicroAgent.__index = MicroAgent

local function parse_tool_call(call)
  local fn = call["function"] or {}
  local name = call.name or fn.name or ""
  local arguments = call.arguments or fn.arguments or {}
  if type(arguments) == "string" then
    if arguments == "" then
      arguments = {}
    else
      local ok, decoded = pcall(function() return json.decode(arguments) end)
      if not ok or type(decoded) ~= "table" then
        return nil, "tool call " .. tostring(name) .. " has invalid JSON arguments"
      end
      arguments = decoded
    end
  elseif type(arguments) ~= "table" then
    arguments = {}
  end
  return {
    id = call.id or call.tool_call_id or name,
    name = name,
    args = arguments,
    raw = call,
  }
end

local function normalize_tool_result(result)
  if result == nil then
    return ac.tool_result.ok({})
  end
  if type(result) == "table" and result.ok ~= nil then
    return result
  end
  return ac.tool_result.ok(result)
end

local function message_has_image_content(message)
  if type(message) ~= "table" or type(message.content) ~= "table" then
    return false
  end
  for _, item in ipairs(message.content) do
    if type(item) == "table" and (item.type == "image_path" or item.type == "image_url") then
      return true
    end
  end
  return false
end

local function trim_image_messages(messages, max_images)
  max_images = tonumber(max_images) or 3
  if max_images < 1 then max_images = 1 end
  local image_indexes = {}
  for i, message in ipairs(messages) do
    if message_has_image_content(message) then
      image_indexes[#image_indexes + 1] = i
    end
  end
  local remove_count = #image_indexes - max_images
  for i = 1, remove_count do
    messages[image_indexes[i]].__ac_drop_message = true
  end
  if remove_count > 0 then
    local kept = {}
    for _, message in ipairs(messages) do
      if not message.__ac_drop_message then
        kept[#kept + 1] = message
      end
      message.__ac_drop_message = nil
    end
    return kept
  end
  return messages
end

local function preserve_assistant_retry_message(message)
  if type(message) ~= "table" then
    return { role = "assistant", content = "" }
  end
  local preserved = {}
  for key, value in pairs(message) do
    preserved[key] = value
  end
  preserved.role = preserved.role or "assistant"
  if preserved.content == nil then
    preserved.content = ""
  end
  if type(preserved.tool_calls) == "table" and #preserved.tool_calls == 0 then
    preserved.tool_calls = nil
  end
  return preserved
end

function MicroAgent:dispatch_tool(ctx, opts, parsed)
  local tool = self.tool_map[parsed.name]
  if not tool then
    return ac.tool_result.error({ code = "unknown_tool", message = "unknown tool: " .. tostring(parsed.name) })
  end
  local ok, validation_error = validate_value(tool.input, parsed.args, "tool." .. parsed.name, true)
  if not ok then
    return ac.tool_result.error({ code = "invalid_input", message = validation_error })
  end
  local callbacks = opts.on_tool_call or {}
  local callback = callbacks[parsed.name]
  local call = { id = parsed.id, name = parsed.name, args = parsed.args, raw = parsed.raw }
  if callback then
    local callback_ok, callback_result = xpcall(function() return callback(call) end, debug.traceback)
    if not callback_ok then
      return ac.tool_result.error({ code = "tool_error", message = tostring(callback_result) })
    end
    return normalize_tool_result(callback_result)
  end
  if tool.handler then
    local handler_ok, handler_result = xpcall(function() return tool.handler(ctx, parsed.args, call) end, debug.traceback)
    if not handler_ok then
      return ac.tool_result.error({ code = "tool_error", message = tostring(handler_result) })
    end
    return normalize_tool_result(handler_result)
  end
  return ac.tool_result.error({ code = "tool_error", message = "tool has no handler: " .. tostring(parsed.name) })
end

function MicroAgent:run_loop(ctx, opts)
  opts = opts or {}
  local messages = {}
  if self.system and self.system ~= "" then
    messages[#messages + 1] = { role = "system", content = self.system }
  end
  local user_content = opts.user_content
  if user_content == nil then
    user_content = json.encode({
      goal = opts.goal or "",
      state = opts.state or {},
    })
  end
  messages[#messages + 1] = {
    role = "user",
    content = user_content,
  }

  local model_tools = {}
  for _, tool in ipairs(self.tools) do
    model_tools[#model_tools + 1] = tool.model_tool
  end

  local max_steps = tonumber(opts.max_steps) or tonumber(self.spec.max_steps) or 10
  local max_missing_tool_replies = tonumber(opts.max_missing_tool_replies)
    or tonumber(self.spec.max_missing_tool_replies)
    or 2
  local missing_tool_replies = 0
  local tool_names = {}
  for _, tool in ipairs(self.tools) do
    tool_names[#tool_names + 1] = tool.name
  end
  table.sort(tool_names)
  log_line("agent", "start", {
    name = self.name,
    model = opts.model or self.spec.model,
    max_steps = max_steps,
    tools = #model_tools,
    goal = opts.goal or "",
  })
  for step = 1, max_steps do
    if ctx.cancelled and ctx:cancelled() then
      log_line("agent", "cancelled", { name = self.name, step = step })
      return { ok = false, error = { code = "operation_cancelled", message = "operation cancelled" } }
    end
    local request = {
      model = opts.model or self.spec.model,
      temperature = self.spec.temperature or 0,
      max_tokens = self.spec.max_tokens or 1200,
      messages = messages,
      tools = model_tools,
      tool_choice = opts.tool_choice or self.spec.tool_choice or "auto",
    }
    if opts.chat_template_kwargs ~= nil then
      request.chat_template_kwargs = opts.chat_template_kwargs
    elseif self.spec.chat_template_kwargs ~= nil then
      request.chat_template_kwargs = self.spec.chat_template_kwargs
    end
    log_line("agent", "model step", {
      name = self.name,
      step = step,
      messages = #messages,
      tools = #model_tools,
      model = request.model,
      temperature = request.temperature,
      max_tokens = request.max_tokens,
    })
    if ctx.trace then ctx:trace("model_request", { step = step, tools = model_tools }) end
    local chat = ac.llm.chat(request)
    local message = chat.message or chat
    if ctx.trace then ctx:trace("model_response", message) end
    local tool_calls = message.tool_calls or chat.tool_calls
    log_line("agent", "model reply", {
      name = self.name,
      step = step,
      content_chars = content_char_count(message),
      tool_calls = type(tool_calls) == "table" and #tool_calls or 0,
    })
    if type(tool_calls) ~= "table" or #tool_calls == 0 then
      missing_tool_replies = missing_tool_replies + 1
      log_line("agent", "missing tool call", { name = self.name, step = step })
      if missing_tool_replies <= max_missing_tool_replies and step < max_steps then
        messages[#messages + 1] = preserve_assistant_retry_message(message)
        messages[#messages + 1] = {
          role = "user",
          content = "Your previous response did not call a tool. Continue by calling exactly one available tool and do not answer in prose. Available tools: " .. table.concat(tool_names, ", "),
        }
        if ctx.trace then
          ctx:trace("missing_tool_retry", { step = step, tools = tool_names })
        end
        goto continue_agent_loop
      end
      return { ok = false, error = { code = "missing_tool_call", message = "micro-agent model response did not contain a tool call" } }
    end
    missing_tool_replies = 0
    messages[#messages + 1] = message

    for _, raw_call in ipairs(tool_calls) do
      local parsed, parse_error = parse_tool_call(raw_call)
      if not parsed then
        log_line("tool", "parse failed", { step = step, error = parse_error })
        return { ok = false, error = { code = "invalid_input", message = parse_error } }
      end
      log_line("tool", "call", {
        step = step,
        id = parsed.id,
        name = parsed.name,
        args = redacted(parsed.args),
      })
      local result = self:dispatch_tool(ctx, opts, parsed)
      log_line("tool", result.ok and "ok" or "failed", {
        step = step,
        id = parsed.id,
        name = parsed.name,
        done = result.done,
        code = result.error and result.error.code or nil,
        message = result.error and result.error.message or nil,
        result = result.result and redacted(result.result) or nil,
      })
      if ctx.trace then
        ctx:trace("tool_call", { tool = parsed.name, args = parsed.args, result = result })
      end
      messages[#messages + 1] = {
        role = "tool",
        tool_call_id = parsed.id,
        content = json.encode(result),
      }
      local image_path = result
        and type(result.result) == "table"
        and result.result.__ac_image_path
        or nil
      if type(image_path) == "string" and image_path ~= "" then
        log_line("agent", "image", { step = step, path = image_path })
        messages[#messages + 1] = {
          role = "user",
          content = {
            { type = "text", text = "Screenshot captured. Inspect this image before choosing the next tool call." },
            { type = "image_path", path = image_path },
          },
        }
        messages = trim_image_messages(messages, opts.max_images or self.spec.max_images or 3)
      end
      if result.done then
        log_line("agent", "done", { name = self.name, step = step, result = redacted(result.result or {}) })
        return { ok = true, result = result.result or {} }
      end
      if not result.ok then
        log_line("agent", "failed", { name = self.name, step = step, error = result.error or { code = "tool_error" } })
        return { ok = false, error = result.error or { code = "tool_error", message = "tool failed" } }
      end
    end
    ::continue_agent_loop::
  end
  log_line("agent", "timeout", { name = self.name, max_steps = max_steps })
  return { ok = false, error = { code = "timeout", message = "micro-agent exceeded max_steps" } }
end

ac.micro_agent = {}
function ac.micro_agent.define(spec)
  spec = spec or {}
  local tools = spec.tools or {}
  local tool_map = {}
  for _, tool in ipairs(tools) do
    if type(tool) ~= "table" or not tool.__ac_tool then
      error("ac.micro_agent.define tools must come from ac.tool.define or ac.tools.*", 2)
    end
    tool_map[tool.name] = tool
  end
  return setmetatable({
    name = spec.name or "",
    system = spec.system or "",
    spec = spec,
    tools = tools,
    tool_map = tool_map,
  }, MicroAgent)
end

function ac.micro_agent.user_content(data, screenshot)
  local content = {
    { type = "text", text = json.encode(data or {}) },
  }
  local image_path = nil
  if type(screenshot) == "string" then
    image_path = screenshot
  elseif type(screenshot) == "table" then
    image_path = screenshot.__ac_image_path or screenshot.path or screenshot.image
  end
  if type(image_path) == "string" and image_path ~= "" then
    content[#content + 1] = { type = "image_path", path = image_path }
  end
  return content
end

function ac.micro_agent.run(ctx, spec)
  spec = spec or {}
  local result = ac.micro_agent.define({
    name = spec.name,
    system = spec.prompt or spec.system,
    model = spec.model or ac.models.main_fabric(),
    max_tokens = spec.max_tokens or 2400,
    max_steps = spec.max_steps or 12,
    max_images = spec.max_images or 3,
    temperature = spec.temperature or 0,
    tool_choice = spec.tool_choice,
    chat_template_kwargs = spec.chat_template_kwargs or { enable_thinking = false },
    tools = spec.tools or {},
  }):run_loop(ctx, {
    goal = spec.goal,
    state = spec.state,
    user_content = spec.user_content,
    max_steps = spec.max_steps,
    tool_choice = spec.tool_choice,
    on_tool_call = spec.on_tool_call,
  })
  if result.ok then
    return result.result or {}
  end

  local err = result.error or {}
  error(tostring(err.message or err.code or "micro-agent failed"), 2)
end

function ac.role(role, name)
  if name == nil then return "role:" .. tostring(role) end
  return 'role:' .. tostring(role) .. '[name="' .. tostring(name):gsub("\\", "\\\\"):gsub('"', '\\"') .. '"]'
end

ac.target = {}
function ac.target.find(query, opts) return ac.request("target_find", merge({ query = query }, opts)) end
function ac.target.resolve(target, opts) return ac.request("target_resolve", merge({ target = target }, opts)) end
function ac.target.explain(target, opts) return ac.request("target_explain", merge({ target = target }, opts)) end

function ac.get(target, field) return ac.request("get", { target = target, field = field or "all" }) end
function ac.click(target, opts) return ac.request("click", merge({ target = target }, opts)) end
function ac.click_rect(rect, opts) return ac.request("click", merge({ rect = rect }, opts)) end
function ac.mouse_click(x, y, opts) return ac.click("point:" .. tostring(x) .. "," .. tostring(y), opts) end
function ac.mouse_move(x, y, opts) return ac.request("mouse_move", merge({ x = x, y = y }, opts)) end
function ac.mouse_drag(from_x, from_y, to_x, to_y, opts)
  return ac.request("mouse_drag", merge({ from = "point:" .. tostring(from_x) .. "," .. tostring(from_y), to = "point:" .. tostring(to_x) .. "," .. tostring(to_y) }, opts))
end
function ac.mouse_down(button, opts) return ac.request("mouse_down", merge({ button = button or "left" }, opts)) end
function ac.mouse_up(button, opts) return ac.request("mouse_up", merge({ button = button or "left" }, opts)) end
function ac.press(keys, opts) return ac.request("press", merge({ keys = keys }, opts)) end
function ac.type(text, opts) return ac.request("type", merge({ text = text, paste = true }, opts)) end
function ac.scroll(dy, dx, opts) return ac.request("scroll", merge({ dy = dy, dx = dx or 0 }, opts)) end
function ac.scroll_read(direction, opts)
  local dy = direction == "up" and 360 or -360
  return ac.scroll(dy, 0, merge({ durationMs = 1400, steps = 56, jitter = 0.08, samples = 4, center_anchor = true }, opts))
end

ac.observe = {}
function ac.observe.events(limit) return ac.request("observe_events", { limit = limit }) end
function ac.observe.frames(event, limit) return ac.request("observe_frames", { event = event or "last", limit = limit }) end

ac.clipboard = {}
function ac.clipboard.read() return ac.request("clipboard_read") end
function ac.clipboard.write(text) return ac.request("clipboard_write", { text = text }) end
function ac.clipboard.paste() return ac.request("clipboard_paste") end

function ac.retry(attempts, fn)
  local last_error = nil
  for i = 1, attempts do
    local ok, result = pcall(fn, i)
    if ok then return result end
    last_error = result
  end
  error(last_error, 2)
end

function ac.agent_step(spec)
  if not context.agent_stdio then
    error("agent_step requires computer.cpp run --agent-stdio", 2)
  end
  io.stdout:write(json.encode({ type = "agent_step", spec = spec }) .. "\n")
  io.stdout:flush()
  local line = io.stdin:read("*l")
  if not line then
    error("agent_step ended without a response line on stdin", 2)
  end
  return json.decode(line)
end

function ac.log(message)
  log_line("log", tostring(message))
end

package.loaded["computer_cpp"] = ac
package.loaded["computer.cpp"] = ac
_G.ac = ac

local script = arg[1]
if not script then
  io.stderr:write("computer.cpp Lua runner expected a script path\n")
  os.exit(2)
end
table.remove(arg, 1)

local chunk, load_error = loadfile(script)
if not chunk then
  io.stderr:write(tostring(load_error) .. "\n")
  os.exit(1)
end

local function main()
  return chunk(table.unpack(arg))
end

local ok, result = xpcall(main, debug.traceback)
local cleanup_ok, cleanup_errors = run_deferred()
if not ok then
  if context.json_output then
    io.stdout:write(json.encode({ ok = false, error = tostring(result), data = { trace = trace, cleanup_errors = cleanup_errors } }) .. "\n")
  else
    io.stderr:write(tostring(result) .. "\n")
    for _, cleanup_error in ipairs(cleanup_errors) do
      io.stderr:write("cleanup failed: " .. tostring(cleanup_error) .. "\n")
    end
  end
  os.exit(1)
end
if not cleanup_ok then
  if context.json_output then
    io.stdout:write(json.encode({ ok = false, error = "Lua cleanup failed", data = { result = result, trace = trace, cleanup_errors = cleanup_errors } }) .. "\n")
  else
    for _, cleanup_error in ipairs(cleanup_errors) do
      io.stderr:write("cleanup failed: " .. tostring(cleanup_error) .. "\n")
    end
  end
  os.exit(1)
end

if context.vars and context.vars.__ac_app_mode then
  local app_ok, payload = xpcall(function() return handle_app_mode(result) end, debug.traceback)
  if not app_ok then
    io.stdout:write(json.encode({ ok = false, code = "internal_error", error = tostring(payload), data = { trace = trace } }) .. "\n")
    os.exit(1)
  end
  io.stdout:write(json.encode(payload) .. "\n")
  os.exit(payload.ok and 0 or 1)
end

if context.json_output then
  io.stdout:write(json.encode({ ok = true, data = { result = result, trace = trace } }) .. "\n")
end
)LUA";
    return out.str();
}

} // namespace ComputerCpp
