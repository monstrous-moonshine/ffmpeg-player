#pragma once
#include <SDL2/SDL.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    int num;
    int den;
} Rational;

typedef struct {
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

    float volume;
    bool muted;

    int width;
    int height;
    bool resized;
    bool done;
} App;

bool app_init(App *app,
        SDL_AudioSpec *wanted_spec,
        Rational *display_aspect);
void app_fini(App *app);
void process_events(App *app);
