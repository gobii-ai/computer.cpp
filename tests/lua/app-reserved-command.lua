local ac = require("computer_cpp")

local app = ac.app.define({ name = "reserved-command-test" })
app:command("computer_cpp_collision", {
  input = {},
  output = {},
  handler = function() return {} end,
})

return app
