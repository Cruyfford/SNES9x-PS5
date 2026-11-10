#include "config.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

config_t cfg;

typedef struct { const char *key; int *val; int lo, hi; } entry_t;

static entry_t entries[] = {
    {"clock_format", &cfg.clock_format, 0, 2},
    {"show_clock", &cfg.show_clock, 0, 1},
    {"backdrop", &cfg.backdrop, 0, 1},
    {"sort", &cfg.sort, 0, 1},
    {"last_game", &cfg.last_game, 0, 100000},
    {"aspect", &cfg.aspect, 0, 2},
    {"smooth", &cfg.smooth, 0, 1},
    {"volume", &cfg.volume, 0, 4},
    {"swap_ab", &cfg.swap_ab, 0, 1},
    {"preview_video", &cfg.preview_video, 0, 1},
};
#define NENTRIES (int)(sizeof(entries) / sizeof(entries[0]))

void config_defaults(void)
{
    cfg.clock_format = CLOCK_SYSTEM;
    cfg.show_clock = 1;
    cfg.backdrop = BACKDROP_SNAP;
    cfg.sort = SORT_NAME;
    cfg.last_game = 0;
    cfg.aspect = 0;
    cfg.smooth = 0;
    cfg.volume = 4;
    cfg.swap_ab = 0;
    cfg.preview_video = 1;
}

static void path(char *out, size_t n) { snprintf(out, n, "%s/config.ini", plat_data_dir()); }

void config_load(void)
{
    char p[300], line[256];
    config_defaults();
    path(p, sizeof(p));
    FILE *f = fopen(p, "r");
    if (!f) return;
    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        int v = atoi(eq + 1);
        for (int i = 0; i < NENTRIES; i++)
            if (!strcmp(line, entries[i].key) && v >= entries[i].lo && v <= entries[i].hi)
                *entries[i].val = v;
    }
    fclose(f);
}

void config_save(void)
{
    char p[300];
    path(p, sizeof(p));
    FILE *f = fopen(p, "w");
    if (!f) return;
    for (int i = 0; i < NENTRIES; i++) fprintf(f, "%s=%d\n", entries[i].key, *entries[i].val);
    fclose(f);
}
