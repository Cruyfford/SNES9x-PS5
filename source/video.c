/* Ogg Theora preview player. A worker thread decodes frames at the clip's
 * frame rate into an ARGB buffer; the UI thread uploads the newest one. */
#include "video.h"
#include <ogg/ogg.h>
#ifdef __PS3__
/* the PS3 portlibs ship Tremor's ogg/config_types.h, which lacks this type */
typedef unsigned short ogg_uint16_t;
#endif
#include <theora/theoradec.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VID_MAX_W 640
#define VID_MAX_H 512

typedef struct {
    FILE *f;
    ogg_sync_state oy;
    ogg_stream_state to;
    th_info ti;
    th_comment tc;
    th_setup_info *ts;
    th_dec_ctx *td;
    int stream_ready;
    int theora_serial;
    double fps;
    long frame_no;
} decoder_t;

static int sync_mode;
static plat_mutex *mtx;

/* shared between threads (guarded by mtx) */
static char req_path[512];
static int req_gen, cur_gen;
static uint8_t *ready_buf;          /* ARGB, VID_MAX_W pitch */
static int ready_w, ready_h, ready_new;
static double ready_time, clip_len;

/* UI side */
static tex_t vtex[2];
static int vidx = -1, vw, vh;
static float vpos;
static int playing;

/* ------------- decoding ------------- */
static int read_page(decoder_t *d, ogg_page *og)
{
    while (ogg_sync_pageout(&d->oy, og) != 1) {
        char *buf = ogg_sync_buffer(&d->oy, 8192);
        size_t n = fread(buf, 1, 8192, d->f);
        if (n == 0) return 0;
        ogg_sync_wrote(&d->oy, (long)n);
    }
    return 1;
}

static void dec_close(decoder_t *d)
{
    if (d->td) th_decode_free(d->td);
    if (d->ts) th_setup_free(d->ts);
    th_comment_clear(&d->tc);
    th_info_clear(&d->ti);
    if (d->stream_ready) ogg_stream_clear(&d->to);
    ogg_sync_clear(&d->oy);
    if (d->f) fclose(d->f);
    memset(d, 0, sizeof(*d));
}

static int dec_open(decoder_t *d, const char *path)
{
    memset(d, 0, sizeof(*d));
    d->f = fopen(path, "rb");
    if (!d->f) return -1;
    ogg_sync_init(&d->oy);
    th_info_init(&d->ti);
    th_comment_init(&d->tc);
    ogg_page og;
    ogg_packet op;
    int headers = 0;
    /* find the Theora stream and read its 3 header packets */
    while (headers < 3) {
        if (!read_page(d, &og)) { dec_close(d); return -1; }
        if (ogg_page_bos(&og) && !d->stream_ready) {
            ogg_stream_state test;
            ogg_stream_init(&test, ogg_page_serialno(&og));
            ogg_stream_pagein(&test, &og);
            if (ogg_stream_packetpeek(&test, &op) == 1 && th_decode_headerin(&d->ti, &d->tc, &d->ts, &op) >= 0) {
                ogg_stream_packetout(&test, &op);
                d->to = test;
                d->stream_ready = 1;
                d->theora_serial = ogg_page_serialno(&og);
                headers = 1;
            } else {
                ogg_stream_clear(&test);
            }
            continue;
        }
        if (d->stream_ready && ogg_page_serialno(&og) == d->theora_serial) {
            ogg_stream_pagein(&d->to, &og);
            while (headers < 3 && ogg_stream_packetout(&d->to, &op) == 1) {
                if (th_decode_headerin(&d->ti, &d->tc, &d->ts, &op) < 0) { dec_close(d); return -1; }
                headers++;
            }
        }
    }
    d->td = th_decode_alloc(&d->ti, d->ts);
    if (!d->td) { dec_close(d); return -1; }
    d->fps = d->ti.fps_denominator ? (double)d->ti.fps_numerator / d->ti.fps_denominator : 30.0;
    if (d->fps < 1 || d->fps > 120) d->fps = 30.0;
    return 0;
}

/* next decoded frame converted to ARGB; returns 0 at end of stream */
static int dec_frame(decoder_t *d, uint8_t *out, int *ow, int *oh)
{
    ogg_packet op;
    ogg_page og;
    for (;;) {
        while (ogg_stream_packetout(&d->to, &op) == 1) {
            ogg_int64_t gp;
            if (th_decode_packetin(d->td, &op, &gp) != 0) continue;
            th_ycbcr_buffer yb;
            th_decode_ycbcr_out(d->td, yb);
            int pw = (int)d->ti.pic_width, ph = (int)d->ti.pic_height;
            int px = (int)d->ti.pic_x, py = (int)d->ti.pic_y;
            /* downscale by an integer step if the clip is larger than the texture */
            int step = 1;
            while (pw / step > VID_MAX_W || ph / step > VID_MAX_H) step++;
            int w = pw / step, h = ph / step;
            int xs = d->ti.pixel_fmt == TH_PF_444 ? 0 : 1;
            int ys = d->ti.pixel_fmt == TH_PF_420 ? 1 : 0;
            for (int y = 0; y < h; y++) {
                int sy = py + y * step;
                const uint8_t *Y = yb[0].data + sy * yb[0].stride;
                const uint8_t *U = yb[1].data + (sy >> ys) * yb[1].stride;
                const uint8_t *V = yb[2].data + (sy >> ys) * yb[2].stride;
                uint8_t *dst = out + (size_t)y * VID_MAX_W * 4;
                for (int x = 0; x < w; x++) {
                    int sx = px + x * step;
                    int yy = (Y[sx] - 16) * 298;
                    int u = U[sx >> xs] - 128, v = V[sx >> xs] - 128;
                    int r = (yy + 409 * v + 128) >> 8;
                    int g = (yy - 100 * u - 208 * v + 128) >> 8;
                    int b = (yy + 516 * u + 128) >> 8;
                    dst[0] = 255;
                    dst[1] = (uint8_t)(r < 0 ? 0 : r > 255 ? 255 : r);
                    dst[2] = (uint8_t)(g < 0 ? 0 : g > 255 ? 255 : g);
                    dst[3] = (uint8_t)(b < 0 ? 0 : b > 255 ? 255 : b);
                    dst += 4;
                }
            }
            *ow = w; *oh = h;
            d->frame_no++;
            return 1;
        }
        if (!read_page(d, &og)) return 0;
        if (ogg_page_serialno(&og) == d->theora_serial) ogg_stream_pagein(&d->to, &og);
    }
}

