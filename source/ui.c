/* PS5-style front end: home screen (game row, backdrop, details) and settings. */
#include "ui.h"
#include "gfx.h"
#include "font.h"
#include "images.h"
#include "library.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ---------- palette ---------- */
#define C_BG        RGBA(5, 7, 13, 255)
#define C_WHITE     RGBA(255, 255, 255, 255)
#define C_DIM       RGBA(255, 255, 255, 150)
#define C_FAINT     RGBA(255, 255, 255, 90)
#define C_CARD      RGBA(30, 34, 48, 200)
#define C_ROW       RGBA(255, 255, 255, 14)
#define C_ROW_FOCUS RGBA(255, 255, 255, 36)
#define C_ACCENT    RGBA(47, 124, 246, 255)
#define C_OK        RGBA(52, 211, 153, 255)

/* ---------- resources ---------- */
enum {
    F_TAB, F_CLOCK, F_SELTITLE, F_HERO, F_HERO_S, F_META, F_BTN, F_LABEL, F_VALUE, F_SUB,
    F_TILE, F_TILE_S, F_H1, F_ROW, F_ROW_DESC, F_HINT, F_TOAST_T, F_TOAST_B, F_COUNT
};
static font_t *fonts[F_COUNT];

typedef struct { const char *name; const image_t *img; } icon_t;
static icon_t icons[] = {
    {"gear", 0}, {"grid", 0}, {"play", 0}, {"folder", 0}, {"image", 0}, {"film", 0},
    {"info", 0}, {"refresh", 0}, {"clock", 0}, {"usb", 0}, {"hdd", 0}, {"check", 0},
    {"xmark", 0}, {"monitor", 0}, {"layers", 0}, {"gamepad", 0}, {"library", 0},
    {"alert", 0}, {"sparkle", 0}, {"btn_cross", 0}, {"btn_circle", 0}, {"btn_square", 0},
    {"btn_triangle", 0}, {"btn_start", 0}, {"btn_l1r1", 0}, {"chevron", 0},
};
#define NICONS (int)(sizeof(icons) / sizeof(icons[0]))

static void draw_icon(const char *name, float x, float y, float size, rgba_t c)
{
    for (int i = 0; i < NICONS; i++)
        if (!strcmp(icons[i].name, name)) {
            if (icons[i].img) gfx_image(icons[i].img->tex, x, y, size, size, 0, 0, 1, 1, c);
            return;
        }
}

/* ---------- state ---------- */
enum { SCR_HOME, SCR_SETTINGS };
enum { FOC_TABS, FOC_TILES, FOC_BUTTONS };

static library_t lib;
static float *tile_w;          /* animated tile widths */
static int sel;
static float scroll;           /* animated row offset */
static int focus = FOC_TILES;
static int tab;                /* 0 games, 1 settings (in the top bar) */
static int btn_sel;            /* 0 play, 1 more */
static int screen = SCR_HOME;
static int screen_next = -1;
static float fade = 1.f;       /* 1 = fully visible */
static uint64_t now_ms, sel_ms;

static int bg_cur = -1, bg_prev = -1;
static float bg_mix = 1.f;

static struct { char icon[24], title[96], body[200]; uint64_t t0; int on; } toast;

/* settings */
enum { ROW_OPTION, ROW_TOGGLE, ROW_ACTION, ROW_INFO };
typedef struct {
    const char *label, *desc;
    int type;
    int *val;
    const char *const *opts;
    int nopts;
    void (*action)(void);
    char info[160];
} row_t;
typedef struct { const char *name, *icon; row_t rows[12]; int nrows; } cat_t;
static cat_t cats[3];
static int ncats;
static int set_cat, set_row, set_focus_panel;

/* ---------- helpers ---------- */
static int confirm_btn(const pad_t *p) { return p->cross_is_enter ? BTN_CROSS : BTN_CIRCLE; }
static int back_btn(const pad_t *p) { return p->cross_is_enter ? BTN_CIRCLE : BTN_CROSS; }
static const char *confirm_icon(const pad_t *p) { return p->cross_is_enter ? "btn_cross" : "btn_circle"; }
static const char *back_icon(const pad_t *p) { return p->cross_is_enter ? "btn_circle" : "btn_cross"; }

void ui_toast(const char *icon, const char *title, const char *body)
{
    snprintf(toast.icon, sizeof(toast.icon), "%s", icon);
    snprintf(toast.title, sizeof(toast.title), "%s", title);
    snprintf(toast.body, sizeof(toast.body), "%s", body ? body : "");
    toast.t0 = now_ms;
    toast.on = 1;
}

static void go_screen(int s)
{
    if (s == screen || screen_next >= 0) return;
    screen_next = s;
}

static void fmt_size(long b, char *out, size_t n)
{
    if (b >= 1024 * 1024) snprintf(out, n, "%.1f MB", b / (1024.0 * 1024.0));
    else snprintf(out, n, "%ld KB", b / 1024);
}

