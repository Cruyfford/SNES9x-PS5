#ifndef LIBRARY_H
#define LIBRARY_H

typedef struct {
    char title[256];     /* "Super Metroid" */
    char region[32];     /* "USA", "Japan", ... or "" */
    char base[256];      /* file name without extension: art is matched by this */
    char path[512];      /* full ROM path */
    char where[16];      /* "HDD" / "USB" */
    long size;
    char cover[512];     /* art/boxart/<base>.png|jpg, "" if missing */
    char backdrop[512];  /* art/snap/<base>.png|jpg, "" if missing */
    char video[512];     /* art/video/<base>.ogv, "" if missing */
    unsigned color;      /* placeholder colour when there is no cover */
} game_t;

typedef struct {
    game_t *games;
    int count;
    char dirs[8][256];   /* folders that were scanned */
    int dir_found[8];
    int ndirs;
} library_t;

void library_scan(library_t *lib);
void library_free(library_t *lib);
const char *library_art_hint(void); /* where art goes, for help text */

#endif
