#include <assert.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#include <SDL2/SDL.h>
#include "app.h"
#include "decode.h"
#include "macro.h"
#include "param.h"
#include "queue.h"

Queue video_queue;
Queue audio_queue;
thread_param_t avparam;
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
    _cleanup_(sws_freectxp) struct SwsContext *sws_ctx = NULL;
    sws_ctx = sws_getContext(
            frame->width, frame->height, frame->format,
            app->viewport.w, app->viewport.h, AV_PIX_FMT_RGBA,
            SWS_BILINEAR, NULL, NULL, NULL);
    if (!sws_ctx) {
        fprintf(stderr, "Error getting swscale context\n");
        exit(1);
    }

    uint8_t *pixels[1];
    int      pitch [1];
    assert(SDL_LockTexture(app->tex, &app->viewport,
                (void **)pixels, pitch) == 0);
    int ret = sws_scale(
            sws_ctx, (const uint8_t * const *)frame->data,
            frame->linesize, 0, frame->height, pixels, pitch);
    if (ret != app->viewport.h) {
        fprintf(stderr, "Error scaling frame\n");
        exit(1);
    }
    SDL_UnlockTexture(app->tex);
}

static AVFrame *resample_frame(SDL_AudioSpec *spec, AVFrame *frame) {
    int err;

    _cleanup_(av_frame_free) AVFrame *resampled = av_frame_alloc();
    if (!resampled) {
        fprintf(stderr, "Error allocating frame\n");
        exit(1);
    }
#ifdef KEEP_CHANNEL_LAYOUT
    if (av_channel_layout_copy(&resampled->ch_layout,
                &frame->ch_layout) < 0) {
        fprintf(stderr, "Error copying channel layout\n");
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
        fprintf(stderr, "Error setting swresample context\n");
        exit(1);
    }
    if (swr_init(swr) < 0) {
        fprintf(stderr, "Error initializing swresample context\n");
        exit(1);
    }
    err = swr_convert_frame(swr, resampled, frame);
    if (err < 0) {
        fprintf(stderr, "Error resampling frame\n");
        exit(1);
    }
    return TAKE_PTR(resampled);
}

static void update_frame(App *app) {
    assert(SDL_SetRenderDrawColor(
                app->ren, 0x00, 0x2b, 0x36, 0xff) == 0);
    assert(SDL_RenderClear(app->ren) == 0);
    assert(SDL_RenderCopy(app->ren, app->tex, NULL, NULL) == 0);
    SDL_RenderPresent(app->ren);
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
        assert(SDL_LockMutex(audio_queue.mutex) == 0);
        if (audio_queue.count == 0) {
            assert(SDL_UnlockMutex(audio_queue.mutex) == 0);
            memset(&stream[out_idx], 0, len);
            return;
        }
        _cleanup_(av_frame_free)
        AVFrame *frame = queue_dequeue(&audio_queue);
        assert(SDL_CondSignal(audio_queue.empty) == 0);
        assert(SDL_UnlockMutex(audio_queue.mutex) == 0);

        _cleanup_(av_frame_free)
        AVFrame *resampled = resample_frame(&app->audio_spec, frame);
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
            memcpy(buffer, &resampled->data[0][nwrite], datasize - nwrite);
            buf_size = datasize - nwrite;
        }
    }
}

int main(int argc, char *argv[]) {
    _cleanup_(app_fini) App app = {};
    avparam = (thread_param_t) {};
    video_queue = (Queue) {};
    audio_queue = (Queue) {};

    if (argc < 2) {
        fprintf(stderr, "Usage: %s input_file\n", argv[0]);
        exit(1);
    }

    atexit(main_exit_handler);

    if (!avparam_init(&avparam, argv[1]))
        exit(1);

    if (!queue_init(&video_queue
#ifdef QUEUE_LOG_COUNT
                , "queue_log_count_video"
#endif
                ) || !queue_init(&audio_queue
#ifdef QUEUE_LOG_COUNT
                    , "queue_log_count_audio"
#endif
                    )) {
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

    Rational display_aspect = {
        .num = avparam.video_ctx->width,
        .den = avparam.video_ctx->height
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
            continue;
        }

        assert(SDL_LockMutex(video_queue.mutex) == 0);
        if (video_queue.count == 0) {
            assert(SDL_UnlockMutex(video_queue.mutex) == 0);
            SDL_Delay(DEFAULT_FRAME_DELAY);
            continue;
        }
        _cleanup_(av_frame_free) AVFrame *frame = queue_dequeue(&video_queue);
        assert(SDL_CondSignal(video_queue.empty) == 0);
        assert(SDL_UnlockMutex(video_queue.mutex) == 0);

        rescale_frame(&app, frame);

        long pts = frame->best_effort_timestamp;
        if (app.t_start == -1) {
            app.t_start = SDL_GetTicks64() - pts;
        }
        app.pts = pts;

        long t_elapsed = SDL_GetTicks64() - app.t_start;
        unsigned delay = max(pts - t_elapsed, 0);
        SDL_Delay(delay);

        update_frame(&app);
    }

    return 0;
}