static void clock_text(char *out, size_t n)
{
    clock_t2 t;
    plat_local_time(&t);
    int use24 = cfg.clock_format == CLOCK_24H || (cfg.clock_format == CLOCK_SYSTEM && plat_use_24h());
    if (use24) snprintf(out, n, "%02d:%02d", t.hour, t.minute);
    else snprintf(out, n, "%d:%02d %s", t.hour % 12 ? t.hour % 12 : 12, t.minute, t.hour < 12 ? "AM" : "PM");
}

static float draw_hint(float x, float y, const char *icon, const char *label)
{
    gfx_circle(x + 17, y + 17, 17, RGBA(255, 255, 255, 40));
    draw_icon(icon, x + 5, y + 5, 24, C_WHITE);
    float w = text_draw(fonts[F_HINT], x + 46, y + 4, label, C_DIM, 0, ALIGN_LEFT);
    return 46 + w + 34;
}

static float hints_width(const char *const *labels, int n)
{
    float w = 0;
    for (int i = 0; i < n; i++) w += 46 + text_width(fonts[F_HINT], labels[i], 0) + 34;
    return w - 34;
}

/* ---------- library ---------- */
static int cmp_region(const void *a, const void *b)
{
    const game_t *x = a, *y = b;
    int r = strcmp(x->region, y->region);
    return r ? r : strcmp(x->title, y->title);
}

static void load_library(void)
{
    library_scan(&lib);
    if (cfg.sort == SORT_REGION && lib.count > 1)
        qsort(lib.games, (size_t)lib.count, sizeof(game_t), cmp_region);
    free(tile_w);
    tile_w = calloc((size_t)(lib.count ? lib.count : 1), sizeof(float));
    for (int i = 0; i < lib.count; i++) tile_w[i] = 150;
    if (sel >= lib.count) sel = lib.count ? lib.count - 1 : 0;
    bg_cur = bg_prev = -1;
    bg_mix = 1;
    sel_ms = now_ms;
}

/* ---------- settings model ---------- */
static const char *const clock_opts[] = {"Same as PS3", "12-hour", "24-hour"};
static const char *const onoff_opts[] = {"Off", "On"};
static const char *const backdrop_opts[] = {"Game snapshot", "Cover art"};
static const char *const sort_opts[] = {"Name", "Region"};

static void act_rescan(void)
{
    load_library();
    char b[96];
    snprintf(b, sizeof(b), "%d game%s found", lib.count, lib.count == 1 ? "" : "s");
    ui_toast("refresh", "Library updated", b);
}

static void build_settings(void)
{
    ncats = 0;
    cat_t *c = &cats[ncats++];
    memset(c, 0, sizeof(*c));
    c->name = "Home Screen"; c->icon = "monitor";
    c->rows[c->nrows++] = (row_t){"Background", "Picture behind the selected game", ROW_OPTION, &cfg.backdrop, backdrop_opts, 2, 0, ""};
    c->rows[c->nrows++] = (row_t){"Show Clock", NULL, ROW_TOGGLE, &cfg.show_clock, onoff_opts, 2, 0, ""};
    c->rows[c->nrows++] = (row_t){"Clock Format", NULL, ROW_OPTION, &cfg.clock_format, clock_opts, 3, 0, ""};

    c = &cats[ncats++];
    memset(c, 0, sizeof(*c));
    c->name = "Game Library"; c->icon = "library";
    c->rows[c->nrows++] = (row_t){"Scan for Games", "Look for new ROMs and artwork", ROW_ACTION, 0, 0, 0, act_rescan, ""};
    c->rows[c->nrows++] = (row_t){"Sort Games By", NULL, ROW_OPTION, &cfg.sort, sort_opts, 2, 0, ""};

    c = &cats[ncats++];
    memset(c, 0, sizeof(*c));
    c->name = "System"; c->icon = "info";
    c->rows[c->nrows++] = (row_t){"Version", NULL, ROW_INFO, 0, 0, 0, 0, APP_VERSION};
    c->rows[c->nrows++] = (row_t){"Playing Games", "The emulator core is added in the next update", ROW_INFO, 0, 0, 0, 0, "Next update"};
}

/* rows that depend on the library are refreshed every time settings opens */
static void refresh_library_rows(void)
{
    cat_t *c = &cats[1];
    c->nrows = 2;
    for (int i = 0; i < lib.ndirs && c->nrows < 12; i++) {
        row_t *r = &c->rows[c->nrows++];
        memset(r, 0, sizeof(*r));
        r->label = lib.dirs[i];
        r->type = ROW_INFO;
        snprintf(r->info, sizeof(r->info), "%s", lib.dir_found[i] ? "Folder found" : "Not found");
    }
    cat_t *s = &cats[2];
    if (s->nrows < 3) {
        row_t *r = &s->rows[s->nrows++];
        memset(r, 0, sizeof(*r));
        r->label = "Games in Library"; r->type = ROW_INFO;
    }
    snprintf(s->rows[2].info, sizeof(s->rows[2].info), "%d", lib.count);
}

