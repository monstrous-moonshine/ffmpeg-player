#pragma once
#include <libavutil/frame.h>
#include <SDL2/SDL.h>

void draw_motion_vectors(
        AVFrame *frame,
        SDL_Renderer *ren,
        SDL_Rect *viewport
        );
