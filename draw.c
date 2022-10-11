#include <assert.h>
#include <math.h>
#include <SDL2/SDL.h>
#include <libavutil/avutil.h>
#include <libavutil/motion_vector.h>
#include "draw.h"
#include "macro.h"

static void draw_arrow(SDL_Renderer *ren, int x1, int y1, int x2, int y2) {
    ASSERT(SDL_SetRenderDrawColor(ren, 0xff, 0xff, 0xff, 0x3f) == 0);
    ASSERT(SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND) == 0);

    // draw arrow shaft
    (void)SDL_RenderDrawLine(ren, x1, y1, x2, y2);

    // draw arrow head
    float len = hypot(x2 - x1, y2 - y1);
    float ang = atan2(y2 - y1, x2 - x1);
    float rr = min(len * 0.1f, 5.0f);
    float a1 = ang + 2 * M_PI * 2 / 5;
    float a2 = ang - 2 * M_PI * 2 / 5;
    int x3 = x2 + rr * cos(a1);
    int y3 = y2 + rr * sin(a1);
    int x4 = x2 + rr * cos(a2);
    int y4 = y2 + rr * sin(a2);
    (void)SDL_RenderDrawLine(ren, x2, y2, x3, y3);
    (void)SDL_RenderDrawLine(ren, x2, y2, x4, y4);
}

void draw_motion_vectors(
        AVFrame *frame,
        SDL_Renderer *ren,
        SDL_Rect *viewport
        ) {
    AVFrameSideData *side_data = av_frame_get_side_data(frame, AV_FRAME_DATA_MOTION_VECTORS);
    if (!side_data)
        return;
    AVMotionVector *motion_vec = (AVMotionVector *)side_data->data;
    int nb_motion_vec = side_data->size / sizeof *motion_vec;
    int x = viewport->x;
    int y = viewport->y;
    float xr = (float)viewport->w / frame->width;
    float yr = (float)viewport->h / frame->height;
    for (int i = 0; i < nb_motion_vec; i++) {
        int x1, y1, x2, y2;
        if (motion_vec[i].source < 0) {
            // the prediction is from a past frame
            // head -> src, tail -> dst
            x1 = x + motion_vec[i].src_x * xr;
            y1 = y + motion_vec[i].src_y * yr;
            x2 = x + motion_vec[i].dst_x * xr;
            y2 = y + motion_vec[i].dst_y * yr;
        } else {
            // the prediction is from a future frame
            // head -> dst, tail -> src
            x1 = x + motion_vec[i].dst_x * xr;
            y1 = y + motion_vec[i].dst_y * yr;
            x2 = x + motion_vec[i].src_x * xr;
            y2 = y + motion_vec[i].src_y * yr;
        }
        draw_arrow(ren, x1, y1, x2, y2);
    }
}
