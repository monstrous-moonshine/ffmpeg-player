#pragma once
#include <SDL2/SDL.h>
#include <stdbool.h>

typedef struct {
    int num;
    int den;
} Rational;

typedef struct {
    SDL_AudioDeviceID audio_devID;
    SDL_AudioSpec audio_spec;
    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_Texture *tex;
    bool done;
    bool resized;
    int width;
    int height;
    SDL_Rect viewport;
    Rational dar;
} App;

bool app_init(App *app, Rational *dar, SDL_AudioSpec *wanted_spec);
void app_fini(App *app);
void process_events(App *app);
