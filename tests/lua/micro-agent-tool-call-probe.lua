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

local function run_case(name, agent_opts)
  local ctx, trace = make_context()
  local reported = {}
  local agent = ac.micro_agent.define({
    name = "probe.tool_call." .. name,
    system = table.concat({
      "You are testing tool calling reliability.",
      "Call report_visible_feed exactly once.",
      "Do not answer in prose.",
      agent_opts.system_suffix or "",
    }, "\n"),
    tools = {
      ac.tool.define("report_visible_feed", {
        description = "Report the visible synthetic feed item.",
        input = {
          screen = { type = "string", required = true },
          posts = { type = "array", required = true },
        },
        handler = function(_, args)
          reported = args
          return ac.tool_result.done({ count = #(args.posts or {}), screen = args.screen })
        end,
      }),
    },
  })

  local ok, result = pcall(function()
    return agent:run_loop(ctx, {
      goal = "Read this synthetic feed text and call report_visible_feed with one post.",
      state = {
        visible_text = "Ada Lovelace · 1st\\nAnalytical engine notes for tool-call training.\\nLike Comment Repost Send",
      },
      max_steps = 3,
      max_missing_tool_replies = 1,
      tool_choice = "required",
      max_tokens = agent_opts.max_tokens or 600,
      chat_template_kwargs = agent_opts.chat_template_kwargs,
    })
  end)

  return {
    name = name,
    pcall_ok = ok,
    ok = ok and result.ok or false,
    error = ok and (result.error or {}) or tostring(result),
    result = ok and result.result or {},
    reported = reported,
    trace_count = #trace,
    model_responses = (function()
      local responses = {}
      for _, item in ipairs(trace) do
        if item.name == "model_response" then
          local message = item.value or {}
          responses[#responses + 1] = {
            content_chars = type(message.content) == "string" and #message.content or 0,
            reasoning_chars = type(message.reasoning_content) == "string" and #message.reasoning_content
              or type(message.reasoningContent) == "string" and #message.reasoningContent
              or type(message.reasoning) == "string" and #message.reasoning
              or 0,
            tool_calls = type(message.tool_calls) == "table" and #message.tool_calls or 0,
            finish_reason = message.finish_reason or "",
            keys = (function()
              local keys = {}
              for key, _ in pairs(message) do keys[#keys + 1] = key end
              table.sort(keys)
              return keys
            end)(),
          }
        end
      end
      return responses
    end)(),
  }
end

return {
  baseline = run_case("baseline", {}),
  enable_thinking_false = run_case("enable_thinking_false", {
    chat_template_kwargs = { enable_thinking = false },
  }),
  enable_thinking_false_short = run_case("enable_thinking_false_short", {
    max_tokens = 220,
    chat_template_kwargs = { enable_thinking = false },
  }),
  preserve_thinking = run_case("preserve_thinking", {
    chat_template_kwargs = { preserve_thinking = true },
  }),
  no_think_prompt = run_case("no_think_prompt", {
    system_suffix = "/no_think",
  }),
  no_think_enable_false = run_case("no_think_enable_false", {
    system_suffix = "/no_think",
    chat_template_kwargs = { enable_thinking = false },
  }),
  preserve_no_think = run_case("preserve_no_think", {
    system_suffix = "/no_think",
    chat_template_kwargs = { preserve_thinking = true },
  }),
}
