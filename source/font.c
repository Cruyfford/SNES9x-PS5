/* FreeType text rendering with a shared glyph atlas texture. */
#include "font.h"
#include "gfx.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define ATLAS_W 2048
#define ATLAS_H 2048
#define HASH_SIZE 4096

typedef struct {
    uint32_t key;       /* font id << 21 | codepoint */
    short x, y, w, h;   /* atlas rect */
    short bx, by;       /* bearing */
    float adv;
    int used;
} glyph_t;

struct font {
    FT_Face face;
    int px, id;
    float ascent, line_h;
};

static FT_Library ft;
static tex_t atlas;
static int shelf_x, shelf_y, shelf_h;
static glyph_t glyphs[HASH_SIZE];
static int font_count;

typedef struct { char path[256]; unsigned char *data; long size; } filebuf_t;
static filebuf_t files[8];
static int nfiles;

int font_system_init(void)
{
    if (FT_Init_FreeType(&ft)) return -1;
    if (plat_tex_alloc(&atlas, ATLAS_W, ATLAS_H)) return -1;
    atlas.smooth = 1;
    shelf_x = 1; shelf_y = 1; shelf_h = 0;
    return 0;
}

static unsigned char *load_file(const char *path, long *size)
{
    for (int i = 0; i < nfiles; i++)
        if (!strcmp(files[i].path, path)) { *size = files[i].size; return files[i].data; }
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *d = malloc((size_t)n);
    if (!d || fread(d, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(d); return NULL; }
    fclose(f);
    if (nfiles < 8) {
        snprintf(files[nfiles].path, sizeof(files[nfiles].path), "%s", path);
        files[nfiles].data = d; files[nfiles].size = n; nfiles++;
    }
    *size = n;
    return d;
}

font_t *font_load(const char *path, int px)
{
    long size = 0;
    unsigned char *data = load_file(path, &size);
    if (!data) { plat_log("font: cannot open %s", path); return NULL; }
    font_t *f = calloc(1, sizeof(*f));
    if (FT_New_Memory_Face(ft, data, size, 0, &f->face)) { free(f); return NULL; }
    FT_Set_Pixel_Sizes(f->face, 0, (FT_UInt)px);
    f->px = px;
    f->id = ++font_count;
    f->ascent = (float)(f->face->size->metrics.ascender >> 6);
    f->line_h = (float)(f->face->size->metrics.height >> 6);
    return f;
}

int font_px(const font_t *f) { return f->px; }
float font_ascent(const font_t *f) { return f->ascent; }
float font_line_h(const font_t *f) { return f->line_h; }

static glyph_t *get_glyph(font_t *f, uint32_t cp)
{
    uint32_t key = ((uint32_t)f->id << 21) | (cp & 0x1fffff);
    uint32_t h = (key * 2654435761u) % HASH_SIZE;
    for (int probe = 0; probe < HASH_SIZE; probe++) {
        glyph_t *g = &glyphs[(h + probe) % HASH_SIZE];
        if (g->used && g->key == key) return g;
        if (!g->used) {
            if (FT_Load_Char(f->face, cp, FT_LOAD_RENDER | FT_LOAD_TARGET_LIGHT)) return NULL;
            FT_GlyphSlot s = f->face->glyph;
            int w = (int)s->bitmap.width, hh = (int)s->bitmap.rows;
            if (shelf_x + w + 2 > ATLAS_W) { shelf_x = 1; shelf_y += shelf_h + 2; shelf_h = 0; }
            if (shelf_y + hh + 2 > ATLAS_H) return NULL; /* atlas full */
            g->used = 1; g->key = key;
            g->x = (short)shelf_x; g->y = (short)shelf_y; g->w = (short)w; g->h = (short)hh;
            g->bx = (short)s->bitmap_left; g->by = (short)s->bitmap_top;
            g->adv = (float)s->advance.x / 64.f;
            for (int row = 0; row < hh; row++) {
                uint8_t *dst = atlas.pixels + (shelf_y + row) * atlas.pitch + shelf_x * 4;
                const uint8_t *src = s->bitmap.buffer + row * s->bitmap.pitch;
                for (int col = 0; col < w; col++) {
                    dst[col * 4 + 0] = src[col];
                    dst[col * 4 + 1] = 255; dst[col * 4 + 2] = 255; dst[col * 4 + 3] = 255;
                }
            }
            shelf_x += w + 2;
            if (hh > shelf_h) shelf_h = hh;
            return g;
        }
    }
    return NULL;
}

static uint32_t utf8_next(const char **ps)
{
    const unsigned char *s = (const unsigned char *)*ps;
    uint32_t c = *s++;
    if (c >= 0xf0 && s[0] && s[1] && s[2]) { c = ((c & 7) << 18) | ((s[0] & 63) << 12) | ((s[1] & 63) << 6) | (s[2] & 63); s += 3; }
    else if (c >= 0xe0 && s[0] && s[1]) { c = ((c & 15) << 12) | ((s[0] & 63) << 6) | (s[1] & 63); s += 2; }
    else if (c >= 0xc0 && s[0]) { c = ((c & 31) << 6) | (s[0] & 63); s += 1; }
    *ps = (const char *)s;
    return c;
}

float text_width(font_t *f, const char *s, float tracking)
{
    if (!f || !s) return 0;
    float w = 0;
    int n = 0;
    while (*s) {
        glyph_t *g = get_glyph(f, utf8_next(&s));
        if (g) w += g->adv + tracking;
        n++;
    }
    if (n) w -= tracking;
    return w;
}

static float draw_run(font_t *f, float x, float y, const char *s, const char *end, rgba_t c, float tracking)
{
    float pen = floorf(x + 0.5f);
    float base = floorf(y + f->ascent + 0.5f);
    while (*s && (!end || s < end)) {
        glyph_t *g = get_glyph(f, utf8_next(&s));
        if (!g) continue;
        if (g->w && g->h) {
            float gx = pen + g->bx, gy = base - g->by;
            gfx_image(&atlas, gx, gy, g->w, g->h,
                      (float)g->x / ATLAS_W, (float)g->y / ATLAS_H,
                      (float)(g->x + g->w) / ATLAS_W, (float)(g->y + g->h) / ATLAS_H, c);
        }
        pen += g->adv + tracking;
    }
    return pen - x;
}

float text_draw(font_t *f, float x, float y, const char *s, rgba_t c, float tracking, int align)
{
    if (!f || !s) return 0;
    if (align != ALIGN_LEFT) {
        float w = text_width(f, s, tracking);
        x -= align == ALIGN_CENTER ? w / 2 : w;
    }
    return draw_run(f, x, y, s, NULL, c, tracking);
}

float text_draw_fit(font_t *f, float x, float y, const char *s, rgba_t c, float max_w, int align)
{
    if (!f || !s) return 0;
    float w = text_width(f, s, 0);
    if (w <= max_w) return text_draw(f, x, y, s, c, 0, align);
    float ell = text_width(f, "...", 0);
    const char *p = s, *cut = s;
    float acc = 0;
    while (*p) {
        const char *q = p;
        glyph_t *g = get_glyph(f, utf8_next(&q));
        float a = g ? g->adv : 0;
        if (acc + a + ell > max_w) break;
        acc += a; p = q; cut = p;
    }
    if (align == ALIGN_CENTER) x -= (acc + ell) / 2;
    else if (align == ALIGN_RIGHT) x -= acc + ell;
    float used = draw_run(f, x, y, s, cut, c, 0);
    return used + draw_run(f, x + used, y, "...", NULL, c, 0);
}

int text_draw_wrap(font_t *f, float x, float y, const char *s, rgba_t c, float max_w,
                   float line_h, int max_lines)
{
    if (!f || !s) return 0;
    int lines = 0;
    const char *p = s;
    char buf[512];
    while (*p && lines < max_lines) {
        while (*p == ' ') p++;
        /* grow the line word by word */
        const char *line_end = p, *q = p;
        while (*q) {
            const char *w = q;
            while (*w && *w != ' ') w++;
            size_t len = (size_t)(w - p);
            if (len >= sizeof(buf)) break;
            memcpy(buf, p, len); buf[len] = 0;
            if (text_width(f, buf, 0) > max_w && line_end != p) break;
            line_end = w;
            q = w;
            while (*q == ' ') q++;
            if (text_width(f, buf, 0) > max_w) break;
        }
        if (line_end == p) { /* single long word */
            while (*line_end && *line_end != ' ') line_end++;
        }
        size_t len = (size_t)(line_end - p);
        if (len >= sizeof(buf)) len = sizeof(buf) - 1;
        memcpy(buf, p, len); buf[len] = 0;
        if (lines == max_lines - 1 && *line_end) {
            /* last allowed line: put the rest and cut with ellipsis */
            text_draw_fit(f, x, y + lines * line_h, p, c, max_w, ALIGN_LEFT);
        } else {
            text_draw_fit(f, x, y + lines * line_h, buf, c, max_w, ALIGN_LEFT);
        }
        lines++;
        p = line_end;
    }
    return lines;
}
