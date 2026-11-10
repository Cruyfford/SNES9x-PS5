#ifndef EMU_H
#define EMU_H

#include "platform.h"

#define EMU_SLOTS 3

enum { ASPECT_4_3 = 0, ASPECT_8_7 = 1, ASPECT_STRETCH = 2 };

int  emu_init(void);
/* load a ROM; base is the art/save name (file name without extension) */
int  emu_load(const char *path, const char *base);
void emu_unload(void);              /* writes SRAM */
int  emu_loaded(void);
void emu_run_frame(const pad_t *pad);
void emu_reset(void);

/* the last frame as a texture (NULL before the first frame) */
const tex_t *emu_frame(float *u1, float *v1, int *w, int *h);
/* draw the game picture into the box according to the display settings */
void emu_draw(float x, float y, float w, float h, float alpha);

int  emu_save_state(int slot);      /* 0 on success */
int  emu_load_state(int slot);
int  emu_state_exists(int slot);
/* path of the PNG thumbnail of a slot */
void emu_state_thumb(int slot, char *out, int n);
long long emu_state_time(int slot);  /* seconds since epoch, 0 if none */
int  emu_screenshot(char *out_path, int n);
void emu_save_sram(void);

#endif
