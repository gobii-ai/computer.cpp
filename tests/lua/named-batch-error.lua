local ac = require("computer_cpp")

ac.batch({
  {
    id = "needs-key",
    method = "llm_chat",
    params = {
      baseUrl = "https://inference.example.test/v1",
      model = "qwen36-27b",
      apiKey = "",
      messages = {
        { role = "user", content = "ping" },
      },
    },
  },
})

return {
  unreachable = true,
}
