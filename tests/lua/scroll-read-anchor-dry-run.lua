ac.scroll_read("up", {
  at = "role:scrollarea",
  no_anchor = true,
  focus_app = "Finder",
  at_offset_x = 3,
  at_offset_y = 4,
})

local step = ac.trace[1].steps[1]
return {
  method = step.method,
  dy = step.params.dy,
  dx = step.params.dx,
  at = step.params.at,
  anchor = step.params.anchor,
  center_anchor = step.params.centerAnchor,
  focus_app = step.params.focusApp,
  at_offset_x = step.params.atOffsetX,
  at_offset_y = step.params.atOffsetY,
}
