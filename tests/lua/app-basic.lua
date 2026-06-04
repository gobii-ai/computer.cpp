local ac = require("computer_cpp")

local app = ac.app.define({
  name = "unit-app",
  title = "Unit App",
  version = "1.0.0",
})

app:command("echo", {
  description = "Echo a structured message",

  input = {
    message = { type = "string", required = true, description = "Message to echo." },
    count = { type = "integer", default = 1, minimum = 1, maximum = 5, description = "Repeat count." },
    loud = { type = "boolean", default = false, description = "Mark the response as loud." },
  },

  output = {
    message = { type = "string" },
    count = { type = "integer" },
    loud = { type = "boolean" },
  },

  handler = function(ctx, args)
    ctx:progress({ step = "echo", count = args.count })
    return {
      message = args.message,
      count = args.count,
      loud = args.loud,
    }
  end,
})

app:command("slow", {
  description = "Sleep briefly for async operation tests",

  input = {
    delay = { type = "integer", default = 1, minimum = 0, maximum = 5 },
  },

  output = {
    done = { type = "boolean" },
  },

  handler = function(ctx, args)
    ctx:progress({ step = "started", delay = args.delay })
    if ctx:cancelled() then
      return ac.cancelled()
    end
    if args.delay > 0 then
      os.execute("sleep " .. tostring(args.delay))
    end
    if ctx:cancelled() then
      return ac.cancelled()
    end
    ctx:progress({ step = "complete" })
    return { done = true }
  end,
})

return app
