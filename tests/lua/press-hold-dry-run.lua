local ac = require("computer_cpp")

ac.press("Shift+F4", {
  hold_ms = 250,
})

local params = ac.trace[1].steps[1].params
return {
  method = ac.trace[1].steps[1].method,
  keys = params.keys,
  hold_ms = params.holdMs,
}
