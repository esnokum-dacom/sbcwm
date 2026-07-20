#define _POSIX_C_SOURCE 199309L
#include <xcb/xcb.h>
#include <xcb/randr.h>
#include <xcb/shape.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xcb_keysyms.h>
#include <xcb/xproto.h>
#include <xcb/xcb_aux.h>
#include <X11/Xlib.h>
#include <X11/Xlib-xcb.h>
#include <X11/Xft/Xft.h>
#include <X11/keysym.h>
#include <X11/extensions/Xinerama.h>
#include <time.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <fcntl.h>

#include "sbcwm.h"
#include "config.h"

#define WBORDER (BORDER ? BORDER_W : 0)
#define TB_CONTENT_H (TITLEBAR_HEIGHT - 2 * WBORDER)

static client *list = NULL;
static client *cur  = NULL;

static XftFont  *mm_font = NULL;
static XftDraw  *mm_draw = NULL;
static XftColor  mm_color;
static int mm_inited = 0;

static XftFont *title_font  = NULL;
static XftFont *button_font = NULL;

static canvas_state canvas = { .pan_x = {0}, .pan_y = {0} };

static int sw, sh;

static int   pan_active   = 0;
static int   pan_start_x  = 0;
static int   pan_start_y  = 0;
static float pan_origin_x = 0;
static float pan_origin_y = 0;
static int   pan_mon      = 0;

static int running = 1;

static unsigned int numlock = 0;
char *client_get_title(xcb_window_t w);

static Display          *dpy;
static xcb_connection_t *conn;
static int                scrno;
static xcb_screen_t      *screen;
static Visual            *visual;
static Colormap           cmap;
static int                depth;
static xcb_key_symbols_t *keysyms;

static xcb_window_t root;
static xcb_window_t minimap_win[MAX_MONITORS] = {0};
static xcb_pixmap_t minimap_pix[MAX_MONITORS] = {0};
static xcb_gcontext_t minimap_gc = 0;

static volatile sig_atomic_t reload_colors = 0;
static ColorScheme cols;

static int randr_event_base = 0;

static MonitorInfo mons[MAX_MONITORS];
static int         n_mons = 0;

static xcb_atom_t canvas_atom_pan_x, canvas_atom_pan_y;
static xcb_atom_t sbcwm_atom_monitor;
static xcb_atom_t net_supported, net_wm_window_type, net_wm_window_type_dock,
                  net_wm_strut, net_wm_strut_partial, net_current_desktop,
                  net_supporting_wm_check, net_wm_name, net_wm_visible_name,
                  net_active_window, ewmh_utf8_string, wm_delete_window,
                  wm_protocols, wm_normal_hints_atom, net_client_list_stacking;

static int strut[4] = {0, 0, 0, 0};
static long mm_last_update_ms = 0;

int             minimap = 1;
static int      minimap_px[MAX_MONITORS] = {0};
static int      minimap_py[MAX_MONITORS] = {0};

static xcb_window_t drag_subwindow = 0;
static uint16_t     drag_button    = 0;
static int16_t      drag_root_x    = 0;
static int16_t      drag_root_y    = 0;

static long now_ms(void) {
    struct timespec t_s;
    clock_gettime(CLOCK_MONOTONIC, &t_s);
    return t_s.tv_sec * 1000L + t_s.tv_nsec / 1000000L;
}

char *copystr(const char *s) {
    size_t len = strlen(s) + 1;
    char *p = malloc(len);
    if (p) memcpy(p, s, len);
    return p;
}

void handle_sigusr1(int sig) { (void)sig; reload_colors = 1; }

static xcb_atom_t get_atom(const char *name) {
    xcb_intern_atom_cookie_t ck = xcb_intern_atom(conn, 0, (uint16_t)strlen(name), name);
    xcb_intern_atom_reply_t *r = xcb_intern_atom_reply(conn, ck, NULL);
    xcb_atom_t a = r ? r->atom : XCB_ATOM_NONE;
    free(r);
    return a;
}

void win_size(xcb_window_t w, int *x, int *y, unsigned int *wd, unsigned int *ht) {
    if (x) *x = 0;
    if (y) *y = 0;
    if (wd) *wd = 0;
    if (ht) *ht = 0;
    if (!w) return;

    xcb_get_geometry_cookie_t ck = xcb_get_geometry(conn, w);
    xcb_get_geometry_reply_t *r = xcb_get_geometry_reply(conn, ck, NULL);
    if (!r) return;
    if (x)  *x  = r->x;
    if (y)  *y  = r->y;
    if (wd) *wd = r->width;
    if (ht) *ht = r->height;
    free(r);
}

void monitors_refresh(void) {
    xcb_randr_get_monitors_cookie_t ck = xcb_randr_get_monitors(conn, root, 1);
    xcb_randr_get_monitors_reply_t *r = xcb_randr_get_monitors_reply(conn, ck, NULL);
    n_mons = 0;
    if (r) {
        xcb_randr_monitor_info_iterator_t it = xcb_randr_get_monitors_monitors_iterator(r);
        for (; it.rem && n_mons < MAX_MONITORS; xcb_randr_monitor_info_next(&it)) {
            xcb_randr_monitor_info_t *m = it.data;
            mons[n_mons].x = m->x;
            mons[n_mons].y = m->y;
            mons[n_mons].w = m->width;
            mons[n_mons].h = m->height;
            n_mons++;
        }
        free(r);
    }
    if (n_mons == 0) {
        mons[0].x = 0; mons[0].y = 0; mons[0].w = sw; mons[0].h = sh;
        n_mons = 1;
    }
}

int mon_from_point(int px, int py) {
    for (int i = 0; i < n_mons; i++)
        if (px >= mons[i].x && px < mons[i].x + mons[i].w &&
            py >= mons[i].y && py < mons[i].y + mons[i].h)
            return i;
    return 0;
}

static void set_client_monitor(client *c, int mon) {
    c->mon = mon;
    uint32_t m = (uint32_t)mon;
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, c->w, sbcwm_atom_monitor,
                         XCB_ATOM_CARDINAL, 32, 1, &m);
}

int mon_at_ptr(void) {
    xcb_query_pointer_cookie_t ck = xcb_query_pointer(conn, root);
    xcb_query_pointer_reply_t *r = xcb_query_pointer_reply(conn, ck, NULL);
    int mon = 0;
    if (r) { mon = mon_from_point(r->root_x, r->root_y); free(r); }
    return mon;
}

int mon_at_win(xcb_window_t w) {
    int wx2, wy2; unsigned int ww2, wh2;
    win_size(w, &wx2, &wy2, &ww2, &wh2);
    return mon_from_point(wx2 + (int)ww2 / 2, wy2 + (int)wh2 / 2);
}

unsigned long hex_to_xcolor(const char *hex) {
    XColor color;
    XParseColor(dpy, cmap, hex, &color);
    XAllocColor(dpy, cmap, &color);
    return color.pixel;
}

void load_colors(void) {
    unsigned long fg = hex_to_xcolor("#ffffff");
    unsigned long bg = hex_to_xcolor("#151515"); cols.background = bg;
    cols.foreground = fg;
    for (int i = 0; i < 16; i++)
        cols.cs[i] = (i == 0) ? bg : fg;

    char path[256];
    const char *home = getenv("HOME");
    if (!home) return;
    snprintf(path, sizeof(path), "%s/.cache/wal/colors", home);
    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[16];
    int i = 0;
    while (fgets(line, sizeof(line), f) && i < 16) {
        line[strcspn(line, "\n")] = 0;
        cols.cs[i++] = hex_to_xcolor(line);
    }
    fclose(f);

    cols.background = cols.cs[0];
    cols.foreground = cols.cs[15];
}

void xcolor_to_xftcolor(unsigned long pixel, XftColor *xft) {
    XColor xc = {0};
    xc.pixel = pixel;
    XQueryColor(dpy, cmap, &xc);
    XRenderColor rc = { .red = xc.red, .green = xc.green, .blue = xc.blue, .alpha = 0xffff };
    XftColorAllocValue(dpy, visual, cmap, &rc, xft);
}

static void minimap_init(void) {
    if (mm_inited) return;
    XRenderColor xr = { .red = 0, .green = 0, .blue = 0, .alpha = 0xFFFF };
    XftColorAllocValue(dpy, visual, cmap, &xr, &mm_color);
    mm_font = XftFontOpenName(dpy, scrno, *fonts);
    mm_inited = 1;
}

