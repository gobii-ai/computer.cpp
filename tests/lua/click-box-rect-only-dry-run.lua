local ac = require("computer_cpp")

local tool = ac.tools.click_box()
local result = tool.handler({}, {
  normalized = false,
  rect = {
    left = 10,
    top = 20,
    right = 30,
    bottom = 40,
  },
})

local params = ac.trace[1].steps[1].params

return {
  ok = result.ok,
  method = ac.trace[1].steps[1].method,
  has_target = params.target ~= nil,
  rect_left = params.rect.left,
  rect_top = params.rect.top,
  rect_right = params.rect.right,
  rect_bottom = params.rect.bottom,
  rect_click_x = params.rectClickXFraction,
  rect_click_y = params.rectClickYFraction,
}
