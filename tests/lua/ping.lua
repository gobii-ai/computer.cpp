local ac = require("computer_cpp")

local response = ac.ping()

return {
  message = response.data.message,
}
