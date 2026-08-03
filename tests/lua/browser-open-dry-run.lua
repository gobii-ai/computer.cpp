local ac = require("computer_cpp")

ac.browser.open("https://example.com", {
  browser = "Safari",
  new_window = false,
  new_instance = true,
})

ac.browser.open("https://example.org", {
  browser = "Firefox",
  no_new_window = true,
  no_new_instance = true,
})

ac.browser.open("https://example.net")

local params = ac.trace[1].steps[1].params
local no_params = ac.trace[2].steps[1].params
local default_params = ac.trace[3].steps[1].params
return {
  method = ac.trace[1].steps[1].method,
  url = params.url,
  browser = params.browser,
  new_window = params.newWindow,
  new_instance = params.newInstance,
  no_method = ac.trace[2].steps[1].method,
  no_url = no_params.url,
  no_browser = no_params.browser,
  no_new_window = no_params.newWindow,
  no_new_instance = no_params.newInstance,
  default_url = default_params.url,
  default_browser = default_params.browser,
  default_profile = default_params.profile,
  default_new_window = default_params.newWindow,
}
