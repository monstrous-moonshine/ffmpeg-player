#pragma once
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <SDL2/SDL.h>
#include <stdbool.h>

#define PLAYER_DISP_MVS

typedef struct {
    AVFormatContext *avctx;
    AVCodecContext *video_ctx;
    AVCodecContext *audio_ctx;
    AVCodecContext *subcc_ctx;
    int video_si, audio_si, subcc_si;

    SDL_mutex *seek_mtx;
    SDL_cond  *seek_done;
    bool do_seek;
    int  seek_flags;
    long seek_pts;

    bool done;
} avparam_t;

bool avparam_init(avparam_t *param, const char *url);
void avparam_fini(avparam_t *param);
