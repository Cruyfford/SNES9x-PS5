/* Host preview platform: renders the exact same UI with a small software
 * rasterizer and writes PNG screenshots. Input comes from a script so the
 * PS3 UI can be checked on a PC/CI machine without a console.
 *
 * Script (argv[1]): comma separated "frame:action" items, e.g.
 *   "30:shot=home.png,40:R,41:R,90:shot=next.png,120:quit"
 * actions: U D L R X O S T L1 R1 L2 R2 ST SE  shot=<file>  quit
 */
#ifndef __PS3__

#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>

#include "ext/stb_image_write.h"

static float fb[SCREEN_W * SCREEN_H * 3];
static uint64_t frame_no;
static int running = 1;
static const char *script = "";
static const char *app_dir = "assets";
static const char *data_dir = "testdata";

void host_set_script(const char *s) { script = s ? s : ""; }
void host_set_dirs(const char *app, const char *data) { app_dir = app; data_dir = data; }

int plat_init(void) { return 0; }
void plat_shutdown(void) {}
int plat_running(void) { return running; }

void plat_begin_frame(rgba_t c)
{
    float r = (c >> 24 & 255) / 255.f, g = (c >> 16 & 255) / 255.f, b = (c >> 8 & 255) / 255.f;
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) { fb[i*3] = r; fb[i*3+1] = g; fb[i*3+2] = b; }
}

static void save_png(const char *name)
{
    unsigned char *out = malloc(SCREEN_W * SCREEN_H * 3);
    for (int i = 0; i < SCREEN_W * SCREEN_H * 3; i++) {
        float v = fb[i]; if (v < 0) v = 0; if (v > 1) v = 1;
        out[i] = (unsigned char)(v * 255.f + 0.5f);
    }
    stbi_write_png(name, SCREEN_W, SCREEN_H, 3, out, SCREEN_W * 3);
    free(out);
    printf("saved %s (frame %llu)\n", name, (unsigned long long)frame_no);
}

/* find script items for this frame */
static uint32_t script_buttons(int do_shots)
{
    uint32_t b = 0;
    const char *p = script;
    while (*p) {
        const char *end = strchr(p, ',');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        char item[256];
        if (len >= sizeof(item)) len = sizeof(item) - 1;
        memcpy(item, p, len); item[len] = 0;
        char *colon = strchr(item, ':');
        if (colon) {
            *colon = 0;
            uint64_t f = strtoull(item, NULL, 10);
            const char *a = colon + 1;
            if (f == frame_no) {
                if (do_shots) {
                    if (!strncmp(a, "shot=", 5)) save_png(a + 5);
                    else if (!strcmp(a, "quit")) running = 0;
                } else {
                    static const struct { const char *n; uint32_t b; } map[] = {
                        {"U", BTN_UP}, {"D", BTN_DOWN}, {"L", BTN_LEFT}, {"R", BTN_RIGHT},
                        {"X", BTN_CROSS}, {"O", BTN_CIRCLE}, {"S", BTN_SQUARE}, {"T", BTN_TRIANGLE},
                        {"L1", BTN_L1}, {"R1", BTN_R1}, {"L2", BTN_L2}, {"R2", BTN_R2},
                        {"ST", BTN_START}, {"SE", BTN_SELECT}, {"L3", BTN_L3}, {"R3", BTN_R3}};
                    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++)
                        if (!strcmp(a, map[i].n)) b |= map[i].b;
                }
            }
        }
        if (!end) break;
        p = end + 1;
    }
    return b;
}

void plat_end_frame(void)
{
    script_buttons(1);
    frame_no++;
    if (frame_no > 100000) running = 0;
}

void plat_gpu_sync(void) {}

void plat_read_pad(pad_t *pad)
{
    uint32_t b = script_buttons(0);
    static uint32_t hold_until[32];
    /* game buttons are held for 10 frames so the emulated game sees them */
    for (int i = 0; i < 32; i++) if (b & (1u << i)) hold_until[i] = (uint32_t)frame_no + 10;
    uint32_t held = 0;
    for (int i = 0; i < 32; i++) if (hold_until[i] > frame_no) held |= 1u << i;
    pad->held = held; pad->pressed = b; pad->cross_is_enter = 1;
    pad->port[0] = held; pad->port[1] = pad->port[2] = pad->port[3] = 0;
}

/* audio: count what would be played (800 frames per 60 Hz frame) */
static long audio_q;
static uint64_t audio_frame;
int plat_audio_init(void) { return 0; }
void plat_audio_write(const float *stereo, int frames) { (void)stereo; audio_q += frames; }
int plat_audio_buffered(void)
{
    while (audio_frame < frame_no) { audio_q -= AUDIO_RATE / 60; if (audio_q < 0) audio_q = 0; audio_frame++; }
    return (int)audio_q;
}
void plat_audio_clear(void) { audio_q = 0; }

uint64_t plat_ms(void) { return frame_no * 1000 / 60; }

void plat_local_time(clock_t2 *o)
{
    o->year = 2026; o->month = 11; o->day = 10; o->hour = 16; o->minute = 41; o->second = 0;
}
int plat_use_24h(void) { return 0; }
long plat_tz_offset(void) { return 0; }

const char *plat_app_dir(void) { return app_dir; }
const char *plat_data_dir(void) { return data_dir; }
int plat_extra_rom_dirs(const char ***dirs) { *dirs = NULL; return 0; }

