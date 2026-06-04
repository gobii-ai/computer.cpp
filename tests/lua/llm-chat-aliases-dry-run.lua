local ac = require("computer_cpp")

ac.llm.chat({
  base_url = "https://inference.example.test/v1",
  api_key = "",
  model = "demo-model",
  timeout_ms = 1234,
  messages = {
    { role = "user", content = "hello" },
  },
})

local params = ac.trace[1].steps[1].params
local response_params = ac.trace[1].response.data.results[1].data.params
return {
  method = ac.trace[1].steps[1].method,
  base_url = params.baseUrl,
  api_key = response_params.apiKey,
  model = params.model,
  timeout_ms = params.timeoutMs,
}