/* ---------- init ---------- */
int ui_init(void)
{
    char p[512];
    struct { int id; const char *file; int px; } fl[] = {
        {F_TAB, "Inter-SemiBold", 30}, {F_CLOCK, "Inter-Medium", 28}, {F_SELTITLE, "Inter-SemiBold", 26},
        {F_HERO, "InterDisplay-Black", 96}, {F_HERO_S, "InterDisplay-Black", 72},
        {F_META, "Inter-Regular", 24}, {F_BTN, "Inter-Bold", 30}, {F_LABEL, "Inter-SemiBold", 18},
        {F_VALUE, "Inter-Bold", 28}, {F_SUB, "Inter-Regular", 20}, {F_TILE, "InterDisplay-Black", 30},
        {F_TILE_S, "InterDisplay-Black", 20}, {F_H1, "Inter-Bold", 52}, {F_ROW, "Inter-Medium", 27},
        {F_ROW_DESC, "Inter-Regular", 20}, {F_HINT, "Inter-Medium", 22}, {F_TOAST_T, "Inter-SemiBold", 24},
        {F_TOAST_B, "Inter-Regular", 20},
    };
    for (size_t i = 0; i < sizeof(fl) / sizeof(fl[0]); i++) {
        snprintf(p, sizeof(p), "%s/fonts/%s.otf", plat_app_dir(), fl[i].file);
        fonts[fl[i].id] = font_load(p, fl[i].px);
        if (!fonts[fl[i].id]) return -1;
    }
    for (int i = 0; i < NICONS; i++) {
        snprintf(p, sizeof(p), "%s/icons/%s.png", plat_app_dir(), icons[i].name);
        icons[i].img = image_load_now(p);
    }
    now_ms = plat_ms();
    build_settings();
    load_library();
    if (cfg.last_game >= 0 && cfg.last_game < lib.count) sel = cfg.last_game;
    for (int i = 0; i < lib.count; i++) tile_w[i] = i == sel ? 236 : 150;
    scroll = 0;
    refresh_library_rows();
    return 0;
}

/* ---------- drawing: home ---------- */
static void cover_uv(const image_t *im, float aspect, float *u0, float *v0, float *u1, float *v1)
{
    /* crop the centre of the picture to the box aspect (w/h) */
    float iw = (float)im->w, ih = (float)im->h;
    float cw = iw, ch = ih;
    if (iw / ih > aspect) cw = ih * aspect; else ch = iw / aspect;
    float x0 = (iw - cw) / 2, y0 = (ih - ch) / 2;
    *u0 = x0 / im->tex->w; *u1 = (x0 + cw) / im->tex->w;
    *v0 = y0 / im->tex->h; *v1 = (y0 + ch) / im->tex->h;
}

static int game_backdrop(const game_t *g, image_t *im)
{
    if (cfg.backdrop == BACKDROP_SNAP && g->backdrop[0]) {
        if (image_get(g->backdrop, IMG_BACKDROP, im)) return 1;
        if (!image_failed(g->backdrop, IMG_BACKDROP)) return 0;
    }
    if (g->cover[0] && image_get(g->cover, IMG_BACKDROP, im)) return 2;
    return 0;
}

static void draw_backdrop_layer(int gi, float alpha)
{
    if (gi < 0 || gi >= lib.count || alpha <= 0) return;
    const game_t *g = &lib.games[gi];
    const float AX = 360, AW = SCREEN_W - 360, AH = SCREEN_H;
    image_t im;
    int kind = game_backdrop(g, &im);
    gfx_push_alpha(alpha);
    if (kind) {
        float u0, v0, u1, v1;
        cover_uv(&im, AW / AH, &u0, &v0, &u1, &v1);
        rgba_t tint = kind == 2 ? RGBA(150, 150, 160, 255) : C_WHITE;
        gfx_image(im.tex, AX, 0, AW, AH, u0, v0, u1, v1, tint);
    } else {
        rgba_t c = g->color;
        gfx_rect4(AX, 0, AW, AH, col_lerp(c, C_BG, 0.55f), col_lerp(c, C_BG, 0.2f),
                  col_lerp(c, C_BG, 0.5f), col_lerp(c, C_BG, 0.8f));
    }
    gfx_pop_alpha();
}

