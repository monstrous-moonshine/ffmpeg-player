#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#include <libavutil/avutil.h>
#include <SDL2/SDL.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app.h"
#include "decode.h"
#include "draw.h"
#include "macro.h"
#include "param.h"
#include "queue.h"

Queue video_queue;
Queue audio_queue;
avparam_t avparam;
static SDL_Thread *fetch_thread = NULL;

static void sws_freectxp(struct SwsContext **pctx) {
    sws_freeContext(*pctx);
}

static void main_exit_handler() {
    if (fetch_thread) {
        avparam.done = true;
        SDL_WaitThread(fetch_thread, NULL);
        /* fetch_thread = NULL; */
    }

    avparam_fini(&avparam);
    queue_fini(&video_queue);
    queue_fini(&audio_queue);
}

static void rescale_frame(App *app, AVFrame *frame) {
    int err = av_frame_apply_cropping(frame, 0);
    if (err < 0) {
        if (err == AVERROR(ERANGE))
            LOG_ERROR("Invalid crop parameters in frame\n");
        else
            LOG_ERROR("Error cropping frame\n");
        // but continue, this is no fatal issue
    }
    _cleanup_(sws_freectxp) struct SwsContext *sws_ctx = NULL;
    sws_ctx = sws_getContext(
            frame->width, frame->height, frame->format,
            app->viewport.w, app->viewport.h, AV_PIX_FMT_RGBA,
            SWS_BILINEAR, NULL, NULL, NULL);
    if (!sws_ctx) {
        LOG_ERROR("Error getting swscale context\n");
        exit(1);
    }

    uint8_t *pixels[1];
    int      pitch [1];
    ASSERT(SDL_LockTexture(app->tex,
                &app->viewport, (void **)pixels, pitch) == 0);
    int ret = sws_scale(
            sws_ctx, (const uint8_t * const *)frame->data,
            frame->linesize, 0, frame->height, pixels, pitch);
    if (ret != app->viewport.h) {
        LOG_ERROR("Error scaling frame\n");
        exit(1);
    }
    SDL_UnlockTexture(app->tex);
}

static AVFrame *resample_frame(SDL_AudioSpec *spec, AVFrame *frame) {
    int err;

    _cleanup_(av_frame_free) AVFrame *resampled = av_frame_alloc();
    if (!resampled) {
        LOG_ERROR("Error allocating frame\n");
        exit(1);
    }
#ifdef KEEP_CHANNEL_LAYOUT
    if (av_channel_layout_copy(&resampled->ch_layout,
                &frame->ch_layout) < 0) {
        LOG_ERROR("Error copying channel layout\n");
        exit(1);
    }
#else
    resampled->ch_layout = (AVChannelLayout) AV_CHANNEL_LAYOUT_STEREO;
#endif
    resampled->sample_rate = spec->freq;
    resampled->format = AV_SAMPLE_FMT_FLT;
    _cleanup_(swr_free) SwrContext *swr = NULL;
    err = swr_alloc_set_opts2(&swr,
            &resampled->ch_layout,
            resampled->format,
            resampled->sample_rate,
            &frame->ch_layout,
            frame->format,
            frame->sample_rate,
            0,
            NULL
            );
    if (err < 0) {
        LOG_ERROR("Error setting swresample context\n");
        exit(1);
    }
    if (swr_init(swr) < 0) {
        LOG_ERROR("Error initializing swresample context\n");
        exit(1);
    }
    err = swr_convert_frame(swr, resampled, frame);
    if (err < 0) {
        LOG_ERROR("Error resampling frame\n");
        exit(1);
    }
    return TAKE_PTR(resampled);
}

static void audio_callback(void *ptr, uint8_t *stream, int len) {
    App *app = (App *)ptr;
    static uint8_t buffer[MAX_BUFFER_SIZE];
    static int buf_idx = 0;
    static int buf_size = 0;

    int out_idx = 0;
    if (buf_idx < buf_size) {
        int buf_len = buf_size - buf_idx;
        int nwrite = min(len, buf_len);
        memcpy(stream, &buffer[buf_idx], nwrite);
        buf_idx += nwrite;
        if (buf_idx == buf_size) {
            buf_idx = 0;
            buf_size = 0;
        }
        len -= nwrite;
        out_idx += nwrite;
    }

    while (len > 0) {
        ASSERT(SDL_LockMutex(audio_queue.mutex) == 0);
        if (audio_queue.count == 0) {
            ASSERT(SDL_UnlockMutex(audio_queue.mutex) == 0);
            memset(&stream[out_idx], 0, len);
            return;
        }
        _cleanup_(av_frame_free) AVFrame *frame = NULL;
        frame = queue_dequeue(&audio_queue);
        ASSERT(SDL_CondSignal(audio_queue.empty) == 0);
        ASSERT(SDL_UnlockMutex(audio_queue.mutex) == 0);

        _cleanup_(av_frame_free) AVFrame *resampled = NULL;
        resampled = resample_frame(&app->audio_spec, frame);
        int sample_size = av_get_bytes_per_sample(resampled->format);
        int datasize = resampled->ch_layout.nb_channels *
            resampled->nb_samples * sample_size;
        float volume = app->muted ? 0 : app->volume;
        for (int i = 0; i < datasize / sample_size; i++) {
            ((float*)resampled->data[0])[i] *= volume;
        }
        int nwrite = min(len, datasize);
        memcpy(&stream[out_idx], resampled->data[0], nwrite);
        len -= nwrite;
        out_idx += nwrite;
        if (nwrite < datasize) {
            buf_size = datasize - nwrite;
            memcpy(buffer, &resampled->data[0][nwrite], buf_size);
        }
    }
}

