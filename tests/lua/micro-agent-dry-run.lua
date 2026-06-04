local ac = require("computer_cpp")

local requests = {}
local replies = {
  {
    message = {
      role = "assistant",
      content = "",
      tool_calls = {
        {
          id = "call_report",
          type = "function",
          ["function"] = {
            name = "report_rows",
            arguments = ac.json.encode({
              rows = {
                { title = "first" },
              },
            }),
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

local trace = {}
local ctx = {}
function ctx:trace(name, value)
  trace[#trace + 1] = { name = name, value = value }
end
function ctx:cancelled()
  return false
end

local rows = {}
local result = ac.micro_agent.run(ctx, {
  name = "unit.feed_reader",
  prompt = "Use tool calls only.",
  goal = "collect rows",
  max_steps = 3,
  tools = {
    ac.tool.define("report_rows", {
      description = "Report visible rows",
      input = {
        rows = {
          type = "array",
          required = true,
          items = {
            type = "object",
            properties = {
              title = { type = "string" },
            },
            additionalProperties = false,
          },
        },
      },
    }),
  },
  user_content = ac.micro_agent.user_content({ source = "fixture" }, "/tmp/rows.png"),
  on_tool_call = {
    report_rows = function(call)
      rows = call.args.rows
      return ac.tool_result.done({ count = #rows })
    end,
  },
})

return {
  ok = result.count == 1,
  rows = #rows,
  request_count = #requests,
  first_tool_type = requests[1].tools[1].type,
  first_tool_name = requests[1].tools[1]["function"].name,
  first_message_image_type = requests[1].messages[2].content[2].type,
  first_message_image_path = requests[1].messages[2].content[2].path,
  trace_count = #trace,
}
