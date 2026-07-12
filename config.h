#ifndef CONFIG_H
#define CONFIG_H

#include "config.h"
#include "sbcwm.h"
#include <X11/X.h>
#include <X11/XF86keysym.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#define MOD           Mod4Mask
#define PAN_STEP      120
#define ROUND_CORNERS 0

// Title bar
#define TITLEBAR    0

// BORDER

#define BORDER	    1
#define BORDER_W    1

// HUD
#define UI_HUD 1

// Coordinates 
#define CORDS 1

// Wal colors (Xresources)
#define XR_COLORS  1

static const char *fonts[] = { "Terminus:style=Regular:pixelsize=16:antialias=false" };
static const char *fontb[] = { "FiraMonoNerdFont:style=Regular:pixelsize=20:antialias=false" };

const FcChar8 *close_sym = (FcChar8 *)"";
const FcChar8 *max_sym = (FcChar8 *)"󰝣";

static const char *menu[]    = {"dmenu_run", 0};
static const char *mterm[]   = {"st", 0};
static const char *xmenu[]   = {"XMenu", 0};
static const char *xwall[]   = {"xwall", 0};
static const char *scrot[]   = {"sh", "-c",
                                "scrot -s - | xclip -selection clipboard"
                                " -t image/png", 0};
static const char *briup[]   = {"bri", "10", "+", 0};
static const char *bridown[] = {"bri", "10", "-", 0};
static const char *voldown[] = {"amixer", "sset", "Master", "5%-", 0};
static const char *volup[]   = {"amixer", "sset", "Master", "5%+", 0};
static const char *volmute[] = {"amixer", "sset", "Master", "toggle", 0};

static const char *tabmen[] = {"sbtb", 0};
static const char *lazymenu[] = {"lm", 0};

static const char *zoomin[]  = {"sbcompctl", "zoom", "+0.1", 0};
static const char *zoomout[] = {"sbcompctl", "zoom", "-0.1", 0};
static const char *zoomreset[] = {"sbcompctl", "zoom", "1", 0};

static struct key keys[] = {
    { MOD | ShiftMask, XK_c,      win_kill,        {0}             },
    { MOD,             XK_c,      win_center,      {0}             },
    { MOD,             XK_f,      win_fs,          {0}             },
    { Mod1Mask,        XK_Tab,    run,		   {.com = tabmen}   },
    { Mod1Mask|ShiftMask, XK_Tab, run,		   {.com = tabmen}   },

    { MOD,             XK_period, ws_focusnext,    {0}             },
    { MOD|ShiftMask,   XK_period, move_nextmon,    {0}             },
    { MOD,             XK_b,	  toggle_minimap,  {0}		   },

    { MOD|ShiftMask,   XK_Left,   canvas_pan_key,  {.i = 0}        },
    { MOD|ShiftMask,   XK_Right,  canvas_pan_key,  {.i = 1}        },
    { MOD|ShiftMask,   XK_Up,     canvas_pan_key,  {.i = 2}        },
    { MOD|ShiftMask,   XK_Down,   canvas_pan_key,  {.i = 3}        },

    { MOD|ShiftMask,   XK_Home,   canvas_reset,    {0}             },

    { MOD,             XK_p,      run,             {.com = menu}   },
    { MOD,             XK_m,      run,             {.com = xmenu}  },
    { MOD,             XK_w,      run,             {.com = xwall}  },
    { MOD|ShiftMask,   XK_Return, run,             {.com = mterm}  },
    { MOD|ShiftMask,   XK_s,      run,             {.com = scrot}  },

    { MOD,	       XK_space,  run,             {.com = lazymenu}  },

    { MOD|ShiftMask,   XK_q,      quit,            {0}		   },

    { MOD,	       XK_F2,	  run,             {.com = zoomout}},
    { MOD,	       XK_F1,	  run,             {.com = zoomin} },
    { MOD,	       XK_r,	  run,             {.com = zoomreset}},

    { 0, XF86XK_AudioLowerVolume, run,             {.com = voldown} },
    { 0, XF86XK_AudioRaiseVolume, run,             {.com = volup}   },
    { 0, XF86XK_AudioMute,        run,             {.com = volmute} },
    { 0, XF86XK_MonBrightnessUp,  run,             {.com = briup}   },
    { 0, XF86XK_MonBrightnessDown,run,             {.com = bridown} },
};

#endif
