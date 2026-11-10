/* Finds SNES ROMs and their artwork. */
#include "library.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <strings.h>

static const char *exts[] = {".sfc", ".smc", ".fig", ".swc", ".bs", ".st", NULL};

static int has_rom_ext(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot) return 0;
    for (int i = 0; exts[i]; i++) if (!strcasecmp(dot, exts[i])) return 1;
    return 0;
}

static int file_exists(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && S_ISREG(st.st_mode);
}

static int dir_exists(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

static void find_art(char *out, size_t n, const char *sub, const char *base, const char *const *types)
{
    out[0] = 0;
    for (int i = 0; types[i]; i++) {
        snprintf(out, n, "%s/art/%s/%s%s", plat_data_dir(), sub, base, types[i]);
        if (file_exists(out)) return;
    }
    out[0] = 0;
}

static const char *known_regions[] = {
    "USA", "Japan", "Europe", "World", "Germany", "France", "Spain", "Italy",
    "Korea", "Brazil", "Australia", "Asia", "Canada", "China", "Netherlands", "Sweden", NULL};

static void parse_name(game_t *g)
{
    /* title = base without (...) and [...] groups */
    char t[256];
    size_t o = 0;
    int depth = 0;
    for (const char *p = g->base; *p && o < sizeof(t) - 1; p++) {
        if (*p == '(' || *p == '[') { depth++; continue; }
        if (*p == ')' || *p == ']') { if (depth) depth--; continue; }
        if (!depth) t[o++] = *p;
    }
    t[o] = 0;
    /* trim, squeeze spaces, "_" to space */
    char *s = t;
    while (*s == ' ' || *s == '_') s++;
    size_t len = strlen(s);
    while (len && (s[len - 1] == ' ' || s[len - 1] == '_' || s[len - 1] == '-')) s[--len] = 0;
    for (char *q = s; *q; q++) if (*q == '_') *q = ' ';
    /* "Legend of Zelda, The - A Link..." -> "The Legend of Zelda - A Link..." */
    char *comma = strstr(s, ", The");
    if (comma && (comma[5] == 0 || comma[5] == ' ')) {
        char rest[256];
        snprintf(rest, sizeof(rest), "%s", comma + 5);
        *comma = 0;
        snprintf(g->title, sizeof(g->title), "The %s%s", s, rest);
    } else {
        snprintf(g->title, sizeof(g->title), "%s", s);
    }
    if (!g->title[0]) snprintf(g->title, sizeof(g->title), "%s", g->base);

    g->region[0] = 0;
    const char *p = g->base;
    while ((p = strchr(p, '(')) != NULL) {
        p++;
        const char *e = strchr(p, ')');
        if (!e) break;
        for (int i = 0; known_regions[i]; i++) {
            size_t rl = strlen(known_regions[i]);
            /* tag may be "USA, Europe" */
            const char *q = p;
            while (q < e) {
                if ((size_t)(e - q) >= rl && !strncmp(q, known_regions[i], rl)) {
                    size_t gl = (size_t)(e - p) < sizeof(g->region) - 1 ? (size_t)(e - p) : sizeof(g->region) - 1;
                    memcpy(g->region, p, gl); g->region[gl] = 0;
                    return;
                }
                q++;
            }
        }
        p = e;
    }
}

static unsigned name_color(const char *s)
{
    static const unsigned pal[] = {
        0x3b4cc0ff, 0x7c3aedff, 0xdb2777ff, 0xea580cff, 0x0d9488ff,
        0x2563ebff, 0x9333eaff, 0x16a34aff, 0xc2410cff, 0x0891b2ff};
    unsigned h = 0;
    while (*s) h = h * 31 + (unsigned char)*s++;
    return pal[h % (sizeof(pal) / sizeof(pal[0]))];
}

static int cmp_games(const void *a, const void *b)
{
    return strcasecmp(((const game_t *)a)->title, ((const game_t *)b)->title);
}

#pragma GCC diagnostic ignored "-Wformat-truncation"
static void scan_dir(library_t *lib, const char *dir, const char *where, int *cap)
{
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.' || !has_rom_ext(e->d_name)) continue;
        if (lib->count >= *cap) {
            int nc = *cap ? *cap * 2 : 64;
            game_t *ng = realloc(lib->games, sizeof(game_t) * (size_t)nc);
            if (!ng) break;
            lib->games = ng; *cap = nc;
        }
        game_t *g = &lib->games[lib->count];
        memset(g, 0, sizeof(*g));
        snprintf(g->path, sizeof(g->path), "%s/%s", dir, e->d_name);
        snprintf(g->base, sizeof(g->base), "%s", e->d_name);
        char *dot = strrchr(g->base, '.');
        if (dot) *dot = 0;
        snprintf(g->where, sizeof(g->where), "%s", where);
        struct stat st;
        if (stat(g->path, &st) == 0) g->size = (long)st.st_size;
        parse_name(g);
        static const char *const img_types[] = {".png", ".jpg", ".PNG", ".JPG", NULL};
        static const char *const vid_types[] = {".ogv", ".OGV", NULL};
        find_art(g->cover, sizeof(g->cover), "boxart", g->base, img_types);
        find_art(g->backdrop, sizeof(g->backdrop), "snap", g->base, img_types);
        find_art(g->video, sizeof(g->video), "video", g->base, vid_types);
        g->color = name_color(g->title);
        lib->count++;
    }
    closedir(d);
}

void library_scan(library_t *lib)
{
    library_free(lib);
    int cap = 0;
    char main_dir[256];
    snprintf(main_dir, sizeof(main_dir), "%s/roms", plat_data_dir());
    lib->ndirs = 0;
    snprintf(lib->dirs[lib->ndirs], sizeof(lib->dirs[0]), "%s", main_dir);
    lib->dir_found[lib->ndirs++] = dir_exists(main_dir);
    scan_dir(lib, main_dir, "HDD", &cap);

    const char **extra = NULL;
    int n = plat_extra_rom_dirs(&extra);
    for (int i = 0; i < n && lib->ndirs < 8; i++) {
        int found = dir_exists(extra[i]);
        snprintf(lib->dirs[lib->ndirs], sizeof(lib->dirs[0]), "%s", extra[i]);
        lib->dir_found[lib->ndirs++] = found;
        if (found) scan_dir(lib, extra[i], strncmp(extra[i], "/dev_usb", 8) ? "HDD" : "USB", &cap);
    }
    if (lib->count > 1) qsort(lib->games, (size_t)lib->count, sizeof(game_t), cmp_games);
}

void library_free(library_t *lib)
{
    free(lib->games);
    lib->games = NULL;
    lib->count = 0;
}

const char *library_art_hint(void) { return plat_data_dir(); }
