/* libretro front end for the statically linked snes9x2010 core. */
#include "emu.h"
#include "libretro.h"
#include "gfx.h"
#include "config.h"
#include "ext/stb_image_write.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define FRAME_W 512
#define FRAME_H 512

static int inited, loaded;
static char rom_base[256];
static char save_dir[300], sys_dir[300], shot_dir[300];
static void *rom_data;
static size_t rom_size;
static uint32_t sram_hash;

static tex_t frame_tex[2];
static int frame_idx = -1, frame_w, frame_h;
static int pal;
static double core_rate = 32040.0;
static uint64_t frames_run;

static const pad_t *cur_pad;

/* ---------- helpers ---------- */
static void mkdir_p(const char *p) { mkdir(p, 0777); }

static uint32_t hash_mem(const uint8_t *d, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) { h ^= d[i]; h *= 16777619u; }
    return h;
}

static int write_file(const char *path, const void *d, size_t n)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t w = fwrite(d, 1, n, f);
    fclose(f);
    return w == n ? 0 : -1;
}

static void *read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return NULL; }
    void *d = malloc((size_t)n);
    if (d && fread(d, 1, (size_t)n, f) != (size_t)n) { free(d); d = NULL; }
    fclose(f);
    if (d) *size = (size_t)n;
    return d;
}

/* ---------- libretro callbacks ---------- */
static bool env_cb(unsigned cmd, void *data)
{
    switch (cmd) {
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        return *(const enum retro_pixel_format *)data == RETRO_PIXEL_FORMAT_RGB565;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        *(const char **)data = sys_dir; return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        *(const char **)data = save_dir; return true;
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:
        *(bool *)data = true; return true;
    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
        return true;
    case RETRO_ENVIRONMENT_SET_GEOMETRY:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
    case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
    case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
    case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
    case RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO:
        return true;
    default:
        return false;
    }
}

static void video_cb(const void *data, unsigned w, unsigned h, size_t pitch)
{
    if (!data || w == 0 || h == 0) return; /* duped frame: keep the last one */
    if (w > FRAME_W) w = FRAME_W;
    if (h > FRAME_H) h = FRAME_H;
    int idx = frame_idx < 0 ? 0 : (frame_idx ^ 1);
    tex_t *t = &frame_tex[idx];
    for (unsigned y = 0; y < h; y++) {
        const uint16_t *src = (const uint16_t *)((const uint8_t *)data + y * pitch);
        uint8_t *dst = t->pixels + y * t->pitch;
        for (unsigned x = 0; x < w; x++) {
            uint16_t c = src[x];
            uint8_t r = (uint8_t)((c >> 11) & 31), g = (uint8_t)((c >> 5) & 63), b = (uint8_t)(c & 31);
            dst[0] = 255;
            dst[1] = (uint8_t)((r << 3) | (r >> 2));
            dst[2] = (uint8_t)((g << 2) | (g >> 4));
            dst[3] = (uint8_t)((b << 3) | (b >> 2));
            dst += 4;
        }
    }
    frame_idx = idx;
    frame_w = (int)w;
    frame_h = (int)h;
}

/* linear resampler to 48 kHz with gentle rate control on the queue size */
static float rs_pos;
static float rs_prev[2];
static float rs_out[4096 * 2];

static size_t audio_batch_cb(const int16_t *d, size_t frames)
{
    const int target = AUDIO_RATE / 20; /* ~50 ms queued */
    int q = plat_audio_buffered();
    float adjust = 1.f + 0.005f * (float)(target - q) / (float)target;
    if (adjust < 0.995f) adjust = 0.995f;
    if (adjust > 1.005f) adjust = 1.005f;
    float step = (float)(core_rate / AUDIO_RATE) / adjust;
    int n = 0;
    float vol = cfg.volume / 4.f;
    for (size_t i = 0; i < frames; i++) {
        float cur[2] = {d[i * 2] / 32768.f, d[i * 2 + 1] / 32768.f};
        while (rs_pos < 1.f && n < 4096) {
            rs_out[n * 2] = (rs_prev[0] + (cur[0] - rs_prev[0]) * rs_pos) * vol;
            rs_out[n * 2 + 1] = (rs_prev[1] + (cur[1] - rs_prev[1]) * rs_pos) * vol;
            n++;
            rs_pos += step;
        }
        rs_pos -= 1.f;
        rs_prev[0] = cur[0]; rs_prev[1] = cur[1];
        if (n >= 4000) { plat_audio_write(rs_out, n); n = 0; }
    }
    if (n) plat_audio_write(rs_out, n);
    return frames;
}

static void audio_cb(int16_t l, int16_t r)
{
    int16_t s[2] = {l, r};
    audio_batch_cb(s, 1);
}

static void input_poll_cb(void) {}

