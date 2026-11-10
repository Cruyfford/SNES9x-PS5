#ifndef CONFIG_H
#define CONFIG_H

enum { CLOCK_SYSTEM = 0, CLOCK_12H = 1, CLOCK_24H = 2 };
enum { BACKDROP_SNAP = 0, BACKDROP_COVER = 1 };
enum { SORT_NAME = 0, SORT_REGION = 1 };

typedef struct {
    int clock_format;   /* CLOCK_* */
    int show_clock;     /* 0/1 */
    int backdrop;       /* BACKDROP_* */
    int sort;           /* SORT_* */
    int last_game;      /* index restored on start */
} config_t;

extern config_t cfg;

void config_defaults(void);
void config_load(void);
void config_save(void);

#endif
