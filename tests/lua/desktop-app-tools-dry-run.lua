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

local tools = ac.tools.desktop_app("UnitApp", {
  store = store,
  frontmostWindowOnly = true,
  requireScreenshot = true,
})

local click = tools[2].handler({}, {
  rect = {
    left = 100,
    top = 50,
    right = 200,
    bottom = 100,
  },
})

local click_params = ac.trace[2].steps[1].params
local missing = ac.tools.click_box({
  store = {},
  requireScreenshot = true,
}).handler({}, {
  rect = {
    left = 0.1,
    top = 0.1,
    right = 0.2,
    bottom = 0.2,
  },
})

local model_grid_click = ac.tools.click_box({
  store = store,
  requireScreenshot = true,
  coordinateSpace = "model_1000",
}).handler({}, {
  rect = {
    left = 250,
    top = 250,
    right = 500,
    bottom = 500,
  },
})
local model_grid_click_params = ac.trace[#ac.trace].steps[1].params
local array_click = ac.tools.click_box({
  store = store,
  requireScreenshot = true,
}).handler({}, {
  rect = { 100, 50, 200, 100 },
})
local array_click_params = ac.trace[#ac.trace].steps[1].params
local helper_center = ac.rect.center({
  left = 10,
  top = 20,
  right = 30,
  bottom = 50,
})
local helper_response = ac.response.data({
  data = {
    value = "ok",
  },
})
local helper_state = ac.desktop.screenshot_state({ width = 100, height = 50 }, {
  coordinateSpace = "model_1000",
})
local inputless = ac.tool.inputless("unit_no_input", "No input unit tool", function()
  return ac.tool_result.done({ value = "done" })
end).handler({}, {})
local typed_screenshot = ac.desktop.type_and_screenshot("abc", store, {
  maxDimension = 640,
  frontmostWindowOnly = true,
})
local pressed_screenshot = ac.desktop.press_and_screenshot("esc", store, {
  allowError = true,
  maxDimension = 640,
  frontmostWindowOnly = true,
})
local current_screenshot = ac.desktop.screenshot_result(store, {
  reason = "already handled",
  maxDimension = 640,
  frontmostWindowOnly = true,
})
local vision_progress = {}
local vision_ctx = {}
function vision_ctx:progress(item)
  vision_progress[#vision_progress + 1] = item
end
local vision_store = {}
local vision = ac.desktop.vision_task(vision_ctx, {
  app = "UnitApp",
  owner = "unit.desktop",
  purpose = "unit vision",
  launchStep = "launch-unit",
  screenshotStep = "screenshot-unit",
  focusTimeoutMs = 1234,
  focusPollMs = 111,
  store = vision_store,
  screenshot = {
    maxDimension = 500,
    frontmostWindowOnly = true,
  },
  coordinateSpace = "model_1000",
  data = {
    value = "seen",
  },
})

return {
  tool_count = #tools,
  screenshot_frontmost_default = tools[1].input.properties.frontmostWindowOnly.default,
  click_ok = click.ok,
  focus_method = ac.trace[1].steps[1].method,
  click_method = ac.trace[2].steps[1].method,
  has_target = click_params.target ~= nil,
  rect_left = click_params.rect.left,
  rect_top = click_params.rect.top,
  rect_right = click_params.rect.right,
  rect_bottom = click_params.rect.bottom,
  rect_click_x = click_params.rectClickXFraction,
  rect_click_y = click_params.rectClickYFraction,
  missing_ok = missing.ok,
  missing_code = missing.error.code,
  model_grid_click_ok = model_grid_click.ok,
  model_grid_rect_left = model_grid_click_params.rect.left,
  model_grid_rect_top = model_grid_click_params.rect.top,
  model_grid_rect_right = model_grid_click_params.rect.right,
  model_grid_rect_bottom = model_grid_click_params.rect.bottom,
  array_click_ok = array_click.ok,
  array_click_rect_left = array_click_params.rect.left,
  array_click_rect_top = array_click_params.rect.top,
  array_click_rect_right = array_click_params.rect.right,
  array_click_rect_bottom = array_click_params.rect.bottom,
  array_rect_center_x = ac.rect.center({ 10, 20, 30, 50 }).x,
  rect_schema_left_min = ac.schemas.rect().properties.left.minimum,
  rect_schema_left_max_missing = ac.schemas.rect().properties.left.maximum == nil,
  text_trimmed = ac.text.trim("  hello  "),
  text_normalized = ac.text.normalized("  Hello   WORLD "),
  text_present = ac.text.present("  yes "),
  text_equals = ac.text.equals(" Hello   WORLD ", "hello world"),
  response_data_value = helper_response.value,
  rect_center_x = helper_center.x,
  rect_center_y = helper_center.y,
  rect_center_width = helper_center.width,
  rect_center_height = helper_center.height,
  rect_invalid_nil = ac.rect.center({ left = 5, top = 5, right = 5, bottom = 6 }) == nil,
  rect_valid = ac.rect.valid({ left = 5, top = 5, right = 6, bottom = 6 }),
  rect_vertical_distance = ac.rect.vertical_distance(
    { left = 0, top = 0, right = 10, bottom = 10 },
    { left = 0, top = 20, right = 10, bottom = 30 }
  ),
  rect_horizontal_gap = ac.rect.horizontal_gap(
    { left = 0, top = 0, right = 10, bottom = 10 },
    { left = 25, top = 0, right = 30, bottom = 10 }
  ),
  screenshot_state_space = helper_state.coordinate_space,
  screenshot_state_width = helper_state.width,
  inputless_done = inputless.done,
  inputless_value = inputless.result.value,
  type_screenshot_ok = typed_screenshot.ok,
  type_screenshot_image = typed_screenshot.result.image,
  press_screenshot_ok = pressed_screenshot.ok,
  press_screenshot_image = pressed_screenshot.result.image,
  current_screenshot_ok = current_screenshot.ok,
  current_screenshot_image = current_screenshot.result.image,
  current_screenshot_ignored = current_screenshot.result.ignored,
  current_screenshot_reason = current_screenshot.result.reason,
  vision_progress_control = vision_progress[1].step,
  vision_progress_purpose = vision_progress[1].purpose,
  vision_progress_launch = vision_progress[2].step,
  vision_progress_screenshot = vision_progress[3].step,
  vision_image = vision.screenshot.__ac_image_path,
  vision_width = vision.screenshot.width,
  vision_store_width = vision_store.last_screenshot_image_width,
  vision_data_value = vision.data.value,
  vision_data_space = vision.data.screenshot.coordinate_space,
  vision_content_image = vision.user_content[2].path,
}
