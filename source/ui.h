#ifndef UI_H
#define UI_H

#include "platform.h"

#define APP_TITLE   "SNES9x PS5"
#define APP_VERSION "0.1.0"

int  ui_init(void);
void ui_frame(const pad_t *pad, float dt);
void ui_toast(const char *icon, const char *title, const char *body);

#endif