void minimap_create(void) {
    monitors_refresh();

    int x0 = 1<<30, y0 = 1<<30, x1 = 0, y1 = 0;
    for (int i = 0; i < n_mons; i++) {
        x0 = MIN(x0, mons[i].x);
        y0 = MIN(y0, mons[i].y);
        x1 = MAX(x1, mons[i].x + mons[i].w);
        y1 = MAX(y1, mons[i].y + mons[i].h);
    }

    float layout_w = (float)(x1 - x0);
    float layout_h = (float)(y1 - y0);
    float box_w = 220, box_h = 220;
    float scale = MIN(box_w / layout_w, box_h / layout_h);

    int gap = 5;
    int origin_x = 10, origin_y = 10;

    minimap_gc = xcb_generate_id(conn);
    xcb_create_gc(conn, minimap_gc, root, 0, NULL);

    for (int i = 0; i < n_mons; i++) {
        int rank_x = 0, rank_y = 0;
        for (int j = 0; j < n_mons; j++) {
            if (mons[j].x < mons[i].x) rank_x++;
            if (mons[j].y < mons[i].y) rank_y++;
        }

        int px = origin_x + (int)((mons[i].x - x0) * scale) + rank_x * gap;
        int py = origin_y + (int)((mons[i].y - y0) * scale) + rank_y * gap;
        minimap_px[i] = px;
        minimap_py[i] = py;
        int pw = MAX(40, (int)(mons[i].w * scale));
        int ph = MAX(30, (int)(mons[i].h * scale));

        minimap_win[i] = xcb_generate_id(conn);
        uint32_t mask = XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL |
                         XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK;
        uint32_t values[] = { 0x111111, 0x444444, 1, XCB_EVENT_MASK_EXPOSURE };
        xcb_create_window(conn, XCB_COPY_FROM_PARENT, minimap_win[i], root,
                           (int16_t)px, (int16_t)py, (uint16_t)pw, (uint16_t)ph, 0,
                           XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual,
                           mask, values);

        minimap_pix[i] = xcb_generate_id(conn);
        xcb_create_pixmap(conn, depth, minimap_pix[i], root, (uint16_t)pw, (uint16_t)ph);

        xcb_map_window(conn, minimap_win[i]);
        uint32_t stack = XCB_STACK_MODE_ABOVE;
        xcb_configure_window(conn, minimap_win[i], XCB_CONFIG_WINDOW_STACK_MODE, &stack);
    }
    xcb_flush(conn);
}

static void minimap_draw_one(xcb_window_t panel, int mon, int mon_w, int mon_h, int mon_x, int mon_y) {
    if (!panel) return;
    if (mon_w <= 0 || mon_h <= 0) return;

    minimap_init();

    unsigned int mw, mh;
    int mxw, myw;
    win_size(panel, &mxw, &myw, &mw, &mh);

    xcb_pixmap_t buf = minimap_pix[mon];
    if (!buf) return;

    if (mm_draw) XftDrawDestroy(mm_draw);
    mm_draw = XftDrawCreate(dpy, buf, visual, cmap);

    uint32_t fg = 0x111111;
    xcb_change_gc(conn, minimap_gc, XCB_GC_FOREGROUND, &fg);
    xcb_rectangle_t full = { 0, 0, (uint16_t)mw, (uint16_t)mh };
    xcb_poly_fill_rectangle(conn, buf, minimap_gc, 1, &full);

    float minx =  1e9f, miny =  1e9f;
    float maxx = -1e9f, maxy = -1e9f;
    int   any  = 0;

    for win {
        if (c->mon != mon) continue;
        any  = 1;
        minx = MIN(minx, c->cx);
        miny = MIN(miny, c->cy);
        maxx = MAX(maxx, c->cx + c->width);
        maxy = MAX(maxy, c->cy + c->height);
    }

    float vx0 = mon_x + canvas.pan_x[mon];
    float vy0 = mon_y + canvas.pan_y[mon];
    float vx1 = vx0 + mon_w;
    float vy1 = vy0 + mon_h;

    if (!any) { minx = vx0; miny = vy0; maxx = vx1; maxy = vy1; }
    minx = MIN(minx, vx0); miny = MIN(miny, vy0);
    maxx = MAX(maxx, vx1); maxy = MAX(maxy, vy1);

    float bw = MAX(maxx - minx, 1.0f);
    float bh = MAX(maxy - miny, 1.0f);
    float scale = MIN(((float)mw - 8) / bw, ((float)mh - 8) / bh);

#define MM_X(v) (int)(4 + ((v) - minx) * scale)
#define MM_Y(v) (int)(4 + ((v) - miny) * scale)

    uint32_t frame_fg = 0x555555;
    xcb_change_gc(conn, minimap_gc, XCB_GC_FOREGROUND, &frame_fg);
    xcb_rectangle_t frame = {
        (int16_t)MM_X(vx0), (int16_t)MM_Y(vy0),
        (uint16_t)MAX(1, (int)((vx1 - vx0) * scale)),
        (uint16_t)MAX(1, (int)((vy1 - vy0) * scale)),
    };
    xcb_poly_rectangle(conn, buf, minimap_gc, 1, &frame);

    for win {
        if (c->mon != mon) continue;

        int rx = MM_X(c->cx);
        int ry = MM_Y(c->cy);
        int rw = MAX(2, (int)(c->width  * scale));
        int rh = MAX(2, (int)(c->height * scale));

        uint32_t box_fg = (c == cur) ? 0xffffff : 0x505050;
        xcb_change_gc(conn, minimap_gc, XCB_GC_FOREGROUND, &box_fg);
        xcb_rectangle_t box = { (int16_t)rx, (int16_t)ry, (uint16_t)rw, (uint16_t)rh };
        xcb_poly_fill_rectangle(conn, buf, minimap_gc, 1, &box);

        char *title = client_get_title(c->w);
        if (!title) title = copystr("");

        XGlyphInfo ext;
        XftTextExtentsUtf8(dpy, mm_font, (FcChar8 *)title, (int)strlen(title), &ext);
        int bx = rx + rw / 2, by = ry + ext.height + 5;
        int x = bx - ext.xOff / 2;

        XftDrawStringUtf8(mm_draw, &mm_color, mm_font, x, by, (FcChar8 *)title, (int)strlen(title));
        free(title);
    }

#undef MM_X
#undef MM_Y
    xcb_copy_area(conn, buf, panel, minimap_gc, 0, 0, 0, 0, (uint16_t)mw, (uint16_t)mh);
    uint32_t stack = XCB_STACK_MODE_ABOVE;
    xcb_configure_window(conn, panel, XCB_CONFIG_WINDOW_STACK_MODE, &stack);
}

void minimap_update(void) {
    long t = now_ms();
    if (t - mm_last_update_ms < 33) return;
    mm_last_update_ms = t;

    for (int i = 0; i < n_mons; i++)
        minimap_draw_one(minimap_win[i], i, mons[i].w, mons[i].h, mons[i].x, mons[i].y);
    xcb_flush(conn);
}

void toggle_minimap(const Arg arg) {
    (void)arg;
    minimap = !minimap;
    for (int i = 0; i < MAX_MONITORS; i++) {
        if (!minimap_win[i]) continue;
        if (minimap) xcb_map_window(conn, minimap_win[i]);
        else         xcb_unmap_window(conn, minimap_win[i]);
    }
    minimap_update();
    xcb_flush(conn);
}

static void always_ot(void) {
    if (!minimap) return;
    for (int i = 0; i < MAX_MONITORS; i++)
        if (minimap_win[i]) {
            uint32_t stack = XCB_STACK_MODE_ABOVE;
            xcb_configure_window(conn, minimap_win[i], XCB_CONFIG_WINDOW_STACK_MODE, &stack);
        }
}

xcb_window_t titlebar_create(client *c) {
    int x, y;
    unsigned int w, h;
    win_size(c->w, &x, &y, &w, &h);

    xcb_window_t titlebar = xcb_generate_id(conn);
    uint32_t mask = XCB_CW_BACK_PIXMAP | XCB_CW_BORDER_PIXEL | XCB_CW_EVENT_MASK;
    uint32_t values[] = {
        XCB_BACK_PIXMAP_NONE, 0,
        XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_BUTTON_PRESS |
        XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION,
    };
    xcb_create_window(conn, XCB_COPY_FROM_PARENT, titlebar, root,
                       (int16_t)x, (int16_t)(y - TITLEBAR_HEIGHT), (uint16_t)w, TITLEBAR_HEIGHT, (uint16_t)WBORDER,
                       XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, mask, values);
    xcb_map_window(conn, titlebar);
    return titlebar;
}

void titlebar_draw(client *c) {
    static ButtonTb btc;

    if (!c || !c->titlebar) return;

    unsigned int tw, th;
    int tx, ty;
    win_size(c->titlebar, &tx, &ty, &tw, &th);

    xcb_pixmap_t tpix = xcb_generate_id(conn);
    xcb_create_pixmap(conn, depth, tpix, root, (uint16_t)tw, (uint16_t)th);
    xcb_gcontext_t tgc = xcb_generate_id(conn);
    xcb_create_gc(conn, tgc, tpix, 0, NULL);

    load_colors();
    signal(SIGUSR1, handle_sigusr1);

    XftColor color;
    xcolor_to_xftcolor(cols.foreground, &color);

    unsigned long bg;

    if (XR_COLORS) { bg = (c == cur) ? cols.cs[2] : 0x000000; } else { bg = (c == cur) ? cols.cs[2] : 0x000000; }

    uint32_t fgc = (uint32_t)bg;
    xcb_change_gc(conn, tgc, XCB_GC_FOREGROUND, &fgc);
    xcb_rectangle_t full = { 0, 0, (uint16_t)tw, (uint16_t)th };
    xcb_poly_fill_rectangle(conn, tpix, tgc, 1, &full);

    if (!button_font) button_font = XftFontOpenName(dpy, scrno, *fontb);
    if (!title_font)  title_font  = XftFontOpenName(dpy, scrno, *fontb);

    if (button_font) {
        XftDraw  *btn_draw = XftDrawCreate(dpy, tpix, visual, cmap);
        XftColor  btn_color;
        XRenderColor btn_xr = { .red = 65535, .green = 65535, .blue = 65535, .alpha = 65535 };
        XftColorAllocValue(dpy, visual, cmap, &btn_xr, &btn_color);

        btc.w = 22; btc.h = (int)th;
        btc.x = (int)tw - btc.w - 4;
        btc.y = 0;
        xcb_rectangle_t bclose = { (int16_t)btc.x, (int16_t)btc.y, (uint16_t)btc.w, (uint16_t)btc.h };
        xcb_poly_fill_rectangle(conn, tpix, tgc, 1, &bclose);
        XGlyphInfo ext;
        XftTextExtentsUtf8(dpy, button_font, close_sym, (int)strlen((char *)close_sym), &ext);
        XftDrawStringUtf8(btn_draw, &btn_color, button_font,
            btc.x + (btc.w - ext.xOff) / 2,
            (int)((btc.h + button_font->ascent) / 2 - 2),
            close_sym, (int)strlen((char *)close_sym));

        int btn_w = 22;
        int btn_f = btc.x - btn_w - 2;
        xcb_rectangle_t bfs = { (int16_t)btn_f, 0, (uint16_t)btn_w, (uint16_t)th };
        xcb_poly_fill_rectangle(conn, tpix, tgc, 1, &bfs);
        XftTextExtentsUtf8(dpy, button_font, max_sym, (int)strlen((char *)max_sym), &ext);
        XftDrawStringUtf8(btn_draw, &btn_color, button_font,
            btn_f + (btn_w - ext.xOff) / 2,
            (int)((btc.h + button_font->ascent) / 2 - 2),
            max_sym, (int)strlen((char *)max_sym));

        XftColorFree(dpy, visual, cmap, &btn_color);
        XftDrawDestroy(btn_draw);
    }

    char buf[256];
    char *win_title = client_get_title(c->w);
    snprintf(buf, sizeof(buf), "%s", win_title ? win_title : "");
    free(win_title);

    XftDraw *draw = XftDrawCreate(dpy, tpix, visual, cmap);
    if (title_font) {
        XRenderColor xr = { .red = 65535, .green = 65535, .blue = 65535, .alpha = 65535 };
        XftColor tcolor;
        XftColorAllocValue(dpy, visual, cmap, &xr, &tcolor);
        XftDrawStringUtf8(draw, &tcolor, title_font, 10, (int)(th / 2) + (title_font->ascent / 2),
            (FcChar8 *)buf, (int)strlen(buf));
        XftColorFree(dpy, visual, cmap, &tcolor);
    }
    XftDrawDestroy(draw);

    xcb_copy_area(conn, tpix, c->titlebar, tgc, 0, 0, 0, 0, (uint16_t)tw, (uint16_t)th);
    xcb_free_pixmap(conn, tpix);
    xcb_free_gc(conn, tgc);
    XftColorFree(dpy, visual, cmap, &color);
    xcb_flush(conn);
}

