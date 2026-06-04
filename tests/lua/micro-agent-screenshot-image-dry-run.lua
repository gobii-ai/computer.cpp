local ac = require("computer_cpp")

local requests = {}
local replies = {
  {
    message = {
      role = "assistant",
      content = "",
      tool_calls = {
        {
          id = "call_screenshot",
          type = "function",
          ["function"] = {
            name = "screenshot",
            arguments = ac.json.encode({
              maxDimension = 900,
            }),
          },
        },
      },
    },
  },
  {
    message = {
      role = "assistant",
      content = "",
      tool_calls = {
        {
          id = "call_done",
          type = "function",
          ["function"] = {
            name = "done",
            arguments = "{}",
          },
        },
      },
    },
  },
}

ac.llm.chat = function(request)
  requests[#requests + 1] = request
  return replies[#requests]
end

local ctx = {}
function ctx:trace() end
function ctx:cancelled() return false end
function ctx:screenshot(args)
  return {
    ok = true,
    data = {
      path = "/tmp/micro-agent-shot.png",
      maxDimension = args.maxDimension,
    },
  }
end

local agent = ac.micro_agent.define({
  name = "unit.screenshot_image",
  system = "Use tool calls only.",
  chat_template_kwargs = { enable_thinking = false },
  tools = {
    ac.tools.screenshot(),
    ac.tools.done(),
  },
})

local result = agent:run_loop(ctx, {
  goal = "capture one screenshot",
  max_steps = 2,
})

local image_message = nil
for _, message in ipairs(requests[2].messages) do
  if message.role == "user" and type(message.content) == "table" then
    for _, item in ipairs(message.content) do
      if item.type == "image_path" then
        image_message = message
      end
    end
  end
end

return {
  ok = result.ok,
  request_count = #requests,
  image_message_role = image_message and image_message.role or "",
  image_text_type = image_message and image_message.content[1].type or "",
  image_type = image_message and image_message.content[2].type or "",
  image_path = image_message and image_message.content[2].path or "",
  thinking_disabled = requests[1].chat_template_kwargs.enable_thinking == false,
}
