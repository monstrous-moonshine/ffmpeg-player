#pragma once
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <SDL2/SDL.h>
#include <stdbool.h>

typedef struct {
    int num;
    int den;
} Rational;

typedef struct {
    AVFormatContext *avctx;
    AVCodecContext *audioctx;
    AVCodecContext *videoctx;
    SDL_mutex *avmtx;
    int64_t t_start;
    int64_t pts;
    bool paused;

    SDL_AudioDeviceID audio_devID;
    SDL_AudioSpec audio_spec;
    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_Texture *tex;
    Rational display_aspect;
    SDL_Rect viewport;

    bool done;
    bool resized;
    int width;
    int height;
} App;

bool app_init(App *app,
        AVFormatContext *avctx,
        AVCodecContext *audioctx,
        AVCodecContext *videoctx,
        SDL_mutex *avmtx,
        SDL_AudioSpec *wanted_spec,
        Rational *display_aspect);
void app_fini(App *app);
void process_events(App *app);