void titlebar_del(client *c) {
    if (!c) return;
    if (c->titlebar) {
        xcb_destroy_window(conn, c->titlebar);
        c->titlebar = 0;
    }
}

client *client_from_titlebar(xcb_window_t w) {
    for win
        if (c->titlebar == w) return c;
    return NULL;
}

int is_titlebar(xcb_window_t w) {
    return client_from_titlebar(w) != NULL;
}

void apply_mask(xcb_window_t w, int wx, int wy, unsigned int ww, unsigned int wh, int bw,
                 int mx, int my, int mw, int mh) {
    if (!w) return;

    int ox0 = wx,                   oy0 = wy;
    int ox1 = wx + (int)ww + 2*bw,  oy1 = wy + (int)wh + 2*bw;

    int clip_l = ox0 < mx;
    int clip_r = ox1 > mx + mw;
    int clip_t = oy0 < my;
    int clip_b = oy1 > my + mh;

    if (!clip_l && !clip_r && !clip_t && !clip_b) {
        xcb_shape_mask(conn, XCB_SHAPE_SO_SET, XCB_SHAPE_SK_BOUNDING, w, 0, 0, XCB_PIXMAP_NONE);
        return;
    }

    int left   = clip_l ? mx        : ox0;
    int right  = clip_r ? mx + mw   : ox1;
    int top    = clip_t ? my        : oy0;
    int bottom = clip_b ? my + mh   : oy1;

    int rw = right  - left;
    int rh = bottom - top;

    if (rw > 0 && rh > 0) {
        xcb_rectangle_t rect = {
            .x = (int16_t)(left - wx - bw), .y = (int16_t)(top - wy - bw),
            .width = (uint16_t)rw, .height = (uint16_t)rh,
        };
        xcb_shape_rectangles(conn, XCB_SHAPE_SO_SET, XCB_SHAPE_SK_BOUNDING,
                              XCB_CLIP_ORDERING_UNSORTED, w, 0, 0, 1, &rect);
    } else {
        xcb_shape_mask(conn, XCB_SHAPE_SO_SET, XCB_SHAPE_SK_BOUNDING, w, 0, 0, XCB_PIXMAP_NONE);
    }
}

void client_move(client *c, int x, int y) {
    if (!c) return;

    c->x = x;
    c->y = y;

    uint32_t values[2] = { (uint32_t)x, (uint32_t)y };
    xcb_configure_window(conn, c->w, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, values);

    uint32_t vstm[2] = { cur->w, XCB_STACK_MODE_ABOVE };
    xcb_configure_window(conn, cur->w, XCB_CONFIG_WINDOW_SIBLING | XCB_CONFIG_WINDOW_STACK_MODE, vstm);

    uint32_t vst[2] = { c->w, XCB_STACK_MODE_BELOW };
    xcb_configure_window(conn, c->w, XCB_CONFIG_WINDOW_SIBLING | XCB_CONFIG_WINDOW_STACK_MODE, vst);

    if (c->titlebar) {
        uint32_t tv[2] = { (uint32_t)x, (uint32_t)(y - TITLEBAR_HEIGHT) };
        xcb_configure_window(conn, c->titlebar, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, tv);
        titlebar_update(c);

        uint32_t sv[2] = { c->w, XCB_STACK_MODE_ABOVE };
        xcb_configure_window(conn, c->titlebar, XCB_CONFIG_WINDOW_SIBLING | XCB_CONFIG_WINDOW_STACK_MODE, sv);
    }

    if (c->mon < n_mons) {
        int mx = mons[c->mon].x, my = mons[c->mon].y, mw = mons[c->mon].w, mh = mons[c->mon].h;
        apply_mask(c->w, x, y, (unsigned)c->width, (unsigned)c->height, WBORDER, mx, my, mw, mh);
        if (c->titlebar)
            apply_mask(c->titlebar, x, y - TITLEBAR_HEIGHT, (unsigned)c->width, TB_CONTENT_H, WBORDER, mx, my, mw, mh);
    }

    xcb_flush(conn);
}

void update_borders(void) {
    for win {
        uint32_t values[1];
	if (c == cur) {
	    values[0] = cols.cs[2];
	} else {
	    values[0] = cols.cs[0];
	}
        xcb_change_window_attributes(conn, c->w, XCB_CW_BORDER_PIXEL, values);
        if (c->titlebar)
            xcb_change_window_attributes(conn, c->titlebar, XCB_CW_BORDER_PIXEL, values);
    }
    if (cur) {
        xcb_window_t w = cur->w;
        xcb_change_property(conn, XCB_PROP_MODE_REPLACE, root, net_active_window, XCB_ATOM_WINDOW, 32, 1, &w);
    }
    xcb_flush(conn);
    update_client_list_stacking();
}

void update_client_list_stacking(void) {
    xcb_query_tree_cookie_t qc = xcb_query_tree(conn, root);
    xcb_query_tree_reply_t *qr = xcb_query_tree_reply(conn, qc, NULL);
    if (!qr) return;
 
    xcb_window_t *children = xcb_query_tree_children(qr);
    int nchild = xcb_query_tree_children_length(qr);
 
    xcb_window_t *stack = malloc(sizeof(xcb_window_t) * (size_t)nchild);
    if (stack) {
        int n = 0;
        for (int i = 0; i < nchild; i++)
            for win
                if (c->w == children[i]) { stack[n++] = c->w; break; }
 
        xcb_change_property(conn, XCB_PROP_MODE_REPLACE, root, net_client_list_stacking,
                             XCB_ATOM_WINDOW, 32, (uint32_t)n, stack);
        free(stack);
        xcb_flush(conn);
    }
    free(qr);
}

void configure(client *c) {
    xcb_configure_notify_event_t ce = {0};
    ce.response_type = XCB_CONFIGURE_NOTIFY;
    ce.event = c->w;
    ce.window = c->w;
    ce.x = (int16_t)c->x;
    ce.y = (int16_t)c->y;
    ce.width = (uint16_t)c->width;
    ce.height = (uint16_t)c->height;
    ce.border_width = WBORDER;
    ce.above_sibling = XCB_NONE;
    ce.override_redirect = 0;
    xcb_send_event(conn, 0, c->w, XCB_EVENT_MASK_STRUCTURE_NOTIFY, (const char *)&ce);
}

void resizeclient(client *c, int w, int h) {
    c->oldwidth = c->width;
    c->width = w;
    c->oldheight = c->height;
    c->height = h;

    uint32_t wv[2] = { (uint32_t)w, (uint32_t)h };
    xcb_configure_window(conn, c->w, XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, wv);
    configure(c);

    if (c->titlebar) {
        uint32_t tv[2] = { (uint32_t)w, TITLEBAR_HEIGHT };
        xcb_configure_window(conn, c->titlebar, XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, tv);
    }
    if (c->mon < n_mons) {
        int mx = mons[c->mon].x, my = mons[c->mon].y, mw = mons[c->mon].w, mh = mons[c->mon].h;
        apply_mask(c->w, c->x, c->y, (unsigned)c->width, (unsigned)c->height, WBORDER, mx, my, mw, mh);
        if (c->titlebar)
            apply_mask(c->titlebar, c->x, c->y - TITLEBAR_HEIGHT, (unsigned)c->width, TB_CONTENT_H, WBORDER, mx, my, mw, mh);
    }
    xcb_flush(conn);
}

