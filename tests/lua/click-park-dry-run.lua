ac.click("point:12,34", {
  hover_safe = true,
  park_before_click = true,
  park_x_fraction = 0.82,
  park_y_fraction = 0.45,
  click_hold_ms = 75,
  pre_click_settle_ms = 10,
})

local params = ac.trace[1].steps[1].params
return {
  method = ac.trace[1].steps[1].method,
  target = params.target,
  hover_safe = params.hoverSafe,
  park_before_click = params.parkBeforeClick,
  park_x_fraction = params.parkXFraction,
  park_y_fraction = params.parkYFraction,
  click_hold_ms = params.clickHoldMs,
  pre_click_settle_ms = params.preClickSettleMs,
}
