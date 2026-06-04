ac.scroll(-120, 0, {
  no_humanize = true,
})
ac.scroll(-80, 0, {
  no_humanize = false,
})

local first = ac.trace[1].steps[1].params
local second = ac.trace[2].steps[1].params
return {
  first_method = ac.trace[1].steps[1].method,
  first_humanize = first.humanize,
  second_method = ac.trace[2].steps[1].method,
  second_humanize = second.humanize,
}
