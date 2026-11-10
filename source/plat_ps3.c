/* PS3 platform layer: tiny3d (RSX) rendering, pad input, sysutil, threads. */
#ifdef __PS3__

#include "platform.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <tiny3d.h>
#include <rsx/rsx.h>
#include <io/pad.h>
#include <sysutil/sysutil.h>
#include <sys/thread.h>
#include <sys/mutex.h>
#include <sys/systime.h>
#include <lv2/systime.h>
#include <lv2/thread.h>
#include <sysmodule/sysmodule.h>

#define APP_ID "SNES95000"
#define APP_DIR "/dev_hdd0/game/" APP_ID "/USRDIR"

#define TEX_POOL_SIZE (96 * 1024 * 1024)

static int running = 1;
static uint8_t *tex_pool;
static size_t tex_used;
static float sx, sy;

static void sys_cb(u64 status, u64 param, void *user)
{
    (void)param; (void)user;
    if (status == SYSUTIL_EXIT_GAME) running = 0;
}

int plat_init(void)
{
    tiny3d_Init(4 * 1024 * 1024);
    sx = (float)Video_Resolution.width / (float)SCREEN_W;
    sy = (float)Video_Resolution.height / (float)SCREEN_H;
    /* 2D virtual space = 1920x1080 at every output resolution */
    tiny3d_UserViewport(1, 0.0f, 0.0f, sx, sy, sx, sy);
    ioPadInit(7);
    sysUtilRegisterCallback(SYSUTIL_EVENT_SLOT0, sys_cb, NULL);
    tex_pool = (uint8_t *)tiny3d_AllocTexture(TEX_POOL_SIZE);
    tex_used = 0;
    if (!tex_pool) return -1;
    return 0;
}

void plat_shutdown(void)
{
    ioPadEnd();
}

int plat_running(void) { return running; }

void plat_begin_frame(rgba_t clear)
{
    /* tiny3d clear colour is 0xAARRGGBB */
    uint32_t argb = ((clear & 0xff) << 24) | (clear >> 8);
    tiny3d_Clear(argb, TINY3D_CLEAR_ALL);
    tiny3d_AlphaTest(1, 0x00, TINY3D_ALPHA_FUNC_GREATER);
    tiny3d_BlendFunc(1,
        TINY3D_BLEND_FUNC_SRC_RGB_SRC_ALPHA | TINY3D_BLEND_FUNC_SRC_ALPHA_SRC_ALPHA,
        TINY3D_BLEND_FUNC_SRC_ALPHA_ONE_MINUS_SRC_ALPHA | TINY3D_BLEND_FUNC_SRC_RGB_ONE_MINUS_SRC_ALPHA,
        TINY3D_BLEND_RGB_FUNC_ADD | TINY3D_BLEND_ALPHA_FUNC_ADD);
    tiny3d_Project2D();
}

void plat_end_frame(void)
{
    tiny3d_Flip();
    sysUtilCheckCallback();
}

void plat_gpu_sync(void)
{
    static u32 ref = 0x100;
    rsxFinish((gcmContextData *)tiny3d_Get_GCM_Context(), ++ref);
}

/* ---- input ---- */
static uint32_t prev_held;
static uint64_t repeat_at;
static int cross_enter = -1;

void plat_read_pad(pad_t *pad)
{
    padInfo info;
    padData d;
    uint32_t held = 0;

    if (cross_enter < 0) {
        s32 v = 1;
        if (sysUtilGetSystemParamInt(SYSUTIL_SYSTEMPARAM_ID_ENTER_BUTTON_ASSIGN, &v) != 0) v = 1;
        cross_enter = v ? 1 : 0;
    }

    ioPadGetInfo(&info);
    for (int i = 0; i < MAX_PADS; i++) {
        if (!info.status[i]) continue;
        memset(&d, 0, sizeof(d));
        if (ioPadGetData(i, &d) != 0 || d.len == 0) {
            /* no new data: keep previous state for this frame */
            held |= prev_held;
            continue;
        }
        if (d.BTN_UP) held |= BTN_UP;
        if (d.BTN_DOWN) held |= BTN_DOWN;
        if (d.BTN_LEFT) held |= BTN_LEFT;
        if (d.BTN_RIGHT) held |= BTN_RIGHT;
        if (d.BTN_CROSS) held |= BTN_CROSS;
        if (d.BTN_CIRCLE) held |= BTN_CIRCLE;
        if (d.BTN_SQUARE) held |= BTN_SQUARE;
        if (d.BTN_TRIANGLE) held |= BTN_TRIANGLE;
        if (d.BTN_L1) held |= BTN_L1;
        if (d.BTN_R1) held |= BTN_R1;
        if (d.BTN_L2) held |= BTN_L2;
        if (d.BTN_R2) held |= BTN_R2;
        if (d.BTN_L3) held |= BTN_L3;
        if (d.BTN_R3) held |= BTN_R3;
        if (d.BTN_START) held |= BTN_START;
        if (d.BTN_SELECT) held |= BTN_SELECT;
        /* left stick acts as d-pad */
        if (d.ANA_L_H < 40) held |= BTN_LEFT;
        if (d.ANA_L_H > 215) held |= BTN_RIGHT;
        if (d.ANA_L_V < 40) held |= BTN_UP;
        if (d.ANA_L_V > 215) held |= BTN_DOWN;
        break; /* first connected pad drives the menu */
    }

    uint32_t pressed = held & ~prev_held;
    uint64_t now = plat_ms();
    const uint32_t dirs = BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT | BTN_L1 | BTN_R1;
    if (pressed & dirs) repeat_at = now + 380;
    else if ((held & dirs) && now >= repeat_at) {
        pressed |= held & dirs;
        repeat_at = now + 75;
    }
    prev_held = held;

    pad->held = held;
    pad->pressed = pressed;
    pad->cross_is_enter = cross_enter;
}

