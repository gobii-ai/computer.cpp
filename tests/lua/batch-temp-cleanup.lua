local ac = require("computer_cpp")

local tmp = assert(ac.vars.tmp, "tmp var required")
local malformed_tmp = assert(ac.vars.malformed_tmp, "malformed_tmp var required")
local write_tmp = assert(ac.vars.write_tmp, "write_tmp var required")
local original_tmpname = os.tmpname
local original_popen = io.popen
local original_open = io.open

local function restore_globals()
  os.tmpname = original_tmpname
  io.popen = original_popen
  io.open = original_open
end

local function run_case(path, patch)
  restore_globals()
  os.tmpname = function()
    return path
  end

  local ok, case_ok, case_err = pcall(function()
    patch()
    return pcall(function()
      ac.batch({
        { method = "ping" },
      })
    end)
  end)

  restore_globals()
  if not ok then
    error(case_ok, 0)
  end
  return case_ok, case_err
end

local function temp_exists(path)
  local file = io.open(path, "r")
  local exists = file ~= nil
  if file then
    file:close()
  end
  return exists
end

local ok, err = run_case(tmp, function()
  io.popen = function()
    error("forced popen failure")
  end
end)

local write_ok, write_err = run_case(write_tmp, function()
  io.open = function(path, mode)
    if path == write_tmp and mode == "w" then
      local real = assert(original_open(path, mode))
      real:write("partial")
      real:close()
      return {
        write = function()
          error("forced write failure")
        end,
        close = function() end,
      }
    end
    return original_open(path, mode)
  end
end)

local malformed_ok, malformed_err = run_case(malformed_tmp, function()
  io.popen = function()
    return {
      read = function()
        return "not-json"
      end,
      close = function()
        return true
      end,
    }
  end
end)

local cleanup_ok, cleanup_response = pcall(function()
  return ac.batch({
    { method = "ping" },
  })
end)

return {
  batch_ok = ok,
  error = tostring(err),
  temp_exists = temp_exists(tmp),
  write_ok = write_ok,
  write_error = tostring(write_err),
  write_temp_exists = temp_exists(write_tmp),
  malformed_ok = malformed_ok,
  malformed_error = tostring(malformed_err),
  malformed_temp_exists = temp_exists(malformed_tmp),
  cleanup_ok = cleanup_ok,
  cleanup_response_ok = cleanup_response and cleanup_response.ok or false,
}
