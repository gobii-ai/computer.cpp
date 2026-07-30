local ac = require("computer_cpp")

local app = ac.app.define({
  name = "secondary-app",
  title = "Secondary App",
  version = "1.0.0",
})

app:command("echo", {
  description = "Identify the secondary app",
  input = {
    message = { type = "string", required = true },
  },
  output = {
    message = { type = "string" },
    source = { type = "string" },
  },
  handler = function(_, args)
    return {
      message = args.message,
      source = "secondary",
    }
  end,
})

app:command("undescribed", {
  input = {},
  output = {
    source = { type = "string" },
  },
  handler = function()
    return { source = "secondary" }
  end,
})

return app
