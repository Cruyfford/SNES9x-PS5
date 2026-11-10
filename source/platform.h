/* Platform layer: everything that differs between the PS3 build (tiny3d/PSL1GHT)
 * and the host preview build (software rasterizer that writes PNG screenshots). */
#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>
#include <stddef.h>

/* Virtual canvas. All UI code draws in this space; the platform scales it
 * to the real video mode (720p / 1080p / 480p). */
#define SCREEN_W 1920
#define SCREEN_H 1080

typedef uint32_t rgba_t; /* 0xRRGGBBAA */

typedef struct {
    float x, y;   /* virtual screen coordinates */
    float u, v;   /* texture coordinates 0..1 (ignored without texture) */
    rgba_t c;     /* vertex colour; multiplies the texture */
} vtx_t;

/* A texture lives in GPU-visible memory. Pixels are 0xAARRGGBB in memory
 * order A,R,G,B (big-endian u32), which is what RSX expects for A8R8G8B8. */
typedef struct {
    int w, h, pitch;   /* pitch in bytes */
    uint8_t *pixels;   /* CPU pointer into GPU memory */
    uint32_t offset;   /* RSX offset (PS3 only) */
    int smooth;        /* bilinear filtering */
} tex_t;

/* buttons */
enum {
    BTN_UP = 1 << 0, BTN_DOWN = 1 << 1, BTN_LEFT = 1 << 2, BTN_RIGHT = 1 << 3,
    BTN_CROSS = 1 << 4, BTN_CIRCLE = 1 << 5, BTN_SQUARE = 1 << 6, BTN_TRIANGLE = 1 << 7,
    BTN_L1 = 1 << 8, BTN_R1 = 1 << 9, BTN_L2 = 1 << 10, BTN_R2 = 1 << 11,
    BTN_L3 = 1 << 12, BTN_R3 = 1 << 13, BTN_START = 1 << 14, BTN_SELECT = 1 << 15,
};

typedef struct {
    uint32_t held;      /* currently down */
    uint32_t pressed;   /* went down this frame, plus auto-repeat for d-pad */
    int cross_is_enter; /* 1 = Cross confirms (Western), 0 = Circle confirms (Japan) */
} pad_t;

typedef struct {
    int year, month, day, hour, minute, second;
} clock_t2;

int  plat_init(void);
void plat_shutdown(void);
int  plat_running(void);              /* 0 once the user quit from the XMB */
void plat_begin_frame(rgba_t clear);
void plat_end_frame(void);
void plat_read_pad(pad_t *pad);
uint64_t plat_ms(void);               /* monotonic milliseconds */
void plat_local_time(clock_t2 *out);
int  plat_use_24h(void);

/* paths */
const char *plat_app_dir(void);       /* folder holding fonts/, icons/ ... */
const char *plat_data_dir(void);      /* folder holding roms/, art/, config */
int  plat_extra_rom_dirs(const char ***dirs); /* USB folders etc. */

/* GPU memory for textures (never freed individually; the UI uses slots) */
int  plat_tex_alloc(tex_t *t, int w, int h);
/* wait for the GPU to finish before overwriting a texture it may still read */
void plat_gpu_sync(void);

/* draw an indexed-free triangle list (n multiple of 3) */
void plat_draw_tris(const vtx_t *v, int n, const tex_t *tex);

/* threads (image loading runs off the UI thread) */
typedef void (*plat_thread_fn)(void *arg);
int  plat_thread_start(plat_thread_fn fn, void *arg);
typedef struct plat_mutex plat_mutex;
plat_mutex *plat_mutex_new(void);
void plat_mutex_lock(plat_mutex *m);
void plat_mutex_unlock(plat_mutex *m);
void plat_sleep_ms(int ms);

void plat_log(const char *fmt, ...);

#endif