void client_resize(client *c, unsigned int w, unsigned int h) {
    if (!c) return;
    int nw = (int)w, nh = (int)h;
    if (applysizehints(c, &nw, &nh))
        resizeclient(c, nw, nh);
    minimap_update();
    if (c->titlebar) titlebar_update(c);
}

void updatesizehints(client *c) {
    xcb_size_hints_t size = {0};
    xcb_get_property_cookie_t ck = xcb_icccm_get_wm_normal_hints(conn, c->w);
    if (!xcb_icccm_get_wm_normal_hints_reply(conn, ck, &size, NULL))
        size.flags = XCB_ICCCM_SIZE_HINT_P_SIZE;

    if (size.flags & XCB_ICCCM_SIZE_HINT_BASE_SIZE) { c->basew = size.base_width; c->baseh = size.base_height; }
    else if (size.flags & XCB_ICCCM_SIZE_HINT_P_MIN_SIZE) { c->basew = size.min_width; c->baseh = size.min_height; }
    else c->basew = c->baseh = 0;

    if (size.flags & XCB_ICCCM_SIZE_HINT_P_RESIZE_INC) { c->incw = size.width_inc; c->inch = size.height_inc; }
    else c->incw = c->inch = 0;

    if (size.flags & XCB_ICCCM_SIZE_HINT_P_MAX_SIZE) { c->maxw = size.max_width; c->maxh = size.max_height; }
    else c->maxw = c->maxh = 0;

    if (size.flags & XCB_ICCCM_SIZE_HINT_P_MIN_SIZE) { c->minw = size.min_width; c->minh = size.min_height; }
    else if (size.flags & XCB_ICCCM_SIZE_HINT_BASE_SIZE) { c->minw = size.base_width; c->minh = size.base_height; }
    else c->minw = c->minh = 0;

    if (size.flags & XCB_ICCCM_SIZE_HINT_P_ASPECT) {
        c->mina = (float)size.min_aspect_den / size.min_aspect_num;
        c->maxa = (float)size.max_aspect_num / size.max_aspect_den;
    } else c->mina = c->maxa = 0.0f;
}

int applysizehints(client *c, int *w, int *h) {
    int baseismin;

    *w = *w < 1 ? 1 : *w;
    *h = *h < 1 ? 1 : *h;

    baseismin = (c->basew == c->minw) && (c->baseh == c->minh);
    if (!baseismin) { *w -= c->basew; *h -= c->baseh; }

    if (c->mina > 0 && c->maxa > 0) {
        if (c->maxa < (float)*w / *h)
            *w = (int)(*h * c->maxa + 0.5);
        else if (c->mina < (float)*h / *w)
            *h = (int)(*w * c->mina + 0.5);
    }

    if (c->incw) *w -= *w % c->incw;
    if (c->inch) *h -= *h % c->inch;

    *w = MAX(*w + (baseismin ? 0 : c->basew), c->minw > 0 ? c->minw : 1);
    *h = MAX(*h + (baseismin ? 0 : c->baseh), c->minh > 0 ? c->minh : 1);

    if (c->maxw) *w = MIN(*w, c->maxw);
    if (c->maxh) *h = MIN(*h, c->maxh);

    return *w != c->width || *h != c->height;
}

char *client_get_title(xcb_window_t w) {
    xcb_icccm_get_wm_class_reply_t hint;
    if (xcb_icccm_get_wm_class_reply(conn, xcb_icccm_get_wm_class(conn, w), &hint, NULL)) {
        char *name = NULL;
        if (hint.class_name && *hint.class_name) name = copystr(hint.class_name);
        else if (hint.instance_name && *hint.instance_name) name = copystr(hint.instance_name);
        xcb_icccm_get_wm_class_reply_wipe(&hint);
        if (name) return name;
    }

    xcb_get_property_cookie_t ck = xcb_get_property(conn, 0, w, net_wm_name, ewmh_utf8_string, 0, 1024);
    xcb_get_property_reply_t *r = xcb_get_property_reply(conn, ck, NULL);
    if (r) {
        int len = xcb_get_property_value_length(r);
        if (len > 0) {
            char *name = malloc((size_t)len + 1);
            if (name) {
                memcpy(name, xcb_get_property_value(r), (size_t)len);
                name[len] = 0;
                free(r);
                return name;
            }
        }
        free(r);
    }
    return copystr("unknown");
}

void titlebar_update(client *c) {
    if (c && c->titlebar) titlebar_draw(c);
}

static void canvas_sync_to_root(void) {
    unsigned long pan_x_raw[MAX_MONITORS];
    unsigned long pan_y_raw[MAX_MONITORS];
    for (int i = 0; i < MAX_MONITORS; i++) {
        memcpy(&pan_x_raw[i], &canvas.pan_x[i], sizeof(float));
        memcpy(&pan_y_raw[i], &canvas.pan_y[i], sizeof(float));
    }
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, root, canvas_atom_pan_x,
                         XCB_ATOM_CARDINAL, 32, MAX_MONITORS, pan_x_raw);
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, root, canvas_atom_pan_y,
                         XCB_ATOM_CARDINAL, 32, MAX_MONITORS, pan_y_raw);
}

void canvas_apply_all(void) {
    if (!cur) return;

    for win {
        if (c->f) continue;

        int m = c->mon;
        float px = canvas.pan_x[m];
        float py = canvas.pan_y[m];

        int sx = canvas_to_screen(c->cx, px);
        int sy = canvas_to_screen(c->cy, py);

        if (m < n_mons) {
            int mx = mons[m].x, my = mons[m].y, mw = mons[m].w, mh = mons[m].h;

            if (sx + c->width  <= mx || sx >= mx + mw ||
                sy + c->height <= my || sy >= my + mh) {
                uint32_t v[2] = { (uint32_t)(mx - c->width - 8000), (uint32_t)my };
                xcb_configure_window(conn, c->w, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, v);
                if (c->titlebar) {
                    uint32_t tv[2] = { (uint32_t)(mx - c->width - 8000), (uint32_t)(my - TITLEBAR_HEIGHT) };
                    xcb_configure_window(conn, c->titlebar, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, tv);
                }
                continue;
            }
            apply_mask(c->w, sx, sy, (unsigned)c->width, (unsigned)c->height, WBORDER, mx, my, mw, mh);
            if (c->titlebar)
                apply_mask(c->titlebar, sx, sy - TITLEBAR_HEIGHT, (unsigned)c->width, (unsigned)TB_CONTENT_H, WBORDER, mx, my, mw, mh);
        }

        client_move(c, sx, sy);
    }

    xcb_flush(conn);
    minimap_update();
    titlebar_update(cur);
    canvas_sync_to_root();
}

void canvas_pan(int mon, float dx, float dy) {
    if (mon < 0 || mon >= MAX_MONITORS) return;
    canvas.pan_x[mon] += dx;
    canvas.pan_y[mon] += dy;
    canvas_apply_all();
}

void canvas_pan_key(const Arg arg) {
    int   mon  = mon_at_ptr();
    float step = (float)PAN_STEP;
    switch (arg.i) {
        case 0: canvas_pan(mon, -step,  0);    break;
        case 1: canvas_pan(mon,  step,  0);    break;
        case 2: canvas_pan(mon,  0,    -step); break;
        case 3: canvas_pan(mon,  0,     step); break;
    }
}

void canvas_reset(const Arg arg) {
    (void)arg;
    int mon = mon_at_ptr();
    canvas.pan_x[mon] = 0;
    canvas.pan_y[mon] = 0;
    canvas_apply_all();
}

void win_focus(client *c) {
    client *prev = cur;

    if (!c)
        c = list ? list->prev : NULL;

    cur = c;

    if (c) {
        xcb_set_input_focus(conn, XCB_INPUT_FOCUS_PARENT, cur->w, XCB_CURRENT_TIME);
	titlebar_draw(cur);
        xcb_change_property(conn, XCB_PROP_MODE_REPLACE, root, net_active_window,
                             XCB_ATOM_WINDOW, 32, 1, &cur->w);
    } else {
        xcb_set_input_focus(conn, XCB_INPUT_FOCUS_POINTER_ROOT, root, XCB_CURRENT_TIME);
        xcb_delete_property(conn, root, net_active_window);
    }

    if (prev && prev != cur) titlebar_draw(prev);

    xcb_flush(conn);
    update_borders();
}

void titlebar_focus(xcb_window_t w) {
    client *own = client_from_titlebar(w);
    if (own) win_focus(own);
}

void canvas_focus(client *c) {
    if (!c || (cur && cur->f) || c->f) return;

    int m = c->mon;
    int mx = 0, my = 0, mw = sw, mh = sh;
    if (m < n_mons) { mx = mons[m].x; my = mons[m].y; mw = mons[m].w; mh = mons[m].h; }

    unsigned int cw, ch;
    win_size(c->w, NULL, NULL, &cw, &ch);

    float target_sx = mx + (mw - (int)cw) / 2.0f;
    float target_sy = my + (mh - (int)ch) / 2.0f;

    canvas.pan_x[m] = c->cx - target_sx;
    canvas.pan_y[m] = c->cy - target_sy;

    canvas_apply_all();

    uint32_t stack = XCB_STACK_MODE_ABOVE;
    xcb_configure_window(conn, c->w, XCB_CONFIG_WINDOW_STACK_MODE, &stack);
    if (c->titlebar) xcb_configure_window(conn, c->titlebar, XCB_CONFIG_WINDOW_STACK_MODE, &stack);

    win_focus(c);
    titlebar_update(cur);
}

