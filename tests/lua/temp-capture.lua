local ac = require("computer_cpp")

local response = ac.batch({
  { method = "ping", params = {} },
}, { allow_error = true })

assert(response.ok == true)
assert(response.data.results[1].ok == true)
assert(response.data.results[1].data.tempCapture == true)
print("temp-capture-ok")
