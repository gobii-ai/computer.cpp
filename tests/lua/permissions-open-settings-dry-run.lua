local ac = require("computer_cpp")

ac.permissions({
  request = true,
})
ac.permissions.open_settings("screen-capture")

return {
  check_method = ac.trace[1].steps[1].method,
  check_request = ac.trace[1].steps[1].params.request,
  open_method = ac.trace[2].steps[1].method,
  open_pane = ac.trace[2].steps[1].params.pane,
}
