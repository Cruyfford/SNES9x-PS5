#ifndef VIDEO_H
#define VIDEO_H

#include "platform.h"

/* Preview clips: Ogg Theora (.ogv) decoded on a worker thread, looping. */
int  video_init(int synchronous);
int  video_open(const char *path);    /* stops the current clip */
void video_close(void);
/* uploads a new frame if one is ready; call once per frame */
void video_update(void);
/* current frame texture, NULL until the first frame */
const tex_t *video_frame(float *u1, float *v1, int *w, int *h);
float video_position(void);           /* seconds into the clip */
float video_length(void);             /* 0 if unknown */
int  video_playing(void);

#endif
