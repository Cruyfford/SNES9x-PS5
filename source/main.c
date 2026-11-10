#include "platform.h"
#include "font.h"
#include "images.h"
#include "config.h"
#include "ui.h"
#include <stdio.h>

#ifndef __PS3__
void host_set_script(const char *s);
void host_set_dirs(const char *app, const char *data);
#endif

int main(int argc, char **argv)
{
#ifndef __PS3__
    /* host preview: snes9x-ps5-host <script> [app_dir] [data_dir] */
    if (argc > 1) host_set_script(argv[1]);
    if (argc > 3) host_set_dirs(argv[2], argv[3]);
    const int sync_images = 1;
#else
    (void)argc; (void)argv;
    const int sync_images = 0;
#endif
    if (plat_init()) return 1;
    config_load();
    if (font_system_init() || images_init(sync_images)) { plat_log("init failed"); return 1; }
    if (ui_init()) { plat_log("ui init failed (fonts missing?)"); return 1; }

    uint64_t last = plat_ms();
    while (plat_running()) {
        pad_t pad;
        plat_read_pad(&pad);
        uint64_t now = plat_ms();
        float dt = (float)(now - last) / 1000.f;
        if (dt > 0.1f) dt = 0.1f;
        if (dt <= 0) dt = 1.f / 60.f;
        last = now;
        ui_frame(&pad, dt);
    }
    config_save();
    plat_shutdown();
    return 0;
}
