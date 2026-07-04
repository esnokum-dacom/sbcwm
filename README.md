<div align="center">
  <h1>ScbWM (SOWM C binding)</h1>
    <div>
      <p align="left">SCBWM is a window manager based on <a href="https://github.com/esnokum-dacom/SOWM-Plus-Plus"> SOWM++</a>, but SCBWM is written in XCB (X C Binding). XCB is more lightweight than the Xlib default; there is not too much difference, is the same code but written in XCB <a href="https://github.com/esnokum-dacom/sbcwm"><img src="https://github.com/esnokum-dacom/sbcwm/blob/main/sbcwm.png" width="59%" align="right"></a></p>
    </div>
</div> 

```
ps -eo args,size,vsize,rss | grep -E 'sowm|sbcwm|dwm'
sbcwm                        1832  16236  9440
dwm                          1212  15928  8228
sowm                         1256  13388  7996
```

<div align="center">

| combo                      | action                 |
| -------------------------- | -----------------------|
| `Mouse`                    | focus under cursor     |
| `MOD1` + `Left Mouse`      | move window            |
| `MOD1` + `Right Mouse`     | resize window          |
| `MOD1` + `f`               | maximize toggle        |
| `MOD1` + `c`               | center window          |
| `MOD1` + `Shift` + `c`     | kill window            |
| `MOD1` + `1-6`             | desktop swap           |
| `MOD1` + `Shift` +`1-6`    | send window to desktop |
| `MOD1` + `TAB` (*alt-tab*) | focus cycle            |
| `MOD1` + `Shift` + `Left`  | Move the canvas to the Left |
| `MOD1` + `Shift` + `Right` | Move the canvas to the Right |
| `MOD1` + `Shift` + `Up`    | Move the canvas to the Up |
| `MOD1` + `Shift` + `Down`  | Move the canvas to the Down |
| `Mouse wheel` (Press)      | Move the canvas with the mouse position |
| `MOD1` + `b` | Toggle the minimap |

| combo                    | action           | program        |
| ------------------------ | ---------------- | -------------- |
| `MOD4` + `Return`        | terminal         | `st`           |
| `MOD4` + `p`             | dmenu            | `dmenu_run`    |
| `MOD4` + `Shift` + `s`   | scrot            | `scr`          |
| `XF86_AudioLowerVolume`  | volume down      | `amixer`       |
| `XF86_AudioRaiseVolume`  | volume up        | `amixer`       |
| `XF86_AudioMute`         | volume toggle    | `amixer`       |
| `XF86_MonBrightnessUp`   | brightness up    | `bri`          |
| `XF86_MonBrightnessDown` | brightness down  | `bri`          |

</div>

<div align="center">
  <h1>Install & Config</h1>
  <p align="left">You can install this with</p>
</div>

  ```
  git clone https://github.com/esnokum-dacom/sbcwm
  ```

<div align="center">
  <p align="left">And then make the source</p>
</div>

  ```
  sudo make clean install
  ```

<div align="center">
  <p align="left">To configure the icons of close and maximize </p>
</div>

```c
/* config.def.h */
const FcChar8 *close_sym = (FcChar8 *)"X";
const FcChar8 *max_sym = (FcChar8 *)"O";
```

<div align="center">
  <p align="left">If you don't want a border or titlebar, or both, you can disable them in config.def.h with these options</p>
</div>

```c
/* config.def.h */
#define TITLEBAR 1

#define BORDER	    1
#define BORDER_W    3

#define UI_HUD 1

#define CORDS 1
```
<div align="center">
  <h1>Dependencies</h1>
</div>

- Xlib (obviously)
- xcb
- Xinerama
- Xft
- xcb-randr
- xcb-shape
- xcb-icccm
- xcb-keysyms 
- xcb-util

Thank you so much
