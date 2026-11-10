/* 2D drawing on top of the platform triangle API: rounded rectangles with
 * anti-aliased edges, gradients, rounded images and shadows. */
#include "gfx.h"
#include <math.h>
#include <string.h>

#define VBUF 8192
static vtx_t vbuf[VBUF];
static int vcount;
static const tex_t *vtex;

static float alpha_stack[16];
static int alpha_top;
static float galpha = 1.f;

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
float ease_out_cubic(float t) { t = clampf(t, 0, 1); t = 1 - t; return 1 - t * t * t; }
float approach(float cur, float target, float speed, float dt)
{
    float k = 1.f - expf(-speed * dt);
    return cur + (target - cur) * k;
}

rgba_t col_alpha(rgba_t c, float a)
{
    float na = (float)(c & 255) * clampf(a, 0, 1);
    return (c & 0xffffff00u) | (uint32_t)(na + 0.5f);
}

rgba_t col_lerp(rgba_t a, rgba_t b, float t)
{
    t = clampf(t, 0, 1);
    uint32_t out = 0;
    for (int s = 0; s < 32; s += 8) {
        float x = (float)((a >> s) & 255), y = (float)((b >> s) & 255);
        out |= ((uint32_t)(x + (y - x) * t + 0.5f) & 255) << s;
    }
    return out;
}

void gfx_begin(void) { vcount = 0; vtex = NULL; alpha_top = 0; galpha = 1.f; }

void gfx_flush(void)
{
    if (vcount) plat_draw_tris(vbuf, vcount, vtex);
    vcount = 0;
}

void gfx_push_alpha(float a)
{
    if (alpha_top < 16) alpha_stack[alpha_top++] = galpha;
    galpha *= clampf(a, 0, 1);
}
void gfx_pop_alpha(void) { if (alpha_top) galpha = alpha_stack[--alpha_top]; }

static void use_tex(const tex_t *t, int need)
{
    if (t != vtex || vcount + need > VBUF) { gfx_flush(); vtex = t; }
}

static inline void put(float x, float y, float u, float v, rgba_t c)
{
    vtx_t *p = &vbuf[vcount++];
    p->x = x; p->y = y; p->u = u; p->v = v;
    p->c = galpha >= 1.f ? c : col_alpha(c, galpha);
}

void gfx_rect4(float x, float y, float w, float h, rgba_t tl, rgba_t tr, rgba_t br, rgba_t bl)
{
    if (w <= 0 || h <= 0) return;
    use_tex(NULL, 6);
    put(x, y, 0, 0, tl); put(x + w, y, 0, 0, tr); put(x + w, y + h, 0, 0, br);
    put(x, y, 0, 0, tl); put(x + w, y + h, 0, 0, br); put(x, y + h, 0, 0, bl);
}

void gfx_rect(float x, float y, float w, float h, rgba_t c) { gfx_rect4(x, y, w, h, c, c, c, c); }

/* ---- rounded outline generation ---- */
#define MAXP 160
typedef struct { float x, y, nx, ny; } pt_t;

static int perimeter(float x, float y, float w, float h, float r, pt_t *out)
{
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    if (r < 0) r = 0;
    int segs = (int)(r / 3.f);
    if (segs < 4) segs = 4;
    if (segs > 36) segs = 36;
    if (r < 0.5f) segs = 1;
    const float cx[4] = {x + w - r, x + w - r, x + r, x + r};
    const float cy[4] = {y + r, y + h - r, y + h - r, y + r};
    const float start[4] = {-(float)M_PI / 2, 0, (float)M_PI / 2, (float)M_PI};
    int n = 0;
    for (int c = 0; c < 4; c++) {
        for (int i = 0; i <= segs; i++) {
            float a = start[c] + (float)M_PI / 2 * (float)i / (float)segs;
            float nx = cosf(a), ny = sinf(a);
            if (segs == 1) { nx = (c == 0 || c == 1) ? 0.7071f : -0.7071f; ny = (c == 1 || c == 2) ? 0.7071f : -0.7071f; }
            out[n].x = cx[c] + nx * r; out[n].y = cy[c] + ny * r;
            out[n].nx = nx; out[n].ny = ny;
            n++;
        }
    }
    return n;
}

typedef rgba_t (*colfn)(float y, void *ud);

