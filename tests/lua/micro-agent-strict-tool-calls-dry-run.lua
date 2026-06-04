local ac = require("computer_cpp")

local function make_context()
  local trace = {}
  local ctx = {}
  function ctx:trace(name, value)
    trace[#trace + 1] = { name = name, value = value }
  end
  function ctx:cancelled()
    return false
  end
  return ctx, trace
end

local function run_missing_tool_call_case()
  local requests = {}
  local retry_message = ""
  ac.llm.chat = function(request)
    requests[#requests + 1] = request
    if #requests == 1 then
      return {
        message = {
          role = "assistant",
          content = "I should inspect the screen before continuing.",
          tool_calls = {},
        },
      }
    end
    retry_message = request.messages[#request.messages].content
    return {
      message = {
        role = "assistant",
        content = "",
        tool_calls = {
          {
            id = "call_done",
            type = "function",
            ["function"] = {
              name = "done",
              arguments = ac.json.encode({ reason = "recovered" }),
            },
          },
        },
      },
    }
  end

  local ctx, trace = make_context()
  local agent = ac.micro_agent.define({
    name = "unit.strict_missing_tool",
    system = "Use tool calls only.",
    tools = {
      ac.tools.done(),
    },
  })

  local result = agent:run_loop(ctx, {
    goal = "recover from one text-only response",
    max_steps = 3,
  })

  return {
    ok = result.ok,
    reason = result.result and result.result.reason or "",
    request_count = #requests,
    retry_message = retry_message,
    trace_count = #trace,
  }
end

local function run_pseudo_tool_case()
  local requests = {}
  ac.llm.chat = function(request)
    requests[#requests + 1] = request
    return {
      message = {
        role = "assistant",
        content = '<screenshot>{"maxDimension":900}',
        tool_calls = {},
      },
    }
  end

  local ctx = make_context()
  function ctx:screenshot()
    error("screenshot should not be called from pseudo-tool text")
  end

  local agent = ac.micro_agent.define({
    name = "unit.strict_pseudo_tool",
    system = "Use tool calls only.",
    tools = {
      ac.tools.screenshot(),
      ac.tools.done(),
    },
  })

  local result = agent:run_loop(ctx, {
    goal = "do not convert textual pseudo-tool calls",
    max_steps = 3,
  })

  return {
    ok = result.ok,
    code = result.error and result.error.code or "",
    request_count = #requests,
  }
end

local function run_invalid_args_case()
  local requests = {}
  ac.llm.chat = function(request)
    requests[#requests + 1] = request
    return {
      message = {
        role = "assistant",
        content = "",
        tool_calls = {
          {
            id = "call_bad",
            type = "function",
            ["function"] = {
              name = "report_value",
              arguments = "{}",
            },
          },
        },
      },
    }
  end

  local ctx = make_context()
  local reported = false
  local agent = ac.micro_agent.define({
    name = "unit.strict_invalid_args",
    system = "Use tool calls only.",
    tools = {
      ac.tool.define("report_value", {
        description = "Report a required value.",
        input = {
          value = { type = "string", required = true },
        },
        handler = function()
          reported = true
          return ac.tool_result.ok({})
        end,
      }),
      ac.tools.done(),
    },
  })

  local result = agent:run_loop(ctx, {
    goal = "fail on invalid tool arguments",
    max_steps = 3,
  })

  return {
    ok = result.ok,
    code = result.error and result.error.code or "",
    message = result.error and result.error.message or "",
    request_count = #requests,
    reported = reported,
  }
end

return {
  missing = run_missing_tool_call_case(),
  pseudo = run_pseudo_tool_case(),
  invalid_args = run_invalid_args_case(),
}
