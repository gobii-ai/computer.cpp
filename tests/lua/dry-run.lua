local ac = require("computer_cpp")

ac.wait_frontmost("Firefox", { timeout_ms = 5000 })
ac.click(ac.role("textarea", "Project code"))
ac.type(ac.vars.project_code)
ac.click(ac.role("button", "Submit Review"))

return {
  project_code = ac.vars.project_code,
  steps = #ac.trace,
  requested = ac.trace[1].response.data.requested,
  executed = ac.trace[1].response.data.executed,
  failed = ac.trace[1].response.data.failed,
  stopped_on_error = ac.trace[1].response.data.stoppedOnError,
}