static void fill_shape(float x, float y, float w, float h, float r,
                       const tex_t *t, float u0, float v0, float u1, float v1,
                       rgba_t ctop, rgba_t cbot, float feather)
{
    pt_t p[MAXP];
    int n = perimeter(x, y, w, h, r, p);
    float cxm = x + w / 2, cym = y + h / 2;
#define UVX(px) (u0 + ((px) - x) / w * (u1 - u0))
#define UVY(py) (v0 + ((py) - y) / h * (v1 - v0))
#define COL(py) (ctop == cbot ? ctop : col_lerp(ctop, cbot, ((py) - y) / h))
    use_tex(t, n * 3 + n * 6);
    rgba_t cc = COL(cym);
    for (int i = 0; i < n; i++) {
        pt_t *a = &p[i], *b = &p[(i + 1) % n];
        put(cxm, cym, UVX(cxm), UVY(cym), cc);
        put(a->x, a->y, UVX(a->x), UVY(a->y), COL(a->y));
        put(b->x, b->y, UVX(b->x), UVY(b->y), COL(b->y));
    }
    if (feather > 0) {
        for (int i = 0; i < n; i++) {
            pt_t *a = &p[i], *b = &p[(i + 1) % n];
            float ax = a->x + a->nx * feather, ay = a->y + a->ny * feather;
            float bx = b->x + b->nx * feather, by = b->y + b->ny * feather;
            rgba_t ca = COL(a->y), cb = COL(b->y);
            rgba_t ca0 = ca & 0xffffff00u, cb0 = cb & 0xffffff00u;
            put(a->x, a->y, UVX(a->x), UVY(a->y), ca);
            put(b->x, b->y, UVX(b->x), UVY(b->y), cb);
            put(bx, by, UVX(b->x), UVY(b->y), cb0);
            put(a->x, a->y, UVX(a->x), UVY(a->y), ca);
            put(bx, by, UVX(b->x), UVY(b->y), cb0);
            put(ax, ay, UVX(a->x), UVY(a->y), ca0);
        }
    }
#undef UVX
#undef UVY
#undef COL
}

void gfx_rrect(float x, float y, float w, float h, float r, rgba_t c)
{
    if (w <= 0 || h <= 0) return;
    fill_shape(x, y, w, h, r, NULL, 0, 0, 1, 1, c, c, 1.2f);
}

void gfx_rrect_vgrad(float x, float y, float w, float h, float r, rgba_t top, rgba_t bottom)
{
    if (w <= 0 || h <= 0) return;
    fill_shape(x, y, w, h, r, NULL, 0, 0, 1, 1, top, bottom, 1.2f);
}

void gfx_circle(float cx, float cy, float r, rgba_t c)
{
    gfx_rrect(cx - r, cy - r, r * 2, r * 2, r, c);
}

void gfx_image(const tex_t *t, float x, float y, float w, float h,
               float u0, float v0, float u1, float v1, rgba_t c)
{
    if (!t || w <= 0 || h <= 0) return;
    use_tex(t, 6);
    put(x, y, u0, v0, c); put(x + w, y, u1, v0, c); put(x + w, y + h, u1, v1, c);
    put(x, y, u0, v0, c); put(x + w, y + h, u1, v1, c); put(x, y + h, u0, v1, c);
}

void gfx_image_rr(const tex_t *t, float x, float y, float w, float h, float r,
                  float u0, float v0, float u1, float v1, rgba_t c)
{
    if (!t || w <= 0 || h <= 0) return;
    fill_shape(x, y, w, h, r, t, u0, v0, u1, v1, c, c, 1.0f);
}

void gfx_rrect_stroke(float x, float y, float w, float h, float r, float thick, rgba_t c)
{
    pt_t in[MAXP], out[MAXP];
    int n = perimeter(x, y, w, h, r, in);
    int m = perimeter(x - thick, y - thick, w + thick * 2, h + thick * 2, r + thick, out);
    if (n != m) { /* different segment counts: fall back to equal radius */
        m = perimeter(x - thick, y - thick, w + thick * 2, h + thick * 2, r, out);
        if (n != m) return;
    }
    rgba_t c0 = c & 0xffffff00u;
    use_tex(NULL, n * 18);
    for (int i = 0; i < n; i++) {
        pt_t *a = &in[i], *b = &in[(i + 1) % n], *A = &out[i], *B = &out[(i + 1) % n];
        /* body */
        put(a->x, a->y, 0, 0, c); put(b->x, b->y, 0, 0, c); put(B->x, B->y, 0, 0, c);
        put(a->x, a->y, 0, 0, c); put(B->x, B->y, 0, 0, c); put(A->x, A->y, 0, 0, c);
        /* inner feather */
        float f = 1.f;
        put(a->x, a->y, 0, 0, c); put(b->x, b->y, 0, 0, c); put(b->x - b->nx * f, b->y - b->ny * f, 0, 0, c0);
        put(a->x, a->y, 0, 0, c); put(b->x - b->nx * f, b->y - b->ny * f, 0, 0, c0); put(a->x - a->nx * f, a->y - a->ny * f, 0, 0, c0);
        /* outer feather */
        put(A->x, A->y, 0, 0, c); put(B->x, B->y, 0, 0, c); put(B->x + B->nx * f, B->y + B->ny * f, 0, 0, c0);
        put(A->x, A->y, 0, 0, c); put(B->x + B->nx * f, B->y + B->ny * f, 0, 0, c0); put(A->x + A->nx * f, A->y + A->ny * f, 0, 0, c0);
    }
}

void gfx_shadow(float x, float y, float w, float h, float r, float spread, rgba_t c)
{
    /* body plus a wide feather ring fading to transparent */
    fill_shape(x, y, w, h, r, NULL, 0, 0, 1, 1, c, c, spread);
}