static void draw_shades(void)
{
    rgba_t b = C_BG;
    /* left: solid until the picture starts, then fade out */
    gfx_rect(0, 0, 360, SCREEN_H, b);
    gfx_rect4(360, 0, 120, SCREEN_H, col_alpha(b, 1), col_alpha(b, 0.92f), col_alpha(b, 0.92f), col_alpha(b, 1));
    gfx_rect4(480, 0, 580, SCREEN_H, col_alpha(b, 0.92f), col_alpha(b, 0.35f), col_alpha(b, 0.35f), col_alpha(b, 0.92f));
    gfx_rect4(1060, 0, 860, SCREEN_H, col_alpha(b, 0.35f), col_alpha(b, 0.06f), col_alpha(b, 0.06f), col_alpha(b, 0.35f));
    /* bottom */
    gfx_rect4(0, 560, SCREEN_W, 260, col_alpha(b, 0), col_alpha(b, 0), col_alpha(b, 0.6f), col_alpha(b, 0.6f));
    gfx_rect4(0, 820, SCREEN_W, 260, col_alpha(b, 0.6f), col_alpha(b, 0.6f), col_alpha(b, 1), col_alpha(b, 1));
    /* top */
    gfx_rect4(0, 0, SCREEN_W, 320, col_alpha(b, 0.85f), col_alpha(b, 0.85f), col_alpha(b, 0), col_alpha(b, 0));
}

static void draw_topbar(const pad_t *pad)
{
    (void)pad;
    const char *tabs[2] = {"Games", "Settings"};
    float x = 96;
    for (int i = 0; i < 2; i++) {
        float w = text_width(fonts[F_TAB], tabs[i], 0);
        int active = (screen == SCR_HOME && i == 0) || (screen == SCR_SETTINGS && i == 1);
        if (focus == FOC_TABS && screen == SCR_HOME && i == tab) {
            gfx_rrect(x - 22, 34, w + 44, 58, 29, RGBA(255, 255, 255, 40));
            gfx_rrect_stroke(x - 22, 34, w + 44, 58, 29, 3, C_WHITE);
        }
        text_draw(fonts[F_TAB], x, 46, tabs[i], active ? C_WHITE : C_DIM, 0, ALIGN_LEFT);
        x += w + 44;
    }
    if (cfg.show_clock) {
        char t[32];
        clock_text(t, sizeof(t));
        text_draw(fonts[F_CLOCK], SCREEN_W - 80, 48, t, C_WHITE, 0, ALIGN_RIGHT);
    }
}

static void draw_tile(int i, float x, float y, float w, float alpha)
{
    const game_t *g = &lib.games[i];
    float r = w > 200 ? 26 : 22;
    image_t im;
    gfx_push_alpha(alpha);
    gfx_shadow(x + 4, y + 10, w - 8, w - 8, r, 18, RGBA(0, 0, 0, 90));
    if (g->cover[0] && image_get(g->cover, IMG_COVER, &im)) {
        float u0, v0, u1, v1;
        cover_uv(&im, 1.f, &u0, &v0, &u1, &v1);
        gfx_image_rr(im.tex, x, y, w, w, r, u0, v0, u1, v1, C_WHITE);
    } else {
        gfx_rrect_vgrad(x, y, w, w, r, col_lerp(g->color, C_WHITE, 0.08f), col_lerp(g->color, C_BG, 0.55f));
        font_t *f = w > 200 ? fonts[F_TILE] : fonts[F_TILE_S];
        float pad = w > 200 ? 18 : 12;
        float lh = font_line_h(f) * 0.92f;
        int lines = 3;
        float ty = y + w - pad - lh * lines;
        text_draw_wrap(f, x + pad, ty, g->title, C_WHITE, w - pad * 2, lh, lines);
    }
    gfx_pop_alpha();
}

static void draw_row(void)
{
    const float Y = 136, X0 = 96, GAP = 22;
    if (!lib.count) return;
    /* raw x positions from animated widths */
    float x = 0, xsel = 0;
    for (int i = 0; i < lib.count; i++) {
        if (i == sel) xsel = x;
        x += tile_w[i] + GAP;
    }
    (void)xsel;
    x = X0 - scroll;
    for (int i = 0; i < lib.count; i++) {
        float w = tile_w[i];
        if (x > SCREEN_W) break;
        if (x + w > -40) {
            float a = 1.f;
            if (x < X0 - 10) a = clampf((x + w - 30) / (X0 + 60), 0, 1);
            if (a > 0.01f) draw_tile(i, x, Y, w, a);
            if (i == sel && focus == FOC_TILES) gfx_rrect_stroke(x - 6, Y - 6, w + 12, w + 12, 32, 4, C_WHITE);
        }
        x += w + GAP;
    }
    /* preload covers just outside the screen */
    image_t tmp;
    for (int d = 1; d <= 8; d++) {
        int j = sel + d;
        if (j < lib.count && lib.games[j].cover[0]) image_get(lib.games[j].cover, IMG_COVER, &tmp);
    }
    text_draw_fit(fonts[F_SELTITLE], X0, Y + 236 + 24, lib.games[sel].title, C_WHITE, 900, ALIGN_LEFT);
}

