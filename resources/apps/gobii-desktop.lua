local ac = require("computer_cpp")

local app = ac.app.define({
  name = "gobii-desktop",
  title = "Gobii Desktop",
  version = "1.0.0",
})

local store = {}
local common = {
  store = store,
  coordinateSpace = "model_1000",
  requireScreenshot = true,
  observeAfterAction = true,
  maxDimension = 1200,
}

local function command_from_tool(name, tool, transform)
  app:command(name, {
    description = tool.description,
    input = tool.input,
    handler = function(ctx, args)
      local response = tool.handler(ctx, args or {})
      if type(response) ~= "table" or response.ok ~= true then
        local failure = type(response) == "table" and response.error or {}
        error(tostring(failure.message or failure.code or "desktop operation failed"), 2)
      end
      local result = response.result or {}
      if transform then return transform(result) end
      local image = result.image or result.__ac_image_path
      if type(image) == "string" and image ~= "" then
        return ac.mcp.result({
          text = name .. " completed",
          structured = result,
          images = {{ path = image, mime_type = "image/png" }},
        })
      end
      return result
    end,
  })
end

command_from_tool("observe", ac.tools.observe_desktop({
  store = store,
  coordinateSpace = "model_1000",
  maxDimension = 1200,
  includeAccessibility = false,
}), function(result)
  local image = result.image or result.__ac_image_path
  return ac.mcp.result({
    text = "Current desktop observation",
    structured = result,
    images = image and {{ path = image, mime_type = "image/png" }} or {},
  })
end)

app:command("click", {
  description = "Click the center of a visible rectangle using an explicit coordinate space and native pointer input.",
  input = {
    rect = ac.schemas.rect_like(),
    coordinateSpace = {
      type = "string",
      required = true,
      enum = { "model_1000", "screen_pixels", "screenshot_pixels" },
    },
    button = {
      type = "string",
      default = "left",
      enum = { "left", "right", "middle" },
    },
    clickCount = {
      type = "integer",
      default = 1,
      minimum = 1,
      maximum = 5,
    },
    application = { type = "string" },
  },
  handler = function(ctx, args)
    local tool = ac.tools.click_box({
      store = store,
      coordinateSpace = args.coordinateSpace,
      requireScreenshot = true,
      observeAfterAction = true,
      focusApp = args.application,
      maxDimension = 1200,
    })
    local response = tool.handler(ctx, args)
    if type(response) ~= "table" or response.ok ~= true then
      local failure = type(response) == "table" and response.error or {}
      error(tostring(failure.message or failure.code or "click failed"), 2)
    end
    local result = response.result or {}
    local image = result.image or result.__ac_image_path
    return ac.mcp.result({
      text = "click completed",
      structured = result,
      images = image and {{ path = image, mime_type = "image/png" }} or {},
    })
  end,
})
command_from_tool("type_text", ac.tools.type_text(common))
command_from_tool("press_key", ac.tools.press_key(common))
command_from_tool("scroll", ac.tools.scroll(common))
command_from_tool("open_application", ac.tools.activate_app(common))
command_from_tool("focus_application", ac.tools.activate_app(common))
command_from_tool("wait_stable", ac.tools.wait_stable())

app:command("computer_status", {
  description = "Report permissions, desktop lock/readiness state, active application, screen size, and the current control lease.",
  input = {},
  handler = function()
    local state = ac.state()
    local lease = ac.request("control_session_status", {
      scope = ac.session.scope(),
    }, { allow_error = true })
    return {
      state = state.data or state,
      lease = lease.data or {
        ok = lease.ok,
        code = lease.code,
      },
    }
  end,
})

return app