void win_prev(const Arg arg) {
    (void)arg;
    if (!cur || !list) return;
    int m = cur->mon;
    if (cur->f) return;

    client *c = cur->prev;
    int checked = 0, total = 0;
    for (client *t = list; t->prev != list; t = t->prev) total++;
    total++;

    while (c != cur && checked < total) {
        if (c->mon == m) break;
        c = c->prev ? c->prev : list;
        checked++;
    }
    if (c && c != cur && c->mon == m) canvas_focus(c);
}

void win_next(const Arg arg) {
    (void)arg;
    if (!cur || !list) return;
    int m = cur->mon;
    if (cur->f) return;

    client *c = cur->next;
    int checked = 0, total = 0;
    for (client *t = list; t->next != list; t = t->next) total++;
    total++;

    while (c != cur && checked < total) {
        if (c->mon == m) break;
        c = c->next ? c->next : list;
        checked++;
    }
    if (c && c != cur && c->mon == m) canvas_focus(c);
}

void notify_destroy(xcb_destroy_notify_event_t *gen_e) {
    xcb_destroy_notify_event_t *e = (xcb_destroy_notify_event_t *)gen_e;

    win_del(e->window);
    minimap_update();

    win_focus(NULL);
    titlebar_update(cur);
}

void client_message(xcb_generic_event_t *gen_e) {
    xcb_client_message_event_t *e = (xcb_client_message_event_t *)gen_e;

    if (e->type == wm_protocols && e->data.data32[0] == wm_delete_window) {
        win_del(e->window);
        return;
    }

    if (e->type == net_active_window) {
        client *target = NULL;
        for win
            if (c->w == e->window) { target = c; break; }
        if (target) canvas_focus(target);
        return;
    }
    // TO_DO; _NET_WM_STATE fullscreen requests here 
}

void configure_notify(xcb_configure_notify_event_t *e) {
    if (e->window == root) {
        sw = e->width;
        sh = e->height;
        monitors_refresh();
        canvas_apply_all();
        return;
    }
 
    update_client_list_stacking();
}

void focusin(xcb_focus_in_event_t *e) {
    if (e->mode == XCB_NOTIFY_MODE_GRAB || e->mode == XCB_NOTIFY_MODE_UNGRAB)
        return;
    if (cur && e->event != cur->w)
        xcb_set_input_focus(conn, XCB_INPUT_FOCUS_PARENT, cur->w, XCB_CURRENT_TIME);
}
 
void notify_unmap(xcb_unmap_notify_event_t *e) {
    xcb_window_t w = e->window;

    int managed = 0;
    for win
        if (c->w == w) { managed = 1; break; }
    if (!managed) return;

    win_del(w);
    win_focus(NULL);
}

void notify_enter(xcb_enter_notify_event_t *e) {
    if (e->mode != XCB_NOTIFY_MODE_NORMAL || e->detail == XCB_NOTIFY_DETAIL_INFERIOR)
        return;

    for win
        if (c->w == e->event) {
            win_focus(c);
            if (c->titlebar) titlebar_update(c);
        }
    minimap_update();
    titlebar_update(cur);
}

void notify_property(xcb_property_notify_event_t *e) {
    for win {
        if (c->w == e->window) {
            if (e->atom == XCB_ATOM_WM_NAME || e->atom == net_wm_visible_name || e->atom == net_wm_name)
                titlebar_update(c);
            else if (e->atom == wm_normal_hints_atom)
                updatesizehints(c);
            break;
        }
    }
}

void notify_motion(xcb_motion_notify_event_t *e) {
    if (pan_active) {
        float dx = e->root_x - pan_start_x;
        float dy = e->root_y - pan_start_y;
        canvas.pan_x[pan_mon] = pan_origin_x - dx;
        canvas.pan_y[pan_mon] = pan_origin_y - dy;
        canvas_apply_all();
        return;
    }
 
    if (!cur || !drag_subwindow || cur->f) return;
 
    int xd = e->root_x - drag_root_x;
    int yd = e->root_y - drag_root_y;
 
    if (drag_button == XCB_BUTTON_INDEX_1) {
        int new_sx = cur->wx + xd;
        int new_sy = cur->wy + yd;
 
        int cenx = new_sx + (int)(cur->ww / 2);
        int ceny = new_sy + (int)(cur->wh / 2);
 
	for (int i = 0; i < n_mons; i++) {
	    if (cenx >= mons[i].x && cenx < mons[i].x + mons[i].w &&
		ceny >= mons[i].y && ceny < mons[i].y + mons[i].h) {
		set_client_monitor(cur, i);
		    break;
	    }
 
	}
 
        client_move(cur, new_sx, new_sy);
 
        if (cur->titlebar) {
            uint32_t stack = XCB_STACK_MODE_ABOVE;
            xcb_configure_window(conn, cur->titlebar, XCB_CONFIG_WINDOW_STACK_MODE, &stack);
        }
        minimap_update();
 
        int m = cur->mon;
        cur->cx = (float)new_sx + canvas.pan_x[m];
        cur->cy = (float)new_sy + canvas.pan_y[m];
    } else if (drag_button == XCB_BUTTON_INDEX_3) {
        client_resize(cur, (unsigned)MAX(1, (int)cur->ww + xd), (unsigned)MAX(1, (int)cur->wh + yd));
    }
}

void key_press(xcb_key_press_event_t *e) {
    xcb_keysym_t keysym = xcb_key_press_lookup_keysym(keysyms, e, 0);
    for (unsigned int i = 0; i < sizeof(keys) / sizeof(*keys); ++i)
        if (keys[i].keysym == keysym && mod_clean(keys[i].mod) == mod_clean(e->state))
            keys[i].function(keys[i].arg);
}

void button_press(xcb_button_press_event_t *gen_e) {
    xcb_button_press_event_t *e = (xcb_button_press_event_t *)gen_e;
    if (!cur) return;
 
    if (e->detail == XCB_BUTTON_INDEX_2) {
        pan_active   = 1;
        pan_mon      = mon_at_ptr();
        pan_start_x  = e->root_x;
        pan_start_y  = e->root_y;
        pan_origin_x = canvas.pan_x[pan_mon];
        pan_origin_y = canvas.pan_y[pan_mon];
        return;
    }
 
    if (is_titlebar(e->event) && !(e->state & MOD)) {
        client *c = client_from_titlebar(e->event);
        if (!c) return;
 
        unsigned int tw, th;
        win_size(c->titlebar, NULL, NULL, &tw, &th);
 
        int btn_w  = 22;
        int btn_x = (int)tw - 20;
        int btn_f  = btn_x - btn_w - 4;
 
        if (e->detail == XCB_BUTTON_INDEX_1 && e->event_x >= btn_x) { win_kill((Arg){0}); return; }
        if (e->detail == XCB_BUTTON_INDEX_1 && e->event_x >= btn_f && e->event_x < btn_x) { win_fs((Arg){0}); return; }
 
        win_focus(c);
        win_size(c->w, &c->wx, &c->wy, &c->ww, &c->wh);
        drag_subwindow = c->w;
        drag_button = e->detail;
        drag_root_x = e->root_x;
        drag_root_y = e->root_y;
 
        uint32_t stack = XCB_STACK_MODE_ABOVE;
        xcb_configure_window(conn, c->w, XCB_CONFIG_WINDOW_STACK_MODE, &stack);
        xcb_configure_window(conn, c->titlebar, XCB_CONFIG_WINDOW_STACK_MODE, &stack);
        xcb_flush(conn);
        return;
    }
 
    if (!e->child) return;
 
    client *target = NULL;
    for win {
        if (c->w == e->child || c->titlebar == e->child) { target = c; break; }
    }
 
    if (target) {
        win_focus(target);
        uint32_t stack = XCB_STACK_MODE_ABOVE;
        xcb_configure_window(conn, target->w, XCB_CONFIG_WINDOW_STACK_MODE, &stack);
        if (target->titlebar) xcb_configure_window(conn, target->titlebar, XCB_CONFIG_WINDOW_STACK_MODE, &stack);
        win_size(target->w, &target->wx, &target->wy, &target->ww, &target->wh);
    } else {
        win_size(e->child, &cur->wx, &cur->wy, &cur->ww, &cur->wh);
        uint32_t stack = XCB_STACK_MODE_ABOVE;
        xcb_configure_window(conn, e->child, XCB_CONFIG_WINDOW_STACK_MODE, &stack);
        if (cur && cur->titlebar) xcb_configure_window(conn, cur->titlebar, XCB_CONFIG_WINDOW_STACK_MODE, &stack);
    }
 
    drag_subwindow = e->child;
    drag_button = e->detail;
    drag_root_x = e->root_x;
    drag_root_y = e->root_y;
 
    xcb_flush(conn);
}

void button_release(xcb_button_release_event_t *e) {
    if (e->detail == XCB_BUTTON_INDEX_2) { pan_active = 0; return; }
    drag_subwindow = 0;
}

void win_add(xcb_window_t w) {
    client *c = calloc(1, sizeof(client));
    if (!c) exit(1);

    c->w = w;
    set_client_monitor(c, mon_at_win(w));

    int sx = 0, sy = 0;
    unsigned int dw2, dh2;
    win_size(w, &sx, &sy, &dw2, &dh2);
    int m = c->mon;
    c->cx = (float)sx + canvas.pan_x[m];
    c->cy = (float)sy + canvas.pan_y[m];

    c->x = sx;
    c->y = sy;
    c->width  = (int)dw2;
    c->height = (int)dh2;
    updatesizehints(c);

    if (TITLEBAR) {
        c->titlebar = titlebar_create(c);
        titlebar_update(c);
    }

    if (list) {
        list->prev->next = c;
        c->prev          = list->prev;
        list->prev       = c;
        c->next          = list;
    } else {
        list       = c;
        list->prev = list->next = list;
    }
    xcb_flush(conn);
}

