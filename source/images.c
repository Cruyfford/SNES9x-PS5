/* Image cache: PNG/JPG decoded on a worker thread, uploaded into fixed
 * texture slots on the UI thread, least-recently-used slots are recycled. */
#include "images.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STB_IMAGE_IMPLEMENTATION
#include "ext/stb_image.h"

typedef struct { int count, max_w, max_h; } kind_cfg_t;
static const kind_cfg_t kinds[2] = {
    {48, 320, 320},  /* covers: tiles are at most ~250px on screen */
    {6, 640, 480},   /* backdrops: SNES snaps are 256x224 */
};

enum { S_EMPTY, S_LOADING, S_READY };

typedef struct {
    char path[256];
    int state, kind;
    uint64_t used;
    int w, h;
    tex_t tex;
    image_t img;
    int gen;
} slot_t;

#define MAX_SLOTS 64
static slot_t slots[MAX_SLOTS];
static int nslots;
static uint64_t frame;
static int sync_mode;

/* failed paths */
#define MAX_FAILED 512
static unsigned failed[MAX_FAILED];
static int nfailed;

/* jobs */
typedef struct {
    int slot, gen, kind;
    char path[256];
    uint8_t *argb; int w, h; int ok;
} job_t;
#define MAX_JOBS 64
static job_t pending[MAX_JOBS]; static int npending;
static job_t done[MAX_JOBS]; static int ndone;
static plat_mutex *mtx;

static unsigned hash_str(const char *s, int kind)
{
    unsigned h = 2166136261u ^ (unsigned)kind;
    while (*s) { h ^= (unsigned char)*s++; h *= 16777619u; }
    return h;
}

int image_failed(const char *path, int kind)
{
    unsigned h = hash_str(path, kind);
    for (int i = 0; i < nfailed; i++) if (failed[i] == h) return 1;
    return 0;
}
static void mark_failed(const char *path, int kind)
{
    if (nfailed < MAX_FAILED && !image_failed(path, kind)) failed[nfailed++] = hash_str(path, kind);
}

/* decode + downscale (area average) + convert to A,R,G,B bytes */
static uint8_t *decode(const char *path, int max_w, int max_h, int *ow, int *oh)
{
    int w, h, n;
    uint8_t *rgba = stbi_load(path, &w, &h, &n, 4);
    if (!rgba) return NULL;
    float scale = 1.f;
    if (w > max_w) scale = (float)max_w / (float)w;
    if (h * scale > max_h) scale = (float)max_h / (float)h;
    int dw = (int)(w * scale + 0.5f), dh = (int)(h * scale + 0.5f);
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    uint8_t *out = malloc((size_t)dw * dh * 4);
    if (!out) { stbi_image_free(rgba); return NULL; }
    float fx = (float)w / dw, fy = (float)h / dh;
    for (int y = 0; y < dh; y++) {
        int sy0 = (int)(y * fy), sy1 = (int)((y + 1) * fy);
        if (sy1 <= sy0) sy1 = sy0 + 1;
        if (sy1 > h) sy1 = h;
        for (int x = 0; x < dw; x++) {
            int sx0 = (int)(x * fx), sx1 = (int)((x + 1) * fx);
            if (sx1 <= sx0) sx1 = sx0 + 1;
            if (sx1 > w) sx1 = w;
            unsigned acc[4] = {0, 0, 0, 0}, cnt = 0;
            for (int yy = sy0; yy < sy1; yy++) {
                const uint8_t *p = rgba + ((size_t)yy * w + sx0) * 4;
                for (int xx = sx0; xx < sx1; xx++, p += 4) {
                    acc[0] += p[0]; acc[1] += p[1]; acc[2] += p[2]; acc[3] += p[3]; cnt++;
                }
            }
            uint8_t *d = out + ((size_t)y * dw + x) * 4;
            d[0] = (uint8_t)(acc[3] / cnt); /* A */
            d[1] = (uint8_t)(acc[0] / cnt); /* R */
            d[2] = (uint8_t)(acc[1] / cnt); /* G */
            d[3] = (uint8_t)(acc[2] / cnt); /* B */
        }
    }
    stbi_image_free(rgba);
    *ow = dw; *oh = dh;
    return out;
}

static void worker(void *arg)
{
    (void)arg;
    for (;;) {
        job_t j;
        int have = 0;
        plat_mutex_lock(mtx);
        if (npending) {
            j = pending[0];
            memmove(pending, pending + 1, sizeof(job_t) * (size_t)(npending - 1));
            npending--;
            have = 1;
        }
        plat_mutex_unlock(mtx);
        if (!have) { plat_sleep_ms(8); continue; }
        j.argb = decode(j.path, kinds[j.kind].max_w, kinds[j.kind].max_h, &j.w, &j.h);
        j.ok = j.argb != NULL;
        plat_mutex_lock(mtx);
        if (ndone < MAX_JOBS) done[ndone++] = j;
        else free(j.argb);
        plat_mutex_unlock(mtx);
    }
}

