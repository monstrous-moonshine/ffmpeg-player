#pragma once
#include <SDL2/SDL.h>
#include <stdbool.h>

typedef struct {
    int num;
    int den;
} Rational;

typedef struct {
    long pts;
    bool paused;

    SDL_AudioDeviceID audio_devID;
    SDL_AudioSpec audio_spec;
    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_Texture *tex;

    // these two fields are used to display to a
    // subregion of correct aspect ratio for any
    // given window size
    Rational display_aspect;
    SDL_Rect viewport;

    float volume;
    bool muted;

    int width;
    int height;
    bool resized;
} App;

bool app_init(App *app,
        SDL_AudioSpec *wanted_spec,
        Rational *display_aspect);
void app_fini(App *app);
void process_events(App *app);
