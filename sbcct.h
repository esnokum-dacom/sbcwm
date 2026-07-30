
/* Sowm C Binding Config Type*/

#pragma once

#include <X11/X.h>
#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>

#include "sbcwm.h"

typedef struct Bind Bind;

struct Bind {
    KeySym key;
    const char *label;
    const char *cmd;
    int input;
}; 

struct key {
    unsigned int  mod;
    xcb_keysym_t  keysym;
    void        (*function)(const Arg arg);
    Arg     arg;
};

typedef struct {
    char *defaultsh;
    struct key *keys;
    int   nkeys;
} Config;

extern Config *cfg;

Config *config_load(const char *path);
void config_free(Config *cfg);