int images_init(int synchronous)
{
    sync_mode = synchronous;
    nslots = 0;
    for (int k = 0; k < 2; k++)
        for (int i = 0; i < kinds[k].count && nslots < MAX_SLOTS; i++) {
            slot_t *s = &slots[nslots];
            memset(s, 0, sizeof(*s));
            s->kind = k;
            if (plat_tex_alloc(&s->tex, kinds[k].max_w, kinds[k].max_h)) return -1;
            nslots++;
        }
    if (!sync_mode) {
        mtx = plat_mutex_new();
        if (!mtx || plat_thread_start(worker, NULL)) sync_mode = 1;
    }
    return 0;
}

static void fill_slot(slot_t *s, const uint8_t *argb, int w, int h)
{
    /* small pixel-art snapshots stay crisp when scaled up */
    s->tex.smooth = !(s->kind == IMG_BACKDROP && w <= 320);
    for (int y = 0; y < h; y++)
        memcpy(s->tex.pixels + (size_t)y * s->tex.pitch, argb + (size_t)y * w * 4, (size_t)w * 4);
    s->w = w; s->h = h;
    s->state = S_READY;
    s->img.tex = &s->tex; s->img.w = w; s->img.h = h;
    /* half-texel inset avoids bleeding from the unused part of the slot */
    s->img.u1 = ((float)w - 0.5f) / s->tex.w;
    s->img.v1 = ((float)h - 0.5f) / s->tex.h;
}

void images_update(void)
{
    frame++;
    if (sync_mode) return;
    job_t local[MAX_JOBS];
    int n = 0;
    plat_mutex_lock(mtx);
    n = ndone; memcpy(local, done, sizeof(job_t) * (size_t)n); ndone = 0;
    plat_mutex_unlock(mtx);
    if (!n) return;
    plat_gpu_sync();
    for (int i = 0; i < n; i++) {
        job_t *j = &local[i];
        slot_t *s = &slots[j->slot];
        if (s->gen == j->gen && s->state == S_LOADING) {
            if (j->ok) fill_slot(s, j->argb, j->w, j->h);
            else { mark_failed(s->path, s->kind); s->state = S_EMPTY; s->path[0] = 0; }
        }
        free(j->argb);
    }
}

int image_get(const char *path, int kind, image_t *out)
{
    if (!path || !path[0] || kind > IMG_BACKDROP) return 0;
    for (int i = 0; i < nslots; i++) {
        slot_t *s = &slots[i];
        if (s->kind == kind && s->state != S_EMPTY && !strcmp(s->path, path)) {
            s->used = frame;
            if (s->state == S_READY) { *out = s->img; return 1; }
            return 0;
        }
    }
    if (image_failed(path, kind)) return 0;
    /* pick the least recently used slot of this kind that is not in use this frame */
    slot_t *best = NULL;
    for (int i = 0; i < nslots; i++) {
        slot_t *s = &slots[i];
        if (s->kind != kind || s->state == S_LOADING) continue;
        if (s->state == S_READY && s->used + 1 >= frame) continue;
        if (!best || s->state == S_EMPTY || (best->state != S_EMPTY && s->used < best->used)) best = s;
        if (best->state == S_EMPTY) break;
    }
    if (!best) return 0;
    if (!sync_mode) {
        plat_mutex_lock(mtx);
        int queued = 0;
        if (npending < MAX_JOBS) {
            job_t *j = &pending[npending++];
            memset(j, 0, sizeof(*j));
            j->slot = (int)(best - slots);
            j->gen = ++best->gen;
            j->kind = kind;
            snprintf(j->path, sizeof(j->path), "%s", path);
            queued = 1;
        }
        plat_mutex_unlock(mtx);
        if (!queued) return 0;
        snprintf(best->path, sizeof(best->path), "%s", path);
        best->state = S_LOADING;
        best->used = frame;
        return 0;
    }
    int w, h;
    uint8_t *argb = decode(path, kinds[kind].max_w, kinds[kind].max_h, &w, &h);
    if (!argb) { mark_failed(path, kind); return 0; }
    snprintf(best->path, sizeof(best->path), "%s", path);
    best->gen++;
    fill_slot(best, argb, w, h);
    free(argb);
    best->used = frame;
    *out = best->img;
    return 1;
}

const image_t *image_load_now(const char *path)
{
    int w, h;
    uint8_t *argb = decode(path, 4096, 4096, &w, &h);
    if (!argb) return NULL;
    slot_t *s = calloc(1, sizeof(*s));
    if (!s || plat_tex_alloc(&s->tex, w, h)) { free(argb); free(s); return NULL; }
    fill_slot(s, argb, w, h);
    s->img.u1 = 1.f; s->img.v1 = 1.f;
    free(argb);
    return &s->img;
}