static uint32_t map_snes(uint32_t b)
{
    uint32_t m = 0;
    int swap = cfg.swap_ab;
    if (b & BTN_CROSS) m |= 1u << (swap ? RETRO_DEVICE_ID_JOYPAD_A : RETRO_DEVICE_ID_JOYPAD_B);
    if (b & BTN_CIRCLE) m |= 1u << (swap ? RETRO_DEVICE_ID_JOYPAD_B : RETRO_DEVICE_ID_JOYPAD_A);
    if (b & BTN_SQUARE) m |= 1u << (swap ? RETRO_DEVICE_ID_JOYPAD_X : RETRO_DEVICE_ID_JOYPAD_Y);
    if (b & BTN_TRIANGLE) m |= 1u << (swap ? RETRO_DEVICE_ID_JOYPAD_Y : RETRO_DEVICE_ID_JOYPAD_X);
    if (b & (BTN_L1 | BTN_L2)) m |= 1u << RETRO_DEVICE_ID_JOYPAD_L;
    if (b & (BTN_R1 | BTN_R2)) m |= 1u << RETRO_DEVICE_ID_JOYPAD_R;
    if (b & BTN_START) m |= 1u << RETRO_DEVICE_ID_JOYPAD_START;
    if (b & BTN_SELECT) m |= 1u << RETRO_DEVICE_ID_JOYPAD_SELECT;
    if (b & BTN_UP) m |= 1u << RETRO_DEVICE_ID_JOYPAD_UP;
    if (b & BTN_DOWN) m |= 1u << RETRO_DEVICE_ID_JOYPAD_DOWN;
    if (b & BTN_LEFT) m |= 1u << RETRO_DEVICE_ID_JOYPAD_LEFT;
    if (b & BTN_RIGHT) m |= 1u << RETRO_DEVICE_ID_JOYPAD_RIGHT;
    return m;
}

static int16_t input_state_cb(unsigned port, unsigned device, unsigned index, unsigned id)
{
    (void)index;
    if (!cur_pad || port > 3 || (device & RETRO_DEVICE_MASK) != RETRO_DEVICE_JOYPAD) return 0;
    uint32_t m = map_snes(cur_pad->port[port]);
    if (id == RETRO_DEVICE_ID_JOYPAD_MASK) return (int16_t)m;
    return (m >> id) & 1 ? 1 : 0;
}

/* ---------- public ---------- */
int emu_init(void)
{
    if (inited) return 0;
    snprintf(save_dir, sizeof(save_dir), "%s/saves", plat_data_dir());
    snprintf(sys_dir, sizeof(sys_dir), "%s/system", plat_data_dir());
    snprintf(shot_dir, sizeof(shot_dir), "%s/screenshots", plat_data_dir());
    mkdir_p(save_dir); mkdir_p(sys_dir); mkdir_p(shot_dir);
    for (int i = 0; i < 2; i++) {
        if (plat_tex_alloc(&frame_tex[i], FRAME_W, FRAME_H)) return -1;
    }
    retro_set_environment(env_cb);
    retro_set_video_refresh(video_cb);
    retro_set_audio_sample(audio_cb);
    retro_set_audio_sample_batch(audio_batch_cb);
    retro_set_input_poll(input_poll_cb);
    retro_set_input_state(input_state_cb);
    retro_init();
    plat_audio_init();
    inited = 1;
    return 0;
}

static void sram_path(char *out, size_t n) { snprintf(out, n, "%s/%s.srm", save_dir, rom_base); }

void emu_save_sram(void)
{
    if (!loaded) return;
    size_t n = retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
    uint8_t *d = retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
    if (!n || !d) return;
    uint32_t h = hash_mem(d, n);
    if (h == sram_hash) return;
    char p[600];
    sram_path(p, sizeof(p));
    if (write_file(p, d, n) == 0) sram_hash = h;
}

int emu_load(const char *path, const char *base)
{
    if (emu_init()) return -1;
    emu_unload();
    rom_data = read_file(path, &rom_size);
    if (!rom_data) return -2;
    struct retro_game_info gi;
    memset(&gi, 0, sizeof(gi));
    gi.path = path; gi.data = rom_data; gi.size = rom_size;
    if (!retro_load_game(&gi)) { free(rom_data); rom_data = NULL; return -3; }
    snprintf(rom_base, sizeof(rom_base), "%s", base);
    struct retro_system_av_info av;
    retro_get_system_av_info(&av);
    core_rate = av.timing.sample_rate > 0 ? av.timing.sample_rate : 32040.0;
    pal = retro_get_region() == RETRO_REGION_PAL;
    for (unsigned p = 0; p < 2; p++) retro_set_controller_port_device(p, RETRO_DEVICE_JOYPAD);
    /* battery save */
    size_t n = retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
    uint8_t *d = retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
    if (n && d) {
        char p[600]; size_t sz = 0;
        sram_path(p, sizeof(p));
        void *s = read_file(p, &sz);
        if (s) { memcpy(d, s, sz < n ? sz : n); free(s); }
        sram_hash = hash_mem(d, n);
    }
    frame_idx = -1;
    frames_run = 0;
    rs_pos = 0; rs_prev[0] = rs_prev[1] = 0;
    plat_audio_clear();
    loaded = 1;
    return 0;
}

