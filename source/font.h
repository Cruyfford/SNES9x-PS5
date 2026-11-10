#ifndef FONT_H
#define FONT_H

#include "platform.h"

typedef struct font font_t;

enum { ALIGN_LEFT = 0, ALIGN_CENTER = 1, ALIGN_RIGHT = 2 };

int     font_system_init(void);
font_t *font_load(const char *path, int px);
int     font_px(const font_t *f);
float   font_ascent(const font_t *f);   /* pixels above baseline */
float   font_line_h(const font_t *f);
/* width of a UTF-8 string, with extra tracking (pixels between letters) */
float   text_width(font_t *f, const char *s, float tracking);
/* draw with the top of the line box at y */
float   text_draw(font_t *f, float x, float y, const char *s, rgba_t c, float tracking, int align);
/* draw, cutting with "..." to fit max_w */
float   text_draw_fit(font_t *f, float x, float y, const char *s, rgba_t c, float max_w, int align);
/* word-wrap into at most max_lines lines; returns the number of lines drawn */
int     text_draw_wrap(font_t *f, float x, float y, const char *s, rgba_t c, float max_w,
                       float line_h, int max_lines);

#endif
