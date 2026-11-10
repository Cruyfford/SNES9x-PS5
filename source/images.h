#ifndef IMAGES_H
#define IMAGES_H

#include "platform.h"

/* An image held in a texture slot. The picture occupies (0,0)-(w,h) of the
 * texture; use u1 = w / tex.w and v1 = h / tex.h when drawing. */
typedef struct {
    const tex_t *tex;
    int w, h;
    float u1, v1;
} image_t;

enum { IMG_COVER = 0, IMG_BACKDROP = 1, IMG_ICON = 2 };

int  images_init(int synchronous);
/* returns 1 and fills *out when ready; otherwise queues the load and returns 0.
 * Missing files are remembered so they are not retried. */
int  image_get(const char *path, int kind, image_t *out);
int  image_failed(const char *path, int kind);
/* drop a cached picture (the file changed on disk) */
void image_forget(const char *path);
/* upload finished decodes; call once per frame before drawing */
void images_update(void);
/* load a small UI image synchronously (icons); returns NULL on failure */
const image_t *image_load_now(const char *path);

#endif