void win_del(xcb_window_t w) {
    client *x = NULL;
    for win if (c->w == w) x = c;
    if (!list || !x) return;
    
    if (x->titlebar) titlebar_del(x);
    
    if (x->prev == x) list = NULL;
    if (list == x)    list = x->next;
    if (x->next) x->next->prev = x->prev;
    if (x->prev) x->prev->next = x->next;
    
    if (x == cur) cur = NULL;
    
    free(x);
    xcb_flush(conn);
}

void win_kill(const Arg arg) {
    (void)arg;
    if (!cur) return;

    xcb_icccm_get_wm_protocols_reply_t protocols;
    int has_delete = 0;
    if (xcb_icccm_get_wm_protocols_reply(conn,
            xcb_icccm_get_wm_protocols(conn, cur->w, wm_protocols), &protocols, NULL)) {
        for (uint32_t i = 0; i < protocols.atoms_len; i++)
            if (protocols.atoms[i] == wm_delete_window) { has_delete = 1; break; }
        xcb_icccm_get_wm_protocols_reply_wipe(&protocols);
    }

    if (has_delete) {
        xcb_client_message_event_t ev = {0};
        ev.response_type = XCB_CLIENT_MESSAGE;
        ev.format = 32;
        ev.window = cur->w;
        ev.type = wm_protocols;
        ev.data.data32[0] = wm_delete_window;
        ev.data.data32[1] = XCB_CURRENT_TIME;
        xcb_send_event(conn, 0, cur->w, XCB_EVENT_MASK_NO_EVENT, (const char *)&ev);
    } else {
        xcb_kill_client(conn, cur->w);
    }
    xcb_flush(conn);
}

void win_center(const Arg arg) {
    (void)arg;
    if (!cur || cur->f) return;

    unsigned int ww_, wh_;
    win_size(cur->w, NULL, NULL, &ww_, &wh_);

    xcb_query_pointer_reply_t *ptr = xcb_query_pointer_reply(conn, xcb_query_pointer(conn, root), NULL);
    int mx = 0, my = 0, mw = sw, mh = sh;
    if (ptr) {
        int m = mon_from_point(ptr->root_x, ptr->root_y);
        if (m < n_mons) { mx = mons[m].x; my = mons[m].y; mw = mons[m].w; mh = mons[m].h; }
        free(ptr);
    }

    int total_w = (int)ww_ + WBORDER * 2;
    int total_h = (int)wh_ + TITLEBAR_HEIGHT + WBORDER;

    int sx = mx + (mw - total_w) / 2;
    int sy = my + (mh - total_h) / 2 + TITLEBAR_HEIGHT;

    client_move(cur, sx, sy);

    set_client_monitor(cur, mon_at_win(cur->w));
    int m = cur->mon;
    cur->cx = (float)sx + canvas.pan_x[m];
    cur->cy = (float)sy + canvas.pan_y[m];
    minimap_update();
}

void win_fs(const Arg arg) {
    (void)arg;
    if (!cur) return;
    if (!cur->f) win_size(cur->w, &cur->wx, &cur->wy, &cur->ww, &cur->wh);

    xcb_query_pointer_reply_t *ptr = xcb_query_pointer_reply(conn, xcb_query_pointer(conn, root), NULL);
    int mx = 0, my = 0, mw = sw, mh = sh;
    if (ptr) {
        int m = mon_from_point(ptr->root_x, ptr->root_y);
        if (m < n_mons) { mx = mons[m].x; my = mons[m].y; mw = mons[m].w; mh = mons[m].h; }
        free(ptr);
    }

    cur->f = !cur->f;

    if (cur->f) {
        if (TITLEBAR) {
            resizeclient(cur, mw, mh - TITLEBAR_HEIGHT);
            client_move(cur, mx, my + TITLEBAR_HEIGHT);
            win_center((Arg){0});
        } else {
            resizeclient(cur, mw, mh);
            client_move(cur, mx, my);
            win_center((Arg){0});
        }

        minimap_update();
        titlebar_update(cur);
        if (!TITLEBAR && cur->titlebar) xcb_unmap_window(conn, cur->titlebar);
        uint32_t stack = XCB_STACK_MODE_ABOVE;
        xcb_configure_window(conn, cur->w, XCB_CONFIG_WINDOW_STACK_MODE, &stack);
    } else {
        resizeclient(cur, (int)cur->ww, (int)cur->wh);
        client_move(cur, cur->wx, cur->wy);
        minimap_update();
        titlebar_update(cur);
        if (cur->titlebar) {
            xcb_map_window(conn, cur->titlebar);
            uint32_t stack = XCB_STACK_MODE_ABOVE;
            xcb_configure_window(conn, cur->titlebar, XCB_CONFIG_WINDOW_STACK_MODE, &stack);
            titlebar_update(cur);
        }
        uint32_t stack = XCB_STACK_MODE_ABOVE;
        xcb_configure_window(conn, cur->w, XCB_CONFIG_WINDOW_STACK_MODE, &stack);
    }
    xcb_flush(conn);
}

void win_round_corners(xcb_window_t w, int rad) {
    unsigned int rw, rh;
    unsigned int dia = 2 * (unsigned int)rad;
    win_size(w, NULL, NULL, &rw, &rh);
    if (rw < dia || rh < dia) return;

    xcb_pixmap_t mask = xcb_generate_id(conn);
    xcb_create_pixmap(conn, 1, mask, w, (uint16_t)rw, (uint16_t)rh);
    xcb_gcontext_t gc = xcb_generate_id(conn);
    xcb_create_gc(conn, gc, mask, 0, NULL);

    uint32_t fg0 = 0;
    xcb_change_gc(conn, gc, XCB_GC_FOREGROUND, &fg0);
    xcb_rectangle_t full = { 0, 0, (uint16_t)rw, (uint16_t)rh };
    xcb_poly_fill_rectangle(conn, mask, gc, 1, &full);

    uint32_t fg1 = 1;
    xcb_change_gc(conn, gc, XCB_GC_FOREGROUND, &fg1);
    xcb_arc_t arcs[4] = {
        { 0, 0, (uint16_t)dia, (uint16_t)dia, 0, 23040 },
        { (int16_t)(rw - dia - 1), 0, (uint16_t)dia, (uint16_t)dia, 0, 23040 },
        { 0, (int16_t)(rh - dia - 1), (uint16_t)dia, (uint16_t)dia, 0, 23040 },
        { (int16_t)(rw - dia - 1), (int16_t)(rh - dia - 1), (uint16_t)dia, (uint16_t)dia, 0, 23040 },
    };
    xcb_poly_fill_arc(conn, mask, gc, 4, arcs);

    xcb_rectangle_t rects[2] = {
        { (int16_t)rad, 0, (uint16_t)(rw - dia), (uint16_t)rh },
        { 0, (int16_t)rad, (uint16_t)rw, (uint16_t)(rh - dia) },
    };
    xcb_poly_fill_rectangle(conn, mask, gc, 2, rects);

    xcb_shape_mask(conn, XCB_SHAPE_SO_SET, XCB_SHAPE_SK_BOUNDING, w, 0, 0, mask);
    xcb_free_pixmap(conn, mask);
    xcb_free_gc(conn, gc);
}

void configure_request(xcb_configure_request_event_t *e) {
    int sx = e->x, sy = e->y;
    uint16_t mask = e->value_mask;
    client *target = NULL;

    for win {
        if (c->w == e->window) {
            target = c;
            int m = c->mon;
            sx = canvas_to_screen(c->cx, canvas.pan_x[m]);
            sy = canvas_to_screen(c->cy, canvas.pan_y[m]);
            mask |= XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y;
            titlebar_update(c);
            break;
        }
    }

    uint32_t values[7];
    int i = 0;
    if (mask & XCB_CONFIG_WINDOW_X)            values[i++] = (uint32_t)sx;
    if (mask & XCB_CONFIG_WINDOW_Y)            values[i++] = (uint32_t)sy;
    if (mask & XCB_CONFIG_WINDOW_WIDTH)	     { values[i++] = e->width;  if (target) target->width = e->width; }
    if (mask & XCB_CONFIG_WINDOW_HEIGHT)     { values[i++] = e->height;  if (target) target->height = e->height; }
    if (mask & XCB_CONFIG_WINDOW_SIBLING)      values[i++] = e->sibling;
    if (mask & XCB_CONFIG_WINDOW_STACK_MODE)   values[i++] = e->stack_mode;
    xcb_configure_window(conn, e->window, mask, values);

    if (target && target->titlebar) {
        uint32_t tv[4] = { (uint32_t)sx, (uint32_t)(sy - TITLEBAR_HEIGHT), e->width, TITLEBAR_HEIGHT };
        xcb_configure_window(conn, target->titlebar,
            XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, tv);
        titlebar_update(target);
    }
    xcb_flush(conn);
}

static int win_is_dock(xcb_window_t w) {
    xcb_get_property_cookie_t ck = xcb_get_property(conn, 0, w, net_wm_window_type, XCB_ATOM_ATOM, 0, 1);
    xcb_get_property_reply_t *r = xcb_get_property_reply(conn, ck, NULL);
    int dock = 0;
    if (r && xcb_get_property_value_length(r) > 0) {
        xcb_atom_t t = *(xcb_atom_t *)xcb_get_property_value(r);
        dock = (t == net_wm_window_type_dock);
    }
    free(r);
    return dock;
}

