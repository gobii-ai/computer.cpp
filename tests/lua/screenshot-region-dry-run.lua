local ac = require("computer_cpp")

ac.screenshot_region("/tmp/region.png", 10, 20, 300, 200, {
  max_dim = 600,
  frontmost_window = true,
})

ac.screenshot(nil, {
  output_path = "/tmp/output-alias.png",
  max_dimension = 500,
})

ac.screenshot(nil, {
  output = "/tmp/output-short-alias.png",
})

local params = ac.trace[1].steps[1].params
local output_params = ac.trace[2].steps[1].params
local short_output_params = ac.trace[3].steps[1].params
return {
  method = ac.trace[1].steps[1].method,
  path = params.path,
  x = params.x,
  y = params.y,
  width = params.width,
  height = params.height,
  max_dimension = params.maxDimension,
  frontmost_window = params.frontmostWindowOnly,
  output_method = ac.trace[2].steps[1].method,
  output_path = output_params.path,
  output_max_dimension = output_params.maxDimension,
  short_output_path = short_output_params.path,
}
