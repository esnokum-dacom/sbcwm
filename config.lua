defaultsh = "/bin/sh"

keys = {
  { mod = {"super", "shift"}, key = "c",     func = "win_kill" },
  { mod = {"super"},          key = "f",     func = "win_fs" },
  { mod = {"super"},          key = "space", func = "run", arg = {"lm"} },
  { mod = {"super","shift"},  key = "Left",  func = "canvas_pan_key", arg = 0 },
}