static void draw_hero(const pad_t *pad)
{
    const game_t *g = &lib.games[sel];
    float y = 452;
    font_t *f = fonts[F_HERO];
    float maxw = 1000;
    float tw = text_width(f, g->title, -2);
    if (tw > maxw * 1.85f) f = fonts[F_HERO_S];
    float lh = font_line_h(f) * 0.86f;
    /* reveal animation when the selection changes */
    float t = ease_out_cubic((float)(now_ms - sel_ms) / 350.f);
    gfx_push_alpha(t);
    float dy = (1 - t) * 18;
    int lines = text_draw_wrap(f, 92, y + dy, g->title, C_WHITE, maxw, lh, 2);
    y += lines * lh + 18;

    char meta[256], sz[32];
    fmt_size(g->size, sz, sizeof(sz));
    snprintf(meta, sizeof(meta), "Super Nintendo%s%s   ·   %s", g->region[0] ? "   ·   " : "", g->region, sz);
    text_draw(fonts[F_META], 96, y + dy, meta, C_DIM, 0, ALIGN_LEFT);
    y += 64;

    /* buttons */
    const char *play = "Play";
    float pw = text_width(fonts[F_BTN], play, 0) + 184;
    int foc_play = focus == FOC_BUTTONS && btn_sel == 0;
    int foc_more = focus == FOC_BUTTONS && btn_sel == 1;
    gfx_rrect(96, y, pw, 76, 38, C_WHITE);
    if (foc_play) gfx_rrect_stroke(90, y - 6, pw + 12, 88, 44, 4, C_WHITE);
    text_draw(fonts[F_BTN], 96 + pw / 2, y + 19, play, RGBA(0, 0, 0, 255), 0, ALIGN_CENTER);
    float mx = 96 + pw + 18;
    gfx_circle(mx + 37, y + 38, 37, foc_more ? RGBA(255, 255, 255, 70) : RGBA(255, 255, 255, 36));
    if (foc_more) gfx_rrect_stroke(mx - 6, y - 6, 86, 86, 43, 4, C_WHITE);
    for (int k = -1; k <= 1; k++) gfx_circle(mx + 37 + k * 12, y + 38, 3.5f, C_WHITE);
    gfx_pop_alpha();
    (void)pad;
}

static void draw_card(float x, float y, float w, float h, const char *label)
{
    gfx_rrect(x, y, w, h, 22, C_CARD);
    gfx_rrect_stroke(x, y, w, h, 22, 1, RGBA(255, 255, 255, 22));
    text_draw(fonts[F_LABEL], x + 24, y + 22, label, C_DIM, 1.5f, ALIGN_LEFT);
}

static void draw_chip(float x, float y, const char *icon, const char *label, int on)
{
    float w = text_width(fonts[F_SUB], label, 0) + 66;
    gfx_rrect(x, y, w, 42, 21, on ? RGBA(52, 211, 153, 40) : RGBA(255, 255, 255, 16));
    draw_icon(on ? "check" : icon, x + 12, y + 9, 24, on ? C_OK : C_FAINT);
    text_draw(fonts[F_SUB], x + 44, y + 9, label, on ? C_WHITE : C_FAINT, 0, ALIGN_LEFT);
}

static void draw_cards(void)
{
    const game_t *g = &lib.games[sel];
    const float y = 852, h = 180, w = 380;
    float x = 96;
    draw_card(x, y, w + 110, h, "ARTWORK");
    float cx = x + 24;
    draw_chip(cx, y + 64, "image", "Cover", g->cover[0] != 0);
    cx += text_width(fonts[F_SUB], "Cover", 0) + 66 + 10;
    draw_chip(cx, y + 64, "image", "Snapshot", g->backdrop[0] != 0);
    cx += text_width(fonts[F_SUB], "Snapshot", 0) + 66 + 10;
    draw_chip(cx, y + 64, "film", "Video", g->video[0] != 0);
    text_draw_fit(fonts[F_SUB], x + 24, y + 124, (g->cover[0] && g->backdrop[0]) ? "Artwork matched by file name" : "Add art in art/boxart, art/snap, art/video", C_FAINT, w + 62, ALIGN_LEFT);
    x += w + 110 + 22;

    draw_card(x, y, w, h, "GAME FILE");
    char sz[32];
    fmt_size(g->size, sz, sizeof(sz));
    text_draw(fonts[F_VALUE], x + 24, y + 62, sz, C_WHITE, 0, ALIGN_LEFT);
    const char *fn = strrchr(g->path, '/');
    text_draw_fit(fonts[F_SUB], x + 24, y + 112, fn ? fn + 1 : g->path, C_DIM, w - 48, ALIGN_LEFT);
    x += w + 22;

    draw_card(x, y, w, h, "LOCATION");
    int usb = !strcmp(g->where, "USB");
    draw_icon(usb ? "usb" : "hdd", x + 24, y + 62, 34, C_WHITE);
    text_draw(fonts[F_VALUE], x + 70, y + 62, usb ? "USB Drive" : "System Storage", C_WHITE, 0, ALIGN_LEFT);
    char dir[512];
    snprintf(dir, sizeof(dir), "%s", g->path);
    char *sl = strrchr(dir, '/');
    if (sl) *sl = 0;
    text_draw_fit(fonts[F_SUB], x + 24, y + 112, dir, C_DIM, w - 48, ALIGN_LEFT);
}

