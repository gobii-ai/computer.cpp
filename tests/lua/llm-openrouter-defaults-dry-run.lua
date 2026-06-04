local ac = require("computer_cpp")

local selected_model = ac.models.main_fabric()

ac.llm.chat({
  messages = {
    { role = "user", content = "hello" },
  },
})

local params = ac.trace[1].steps[1].params
local response_params = ac.trace[1].response.data.results[1].data.params

return {
  selected_model_is_nil = selected_model == nil,
  method = ac.trace[1].steps[1].method,
  provider_is_nil = params.provider == nil,
  base_url_is_nil = params.baseUrl == nil,
  model_is_nil = params.model == nil,
  api_key_is_nil = response_params.apiKey == nil,
}
