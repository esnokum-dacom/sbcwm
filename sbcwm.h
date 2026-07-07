#pragma once
#include <xcb/xcb.h>
#include <xcb/randr.h>
#include <X11/Xft/Xft.h>
#include <signal.h>
#include <stdlib.h>
#include <xcb/xproto.h>

#define MAX_MONITORS 8
#define TITLEBAR_HEIGHT 25
#define SPAWN_SEARCH_STEP 30
#define SPAWN_SEARCH_MAX  40

#define win (client *t = 0, *c = list; c && t != list->prev; t = c, c = c->next)

#define canvas_to_screen(val, pan) (int)(((val) - (pan)))
#define screen_to_canvas(val, pan) ((val) / (pan))

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define CLAMP(v, lo, hi) (MAX((lo), MIN((v), (hi))))

#define mod_clean(mask) \
  (mask & ~(numlock | XCB_MOD_MASK_LOCK) & \
   (XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_CONTROL | XCB_MOD_MASK_1 | XCB_MOD_MASK_2 | \
    XCB_MOD_MASK_3 | XCB_MOD_MASK_4 | XCB_MOD_MASK_5))

typedef struct {
  const char **com;
  const int    i;
  const float  f;
  const xcb_window_t w;
} Arg;

struct key {
    unsigned int  mod;
    xcb_keysym_t  keysym;
    void        (*function)(const Arg arg);
    const Arg     arg;
};

typedef struct {
    xcb_window_t border;
    xcb_window_t titlebar;
} decorations;

typedef struct client {
    struct client *next, *prev;
    xcb_window_t w;
    xcb_window_t decs;
    decorations dec;
    int mon;
    int f;
    int wx, wy;
    unsigned int ww, wh;
    int x, y;
    int width, height;
    int oldx, oldy, oldwidth, oldheight;
    int basew, baseh, incw, inch, maxw, maxh, minw, minh;
    float mina, maxa;
    float cx, cy;
} client;

typedef struct {
    float pan_x[MAX_MONITORS];
    float pan_y[MAX_MONITORS];
} canvas_state;

typedef struct {
    int x, y, w, h;
} MonitorInfo;

typedef struct {
    int x, y;
    int tx, ty;
    int active;
} MinimapState;

typedef struct {
    int w, h;
    int x, y;
} ButtonTb;

typedef struct {
    unsigned long cs[16];
    unsigned long background;
    unsigned long foreground;
} ColorScheme;

void button_press(xcb_button_press_event_t *e);
void button_release(xcb_button_release_event_t *e);
void configure_request(xcb_configure_request_event_t *e);
void input_grab(xcb_window_t root);
void key_press(xcb_key_press_event_t *e);
void notify_property(xcb_property_notify_event_t *e);
void notify_unmap(xcb_unmap_notify_event_t *e);
void map_request(xcb_map_request_event_t *e);
void expose_event(xcb_expose_event_t *e);
void mapping_notify(xcb_mapping_notify_event_t *e);
void notify_destroy(xcb_destroy_notify_event_t *e);
void notify_enter(xcb_enter_notify_event_t *e);
void notify_motion(xcb_motion_notify_event_t *e);
void notify_screen_change(xcb_randr_screen_change_notify_event_t *e);

void run(const Arg arg);
void quit(const Arg arg);
void win_add(xcb_window_t w);
void win_center(const Arg arg);
void win_del(xcb_window_t w);
void win_fs(const Arg arg);
void win_focus(client *c);
void client_restack(client *c);
void titlebar_focus(xcb_window_t w);
void win_kill(const Arg arg);
void win_prev(const Arg arg);
void win_next(const Arg arg);
void win_round_corners(xcb_window_t w, int rad);
void move_nextmon(const Arg arg);
void ws_focusnext(const Arg arg);

void canvas_pan(int mon, float dx, float dy);
void canvas_pan_key(const Arg arg);
void canvas_reset(const Arg arg);
void canvas_apply_all(void);
void apply_mask(xcb_window_t w, int wx, int wy, unsigned int ww, unsigned int wh,
                 int mx, int my, int mw, int mh);
void canvas_focus(client *c);

void hud_update(void);

void minimap_create(void);
void minimap_update(void);
void toggle_minimap(const Arg arg);

void titlebar_update(client *c);
xcb_window_t titlebar_create(client *c);
void titlebar_draw(client *c);
void titlebar_del(client *c);
client *client_from_titlebar(xcb_window_t w);
int is_titlebar(xcb_window_t w);

xcb_window_t border_create(client *c);
void border_draw(client *c);
void border_del(client *c);

unsigned long hex_to_xcolor(const char *hex);
void load_colors(void);
void xcolor_to_xftcolor(unsigned long pixel, XftColor *xft);


xcb_window_t dec_params(client *c);
void decors(client *c, uint8_t tb_, uint8_t bd_);

void client_move(client *c, int x, int y);
void updatesizehints(client *c);
void resizeclient(client *c, int w, int h);
void configure(client *c);
void client_resize(client *c, unsigned int w, unsigned int h);
int applysizehints(client *c, int *w, int *h);
char *copystr(const char *s);
void win_size(xcb_window_t w, int *x, int *y, unsigned int *wd, unsigned int *ht);

void handle_sigusr1(int sig);

void monitors_refresh(void);
int mon_at_ptr(void);
int mon_at_win(xcb_window_t w);
int mon_from_point(int px, int py);