static void draw_home_hints(const pad_t *pad)
{
    const char *labels[2] = {"Play", "Settings"};
    float w = hints_width(labels, 2);
    float x = SCREEN_W - 80 - w, y = 1000;
    x += draw_hint(x, y, confirm_icon(pad), labels[0]);
    draw_hint(x, y, "btn_start", labels[1]);
}

static void draw_empty(const pad_t *pad)
{
    gfx_rect4(0, 0, SCREEN_W, SCREEN_H, RGBA(27, 33, 64, 255), RGBA(12, 15, 30, 255), C_BG, RGBA(10, 12, 22, 255));
    draw_topbar(pad);
    float x = 96, y = 190;
    text_draw(fonts[F_HERO_S], x - 3, y, "Add your games", C_WHITE, -1, ALIGN_LEFT);
    y += 110;
    text_draw(fonts[F_ROW], x, y, "Copy SNES ROM files (.sfc .smc .fig .swc .bs) into one of these folders:", C_DIM, 0, ALIGN_LEFT);
    y += 60;
    for (int i = 0; i < lib.ndirs; i++) {
        gfx_rrect(x, y, 1100, 64, 16, C_ROW);
        draw_icon(lib.dir_found[i] ? "check" : "folder", x + 20, y + 16, 32, lib.dir_found[i] ? C_OK : C_FAINT);
        text_draw(fonts[F_ROW], x + 70, y + 15, lib.dirs[i], C_WHITE, 0, ALIGN_LEFT);
        text_draw(fonts[F_SUB], x + 1080, y + 20, lib.dir_found[i] ? "Folder found" : "Not found", C_FAINT, 0, ALIGN_RIGHT);
        y += 76;
    }
    y += 30;
    char line[600];
    text_draw(fonts[F_ROW], x, y, "Artwork, named exactly like the ROM file:", C_DIM, 0, ALIGN_LEFT);
    y += 50;
    snprintf(line, sizeof(line), "Covers  %s/art/boxart/<rom name>.png", plat_data_dir());
    text_draw(fonts[F_SUB], x, y, line, C_WHITE, 0, ALIGN_LEFT); y += 38;
    snprintf(line, sizeof(line), "Backgrounds  %s/art/snap/<rom name>.png", plat_data_dir());
    text_draw(fonts[F_SUB], x, y, line, C_WHITE, 0, ALIGN_LEFT); y += 38;
    snprintf(line, sizeof(line), "Preview videos  %s/art/video/<rom name>.ogv", plat_data_dir());
    text_draw(fonts[F_SUB], x, y, line, C_WHITE, 0, ALIGN_LEFT);

    const char *labels[2] = {"Scan Again", "Settings"};
    float w = hints_width(labels, 2);
    float hx = SCREEN_W - 80 - w;
    hx += draw_hint(hx, 1000, confirm_icon(pad), labels[0]);
    draw_hint(hx, 1000, "btn_start", labels[1]);
}

static void home_input(const pad_t *pad)
{
    uint32_t p = pad->pressed;
    if (p & BTN_START) { go_screen(SCR_SETTINGS); return; }
    if (!lib.count) {
        if (p & confirm_btn(pad)) act_rescan();
        return;
    }
    if (focus == FOC_TABS) {
        if (p & BTN_LEFT) tab = 0;
        if (p & BTN_RIGHT) tab = 1;
        if (p & BTN_DOWN) focus = FOC_TILES;
        if (p & confirm_btn(pad)) { if (tab == 1) go_screen(SCR_SETTINGS); else focus = FOC_TILES; }
        return;
    }
    if (focus == FOC_BUTTONS) {
        if (p & BTN_LEFT) btn_sel = 0;
        if (p & BTN_RIGHT) btn_sel = 1;
        if (p & (BTN_UP | back_btn(pad))) focus = FOC_TILES;
        if (p & confirm_btn(pad)) {
            if (btn_sel == 0) ui_toast("gamepad", "Coming in the next update", "Games will start from here once the emulator core is added.");
            else ui_toast("info", lib.games[sel].title, lib.games[sel].path);
        }
        return;
    }
    int old = sel;
    if (p & BTN_RIGHT) sel++;
    if (p & BTN_LEFT) sel--;
    if (p & BTN_R1) sel += 6;
    if (p & BTN_L1) sel -= 6;
    if (sel < 0) sel = 0;
    if (sel >= lib.count) sel = lib.count - 1;
    if (sel != old) sel_ms = now_ms;
    if (p & BTN_UP) { focus = FOC_TABS; tab = 0; }
    if (p & BTN_DOWN) { focus = FOC_BUTTONS; btn_sel = 0; }
    if (p & confirm_btn(pad)) ui_toast("gamepad", "Coming in the next update", "Games will start from here once the emulator core is added.");
}

