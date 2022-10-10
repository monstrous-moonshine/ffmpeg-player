#pragma once
#include <SDL2/SDL.h>
#include <libavutil/frame.h>

void draw_motion_vectors(
        AVFrame *frame,
        SDL_Renderer *ren,
        SDL_Rect *viewport
        );
