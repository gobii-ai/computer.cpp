ac.scroll(-900, 0, {
  max_gesture_delta = 150,
})
ac.scroll(-800, 0, {
  max_scroll_gesture_delta = 140,
})

local first = ac.trace[1].steps[1].params
local second = ac.trace[2].steps[1].params
return {
  first_method = ac.trace[1].steps[1].method,
  first_dy = first.dy,
  first_dx = first.dx,
  first_max_gesture_delta = first.maxGestureDelta,
  second_method = ac.trace[2].steps[1].method,
  second_dy = second.dy,
  second_dx = second.dx,
  second_max_scroll_gesture_delta = second.maxScrollGestureDelta,
}