static void home_update(float dt)
{
    if (!lib.count) return;
    for (int i = 0; i < lib.count; i++) tile_w[i] = approach(tile_w[i], i == sel ? 236.f : 150.f, 16.f, dt);
    float xsel = 0;
    for (int i = 0; i < sel; i++) xsel += tile_w[i] + 22;
    scroll = approach(scroll, xsel, 14.f, dt);
    /* backdrop crossfade */
    if (bg_cur != sel) {
        if (bg_cur < 0) { bg_cur = sel; bg_mix = 1; }
        else { bg_prev = bg_mix > 0.5f ? bg_cur : bg_prev; bg_cur = sel; bg_mix = 0; }
    }
    bg_mix = clampf(bg_mix + dt / 0.45f, 0, 1);
    if (bg_mix >= 1) bg_prev = -1;
}

static void draw_home(const pad_t *pad)
{
    if (!lib.count) { draw_empty(pad); return; }
    if (bg_prev >= 0) draw_backdrop_layer(bg_prev, 1.f);
    draw_backdrop_layer(bg_cur, bg_prev >= 0 ? ease_out_cubic(bg_mix) : 1.f);
    draw_shades();
    draw_topbar(pad);
    draw_row();
    draw_hero(pad);
    draw_cards();
    draw_home_hints(pad);
}

/* ---------- settings ---------- */
static void settings_input(const pad_t *pad)
{
    uint32_t p = pad->pressed;
    cat_t *c = &cats[set_cat];
    if (!set_focus_panel) {
        if (p & BTN_UP && set_cat > 0) set_cat--;
        if (p & BTN_DOWN && set_cat < ncats - 1) set_cat++;
        if (p & (BTN_RIGHT | confirm_btn(pad))) { set_focus_panel = 1; set_row = 0; }
        if (p & (back_btn(pad) | BTN_START)) { config_save(); go_screen(SCR_HOME); }
        return;
    }
    if (p & BTN_UP && set_row > 0) set_row--;
    if (p & BTN_DOWN && set_row < c->nrows - 1) set_row++;
    if (p & back_btn(pad)) { set_focus_panel = 0; return; }
    if (p & BTN_START) { config_save(); go_screen(SCR_HOME); return; }
    row_t *r = &c->rows[set_row];
    int dir = 0;
    if (p & BTN_RIGHT) dir = 1;
    if (p & BTN_LEFT) { if (r->type == ROW_OPTION || r->type == ROW_TOGGLE) dir = -1; else { set_focus_panel = 0; return; } }
    if (p & confirm_btn(pad)) dir = 2;
    if (!dir) return;
    if ((r->type == ROW_OPTION || r->type == ROW_TOGGLE) && r->val) {
        int step = dir == -1 ? -1 : 1;
        *r->val = (*r->val + step + r->nopts) % r->nopts;
        config_save();
        if (r->val == &cfg.sort) { load_library(); refresh_library_rows(); }
        if (r->val == &cfg.backdrop) { bg_prev = -1; }
    } else if (r->type == ROW_ACTION && r->action && dir == 2) {
        r->action();
        refresh_library_rows();
    }
}

static void draw_switch(float x, float y, int on)
{
    gfx_rrect(x, y, 76, 42, 21, on ? C_ACCENT : RGBA(255, 255, 255, 50));
    gfx_circle(on ? x + 55 : x + 21, y + 21, 16, C_WHITE);
}