static void update_struts(xcb_window_t w) {
    xcb_get_property_cookie_t ck = xcb_get_property(conn, 0, w, net_wm_strut_partial, XCB_ATOM_CARDINAL, 0, 12);
    xcb_get_property_reply_t *r = xcb_get_property_reply(conn, ck, NULL);
    if (r && xcb_get_property_value_length(r) >= (int)(4 * sizeof(uint32_t))) {
        uint32_t *s = xcb_get_property_value(r);
        strut[0] = MAX(strut[0], (int)s[0]);
        strut[1] = MAX(strut[1], (int)s[1]);
        strut[2] = MAX(strut[2], (int)s[2]);
        strut[3] = MAX(strut[3], (int)s[3]);
        free(r);
        return;
    }
    free(r);

    ck = xcb_get_property(conn, 0, w, net_wm_strut, XCB_ATOM_CARDINAL, 0, 4);
    r = xcb_get_property_reply(conn, ck, NULL);
    if (r && xcb_get_property_value_length(r) == (int)(4 * sizeof(uint32_t))) {
        uint32_t *s = xcb_get_property_value(r);
        strut[0] = MAX(strut[0], (int)s[0]);
        strut[1] = MAX(strut[1], (int)s[1]);
        strut[2] = MAX(strut[2], (int)s[2]);
        strut[3] = MAX(strut[3], (int)s[3]);
    }
    free(r);
}

void expose_event(xcb_expose_event_t *e) {
    xcb_window_t w = e->window;

    client *c = client_from_titlebar(w);
    if (c) { titlebar_update(c); return; }
}

void map_request(xcb_map_request_event_t *e) {
    xcb_window_t w = e->window;
 
    xcb_get_window_attributes_reply_t *wa =
        xcb_get_window_attributes_reply(conn, xcb_get_window_attributes(conn, w), NULL);
    if (!wa || wa->override_redirect) { free(wa); return; }
    free(wa);
 
    if (win_is_dock(w)) {
        update_struts(w);
        xcb_map_window(conn, w);
        xcb_flush(conn);
        return;
    }
 
    for win
        if (c->w == w) { xcb_map_window(conn, w); xcb_flush(conn); return; }
 
 
    xcb_window_t transient_for = XCB_NONE;
    xcb_icccm_get_wm_transient_for_reply(conn, xcb_icccm_get_wm_transient_for(conn, w), &transient_for, NULL);
 
    static xcb_atom_t skip_types[7];
    static int skip_types_inited = 0;
    if (!skip_types_inited) {
        skip_types[0] = get_atom("_NET_WM_WINDOW_TYPE_UTILITY");
        skip_types[1] = get_atom("_NET_WM_WINDOW_TYPE_SPLASH");
        skip_types[2] = get_atom("_NET_WM_WINDOW_TYPE_TOOLTIP");
        skip_types[3] = get_atom("_NET_WM_WINDOW_TYPE_NOTIFICATION");
        skip_types[4] = get_atom("_NET_WM_WINDOW_TYPE_POPUP_MENU");
        skip_types[5] = get_atom("_NET_WM_WINDOW_TYPE_DROPDOWN_MENU");
        skip_types[6] = get_atom("_NET_WM_WINDOW_TYPE_MENU");
        skip_types_inited = 1;
    }
 
    xcb_get_property_cookie_t ck = xcb_get_property(conn, 0, w, net_wm_window_type, XCB_ATOM_ATOM, 0, 1);
    xcb_get_property_reply_t *r = xcb_get_property_reply(conn, ck, NULL);
    if (r && xcb_get_property_value_length(r) > 0) {
        xcb_atom_t t = *(xcb_atom_t *)xcb_get_property_value(r);
        for (unsigned int i = 0; i < sizeof(skip_types) / sizeof(skip_types[0]); i++) {
            if (t == skip_types[i]) {
                free(r);
                xcb_map_window(conn, w);
                xcb_flush(conn);
                return;
            }
        }
    }
    free(r);
 
    uint32_t evmask = XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_ENTER_WINDOW |
                       XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_FOCUS_CHANGE;
    xcb_change_window_attributes(conn, w, XCB_CW_EVENT_MASK, &evmask);
 
    uint32_t bw = WBORDER;
    xcb_configure_window(conn, w, XCB_CONFIG_WINDOW_BORDER_WIDTH, &bw);
 
    int nx = 0, ny = 0; unsigned int nw = 0, nh = 0;
    win_size(w, &nx, &ny, &nw, &nh);
    win_add(w);
 
    client *oc = cur;
    cur = list->prev;
 
    if (transient_for != XCB_NONE) {
        int px = 0, py = 0; unsigned int pw = 0, ph = 0;
        win_size(transient_for, &px, &py, &pw, &ph);
        if (pw && ph) {
            int cx = px + ((int)pw - (int)nw) / 2;
            int cy = py + ((int)ph - (int)nh) / 2;
            client_move(cur, cx, cy);
        } else if (nx + ny == 0) {
            win_center((Arg){0});
        }
    } else if (nx + ny == 0) {
        win_center((Arg){0});
    }
 
 
    if (cur->titlebar) titlebar_update(cur);
 
    xcb_map_window(conn, w);
    minimap_update();
    cur = oc;
    win_focus(list->prev);
    xcb_flush(conn);
}
 
void mapping_notify(xcb_mapping_notify_event_t *e) {
    if (e->request == XCB_MAPPING_KEYBOARD || e->request == XCB_MAPPING_MODIFIER) {
        xcb_refresh_keyboard_mapping(keysyms, e);
        input_grab(root);
    }
}

void run(const Arg arg) {
    if (fork()) return;
    if (conn) close(xcb_get_file_descriptor(conn));
    setsid();
    execvp((char *)arg.com[0], (char **)arg.com);
    exit(1);
}

void quit(const Arg arg) {
	running = 0;
}

