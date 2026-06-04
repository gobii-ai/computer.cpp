local ac = require("computer_cpp")

local path = assert(ac.vars.path, "path var required")

ac.defer(function()
  local file = assert(io.open(path, "w"))
  file:write("cleanup\n")
  file:close()
end)

return {
  deferred = true,
}
