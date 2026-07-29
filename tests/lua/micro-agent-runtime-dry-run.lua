local ac = require("computer_cpp")

local real_time = os.time
local function timed(values)
  local index = 0
  os.time = function()
    index = index + 1
    return values[index] or values[#values]
  end
end

local timeout_requests = 0
timed({ 100, 100, 102 })
ac.llm.chat = function()
  timeout_requests = timeout_requests + 1
  return { message = { role = "assistant", content = "still thinking" } }
end
local timeout_agent = ac.micro_agent.define({
  name = "unit.runtime-timeout",
  system = "Use tools.",
  max_steps = 3,
  maxRuntimeMs = 1000,
  tools = { ac.tools.done() },
})
local timeout_result = timeout_agent:run_loop({
  cancelled = function() return false end,
}, {
  goal = "time out",
})

local paused_requests = 0
timed({ 200, 202, 202 })
ac.llm.chat = function()
  paused_requests = paused_requests + 1
  return {
    message = {
      role = "assistant",
      content = "",
      tool_calls = {
        {
          id = "done",
          type = "function",
          ["function"] = {
            name = "done",
            arguments = "{}",
          },
        },
      },
    },
  }
end
local paused_agent = ac.micro_agent.define({
  name = "unit.runtime-paused",
  system = "Use tools.",
  max_steps = 2,
  max_runtime_ms = 1000,
  tools = { ac.tools.done() },
})
local paused_result = paused_agent:run_loop({
  _ac_paused_ms = 2000,
  cancelled = function() return false end,
}, {
  goal = "finish after paused time",
})

os.time = real_time

return {
  timeout_ok = timeout_result.ok,
  timeout_code = timeout_result.error and timeout_result.error.code or "",
  timeout_requests = timeout_requests,
  paused_ok = paused_result.ok,
  paused_requests = paused_requests,
}
