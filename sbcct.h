
/* Sowm C Binding Config Type*/

#pragma once

#include <X11/X.h>
#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>

#include "sbcwm.h"

struct key {
    unsigned int  mod;
    xcb_keysym_t  keysym;
    void        (*function)(const Arg arg);
    Arg     arg;
};

typedef struct {
    char *defaultsh;
    char *fonts;
    char *fontb;

    uint32_t pan_step;
    uint8_t titlebar;
    uint8_t ui;

    uint8_t xr_colors;

    uint8_t border;
    uint16_t border_width;

    struct key *keys;
    int   nkeys;
} Config;

extern Config *cfg;

Config *config_load(const char *path);
void config_free(Config *cfg);