/* ------------- worker ------------- */
static decoder_t dec;
static int dec_gen = -1;
static uint8_t *work_buf;
static uint64_t clip_start_ms;

/* one step of the player: (re)open on request, decode frames that are due */
static void player_step(void)
{
    char path[512];
    int gen;
    plat_mutex_lock(mtx);
    gen = req_gen;
    snprintf(path, sizeof(path), "%s", req_path);
    plat_mutex_unlock(mtx);

    if (gen != dec_gen) {
        if (dec.f) dec_close(&dec);
        dec_gen = gen;
        if (path[0] && dec_open(&dec, path) == 0) clip_start_ms = plat_ms();
        else return;
    }
    if (!dec.f) return;

    double t = (double)(plat_ms() - clip_start_ms) / 1000.0;
    double due = (double)dec.frame_no / dec.fps;
    if (t < due) return;
    int w, h;
    if (!dec_frame(&dec, work_buf, &w, &h)) {
        /* loop: remember the length, start again */
        double len = (double)dec.frame_no / dec.fps;
        dec_close(&dec);
        if (dec_open(&dec, path) != 0) return;
        clip_start_ms = plat_ms();
        plat_mutex_lock(mtx);
        clip_len = len;
        plat_mutex_unlock(mtx);
        if (!dec_frame(&dec, work_buf, &w, &h)) return;
    }
    plat_mutex_lock(mtx);
    if (gen == req_gen) {
        for (int y = 0; y < h; y++)
            memcpy(ready_buf + (size_t)y * VID_MAX_W * 4, work_buf + (size_t)y * VID_MAX_W * 4, (size_t)w * 4);
        ready_w = w; ready_h = h; ready_new = 1;
        ready_time = (double)(dec.frame_no - 1) / dec.fps;
        cur_gen = gen;
    }
    plat_mutex_unlock(mtx);
}

static void worker(void *arg)
{
    (void)arg;
    for (;;) {
        player_step();
        plat_sleep_ms(4);
    }
}

int video_init(int synchronous)
{
    sync_mode = synchronous;
    mtx = plat_mutex_new();
    ready_buf = calloc((size_t)VID_MAX_W * VID_MAX_H, 4);
    work_buf = calloc((size_t)VID_MAX_W * VID_MAX_H, 4);
    if (!mtx || !ready_buf || !work_buf) return -1;
    for (int i = 0; i < 2; i++)
        if (plat_tex_alloc(&vtex[i], VID_MAX_W, VID_MAX_H)) return -1;
    if (!sync_mode && plat_thread_start(worker, NULL)) sync_mode = 1;
    return 0;
}

int video_open(const char *path)
{
    plat_mutex_lock(mtx);
    if (!strcmp(req_path, path) && playing) { plat_mutex_unlock(mtx); return 0; }
    snprintf(req_path, sizeof(req_path), "%s", path);
    req_gen++;
    ready_new = 0;
    clip_len = 0;
    plat_mutex_unlock(mtx);
    vidx = -1;
    playing = 1;
    vpos = 0;
    return 0;
}

void video_close(void)
{
    plat_mutex_lock(mtx);
    req_path[0] = 0;
    req_gen++;
    ready_new = 0;
    plat_mutex_unlock(mtx);
    vidx = -1;
    playing = 0;
}

void video_update(void)
{
    if (!playing) return;
    if (sync_mode) player_step();
    plat_mutex_lock(mtx);
    if (ready_new && cur_gen == req_gen) {
        int idx = vidx < 0 ? 0 : vidx ^ 1;
        tex_t *t = &vtex[idx];
        for (int y = 0; y < ready_h; y++)
            memcpy(t->pixels + (size_t)y * t->pitch, ready_buf + (size_t)y * VID_MAX_W * 4, (size_t)ready_w * 4);
        vidx = idx; vw = ready_w; vh = ready_h;
        vpos = (float)ready_time;
        ready_new = 0;
    }
    plat_mutex_unlock(mtx);
}

const tex_t *video_frame(float *u1, float *v1, int *w, int *h)
{
    if (!playing || vidx < 0) return NULL;
    const tex_t *t = &vtex[vidx];
    if (u1) *u1 = (float)vw / t->w;
    if (v1) *v1 = (float)vh / t->h;
    if (w) *w = vw;
    if (h) *h = vh;
    return t;
}

float video_position(void) { return vpos; }
float video_length(void) { float l; plat_mutex_lock(mtx); l = (float)clip_len; plat_mutex_unlock(mtx); return l; }
int video_playing(void) { return playing; }