/* ---- time ---- */
uint64_t plat_ms(void)
{
    return (uint64_t)sysGetSystemTime() / 1000ULL;
}

void plat_local_time(clock_t2 *out)
{
    u64 sec = 0, nsec = 0;
    s32 tz = 0, dst = 0;
    sysGetCurrentTime(&sec, &nsec);
    sysUtilGetSystemParamInt(SYSUTIL_SYSTEMPARAM_ID_TIMEZONE, &tz);
    sysUtilGetSystemParamInt(SYSUTIL_SYSTEMPARAM_ID_SUMMERTIME, &dst);
    time_t t = (time_t)sec + (time_t)tz * 60 + (dst ? 3600 : 0);
    struct tm tmv;
    gmtime_r(&t, &tmv);
    out->year = tmv.tm_year + 1900; out->month = tmv.tm_mon + 1; out->day = tmv.tm_mday;
    out->hour = tmv.tm_hour; out->minute = tmv.tm_min; out->second = tmv.tm_sec;
}

int plat_use_24h(void)
{
    s32 v = 0;
    if (sysUtilGetSystemParamInt(SYSUTIL_SYSTEMPARAM_ID_TIME_FORMAT, &v) != 0) return 0;
    return v == 1; /* 0 = 12h, 1 = 24h */
}

/* ---- paths ---- */
const char *plat_app_dir(void) { return APP_DIR; }
const char *plat_data_dir(void) { return APP_DIR; }

static const char *usb_dirs[] = {
    "/dev_usb000/SNES", "/dev_usb001/SNES", "/dev_usb006/SNES",
    "/dev_hdd0/ROMS/SNES", "/dev_hdd0/SNES",
};
int plat_extra_rom_dirs(const char ***dirs)
{
    *dirs = usb_dirs;
    return (int)(sizeof(usb_dirs) / sizeof(usb_dirs[0]));
}

/* ---- textures ---- */
int plat_tex_alloc(tex_t *t, int w, int h)
{
    size_t pitch = (size_t)w * 4;
    size_t size = (pitch * (size_t)h + 127) & ~(size_t)127;
    if (tex_used + size > TEX_POOL_SIZE) return -1;
    t->w = w; t->h = h; t->pitch = (int)pitch;
    t->pixels = tex_pool + tex_used;
    t->offset = tiny3d_TextureOffset(t->pixels);
    t->smooth = 1;
    tex_used += size;
    memset(t->pixels, 0, pitch * (size_t)h);
    return 0;
}

void plat_draw_tris(const vtx_t *v, int n, const tex_t *tex)
{
    if (n < 3) return;
    if (tex)
        tiny3d_SetTexture(0, tex->offset, tex->w, tex->h, tex->pitch,
                          TINY3D_TEX_FORMAT_A8R8G8B8, tex->smooth ? TEXTURE_LINEAR : TEXTURE_NEAREST);
    tiny3d_SetPolygon(TINY3D_TRIANGLES);
    for (int i = 0; i < n; i++) {
        tiny3d_VertexPos(v[i].x, v[i].y, 0);
        tiny3d_VertexColor(v[i].c);
        if (tex) tiny3d_VertexTexture(v[i].u, v[i].v);
    }
    tiny3d_End();
}

/* ---- threads ---- */
typedef struct { plat_thread_fn fn; void *arg; } thread_start_t;

static void thread_entry(void *p)
{
    thread_start_t s = *(thread_start_t *)p;
    free(p);
    s.fn(s.arg);
    sysThreadExit(0);
}

int plat_thread_start(plat_thread_fn fn, void *arg)
{
    sys_ppu_thread_t id;
    thread_start_t *s = malloc(sizeof(*s));
    if (!s) return -1;
    s->fn = fn; s->arg = arg;
    if (sysThreadCreate(&id, thread_entry, s, 1200, 256 * 1024, 0, (char *)"loader") != 0) {
        free(s);
        return -1;
    }
    return 0;
}

struct plat_mutex { sys_mutex_t id; };

plat_mutex *plat_mutex_new(void)
{
    sys_mutex_attr_t attr;
    plat_mutex *m = malloc(sizeof(*m));
    if (!m) return NULL;
    sysMutexAttrInitialize(attr);
    if (sysMutexCreate(&m->id, &attr) != 0) { free(m); return NULL; }
    return m;
}
void plat_mutex_lock(plat_mutex *m) { sysMutexLock(m->id, 0); }
void plat_mutex_unlock(plat_mutex *m) { sysMutexUnlock(m->id); }
void plat_sleep_ms(int ms) { sysUsleep((u32)ms * 1000); }

static FILE *logf;
void plat_log(const char *fmt, ...)
{
    va_list ap;
    if (!logf) logf = fopen(APP_DIR "/log.txt", "w");
    if (!logf) return;
    va_start(ap, fmt);
    vfprintf(logf, fmt, ap);
    va_end(ap);
    fputc('\n', logf);
    fflush(logf);
}

#endif
