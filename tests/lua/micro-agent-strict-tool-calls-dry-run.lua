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
  local preserved_assistant = ""
  local preserved_reasoning = ""
  local preserved_empty_tool_calls = true
  ac.llm.chat = function(request)
    requests[#requests + 1] = request
    if #requests == 1 then
      return {
        message = {
          role = "assistant",
          content = "I should inspect the screen before continuing.",
          reasoning_content = "I can see the available tool and should use it next.",
          tool_calls = {},
        },
      }
    end
    retry_message = request.messages[#request.messages].content
    for _, message in ipairs(request.messages) do
      if message.role == "assistant" then
        preserved_assistant = message.content or ""
        preserved_reasoning = message.reasoning_content or message.reasoningContent or message.reasoning or ""
        preserved_empty_tool_calls = type(message.tool_calls) == "table" and #message.tool_calls == 0
      end
    end
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
    preserved_assistant = preserved_assistant,
    preserved_reasoning = preserved_reasoning,
    preserved_empty_tool_calls = preserved_empty_tool_calls,
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

local function run_reasoning_only_recovery_case()
  local requests = {}
  local second_request_reasoning = ""
  local second_request_retry_prompt = ""
  local report_args = {}
  ac.llm.chat = function(request)
    requests[#requests + 1] = request
    if #requests == 1 then
      return {
        finishReason = "length",
        usage = { completion_tokens = 1600 },
        message = {
          role = "assistant",
          content = "",
          reasoning_content = "Visible feed has one organic post by Ada Lovelace. I should call report_visible_feed with that post.",
          tool_calls = {},
        },
      }
    end

    if #requests == 2 then
      second_request_retry_prompt = request.messages[#request.messages].content
    end
    for _, message in ipairs(request.messages) do
      if message.role == "assistant" then
        second_request_reasoning = message.reasoning_content or message.reasoningContent or message.reasoning or ""
      end
    end

    return {
      message = {
        role = "assistant",
        content = "",
        tool_calls = {
          {
            id = "call_report",
            type = "function",
            ["function"] = {
              name = "report_visible_feed",
              arguments = ac.json.encode({
                screen = "feed",
                posts = {
                  {
                    author = "Ada Lovelace",
                    body_excerpt = "Analytical engines need clear tool calls.",
                    sponsored = false,
                  },
                },
              }),
            },
          },
        },
      },
    }
  end

  local ctx, trace = make_context()
  local agent = ac.micro_agent.define({
    name = "unit.reasoning_only_recovery",
    system = "Use tool calls only.",
    tools = {
      ac.tool.define("report_visible_feed", {
        description = "Report visible feed posts.",
        input = {
          screen = { type = "string", required = true },
          posts = { type = "array", required = true },
        },
        handler = function(_, args)
          report_args = args
          return ac.tool_result.done({ count = #(args.posts or {}), screen = args.screen })
        end,
      }),
    },
  })

  local result = agent:run_loop(ctx, {
    goal = "recover from reasoning-only missing tool response",
    max_steps = 3,
    max_missing_tool_replies = 1,
    tool_choice = "required",
    chat_template_kwargs = { preserve_thinking = true },
  })

  return {
    ok = result.ok,
    count = result.result and result.result.count or 0,
    request_count = #requests,
    first_preserve_thinking = requests[1].chat_template_kwargs and requests[1].chat_template_kwargs.preserve_thinking or false,
    second_preserve_thinking = requests[2].chat_template_kwargs and requests[2].chat_template_kwargs.preserve_thinking or false,
    second_request_reasoning = second_request_reasoning,
    second_request_retry_prompt = second_request_retry_prompt,
    reported_author = report_args.posts and report_args.posts[1] and report_args.posts[1].author or "",
    trace_count = #trace,
  }
end

return {
  missing = run_missing_tool_call_case(),
  reasoning_only = run_reasoning_only_recovery_case(),
  pseudo = run_pseudo_tool_case(),
  invalid_args = run_invalid_args_case(),
}
