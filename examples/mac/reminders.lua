local ac = require("computer_cpp")

local APP_NAME = "Reminders"
local SCREENSHOT_MAX_DIMENSION = 1000
local COORDINATE_SPACE = "model_1000"

local RECT_TOOL_PROMPT = [[
For click tools, pass only rect objects. Rect coordinates use the model's standard
0-1000 image grid: 0 is the left/top edge of the screenshot and 1000 is the right/bottom
edge. The tool scales them to the real window and clicks the center of the rect.
]]

local LIST_PROMPT = [[
Read the supplied Reminders screenshot. Call report_visible_reminders once with only the
visible reminder rows from the requested list. If the list is blocked, unreadable, or not
the requested list, call blocked with a concrete reason.
]]

local ADD_PROMPT = RECT_TOOL_PROMPT .. [[
Use only the visible Reminders app and the provided tools. Create the requested reminder
title, verify it in a screenshot, then call confirm_reminder_created.
Call click_toolbar_plus exactly once with a tight rect around the visible + button in the
top Reminders toolbar, then call type_requested_reminder_text exactly once. It types the
requested title and requested notes when notes are non-empty, then returns a fresh
screenshot. Then call finish_reminder_editing. Never create a second row. Do not click the
macOS red/yellow/green window controls, the sidebar Suggested List +, or any blank row
circle. After finish_reminder_editing, do not call click_toolbar_plus or
type_requested_reminder_text again; inspect the returned screenshot and either confirm or
block. If the visible title is duplicated, malformed, or not exactly the requested title,
call blocked. If notes is non-empty and the visible notes are absent or malformed, call
blocked. If blocked, call blocked. In confirm_reminder_created, set visible_title to the
exact visible requested title, visible_notes to the exact visible notes text when notes
were requested or "" otherwise, and evidence to the visible reason creation is proven.
Never call confirm_reminder_created with empty arguments.
]]

local COMPLETE_PROMPT = RECT_TOOL_PROMPT .. [[
Use only the visible Reminders app and the provided tools. If the requested list is not
selected, click that list in the sidebar and screenshot again. Locate the exact requested
title and the empty circular checkbox immediately to the left of that same row. Call
complete_reminder_checkbox with tight rects around both the visible title text glyphs and
the checkbox. Pass rects as { left, top, right, bottom } objects; [left, top, right, bottom]
arrays are also accepted. Do not pass requested_title; the tool already knows the command
title. Do not use a row location from memory; the title_rect must enclose the actual visible
requested title in the screenshot. Ignore blank edit rows. Then inspect the returned
screenshot and call confirm_reminder_completed only if the requested list is still selected
and the exact requested title is no longer visible as an incomplete row. If the title is
still visible, a blank edit row was opened, the selected list changed, or the row cannot be
found, call blocked. In confirm_reminder_completed, set visible_list to the selected list
heading and evidence to the visible reason completion is proven. Never call
confirm_reminder_completed with empty arguments.
]]

local REMINDER_ROW_SCHEMA = {
  type = "object",
  properties = {
    title = { type = "string" },
    completed = { type = "boolean", default = false },
    due = { type = "string", default = "" },
    notes = { type = "string", default = "" },
  },
  additionalProperties = false,
}

local function reminder_row(row)
  row = row or {}
  return {
    title = ac.value.string(row.title),
    completed = ac.value.boolean(row.completed),
    due = ac.value.string(row.due),
    notes = ac.value.string(row.notes),
  }
end

