local ac = require("computer_cpp")

local store = {
  last_screenshot_bounds = {
    available = true,
    x = 100,
    y = 50,
    width = 200,
    height = 100,
  },
  last_screenshot_image_width = 400,
  last_screenshot_image_height = 200,
}

local tools = ac.tools.desktop_agent({
  store = store,
  coordinateSpace = "model_1000",
  maxDimension = 640,
  frontmostWindowOnly = true,
})

local names = {}
for _, tool in ipairs(tools) do names[#names + 1] = tool.name end

local scroll_schema = tools[7].model_tool["function"].parameters
local click = tools[5].handler({}, {
  rect = { left = 250, top = 250, right = 500, bottom = 500 },
  button = "right",
  clickCount = 2,
})

local drag = tools[6].handler({}, {
  fromRect = { left = 0, top = 0, right = 250, bottom = 250 },
  toRect = { left = 750, top = 750, right = 1000, bottom = 1000 },
  button = "left",
  durationMs = 450,
  steps = 12,
})

local observed = tools[1].handler({}, {
  includeAccessibility = true,
  frontmostWindowOnly = true,
  maxDimension = 640,
})

local original_focus_app = ac.desktop.focus_app
local activate_allow_error = false
ac.desktop.focus_app = function(_, opts)
  activate_allow_error = opts.allowError == true
  return { ok = true, data = { focused = true } }
end
local activated = tools[2].handler({}, { app = "Calculator" })
ac.desktop.focus_app = original_focus_app

local function latest_params(method)
  local found = nil
  for _, batch in ipairs(ac.trace) do
    for _, step in ipairs(batch.steps or {}) do
      if step.method == method then found = step.params end
    end
  end
  return found or {}
end

local click_params = latest_params("click")
local drag_params = latest_params("mouse_drag")

return {
  tool_count = #tools,
  tool_names = table.concat(names, ","),
  scroll_direction_required = scroll_schema.required[1] == "direction",
  scroll_direction_has_nested_required = scroll_schema.properties.direction.required ~= nil,
  activate_ok = activated.ok,
  activate_allow_error = activate_allow_error,
  click_ok = click.ok,
  click_image = click.result and click.result.image or "",
  click_button = click_params.button,
  click_count = click_params.clickCount,
  click_left = click_params.rect and click_params.rect.left or 0,
  drag_ok = drag.ok,
  drag_image = drag.result and drag.result.image or "",
  drag_from = drag_params.from,
  drag_to = drag_params.to,
  drag_duration = drag_params.durationMs,
  observe_ok = observed.ok,
  observe_image = observed.result and observed.result.image or "",
  observe_has_coordinate = observed.result and observed.result.coordinate ~= nil,
  observe_has_accessibility = observed.result and observed.result.accessibility ~= nil,
}
