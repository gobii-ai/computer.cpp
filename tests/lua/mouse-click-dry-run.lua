ac.mouse_click(12, 34, {
  button = "right",
  click_count = 2,
  instant = true,
  no_park_before_click = true,
})

local step = ac.trace[1].steps[1]
return {
  method = step.method,
  target = step.params.target,
  button = step.params.button,
  click_count = step.params.clickCount,
  motion = step.params.motion,
  park_before_click = step.params.parkBeforeClick,
}
