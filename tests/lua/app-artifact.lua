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
    devicePath = { type = "string" },
    legacyPath = { type = "string" },
    plainLegacyPath = { type = "string" },
    legacyKind = { type = "string" },
    writeError = { type = "string" },
  },
  handler = function(ctx)
    local filename = "../" .. string.rep("a", 190) .. ".html"
    local artifact = ctx:artifact_bytes("<html><body>diagnostic DOM</body></html>", {
      filename = filename,
      contentType = "text/html",
    })
    local second = ctx:artifact_bytes("second artifact", {
      filename = filename,
      contentType = "text/plain",
    })
    local device = ctx:artifact_bytes("reserved device test", {
      filename = "NUL.txt",
      contentType = "text/plain",
    })
    local legacyMetadata = { filename = "shot.png", kind = "original" }
    local legacy = ctx:artifact("/tmp/existing-screenshot.png", legacyMetadata)
    local plainLegacy = ctx:artifact("/tmp/plain-artifact.png")
    legacyMetadata.kind = "changed"
    return {
      path = artifact.path,
      secondPath = second.path,
      devicePath = device.path,
      legacyPath = legacy.path,
      plainLegacyPath = plainLegacy.path,
      legacyKind = legacy.metadata.kind,
      writeError = artifact.metadata.writeError or "",
    }
  end,
})

return app
