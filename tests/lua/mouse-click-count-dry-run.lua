local ac = require("computer_cpp")

ac.mouse_down("left", {
  click_count = 2,
})
ac.mouse_up("left", {
  click_count = 2,
})

return {
  down_method = ac.trace[1].steps[1].method,
  down_button = ac.trace[1].steps[1].params.button,
  down_click_count = ac.trace[1].steps[1].params.clickCount,
  up_method = ac.trace[2].steps[1].method,
  up_button = ac.trace[2].steps[1].params.button,
  up_click_count = ac.trace[2].steps[1].params.clickCount,
}