static void draw_settings(const pad_t *pad)
{
    gfx_rect4(0, 0, SCREEN_W, SCREEN_H, RGBA(10, 12, 20, 255), RGBA(27, 33, 64, 255), RGBA(10, 12, 20, 255), RGBA(8, 10, 16, 255));
    draw_topbar(pad);
    text_draw(fonts[F_H1], 96, 120, "Settings", C_WHITE, 0, ALIGN_LEFT);

    float y = 220;
    for (int i = 0; i < ncats; i++) {
        int on = i == set_cat;
        rgba_t tc = C_DIM;
        if (on && !set_focus_panel) { gfx_rrect(96, y, 520, 74, 18, C_WHITE); tc = RGBA(0, 0, 0, 255); }
        else if (on) { gfx_rrect(96, y, 520, 74, 18, RGBA(255, 255, 255, 30)); tc = C_WHITE; }
        draw_icon(cats[i].icon, 122, y + 20, 34, tc);
        text_draw(fonts[F_ROW], 178, y + 20, cats[i].name, tc, 0, ALIGN_LEFT);
        y += 80;
    }

    cat_t *c = &cats[set_cat];
    float px = 680, pw = 1144;
    y = 220;
    for (int i = 0; i < c->nrows; i++) {
        row_t *r = &c->rows[i];
        int has_desc = r->desc != NULL;
        float h = has_desc ? 116 : 88;
        int foc = set_focus_panel && i == set_row;
        gfx_rrect(px, y, pw, h, 18, foc ? C_ROW_FOCUS : C_ROW);
        if (foc) gfx_rrect_stroke(px, y, pw, h, 18, 3, RGBA(255, 255, 255, 220));
        float ty = has_desc ? y + 24 : y + 28;
        float value_w = 0;
        if (r->type == ROW_TOGGLE && r->val) {
            draw_switch(px + pw - 110, y + h / 2 - 21, *r->val);
            value_w = 120;
        } else if (r->type == ROW_OPTION && r->val) {
            draw_icon("chevron", px + pw - 60, y + h / 2 - 15, 30, C_DIM);
            float w = text_draw(fonts[F_ROW], px + pw - 70, y + h / 2 - 16, r->opts[*r->val], C_DIM, 0, ALIGN_RIGHT);
            value_w = w + 90;
        } else if (r->type == ROW_ACTION) {
            draw_icon("chevron", px + pw - 60, y + h / 2 - 15, 30, C_DIM);
            value_w = 70;
        } else if (r->type == ROW_INFO && r->info[0]) {
            float w = text_draw(fonts[F_ROW], px + pw - 34, y + h / 2 - 16, r->info, C_DIM, 0, ALIGN_RIGHT);
            value_w = w + 60;
        }
        text_draw_fit(fonts[F_ROW], px + 34, ty, r->label, C_WHITE, pw - 68 - value_w, ALIGN_LEFT);
        if (has_desc) text_draw_fit(fonts[F_ROW_DESC], px + 34, ty + 40, r->desc, C_FAINT, pw - 68 - value_w, ALIGN_LEFT);
        y += h + 12;
    }

    float hx = 96, hy = 1000;
    hx += draw_hint(hx, hy, confirm_icon(pad), set_focus_panel ? "Change" : "Select");
    hx += draw_hint(hx, hy, back_icon(pad), set_focus_panel ? "Back" : "Home");
}

/* ---------- toast ---------- */
static void draw_toast(void)
{
    if (!toast.on) return;
    float t = (float)(now_ms - toast.t0) / 1000.f;
    const float life = 4.0f;
    if (t > life) { toast.on = 0; return; }
    float in = ease_out_cubic(t / 0.35f), out = ease_out_cubic((life - t) / 0.35f);
    float a = in < out ? in : out;
    float w = 640, h = 136;
    float x = SCREEN_W - 80 - w + (1 - a) * 60, y = 110;
    gfx_push_alpha(a);
    gfx_shadow(x, y + 8, w, h, 24, 22, RGBA(0, 0, 0, 120));
    gfx_rrect(x, y, w, h, 24, RGBA(38, 42, 58, 245));
    gfx_rrect_stroke(x, y, w, h, 24, 1, RGBA(255, 255, 255, 30));
    gfx_circle(x + 56, y + h / 2, 32, RGBA(255, 255, 255, 30));
    draw_icon(toast.icon, x + 38, y + h / 2 - 18, 36, C_WHITE);
    text_draw_fit(fonts[F_TOAST_T], x + 106, y + 20, toast.title, C_WHITE, w - 134, ALIGN_LEFT);
    text_draw_wrap(fonts[F_TOAST_B], x + 106, y + 56, toast.body, C_DIM, w - 134, 30, 2);
    gfx_pop_alpha();
}

/* ---------- frame ---------- */
void ui_frame(const pad_t *pad, float dt)
{
    now_ms = plat_ms();
    images_update();

    if (screen_next >= 0) {
        fade -= dt / 0.18f;
        if (fade <= 0) {
            fade = 0;
            if (screen_next == SCR_SETTINGS) { refresh_library_rows(); set_focus_panel = 0; }
            if (screen_next == SCR_HOME) focus = FOC_TILES;
            screen = screen_next;
            screen_next = -1;
        }
    } else {
        if (fade < 1) fade = clampf(fade + dt / 0.22f, 0, 1);
        if (screen == SCR_HOME) home_input(pad); else settings_input(pad);
    }
    if (screen == SCR_HOME) home_update(dt);
    if (lib.count && cfg.last_game != sel) cfg.last_game = sel; /* saved with settings / on exit */

    plat_begin_frame(C_BG);
    gfx_begin();
    gfx_push_alpha(ease_out_cubic(fade));
    if (screen == SCR_HOME) draw_home(pad); else draw_settings(pad);
    gfx_pop_alpha();
    draw_toast();
    gfx_flush();
    plat_end_frame();
}
