defaultsh = "/bin/sh"

fonts = "Terminus:style=Regular:pixelsize=16:antialias=false"
fontb = "FiraMonoNerdFont:style=Regular:pixelsize=20:antialias=false"

opts = {
  pan_step = 120,
  titlebar = 0,
  ui = 1,
  xr_colors = 1,
  border = 1,
  border_width = 1,
}

keys = {
  { mod = {"super", "shift"}, key = "c",     func = "win_kill" },
  { mod = {"super"},          key = "f",     func = "win_fs" },
  { mod = {"super"},          key = "space", func = "run", arg = {"lm"} },
  { mod = {"super","shift"},  key = "Left",  func = "canvas_pan_key", arg = 0 },
}