void emu_unload(void)
{
    if (!loaded) return;
    emu_save_sram();
    retro_unload_game();
    free(rom_data);
    rom_data = NULL;
    loaded = 0;
    frame_idx = -1;
    plat_audio_clear();
}

int emu_loaded(void) { return loaded; }

void emu_run_frame(const pad_t *pad)
{
    if (!loaded) return;
    cur_pad = pad;
    frames_run++;
    /* the display runs at 60 Hz: PAL games skip every 6th refresh */
    if (pal && frames_run % 6 == 0) return;
    retro_run();
    if (frames_run % 600 == 0) emu_save_sram(); /* every ~10 s if changed */
}

void emu_reset(void) { if (loaded) retro_reset(); }

const tex_t *emu_frame(float *u1, float *v1, int *w, int *h)
{
    if (frame_idx < 0) return NULL;
    const tex_t *t = &frame_tex[frame_idx];
    if (u1) *u1 = (float)frame_w / t->w;
    if (v1) *v1 = (float)frame_h / t->h;
    if (w) *w = frame_w;
    if (h) *h = frame_h;
    return t;
}

void emu_draw(float bx, float by, float bw, float bh, float alpha)
{
    float u1, v1;
    int w, h;
    const tex_t *t = emu_frame(&u1, &v1, &w, &h);
    if (!t) return;
    frame_tex[frame_idx].smooth = cfg.smooth;
    /* SNES pictures are 4:3 on a TV; 8:7 shows square pixels */
    float ar = cfg.aspect == ASPECT_8_7 ? 8.f / 7.f : 4.f / 3.f;
    float dw, dh;
    if (cfg.aspect == ASPECT_STRETCH) { dw = bw; dh = bh; }
    else {
        dh = bh; dw = dh * ar;
        if (dw > bw) { dw = bw; dh = dw / ar; }
    }
    float x = bx + (bw - dw) / 2, y = by + (bh - dh) / 2;
    gfx_image(t, x, y, dw, dh, 0, 0, u1, v1, col_alpha(0xffffffffu, alpha));
}

/* ---------- save states ---------- */
static void state_path(int slot, char *out, int n) { snprintf(out, (size_t)n, "%s/%s.state%d", save_dir, rom_base, slot + 1); }
void emu_state_thumb(int slot, char *out, int n) { snprintf(out, (size_t)n, "%s/%s.state%d.png", save_dir, rom_base, slot + 1); }

int emu_state_exists(int slot)
{
    char p[600]; struct stat st;
    state_path(slot, p, sizeof(p));
    return stat(p, &st) == 0;
}

long long emu_state_time(int slot)
{
    char p[600]; struct stat st;
    state_path(slot, p, sizeof(p));
    if (stat(p, &st)) return 0;
    return (long long)st.st_mtime;
}

/* frame -> RGB buffer (for PNG) */
static uint8_t *frame_rgb(int *w, int *h)
{
    int fw, fh;
    const tex_t *t = emu_frame(NULL, NULL, &fw, &fh);
    if (!t) return NULL;
    uint8_t *rgb = malloc((size_t)fw * fh * 3);
    if (!rgb) return NULL;
    for (int y = 0; y < fh; y++)
        for (int x = 0; x < fw; x++) {
            const uint8_t *s = t->pixels + y * t->pitch + x * 4;
            uint8_t *d = rgb + ((size_t)y * fw + x) * 3;
            d[0] = s[1]; d[1] = s[2]; d[2] = s[3];
        }
    *w = fw; *h = fh;
    return rgb;
}

int emu_save_state(int slot)
{
    if (!loaded) return -1;
    size_t n = retro_serialize_size();
    void *d = malloc(n);
    if (!d) return -1;
    int ok = retro_serialize(d, n);
    char p[600];
    state_path(slot, p, sizeof(p));
    int r = ok ? write_file(p, d, n) : -1;
    free(d);
    if (r == 0) {
        int w, h;
        uint8_t *rgb = frame_rgb(&w, &h);
        if (rgb) {
            emu_state_thumb(slot, p, sizeof(p));
            stbi_write_png(p, w, h, 3, rgb, w * 3);
            free(rgb);
        }
    }
    return r;
}

int emu_load_state(int slot)
{
    if (!loaded) return -1;
    char p[600]; size_t n = 0;
    state_path(slot, p, sizeof(p));
    void *d = read_file(p, &n);
    if (!d) return -1;
    int ok = retro_unserialize(d, n);
    free(d);
    plat_audio_clear();
    return ok ? 0 : -1;
}

int emu_screenshot(char *out, int n)
{
    int w, h;
    uint8_t *rgb = frame_rgb(&w, &h);
    if (!rgb) return -1;
    clock_t2 t;
    plat_local_time(&t);
    snprintf(out, (size_t)n, "%s/%s %04d-%02d-%02d %02d%02d%02d.png", shot_dir, rom_base,
             t.year, t.month, t.day, t.hour, t.minute, t.second);
    int ok = stbi_write_png(out, w, h, 3, rgb, w * 3);
    free(rgb);
    return ok ? 0 : -1;
}
