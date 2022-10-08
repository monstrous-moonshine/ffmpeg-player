#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct AVFormatContext AVFormatContext;
typedef struct AVCodecContext AVCodecContext;
typedef struct SDL_mutex SDL_mutex;
typedef struct SDL_cond SDL_cond;

typedef struct {
    AVFormatContext *avctx;
    AVCodecContext *video_ctx;
    AVCodecContext *audio_ctx;
    int video_si, audio_si;

    bool do_seek;
    int seek_flags;
    int64_t seek_pts;
    SDL_mutex *seek_mtx;
    SDL_cond *seek_done;

    bool done;
} thread_param_t;
