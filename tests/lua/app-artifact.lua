local ac = require("computer_cpp")

local app = ac.app.define({
  name = "artifact-test",
  title = "Artifact Test",
  version = "1.0.0",
})

app:command("write", {
  input = {},
  output = {
    path = { type = "string" },
    secondPath = { type = "string" },
  },
  handler = function(ctx)
    local artifact = ctx:artifact("<html><body>diagnostic DOM</body></html>", {
      filename = "../dom snapshot.html",
      contentType = "text/html",
    })
    local second = ctx:artifact("second artifact", {
      filename = "../dom snapshot.html",
      contentType = "text/plain",
    })
    return { path = artifact.path, secondPath = second.path }
  end,
})

return app
