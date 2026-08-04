
/* Sowm C Binding Lua config */

#include <X11/X.h>
#include <X11/Xlib.h>
#include <lua5.3/lua.h>
#include <lua5.3/lualib.h>
#include <lua5.3/lauxlib.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "sbcct.h"

typedef struct { const char *name; void (*fn)(const Arg); } FuncEntry;
typedef struct { const char *name; unsigned int mask; } ModEntry;

static void *dupstr(const char *s);
static void (*func_from_name(const char *n))(const Arg);
static unsigned int mod_from_name(const char *n);
static void build_key(lua_State *L, int idx, struct key *k);
void config_free(Config *cfg);

static void *dupstr(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;

    char *r = malloc(n);

    memcpy(r, s, n);

    return r;
}

static FuncEntry funcs[] = {
    {"win_kill", win_kill}, {"win_center", win_center}, {"win_fs", win_fs},
    {"run", run}, {"quit", quit}, {"canvas_pan_key", canvas_pan_key},
    {"canvas_reset", canvas_reset}, {"move_nextmon", move_nextmon},
    {"ws_focusnext", ws_focusnext}, {"toggle_minimap", toggle_minimap},
    {"reload_config", reload_config},
};

static ModEntry mods[] = {
    {"super", Mod4Mask}, {"shift", ShiftMask},
    {"ctrl", ControlMask}, {"alt", Mod1Mask},
};

static void (*func_from_name(const char *n))(const Arg) {
    for (size_t i = 0; i < sizeof(funcs)/sizeof(*funcs); i++)
        if (n && !strcmp(funcs[i].name, n)) return funcs[i].fn;
    return NULL;
}

static unsigned int mod_from_name(const char *n) {
    for (size_t i = 0; i < sizeof(mods)/sizeof(*mods); i++)
        if (n && !strcmp(mods[i].name, n)) return mods[i].mask;
    return 0;
}

static void build_key(lua_State *L, int idx, struct key *k) {
    memset(k, 0, sizeof(*k));

    unsigned int mod = 0;
    lua_getfield(L, idx, "mod");
    if (lua_istable(L, -1)) {
        int n = lua_rawlen(L, -1);
        for (int i = 1; i <= n; i++) {
            lua_rawgeti(L, -1, i);
            mod |= mod_from_name(lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    k->mod = mod;

    lua_getfield(L, idx, "key");
    const char *keystr = lua_tostring(L, -1);
    k->keysym = keystr ? XStringToKeysym(keystr) : NoSymbol;
    lua_pop(L, 1);

    lua_getfield(L, idx, "func");
    k->function = func_from_name(lua_tostring(L, -1));
    if (!k->function)
	fprintf(stderr, "sbcwm: unknown func '%s' in config.lua\n", lua_tostring(L, -1));
    lua_pop(L, 1);

    Arg a = {0};
    lua_getfield(L, idx, "arg");
    if (lua_istable(L, -1)) {
        int n = lua_rawlen(L, -1);
        const char **argv = malloc(sizeof(char*) * (n + 1));
        for (int i = 1; i <= n; i++) {
            lua_rawgeti(L, -1, i);
            argv[i-1] = dupstr(lua_tostring(L, -1));
            lua_pop(L, 1);
        }
        argv[n] = NULL;
        Arg tmp = { .com = argv };
        memcpy(&a, &tmp, sizeof(Arg));
    } else if (lua_isnumber(L, -1)) {
        double d = lua_tonumber(L, -1);
        Arg tmp = { .i = (int)d, .f = (float)d };
        memcpy(&a, &tmp, sizeof(Arg));
    }
    lua_pop(L, 1);
    memcpy(&k->arg, &a, sizeof(Arg));
}

static int opt_int(lua_State *L, const char *key, int def) {
    lua_getfield(L, -1, key);
    int v = lua_isnumber(L, -1) ? (int)lua_tonumber(L, -1) : def;
    lua_pop(L, 1);
    return v;
}

Config *config_load(const char *path) {
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);

    if (luaL_dofile(L, path) != LUA_OK) {
        fprintf(stderr, "Sbcwm - %s\n", lua_tostring(L, -1));
        lua_close(L);
        return NULL;
    }

    Config *cfg = malloc(sizeof(Config));

    cfg->pan_step     = 120;
    cfg->titlebar     = 0;
    cfg->ui           = 1;
    cfg->xr_colors    = 1;
    cfg->border       = 1;
    cfg->border_width = 1;
    cfg->defaultsh    = dupstr("/bin/sh");
    cfg->fonts        = dupstr("Terminus:style=Regular:pixelsize=16:antialias=false");
    cfg->fontb        = dupstr("FiraMonoNerdFont:style=Regular:pixelsize=20:antialias=false");

    lua_getglobal(L, "opts");
    if (lua_istable(L, -1)) {
        cfg->pan_step     = (uint32_t)opt_int(L, "pan_step",     120);
        cfg->titlebar     = (uint8_t)opt_int(L,  "titlebar",     0);
        cfg->ui           = (uint8_t)opt_int(L,  "ui",           1);
        cfg->xr_colors    = (uint8_t)opt_int(L,  "xr_colors",    1);
        cfg->border       = (uint8_t)opt_int(L,  "border",       1);
        cfg->border_width = (uint16_t)opt_int(L, "border_width", 1);
    }
    lua_pop(L, 1);

    lua_getglobal(L, "defaultsh");
    if (lua_isstring(L, -1)) { free(cfg->defaultsh); cfg->defaultsh = dupstr(lua_tostring(L, -1)); }
    lua_pop(L, 1);

    lua_getglobal(L, "fonts");
    if (lua_isstring(L, -1)) { free(cfg->fonts); cfg->fonts = dupstr(lua_tostring(L, -1)); }
    lua_pop(L, 1);

    lua_getglobal(L, "fontb");
    if (lua_isstring(L, -1)) { free(cfg->fontb); cfg->fontb = dupstr(lua_tostring(L, -1)); }
    lua_pop(L, 1);

    lua_getglobal(L, "keys");
    if (lua_istable(L, -1)) {
        int n = lua_rawlen(L, -1);
        cfg->keys = malloc(sizeof(struct key) * n);
        cfg->nkeys = n;
        for (int i = 1; i <= n; i++) {
            lua_rawgeti(L, -1, i);
            build_key(L, lua_gettop(L), &cfg->keys[i - 1]);
            lua_pop(L, 1);
        }
    } else {
        cfg->keys = NULL;
        cfg->nkeys = 0;
    }
    lua_pop(L, 1);

    lua_close(L);
    return cfg;
}

void config_free(Config *cfg) {
    if (!cfg) return;
    free(cfg->defaultsh);
    free(cfg->fonts);
    free(cfg->fontb);
    for (int i = 0; i < cfg->nkeys; i++) {
        if (cfg->keys[i].arg.com) {
            for (int j = 0; cfg->keys[i].arg.com[j]; j++)
                free((void *)cfg->keys[i].arg.com[j]);
            free((void *)cfg->keys[i].arg.com);
        }
    }
    free(cfg->keys);
    free(cfg);
}
