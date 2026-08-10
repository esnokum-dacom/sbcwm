<div align="center">
  <h1>ScbWM</h1>
  <p>
    <b>S</b>ome <b>C</b>ompact <b>b</b>ox <b>WM</b> — a lightweight, canvas-based window manager written in <b>XCB</b>,
    inspired by <a href="https://github.com/esnokum-dacom/SOWM-Plus-Plus">SOWM++</a>.
  </p>
  <p>
    <img src="https://github.com/esnokum-dacom/sbcwm/blob/main/sbcwm.png" width="59%" align="center">
  </p>
</div>

---

## What is it?

ScbWM is a stacking window manager built directly on **XCB (X C Binding)** instead of Xlib. The code follows the same spirit and layout as SOWM++, but XCB keeps the runtime footprint smaller and the codebase closer to the wire protocol.

It ships with a few extras that plain sowm doesn't have:

- **Lua runtime configuration** (`config.lua`) — reloadable while running
- **Canvas panning** — an infinite desktop you can move around with the keyboard or mouse
- **HUD** that tracks window stack state across monitors
- **Minimap** — a live overview of your canvas
- **Titlebars** with close / maximize buttons (drawable via Xft icons)
- **Desktop icon shortcuts** — spawn apps from icons on the canvas
- **Right-click context menu** with your own entries
- **`sbcwmctl`** — a socket-based control client for runtime options, config reload and icon/shortcut management

## Memory footprint

```
ps -eo args,size,vsize,rss | grep -E 'sowm|sbcwm|dwm'
sbcwm                        1832  16236  9440
dwm                          1212  15928  8228
sowm                         1256  13388  7996
```

## Dependencies

- Xlib
- xcb
- Xinerama
- Xft
- xcb-randr
- xcb-shape
- xcb-icccm
- xcb-keysyms
- xcb-util
- lua5.3
- libpng16, libjpeg

## Install & Config

```sh
git clone https://github.com/esnokum-dacom/sbcwm
cd sbcwm
sudo make clean install
```

After install you can tweak the running instance without recompiling — the wm reads your config from `~/.config/sbcwm/config.lua` (a copy is placed there by `make install`).

### Runtime configuration (config.lua)

Most of the behaviour is Lua, so you can reload it live:

```lua
-- ~/.config/sbcwm/config.lua
fonts = "Terminus:style=Regular:pixelsize=16:antialias=false"

opts = {
  pan_step      = 120,     -- canvas pan distance per step
  titlebar      = 0,       -- enable/disable titlebars
  ui            = 1,       -- show the HUD
  xr_colors     = 1,       -- use Xresources colors
  border        = 1,       -- draw window borders
  border_width  = 1,
  ctxbg         = "#151515",
  ctxborder     = "#2d4d66",
}

-- right-click context menu entries
ctx = {
  { label = "Terminal",  func = "run", arg = {"st"} },
  { label = "Launcher",  func = "run", arg = {"lm"} },
  { label = "Wallpaper", func = "run", arg = {"xwall"} },
  { label = "Icons",     func = "toggle_icons" },
}

-- desktop icon shortcuts
icons = {
  { name = "Term", image = os.getenv("HOME") .. "/.config/sbcwm/icons/terminal.png",
    x = 80, y = 80, cmd = {"st"} },
}
```

#### Runtime options with sbcwmctl

```sh
sbcwmctl get <option>              # read a runtime option
sbcwmctl set <option> <value>      # write a runtime option
sbcwmctl reload                    # reload config.lua
sbcwmctl options                   # list runtime options
sbcwmctl shortcut add <name> <image> <x> <y> <cmd...>
sbcwmctl shortcut del <name>
sbcwmctl shortcut move <name> <x> <y>
sbcwmctl shortcut list
sbcwmctl shortcut show on|off
```

Titlebar icon glyphs (Xft symbol strings):

```c
const FcChar8 *close_sym = (FcChar8 *)"X";
const FcChar8 *max_sym   = (FcChar8 *)"O";
```

## Mouse bindings

| Combination           | Action                                  |
| --------------------- | ---------------------------------------- |
| `Mouse`               | focus under cursor                       |
| `MOD1` + `Left Mouse` | move window                              |
| `MOD1` + `Right Mouse`| resize window                            |
| `Mouse wheel` (Press) | move the canvas with the mouse position  |

## Keyboard bindings

| Combination                      | Action                       |
| -------------------------------- | ---------------------------- |
| `MOD1` + `f`                     | maximize toggle              |
| `MOD1` + `c`                     | center window                |
| `MOD1` + `Shift` + `c`           | kill window                  |
| `MOD1` + `1-6`                   | desktop swap                 |
| `MOD1` + `Shift` + `1-6`         | send window to desktop       |
| `MOD1` + `TAB` (*alt-tab*)       | focus cycle                  |
| `MOD1` + `Shift` + `Left/Right`  | pan canvas left / right      |
| `MOD1` + `Shift` + `Up/Down`     | pan canvas up / down         |
| `MOD1` + `b`                     | toggle minimap               |

> Keybindings come from `config.h` / `config.lua` — the defaults above are
> just what's shipped. Rebind anything without touching the source.

## Launch & utility bindings

| Combination              | Action        | Program      |
| ------------------------ | ------------- | ------------ |
| `MOD4` + `Return`        | terminal      | `st`         |
| `MOD4` + `p`             | dmenu         | `dmenu_run`  |
| `MOD4` + `Shift` + `s`   | scrot         | `scr`        |
| `XF86_AudioLowerVolume`  | volume down   | `amixer`     |
| `XF86_AudioRaiseVolume`  | volume up     | `amixer`     |
| `XF86_AudioMute`         | volume toggle | `amixer`     |
| `XF86_MonBrightnessUp`   | brightness up | `bri`        |
| `XF86_MonBrightnessDown` | brightness down | `bri`      |

---

Thank you so much
