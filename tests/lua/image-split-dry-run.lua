local ac = require("computer_cpp")

ac.image.split("/tmp/source.png", {
  out_dir = "/tmp/chunks",
  chunk_height = 128,
  overlap = 8,
  prefix = "tile",
})
ac.image.split("/tmp/source.png", {
  output_dir = "/tmp/output-chunks",
})

local params = ac.trace[1].steps[1].params
local output_params = ac.trace[2].steps[1].params
return {
  method = ac.trace[1].steps[1].method,
  path = params.path,
  out_dir = params.outDir,
  chunk_height = params.chunkHeight,
  overlap = params.overlap,
  prefix = params.prefix,
  output_method = ac.trace[2].steps[1].method,
  output_dir = output_params.outputDir,
}
