local ac = require("computer_cpp")

local function raise(value)
  error(setmetatable(value or {}, {
    __tostring = function(item)
      return tostring(item.message or item.code or "operation failed")
    end,
  }), 0)
end

local app = ac.app.define({
  name = "unit-approval-app",
  title = "Unit Approval App",
  version = "1.0.0",
})

app:command("needs-approval", {
  description = "Wait for one approval response.",
  input = {
    message = { type = "string", required = true },
    timeoutMs = { type = "integer", default = 10000, minimum = 1000, maximum = 30000 },
  },
  output = {
    approved = { type = "boolean" },
    message = { type = "string" },
    note = { type = "string" },
  },
  handler = function(ctx, args)
    local approval, err = ctx:request_approval({
      title = "Approve unit action",
      reason = "Exercise the operation approval lifecycle.",
      action = args.message,
      risk = "external_side_effect",
      timeoutMs = args.timeoutMs,
    })
    if not approval then raise(err) end
    return {
      approved = true,
      message = args.message,
      note = approval.note,
    }
  end,
})

return app
