#ifndef GFX_H
#define GFX_H

#include "platform.h"

/* colour helpers */
#define RGBA(r, g, b, a) ((rgba_t)(((uint32_t)(r) << 24) | ((uint32_t)(g) << 16) | ((uint32_t)(b) << 8) | (uint32_t)(a)))
rgba_t col_alpha(rgba_t c, float a);           /* multiply alpha */
rgba_t col_lerp(rgba_t a, rgba_t b, float t);

void gfx_begin(void);
void gfx_flush(void);

/* global opacity applied to everything drawn (for fades); push/pop */
void gfx_push_alpha(float a);
void gfx_pop_alpha(void);

void gfx_rect(float x, float y, float w, float h, rgba_t c);
/* corners: top-left, top-right, bottom-right, bottom-left */
void gfx_rect4(float x, float y, float w, float h, rgba_t tl, rgba_t tr, rgba_t br, rgba_t bl);
void gfx_rrect(float x, float y, float w, float h, float r, rgba_t c);
void gfx_rrect_vgrad(float x, float y, float w, float h, float r, rgba_t top, rgba_t bottom);
void gfx_rrect_stroke(float x, float y, float w, float h, float r, float thick, rgba_t c);
void gfx_circle(float cx, float cy, float r, rgba_t c);

/* textured: (u0,v0)-(u1,v1) selects the source area */
void gfx_image(const tex_t *t, float x, float y, float w, float h,
               float u0, float v0, float u1, float v1, rgba_t c);
void gfx_image_rr(const tex_t *t, float x, float y, float w, float h, float r,
                  float u0, float v0, float u1, float v1, rgba_t c);

/* soft drop shadow below a rounded box */
void gfx_shadow(float x, float y, float w, float h, float r, float spread, rgba_t c);

/* easing */
float ease_out_cubic(float t);
float clampf(float v, float lo, float hi);
float approach(float cur, float target, float speed, float dt); /* exponential smoothing */

#endif