local function visible_rows(rows, limit)
  local out = {}
  for _, row in ipairs(rows or {}) do
    if #out >= (limit or 25) then break end
    local item = reminder_row(row)
    if item.title ~= "" then
      out[#out + 1] = item
    end
  end
  return out
end

local function reminder_array_schema()
  return ac.schemas.array(REMINDER_ROW_SCHEMA)
end

local function screenshot_options()
  return {
    maxDimension = SCREENSHOT_MAX_DIMENSION,
    frontmostWindowOnly = true,
  }
end

local function screenshot_result(store, reason)
  local opts = screenshot_options()
  opts.reason = reason
  return ac.desktop.screenshot_result(store, opts)
end

local function click_options()
  local opts = screenshot_options()
  opts.coordinateSpace = COORDINATE_SPACE
  opts.requireScreenshot = true
  return opts
end

local function active_app_matches(app_name)
  local result = ac.request("app_active", {}, { allow_error = true })
  if not result.ok then return false, result end
  local data = ac.response.data(result)
  local app = data.app or data.frontmostApp or data
  if type(app) ~= "table" or app.available == false then return false, result end
  local haystack = ac.text.normalized(ac.value.string(app.name) .. " " .. ac.value.string(app.bundleId))
  return haystack:find(ac.text.normalized(app_name), 1, true) ~= nil, result
end

local function ensure_reminders_frontmost(store, reason)
  local active = active_app_matches(APP_NAME)
  if active then return true end
  ac.desktop.focus_app(APP_NAME, {
    timeoutMs = 15000,
    pollMs = 250,
    allowError = true,
  })
  return false, screenshot_result(store, reason or "Reminders is not frontmost; inspect the screenshot and continue only in Reminders")
end

local function looks_like_window_control(rect)
  local center = ac.rect.center(rect)
  if not center then return false end
  return center.x <= 120 and center.y <= 100
end

local function looks_like_toolbar_plus(rect)
  local center = ac.rect.center(rect)
  if not center then return false end
  return center.x >= 560 and center.y <= 140
end

local function evidence_looks_like_path(evidence)
  local text = ac.value.string(evidence)
  return text:match("^/") ~= nil or text:match("%.png$") ~= nil
end

local function reminders_task(ctx, purpose, store, agent)
  return ac.desktop.vision_task(ctx, {
    app = APP_NAME,
    owner = "examples.mac.reminders",
    purpose = purpose,
    ttlMs = 180000,
    waitMs = 30000,
    maxRuntimeMs = 900000,
    launchStep = "launch-reminders",
    screenshotStep = "screenshot-reminders",
    focusTimeoutMs = 15000,
    focusPollMs = 250,
    store = store,
    screenshot = screenshot_options(),
    coordinateSpace = COORDINATE_SPACE,
    data = agent.data,
    agent = agent,
  }).result or {}
end

local function validate_checkbox_title_rects(checkbox_rect, title_rect)
  local checkbox = ac.rect.center(checkbox_rect)
  local title = ac.rect.center(title_rect)
  if not checkbox then return "checkbox_rect must be a non-empty rectangle" end
  if not title then return "title_rect must be a non-empty rectangle" end

  local vertical_distance = ac.rect.vertical_distance(checkbox_rect, title_rect)
  local max_vertical_distance = math.max(18, (checkbox.height + title.height) * 0.8)
  if vertical_distance > max_vertical_distance then
    return "checkbox_rect and title_rect are not on the same row"
  end

  local horizontal_gap = ac.rect.horizontal_gap(checkbox_rect, title_rect)
  if horizontal_gap < -8 then
    return "checkbox_rect must be to the left of title_rect"
  end
  if horizontal_gap > 90 then
    return "checkbox_rect is too far from title_rect"
  end

  return nil
end

local function complete_checkbox_tool(store)
  return ac.tool.define("complete_reminder_checkbox", {
    description = "Click the visible completion checkbox for the requested reminder row, then return a fresh screenshot.",
    input = {
      type = "object",
      properties = {
        requested_title = { type = "string" },
        title_rect = ac.schemas.rect_like(),
        checkbox_rect = ac.schemas.rect_like(),
      },
      required = { "title_rect", "checkbox_rect" },
      additionalProperties = false,
    },
    handler = function(_, args)
      local checkbox_rect = ac.rect.normalize(args.checkbox_rect)
      local title_rect = ac.rect.normalize(args.title_rect)
      local mismatch = validate_checkbox_title_rects(checkbox_rect, title_rect)
      if mismatch then
        return ac.tool_result.invalid("complete_reminder_checkbox " .. mismatch)
      end

      local result = ac.desktop.click_rect_and_screenshot(store, checkbox_rect, click_options())
      if result.ok then result.result.requested_title = store.title end
      return result
    end,
  })
end

local function report_visible_reminders(store)
  return ac.tool.define("report_visible_reminders", {
    description = "Report visible reminder rows from the current screenshot.",
    input = {
      reminders = {
        type = "array",
        required = true,
        items = REMINDER_ROW_SCHEMA,
      },
    },
    handler = function(_, args)
      store.visible_reminders = visible_rows(args.reminders, store.limit or 25)
      return ac.tool_result.done({ count = #store.visible_reminders })
    end,
  })
end

local function not_verified(code, message)
  return ac.tool_result.error({ code = code, message = message })
end

local function confirm_reminder_created(store)
  return ac.tool.define("confirm_reminder_created", {
    description = "Confirm that the requested reminder was created and is visible.",
    input = {
      visible_title = { type = "string", default = "", description = "Exact visible reminder title text. Do not omit." },
      visible_notes = { type = "string", default = "", description = "Exact visible notes text, or empty string when no notes were requested. Do not omit." },
      evidence = { type = "string", default = "", description = "Visible screenshot evidence that proves creation. Do not omit." },
    },
    handler = function(_, args)
      if not store.title_typed then
        return not_verified("creation_not_verified", "the requested title was not typed by the app tool")
      end
      local frontmost, frontmost_result = ensure_reminders_frontmost(store, "Reminders is not frontmost, so creation cannot be visually confirmed")
      if not frontmost then return frontmost_result end
      if not ac.text.present(args.visible_title) then
        return screenshot_result(store, "confirm_reminder_created requires visible_title, visible_notes, and evidence from the screenshot")
      end
      if not ac.text.present(args.evidence) then
        return screenshot_result(store, "confirm_reminder_created requires evidence describing visible screenshot proof")
      end
      if evidence_looks_like_path(args.evidence) then
        return screenshot_result(store, "evidence must describe visible Reminders content, not repeat the screenshot path")
      end
      if not ac.text.equals(args.visible_title, store.title) then
        return screenshot_result(store, "visible_title must exactly match the requested title")
      end
      if ac.value.string(store.notes) ~= "" then
        if not store.notes_typed then
          return not_verified("creation_not_verified", "the requested notes were not typed by the app tool")
        end
        if not ac.text.present(args.visible_notes) then
          return screenshot_result(store, "confirm_reminder_created requires visible_notes because notes were requested")
        end
        if not ac.text.equals(args.visible_notes, store.notes) then
          return screenshot_result(store, "visible_notes must exactly match the requested notes")
        end
      end
      store.created = {
        title = store.title,
        visible_title = ac.value.string(args.visible_title),
        visible_notes = ac.value.string(args.visible_notes),
        evidence = ac.value.string(args.evidence),
      }
      return ac.tool_result.done(store.created)
    end,
  })
end

local function confirm_reminder_completed(store)
  return ac.tool.define("confirm_reminder_completed", {
    description = "Confirm that the requested reminder was completed and visually verified.",
    input = {
      completed = { type = "boolean", default = true },
      requested_title = { type = "string", default = "" },
      matched_title = { type = "string", default = "" },
      visible_list = { type = "string", default = "" },
      evidence = { type = "string", default = "" },
    },
    handler = function(_, args)
      if args.completed == false then
        return not_verified("completion_not_verified", "reminder completion was not visually verified")
      end
      local frontmost, frontmost_result = ensure_reminders_frontmost(store, "Reminders is not frontmost, so completion cannot be visually confirmed")
      if not frontmost then return frontmost_result end
      if args.requested_title ~= "" and not ac.text.equals(args.requested_title, store.title) then
        return screenshot_result(store, "requested_title must exactly match this command title")
      end
      if args.matched_title ~= "" and not ac.text.equals(args.matched_title, store.title) then
        return screenshot_result(store, "matched_title must exactly match this command title")
      end
      if not ac.text.present(args.visible_list) then
        return screenshot_result(store, "confirm_reminder_completed requires visible_list and evidence from the screenshot")
      end
      if ac.value.string(store.list) ~= "" and not ac.text.equals(args.visible_list, store.list) then
        return screenshot_result(store, "visible_list must exactly match this command list")
      end
      if not ac.text.present(args.evidence) then
        return screenshot_result(store, "confirm_reminder_completed requires evidence describing visible screenshot proof")
      end
      if evidence_looks_like_path(args.evidence) then
        return screenshot_result(store, "evidence must describe visible Reminders content, not repeat the screenshot path")
      end
      store.completed = {
        requested_title = store.title,
        matched_title = ac.value.string(args.matched_title ~= "" and args.matched_title or store.title),
        evidence = ac.value.string(args.evidence),
      }
      return ac.tool_result.done(store.completed)
    end,
  })
end

local function screenshot_tool(store)
  local opts = screenshot_options()
  opts.store = store
  return ac.tools.screenshot(opts)
end

local function click_tool(store)
  return ac.tool.define("click_box", {
    description = "Click the center of a visible rectangle in Reminders, then return a fresh screenshot.",
    input = {
      type = "object",
      properties = {
        rect = ac.schemas.rect_like(),
      },
      required = { "rect" },
      additionalProperties = false,
    },
    handler = function(_, args)
      if store.title_typed or store.finished_editing then
        return screenshot_result(store, "reminder text was already entered; inspect the screenshot and confirm or block")
      end
      local rect = ac.rect.normalize(args.rect)
      if looks_like_window_control(rect) then
        return screenshot_result(store, "refusing to click macOS window controls; click the Reminders toolbar plus button instead")
      end
      local frontmost, frontmost_result = ensure_reminders_frontmost(store, "Reminders is not frontmost; click the toolbar plus only after Reminders is visible")
      if not frontmost then return frontmost_result end
      return ac.desktop.click_rect_and_screenshot(store, rect, click_options())
    end,
  })
end

local function click_toolbar_plus_tool(store)
  return ac.tool.define("click_toolbar_plus", {
    description = "Click the visible + button in the top Reminders toolbar, then return a fresh screenshot.",
    input = {
      type = "object",
      properties = {
        rect = ac.schemas.rect_like(),
      },
      required = { "rect" },
      additionalProperties = false,
    },
    handler = function(_, args)
      if store.title_typed or store.finished_editing then
        return screenshot_result(store, "reminder text was already entered; inspect the screenshot and confirm or block")
      end
      local rect = ac.rect.normalize(args.rect)
      if looks_like_window_control(rect) then
        return screenshot_result(store, "refusing to click macOS window controls; pass the top Reminders toolbar + rect")
      end
      if not looks_like_toolbar_plus(rect) then
        return screenshot_result(store, "rect does not look like the top Reminders toolbar + button; pass a tight rect around the visible toolbar +")
      end
      local frontmost, frontmost_result = ensure_reminders_frontmost(store, "Reminders is not frontmost; click the toolbar plus only after Reminders is visible")
      if not frontmost then return frontmost_result end
      return ac.desktop.click_rect_and_screenshot(store, rect, click_options())
    end,
  })
end

local function type_requested_reminder_text_tool(store)
  return ac.tool.inputless(
    "type_requested_reminder_text",
    "Type the command's requested reminder title and optional notes exactly once into the focused new reminder row, then return a fresh screenshot.",
    function()
      if store.title_typed then
        return screenshot_result(store, "requested reminder text was already typed; inspect the screenshot and confirm or block")
      end
      if store.finished_editing then
        return ac.tool_result.invalid("reminder text must be typed before finishing editing")
      end

      local frontmost, frontmost_result = ensure_reminders_frontmost(store, "Reminders is not frontmost, so typing was not sent; click the toolbar plus in Reminders before typing")
      if not frontmost then return frontmost_result end

      store.title_typed = true

      local title_result = ac.request("type", { text = store.title, paste = true }, { allow_error = true })
      if not title_result.ok then return title_result end

      if ac.value.string(store.notes) ~= "" then
        local tab_result = ac.request("press", { keys = "tab" }, { allow_error = true })
        if not tab_result.ok then return tab_result end
        local notes_result = ac.request("type", { text = store.notes, paste = true }, { allow_error = true })
        if not notes_result.ok then return notes_result end
        store.notes_typed = true
      end

      return ac.desktop.screenshot_result(store, screenshot_options())
    end
  )
end

local function finish_reminder_editing_tool(store)
  return ac.tool.inputless(
    "finish_reminder_editing",
    "Finish editing the current reminder row and return a fresh screenshot.",
    function()
      if store.finished_editing then
        return screenshot_result(store, "editing was already finished; inspect the screenshot and confirm or block")
      end
      local frontmost, frontmost_result = ensure_reminders_frontmost(store, "Reminders is not frontmost, so editing was not finished; inspect the screenshot and continue in Reminders")
      if not frontmost then return frontmost_result end
      store.finished_editing = true
      return ac.desktop.press_and_screenshot("esc", store, {
        allowError = true,
        maxDimension = SCREENSHOT_MAX_DIMENSION,
        frontmostWindowOnly = true,
      })
    end
  )
end

local function add_tools(store)
  local tools = {
    click_toolbar_plus_tool(store),
    type_requested_reminder_text_tool(store),
  }
  tools[#tools + 1] = finish_reminder_editing_tool(store)
  tools[#tools + 1] = confirm_reminder_created(store)
  tools[#tools + 1] = ac.tools.blocked()
  return tools
end

local function complete_tools(store)
  return {
    screenshot_tool(store),
    click_tool(store),
    ac.tools.scroll_down(),
    ac.tools.scroll_up(),
    complete_checkbox_tool(store),
    confirm_reminder_completed(store),
    ac.tools.blocked(),
  }
end

local function list_reminders(ctx, args)
  local limit = args.limit or 25
  local store = { limit = limit, visible_reminders = {} }

  reminders_task(ctx, "list Reminders rows", store, {
    name = "examples.mac.reminders.list",
    prompt = LIST_PROMPT,
    goal = "Read visible reminders.",
    max_steps = 1,
    max_tokens = 900,
    data = {
      list = args.list,
      limit = limit,
    },
    tools = {
      report_visible_reminders(store),
      ac.tools.blocked(),
    },
  })

  local rows = visible_rows(store.visible_reminders, limit)
  return {
    list = args.list,
    count = #rows,
    reminders = ac.value.array(rows),
  }
end

local function add_reminder(ctx, args)
  if ac.value.string(args.due) ~= "" then
    error("due dates are not supported in this Reminders example", 0)
  end

  local store = {
    title = args.title,
    notes = ac.value.string(args.notes),
  }

  reminders_task(ctx, "add Reminders row", store, {
    name = "examples.mac.reminders.add",
    prompt = ADD_PROMPT,
    goal = "Create and verify one reminder.",
    max_steps = 16,
    data = {
      list = args.list,
      title = args.title,
      notes = store.notes,
    },
    tools = add_tools(store),
  })

  if not store.created then
    error("micro-agent finished without confirming reminder creation", 0)
  end

  return {
    created = true,
    list = args.list,
    title = args.title,
    notes = store.notes,
    due = "",
    visible_title = store.created.visible_title,
    visible_notes = store.created.visible_notes,
    evidence = store.created.evidence,
  }
end

local function complete_reminder(ctx, args)
  local store = {
    title = args.title,
    list = args.list or "",
  }

  reminders_task(ctx, "complete Reminders row", store, {
    name = "examples.mac.reminders.complete",
    prompt = COMPLETE_PROMPT,
    goal = "Complete and verify one reminder.",
    max_steps = 16,
    data = {
      list = args.list or "",
      title = args.title,
    },
    tools = complete_tools(store),
  })

  if not store.completed then
    error("micro-agent finished without confirming reminder completion", 0)
  end

  return {
    completed = true,
    title = args.title,
    matched_title = store.completed.matched_title,
    evidence = store.completed.evidence,
  }
end

local function summarize_rows(list_name, rows)
  if #rows == 0 then
    return "No visible reminders were reported for " .. list_name .. "."
  end

  local open_count = 0
  local completed_count = 0
  local titles = {}
  for _, row in ipairs(rows) do
    if row.completed then
      completed_count = completed_count + 1
    else
      open_count = open_count + 1
      if #titles < 5 then
        titles[#titles + 1] = row.title
      end
    end
  end

  local summary = tostring(#rows) .. " visible reminder"
  if #rows ~= 1 then summary = summary .. "s" end
  summary = summary .. " in " .. list_name .. ": " .. tostring(open_count) .. " open"
  if completed_count > 0 then
    summary = summary .. ", " .. tostring(completed_count) .. " completed"
  end
  if #titles > 0 then
    summary = summary .. ". Open items include: " .. table.concat(titles, "; ") .. "."
  else
    summary = summary .. "."
  end
  return summary
end

local function summarize_list(ctx, args)
  local listed = list_reminders(ctx, {
    list = args.list,
    limit = args.limit or 50,
  })
  local rows = visible_rows(listed.reminders, args.limit or 50)
  return {
    list = args.list,
    count = #rows,
    summary = summarize_rows(args.list, rows),
    reminders = ac.value.array(rows),
  }
end

local app = ac.app.define({
  name = "mac.reminders",
  title = "macOS Reminders",
  version = "1.0.0",
  description = "A screenshot-guided desktop automation API for macOS Reminders.",
})

app:command("list-reminders", {
  description = "List visible reminders from a Reminders list.",
  input = {
    list = { type = "string", required = true, description = "Reminders list name to select." },
    limit = { type = "integer", default = 25, minimum = 1, maximum = 100 },
  },
  output = {
    list = { type = "string" },
    count = { type = "integer" },
    reminders = reminder_array_schema(),
  },
  handler = list_reminders,
})

app:command("add-reminder", {
  description = "Create a reminder in a Reminders list and verify it visually.",
  input = {
    list = { type = "string", required = true, description = "Reminders list name to select." },
    title = { type = "string", required = true, description = "Reminder title to create." },
    notes = { type = "string", default = "", description = "Optional notes text to add to the reminder." },
    due = { type = "string", default = "", description = "Unsupported; must be empty." },
  },
  output = {
    created = { type = "boolean" },
    list = { type = "string" },
    title = { type = "string" },
    notes = { type = "string" },
    due = { type = "string" },
    visible_title = { type = "string" },
    visible_notes = { type = "string" },
    evidence = { type = "string" },
  },
  handler = add_reminder,
})

app:command("complete-reminder", {
  description = "Complete a reminder by title and verify completion visually.",
  input = {
    title = { type = "string", required = true, description = "Reminder title to complete." },
    list = { type = "string", default = "", description = "Optional list name to select before finding the reminder." },
  },
  output = {
    completed = { type = "boolean" },
    title = { type = "string" },
    matched_title = { type = "string" },
    evidence = { type = "string" },
  },
  handler = complete_reminder,
})

app:command("summarize-list", {
  description = "Summarize visible reminders from a Reminders list.",
  input = {
    list = { type = "string", required = true, description = "Reminders list name to summarize." },
    limit = { type = "integer", default = 50, minimum = 1, maximum = 100 },
  },
  output = {
    list = { type = "string" },
    count = { type = "integer" },
    summary = { type = "string" },
    reminders = reminder_array_schema(),
  },
  handler = summarize_list,
})

return app
