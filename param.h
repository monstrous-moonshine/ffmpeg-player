#pragma once
#include <stdbool.h>

typedef struct AVFormatContext AVFormatContext;
typedef struct AVCodecContext  AVCodecContext;
typedef struct SDL_mutex SDL_mutex;
typedef struct SDL_cond  SDL_cond;

typedef struct {
    AVFormatContext *avctx;
    AVCodecContext *video_ctx;
    AVCodecContext *audio_ctx;
    int video_si, audio_si;

    SDL_mutex *seek_mtx;
    SDL_cond  *seek_done;
    bool do_seek;
    int  seek_flags;
    long seek_pts;

    bool done;
} thread_param_t;

bool avparam_init(thread_param_t *param, const char *url);
void avparam_fini(thread_param_t *param);