static void update_frame(App *app) {
    ASSERT(SDL_SetRenderDrawColor(
                app->ren, 0x00, 0x2b, 0x36, 0xff) == 0);
    ASSERT(SDL_RenderClear(app->ren) == 0);
    ASSERT(SDL_RenderCopy(app->ren, app->tex, NULL, NULL) == 0);
}

static void render_frame(App *app) {
    SDL_RenderPresent(app->ren);
}

int main(int argc, char *argv[]) {
    _cleanup_(av_frame_free) AVFrame *frame = NULL;
    _cleanup_(app_fini) App app = {};
    avparam = (avparam_t) {};
    video_queue = (Queue) {};
    audio_queue = (Queue) {};

    if (argc < 2) {
        fprintf(stderr, "Usage: %s input_file\n", argv[0]);
        exit(1);
    }

    atexit(main_exit_handler);

    if (!avparam_init(&avparam, argv[1]))
        exit(1);

    if (!queue_init(&video_queue , "video_cnt") ||
            !queue_init(&audio_queue , "audio_cnt")) {
        LOG_ERROR("Error initializing frame queue\n");
        exit(1);
    }

    fetch_thread = SDL_CreateThread(
            fetch_frames, "fetch_thread", NULL);
    if (!fetch_thread) {
        LOG_ERROR("Error launching inferior thread\n");
        exit(1);
    }

    SDL_AudioSpec wanted_spec = {
        .callback = audio_callback,
#ifdef KEEP_CHANNEL_LAYOUT
        .channels = avparam.audio_ctx->ch_layout.nb_channels;
#else
        .channels = 2,
#endif
        .format   = AUDIO_F32,
        .freq     = avparam.audio_ctx->sample_rate,
        .samples  = 1024,
        .userdata = &app,
    };

    AVRational sample_aspect = avparam.video_ctx->sample_aspect_ratio;
    AVRational display_res = {
        .num = avparam.video_ctx->width,
        .den = avparam.video_ctx->height,
    };
    AVRational display_aspect_av = av_mul_q(sample_aspect, display_res);
    Rational display_aspect = {
        .num = display_aspect_av.num,
        .den = display_aspect_av.den,
    };

    if (!app_init(&app, &wanted_spec, &display_aspect)) {
        exit(1);
    }

    while (!app.done) {
        process_events(&app);
        if (app.resized) {
            SDL_DestroyTexture(app.tex);
            app.tex = SDL_CreateTexture(
                    app.ren, SDL_PIXELFORMAT_RGBA32,
                    SDL_TEXTUREACCESS_STREAMING,
                    app.width, app.height);
            if (!app.tex) {
                fprintf(stderr, "Error creating texture\n");
                exit(1);
            }
            app.resized = false;
        }
        if (app.paused) {
            SDL_Delay(DEFAULT_FRAME_DELAY);
            goto do_render;
        }

        ASSERT(SDL_LockMutex(video_queue.mutex) == 0);
        if (video_queue.count == 0) {
            ASSERT(SDL_UnlockMutex(video_queue.mutex) == 0);
            SDL_Delay(DEFAULT_FRAME_DELAY);
            goto do_render;
        }
        av_frame_free(&frame);
        frame = queue_dequeue(&video_queue);
        ASSERT(SDL_CondSignal(video_queue.empty) == 0);
        ASSERT(SDL_UnlockMutex(video_queue.mutex) == 0);
        rescale_frame(&app, frame);

        long pts = frame->best_effort_timestamp;
        if (app.t_start == -1) {
            app.t_start = SDL_GetTicks64() - pts;
        }
        app.pts = pts;

        long t_elapsed = SDL_GetTicks64() - app.t_start;
        unsigned delay = max(pts - t_elapsed, 0);
        SDL_Delay(delay);

do_render:
        update_frame(&app);
#ifdef PLAYER_DISP_MVS
        draw_motion_vectors(frame, app.ren, &app.viewport);
#endif
        render_frame(&app);
    }

    return 0;
}