void input_grab(xcb_window_t rootw) {
    unsigned int modifiers[] = { 0, XCB_MOD_MASK_LOCK, numlock, numlock | XCB_MOD_MASK_LOCK };

    xcb_get_modifier_mapping_reply_t *modmap =
        xcb_get_modifier_mapping_reply(conn, xcb_get_modifier_mapping(conn), NULL);
    if (modmap) {
        xcb_keycode_t *mk = xcb_get_modifier_mapping_keycodes(modmap);
        xcb_keycode_t *num_kc = xcb_key_symbols_get_keycode(keysyms, 0xff7f);
        if (num_kc) {
            for (unsigned int i = 0; i < 8; i++)
                for (int k = 0; k < modmap->keycodes_per_modifier; k++)
                    if (mk[i * modmap->keycodes_per_modifier + k] == num_kc[0])
                        numlock = (1u << i);
            free(num_kc);
        }
        free(modmap);
    }

    xcb_ungrab_key(conn, XCB_GRAB_ANY, rootw, XCB_MOD_MASK_ANY);

    for (unsigned int i = 0; i < sizeof(keys) / sizeof(*keys); i++) {
        xcb_keycode_t *kc = xcb_key_symbols_get_keycode(keysyms, keys[i].keysym);
        if (!kc) continue;
        for (unsigned int j = 0; j < sizeof(modifiers) / sizeof(*modifiers); j++)
            xcb_grab_key(conn, 1, rootw, (uint16_t)(keys[i].mod | modifiers[j]), kc[0],
                         XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
        free(kc);
    }

    for (int i = 1; i < 4; i++)
        for (unsigned int j = 0; j < sizeof(modifiers) / sizeof(*modifiers); j++)
            xcb_grab_button(conn, 1, rootw,
                             XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
                             XCB_EVENT_MASK_POINTER_MOTION,
                             XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC,
                             XCB_NONE, XCB_NONE, (uint8_t)i, (uint16_t)(MOD | modifiers[j]));

    xcb_flush(conn);
}

void ws_focusnext(const Arg arg) {
    (void)arg;
    xcb_query_pointer_reply_t *ptr = xcb_query_pointer_reply(conn, xcb_query_pointer(conn, root), NULL);
    if (!ptr) return;
    if (n_mons < 2) { free(ptr); return; }

    int cur_mon = mon_from_point(ptr->root_x, ptr->root_y);
    int next = (cur_mon + 1) % n_mons;
    xcb_warp_pointer(conn, XCB_NONE, root, 0, 0, 0, 0,
                      (int16_t)(mons[next].x + mons[next].w / 2),
                      (int16_t)(mons[next].y + mons[next].h / 2));
    minimap_update();
    titlebar_update(cur);
    free(ptr);
    xcb_flush(conn);
}

void move_nextmon(const Arg arg) {
    (void)arg;
    xcb_query_pointer_reply_t *ptr = xcb_query_pointer_reply(conn, xcb_query_pointer(conn, root), NULL);
    if (!ptr) return;
    if (n_mons < 2) { free(ptr); return; }
    if (cur->f) { free(ptr); return; }

    int cur_mon = mon_from_point(ptr->root_x, ptr->root_y);
    int next = (cur_mon + 1) % n_mons;
    xcb_warp_pointer(conn, XCB_NONE, root, 0, 0, 0, 0,
                      (int16_t)(mons[next].x + mons[next].w / 2),
                      (int16_t)(mons[next].y + mons[next].h / 2));

    if (cur) {
        unsigned int cw, ch;
        int cwx, cwy;
        win_size(cur->w, &cwx, &cwy, &cw, &ch);
        int new_sx = mons[next].x + (mons[next].w - (int)cw) / 2;
        int new_sy = mons[next].y + (mons[next].h - (int)ch) / 2;
        client_move(cur, new_sx, new_sy);
        minimap_update();
        titlebar_update(cur);
        set_client_monitor(cur, next);
        cur->cx = (float)new_sx + canvas.pan_x[next];
        cur->cy = (float)new_sy + canvas.pan_y[next];
    }

    free(ptr);
    xcb_flush(conn);
}

void notify_screen_change(xcb_randr_screen_change_notify_event_t *e) {
    (void)e;
    monitors_refresh();
    canvas_apply_all();
}

int main(void) {
    dpy = XOpenDisplay(NULL);
    if (!dpy) exit(1);

    conn = XGetXCBConnection(dpy);
    if (!conn || xcb_connection_has_error(conn)) exit(1);
    XSetEventQueueOwner(dpy, XCBOwnsEventQueue);

    signal(SIGCHLD, SIG_IGN);
    signal(SIGUSR1, handle_sigusr1);

    scrno  = DefaultScreen(dpy);
    screen = xcb_aux_get_screen(conn, scrno);
    root   = screen->root;
    sw     = screen->width_in_pixels;
    sh     = screen->height_in_pixels;
    visual = DefaultVisual(dpy, scrno);
    cmap   = DefaultColormap(dpy, scrno);
    depth  = DefaultDepth(dpy, scrno);

    keysyms = xcb_key_symbols_alloc(conn);

    net_supporting_wm_check  = get_atom("_NET_SUPPORTING_WM_CHECK");
    net_wm_name              = get_atom("_NET_WM_NAME");
    net_wm_visible_name      = get_atom("_NET_WM_VISIBLE_NAME");
    net_active_window        = get_atom("_NET_ACTIVE_WINDOW");
    ewmh_utf8_string         = get_atom("UTF8_STRING");
    net_supported            = get_atom("_NET_SUPPORTED");
    net_wm_window_type       = get_atom("_NET_WM_WINDOW_TYPE");
    net_wm_window_type_dock  = get_atom("_NET_WM_WINDOW_TYPE_DOCK");
    net_wm_strut             = get_atom("_NET_WM_STRUT");
    net_wm_strut_partial     = get_atom("_NET_WM_STRUT_PARTIAL");
    net_current_desktop      = get_atom("_NET_CURRENT_DESKTOP");
    wm_delete_window         = get_atom("WM_DELETE_WINDOW");
    wm_protocols             = get_atom("WM_PROTOCOLS");
    wm_normal_hints_atom     = XCB_ATOM_WM_NORMAL_HINTS;
    net_client_list_stacking = get_atom("_NET_CLIENT_LIST_STACKING");

    canvas_atom_pan_x = get_atom("_CANVAS_PAN_X");
    canvas_atom_pan_y = get_atom("_CANVAS_PAN_Y");
    sbcwm_atom_monitor = get_atom("_SBCWM_MONITOR");

    const xcb_query_extension_reply_t *randr_ext = xcb_get_extension_data(conn, &xcb_randr_id);
    if (randr_ext && randr_ext->present) {
        xcb_randr_query_version_cookie_t vck = xcb_randr_query_version(conn, 1, 5);
        xcb_randr_query_version_reply_t *vr = xcb_randr_query_version_reply(conn, vck, NULL);
        free(vr);

        randr_event_base = randr_ext->first_event;
        xcb_randr_select_input(conn, root, XCB_RANDR_NOTIFY_MASK_SCREEN_CHANGE);
    }

    monitors_refresh();
    canvas_sync_to_root();

    xcb_window_t wmcheck = xcb_generate_id(conn);
    xcb_create_window(conn, XCB_COPY_FROM_PARENT, wmcheck, root, 0, 0, 1, 1, 0,
                       XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, 0, NULL);
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, root, net_supporting_wm_check,
                         XCB_ATOM_WINDOW, 32, 1, &wmcheck);
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, wmcheck, net_supporting_wm_check,
                         XCB_ATOM_WINDOW, 32, 1, &wmcheck);
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, wmcheck, net_wm_name,
                         ewmh_utf8_string, 8, 6, "sbcwm");
 
    xcb_atom_t supported[] = {
        net_supporting_wm_check, net_wm_name, net_wm_window_type,
        net_wm_window_type_dock, net_wm_strut, net_wm_strut_partial,
        net_current_desktop, net_client_list_stacking,
    };
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, root, net_supported,
                         XCB_ATOM_ATOM, 32, sizeof(supported) / sizeof(*supported), supported);

    uint32_t cur_ws = 0;
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, root, net_current_desktop,
                         XCB_ATOM_CARDINAL, 32, 1, &cur_ws);

    uint32_t root_mask = XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT;
    xcb_change_window_attributes(conn, root, XCB_CW_EVENT_MASK, &root_mask);

    xcb_font_t cursor_font = xcb_generate_id(conn);
    xcb_open_font(conn, cursor_font, strlen("cursor"), "cursor");
    xcb_cursor_t cursor = xcb_generate_id(conn);
    xcb_create_glyph_cursor(conn, cursor, cursor_font, cursor_font, 68, 68 + 1,
                             0, 0, 0, 0xffff, 0xffff, 0xffff);
    xcb_change_window_attributes(conn, root, XCB_CW_CURSOR, &cursor);
    xcb_close_font(conn, cursor_font);

    input_grab(root);

    if (UI_HUD) {
        minimap_create();
        minimap_update();
        if (!minimap) {
            for (int i = 0; i < MAX_MONITORS; i++)
                if (minimap_win[i]) {
                    uint32_t v[2] = { (uint32_t)-800, (uint32_t)minimap_py[i] };
                    xcb_configure_window(conn, minimap_win[i], XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, v);
                }
        }
    }

    xcb_flush(conn);

    xcb_generic_event_t *ev;
    while (running && (ev = xcb_wait_for_event(conn))) {
        uint8_t type = ev->response_type & ~0x80;

        switch (type) {
            case XCB_BUTTON_PRESS:      button_press((xcb_button_press_event_t *)ev); break;
            case XCB_BUTTON_RELEASE:    button_release((xcb_button_release_event_t *)ev); break;
	    case XCB_CONFIGURE_NOTIFY:  configure_notify((xcb_configure_notify_event_t *) ev); break;
            case XCB_CONFIGURE_REQUEST: configure_request((xcb_configure_request_event_t *)ev); break;
            case XCB_KEY_PRESS:         key_press((xcb_key_press_event_t *)ev); break;
            case XCB_EXPOSE:            expose_event((xcb_expose_event_t *)ev); break;
            case XCB_MAP_REQUEST:       map_request((xcb_map_request_event_t *)ev); break;
            case XCB_MAPPING_NOTIFY:    mapping_notify((xcb_mapping_notify_event_t *)ev); break;
            case XCB_DESTROY_NOTIFY:    notify_destroy((xcb_destroy_notify_event_t *)ev); break;
            case XCB_UNMAP_NOTIFY:      notify_unmap((xcb_unmap_notify_event_t *)ev); break;
            case XCB_ENTER_NOTIFY:      notify_enter((xcb_enter_notify_event_t *)ev); break;
            case XCB_PROPERTY_NOTIFY:   notify_property((xcb_property_notify_event_t *)ev); break;
	    case XCB_FOCUS_IN:		focusin((xcb_focus_in_event_t *) ev); break;
	    case XCB_CLIENT_MESSAGE:	client_message((xcb_generic_event_t *) ev); break;
            case XCB_MOTION_NOTIFY: {
                xcb_generic_event_t *next;
                xcb_motion_notify_event_t *last = (xcb_motion_notify_event_t *)ev;
                while ((next = xcb_poll_for_queued_event(conn))) {
                    if ((next->response_type & ~0x80) != XCB_MOTION_NOTIFY) {
                        free(ev);
                        ev = next;
                        goto handle_non_motion;
                    }
                    free(ev);
                    ev = next;
                    last = (xcb_motion_notify_event_t *)ev;
                }
                notify_motion(last);
                break;
handle_non_motion:
                type = ev->response_type & ~0x80;
                switch (type) {
                    case XCB_BUTTON_PRESS:      button_press((xcb_button_press_event_t *)ev); break;
                    case XCB_BUTTON_RELEASE:    button_release((xcb_button_release_event_t *)ev); break;
		    case XCB_CONFIGURE_NOTIFY:  configure_notify((xcb_configure_notify_event_t *) ev); break;
                    case XCB_CONFIGURE_REQUEST: configure_request((xcb_configure_request_event_t *)ev); break;
                    case XCB_KEY_PRESS:         key_press((xcb_key_press_event_t *)ev); break;
                    case XCB_EXPOSE:            expose_event((xcb_expose_event_t *)ev); break;
                    case XCB_MAP_REQUEST:       map_request((xcb_map_request_event_t *)ev); break;
                    case XCB_MAPPING_NOTIFY:    mapping_notify((xcb_mapping_notify_event_t *)ev); break;
                    case XCB_DESTROY_NOTIFY:    notify_destroy((xcb_destroy_notify_event_t *)ev); break;
                    case XCB_UNMAP_NOTIFY:      notify_unmap((xcb_unmap_notify_event_t *)ev); break;
                    case XCB_ENTER_NOTIFY:      notify_enter((xcb_enter_notify_event_t *)ev); break;
                    case XCB_PROPERTY_NOTIFY:   notify_property((xcb_property_notify_event_t *)ev); break;
		    case XCB_FOCUS_IN:		focusin((xcb_focus_in_event_t *) ev); break;
		    case XCB_CLIENT_MESSAGE:	client_message((xcb_generic_event_t *) ev); break;
                    default: break;
                }
                break;
            }
            default:
                if (randr_event_base && type == randr_event_base + XCB_RANDR_SCREEN_CHANGE_NOTIFY)
                    notify_screen_change((xcb_randr_screen_change_notify_event_t *)ev);
                break;
        }

        free(ev);

        if (UI_HUD) {
            always_ot();
        }
    }

    return 0;
}