int plat_tex_alloc(tex_t *t, int w, int h)
{
    t->w = w; t->h = h; t->pitch = w * 4; t->offset = 0; t->smooth = 1;
    t->pixels = calloc((size_t)w * h, 4);
    return t->pixels ? 0 : -1;
}

/* ---- rasterizer ---- */
static inline void sample(const tex_t *t, float u, float v, float out[4])
{
    if (!t->smooth) {
        int x = (int)(u * t->w), y = (int)(v * t->h);
        if (x < 0) x = 0; if (y < 0) y = 0; if (x >= t->w) x = t->w - 1; if (y >= t->h) y = t->h - 1;
        const uint8_t *p = t->pixels + y * t->pitch + x * 4;
        out[0] = p[1] / 255.f; out[1] = p[2] / 255.f; out[2] = p[3] / 255.f; out[3] = p[0] / 255.f;
        return;
    }
    float fx = u * t->w - 0.5f, fy = v * t->h - 0.5f;
    int x0 = (int)floorf(fx), y0 = (int)floorf(fy);
    float ax = fx - x0, ay = fy - y0;
    float acc[4] = {0, 0, 0, 0};
    for (int j = 0; j < 2; j++) for (int i = 0; i < 2; i++) {
        int x = x0 + i, y = y0 + j;
        if (x < 0) x = 0; if (y < 0) y = 0; if (x >= t->w) x = t->w - 1; if (y >= t->h) y = t->h - 1;
        float wgt = (i ? ax : 1 - ax) * (j ? ay : 1 - ay);
        const uint8_t *p = t->pixels + y * t->pitch + x * 4;
        acc[0] += wgt * p[1]; acc[1] += wgt * p[2]; acc[2] += wgt * p[3]; acc[3] += wgt * p[0];
    }
    for (int k = 0; k < 4; k++) out[k] = acc[k] / 255.f;
}

static void tri(const vtx_t *a, const vtx_t *b, const vtx_t *c, const tex_t *tex)
{
    float minx = fminf(a->x, fminf(b->x, c->x)), maxx = fmaxf(a->x, fmaxf(b->x, c->x));
    float miny = fminf(a->y, fminf(b->y, c->y)), maxy = fmaxf(a->y, fmaxf(b->y, c->y));
    int x0 = (int)floorf(minx), x1 = (int)ceilf(maxx), y0 = (int)floorf(miny), y1 = (int)ceilf(maxy);
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 > SCREEN_W) x1 = SCREEN_W; if (y1 > SCREEN_H) y1 = SCREEN_H;
    float area = (b->x - a->x) * (c->y - a->y) - (b->y - a->y) * (c->x - a->x);
    if (fabsf(area) < 1e-6f) return;
    float ca[4] = {(a->c>>24&255)/255.f,(a->c>>16&255)/255.f,(a->c>>8&255)/255.f,(a->c&255)/255.f};
    float cb[4] = {(b->c>>24&255)/255.f,(b->c>>16&255)/255.f,(b->c>>8&255)/255.f,(b->c&255)/255.f};
    float cc[4] = {(c->c>>24&255)/255.f,(c->c>>16&255)/255.f,(c->c>>8&255)/255.f,(c->c&255)/255.f};
    for (int y = y0; y < y1; y++) {
        float py = y + 0.5f;
        for (int x = x0; x < x1; x++) {
            float px = x + 0.5f;
            float w0 = ((b->x - px) * (c->y - py) - (b->y - py) * (c->x - px)) / area;
            float w1 = ((c->x - px) * (a->y - py) - (c->y - py) * (a->x - px)) / area;
            float w2 = 1.f - w0 - w1;
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            float col[4];
            for (int k = 0; k < 4; k++) col[k] = w0 * ca[k] + w1 * cb[k] + w2 * cc[k];
            if (tex) {
                float s[4];
                sample(tex, w0 * a->u + w1 * b->u + w2 * c->u, w0 * a->v + w1 * b->v + w2 * c->v, s);
                for (int k = 0; k < 4; k++) col[k] *= s[k];
            }
            float al = col[3];
            if (al <= 0) continue;
            float *d = fb + (y * SCREEN_W + x) * 3;
            d[0] = col[0] * al + d[0] * (1 - al);
            d[1] = col[1] * al + d[1] * (1 - al);
            d[2] = col[2] * al + d[2] * (1 - al);
        }
    }
}

void plat_draw_tris(const vtx_t *v, int n, const tex_t *tex)
{
    for (int i = 0; i + 2 < n; i += 3) tri(&v[i], &v[i+1], &v[i+2], tex);
}

/* ---- threads ---- */
typedef struct { plat_thread_fn fn; void *arg; } ts_t;
static void *th(void *p) { ts_t s = *(ts_t *)p; free(p); s.fn(s.arg); return NULL; }
int plat_thread_start(plat_thread_fn fn, void *arg)
{
    pthread_t t; ts_t *s = malloc(sizeof(*s)); s->fn = fn; s->arg = arg;
    if (pthread_create(&t, NULL, th, s)) { free(s); return -1; }
    pthread_detach(t);
    return 0;
}
struct plat_mutex { pthread_mutex_t m; };
plat_mutex *plat_mutex_new(void) { plat_mutex *m = malloc(sizeof(*m)); pthread_mutex_init(&m->m, NULL); return m; }
void plat_mutex_lock(plat_mutex *m) { pthread_mutex_lock(&m->m); }
void plat_mutex_unlock(plat_mutex *m) { pthread_mutex_unlock(&m->m); }
void plat_sleep_ms(int ms) { usleep(ms * 1000); }

void plat_log(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); fputc('\n', stderr);
}

#endif
