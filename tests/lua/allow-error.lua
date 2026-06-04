local ac = require("computer_cpp")

local result = ac.request("llm_chat", {
  baseUrl = "https://inference.example.test/v1",
  model = "qwen36-27b",
  apiKey = "",
  messages = {
    { role = "user", content = "ping" },
  },
}, { allow_error = true })

return {
  ok = result.ok,
  code = result.code,
}
