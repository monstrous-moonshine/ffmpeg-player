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

Queue frame_queue;
thread_param_t thread_params;
static SDL_Thread *fetch_thread = NULL;

static inline void avctx_freep(AVFormatContext **pformat_ctx) {
    avformat_close_input(pformat_ctx);
    avformat_free_context(*pformat_ctx);
}

static inline void sws_freectxp(struct SwsContext **pctx) {
    sws_freeContext(*pctx);
}

static inline void entry_freep(entry_t *entry) {
    av_frame_free(&entry->frame);
}

static inline const char *my_avstrerror(int err) {
    static char buffer[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(err, buffer, AV_ERROR_MAX_STRING_SIZE);
    return buffer;
}

static void main_exit_handler() {
    if (fetch_thread) {
        thread_params.done = true;
        SDL_WaitThread(fetch_thread, NULL);
        /* fetch_thread = NULL; */
    }

    queue_fini(&frame_queue);

    if (thread_params.seek_mtx)
        SDL_DestroyMutex(thread_params.seek_mtx);
    if (thread_params.seek_done)
        SDL_DestroyCond(thread_params.seek_done);
}

static AVCodecContext *get_codec_context(
        AVFormatContext *avctx, int stream_index) {
    AVCodecParameters *codec_param;
    const AVCodec *codec;
    AVCodecContext *codec_ctx;

    codec_param = avctx->streams[stream_index]->codecpar;
    codec = avcodec_find_decoder(codec_param->codec_id);
    if (!codec) {
        const char *name = avcodec_get_name(codec_param->codec_id);
        fprintf(stderr, "Failed to find decoder: %s\n", name);
        exit(1);
    }
    codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        fprintf(stderr, "Error allocating codec context\n");
        exit(1);
    }
    if (avcodec_parameters_to_context(codec_ctx, codec_param) < 0) {
        fprintf(stderr, "Error initializing codec context\n");
        exit(1);
    }
    if (avcodec_open2(codec_ctx, codec, NULL) < 0) {
        fprintf(stderr, "Error opening codec context\n");
        exit(1);
    }
    return codec_ctx;
}

static void rescale_frame(App *app, AVFrame *frame) {
    _cleanup_(sws_freectxp)
    struct SwsContext *sws_ctx = sws_getContext(
            frame->width, frame->height, frame->format,
            app->viewport.w, app->viewport.h, AV_PIX_FMT_RGBA,
            SWS_BILINEAR, NULL, NULL, NULL);
    if (!sws_ctx) {
        fprintf(stderr, "Error getting swscale context\n");
        exit(1);
    }

    uint8_t *pixels[1];
    int pitch[1];
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

static AVFrame *resample_frame(App *app, AVFrame *frame) {
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
    resampled->sample_rate = app->audio_spec.freq;
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

int main(int argc, char *argv[]) {
    int err;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s input_file\n", argv[0]);
        exit(1);
    }

    _cleanup_(avctx_freep) AVFormatContext *avctx = NULL;
    const char *url = argv[1];
    err = avformat_open_input(&avctx, url, NULL, NULL);
    if (err < 0) {
        fprintf(stderr, "Error opening file '%s': %s\n", url, 
                my_avstrerror(err));
        exit(1);
    }
    if (avformat_find_stream_info(avctx, NULL) < 0) {
        fprintf(stderr, "Error getting stream information\n");
        exit(1);
    }
    // av_dump_format(avctx, 0, argv[1], 0);

    int video_si = -1; // video stream index
    int audio_si = -1; // audio stream index
    video_si = av_find_best_stream(
            avctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    audio_si = av_find_best_stream(
            avctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (video_si < 0 || audio_si < 0) {
        fprintf(stderr, "No audio/video stream available\n");
        exit(1);
    }

    _cleanup_(avcodec_free_context)
    AVCodecContext *video_ctx = get_codec_context(avctx, video_si);
    _cleanup_(avcodec_free_context)
    AVCodecContext *audio_ctx = get_codec_context(avctx, audio_si);

    SDL_AudioSpec wanted_spec = {
#ifdef KEEP_CHANNEL_LAYOUT
        .channels = audio_ctx->ch_layout.nb_channels;
#else
        .channels = 2,
#endif
        .format   = AUDIO_F32,
        .freq     = audio_ctx->sample_rate,
        .samples  = 1024,
    };

    Rational display_aspect = {
        .num = video_ctx->width,
        .den = video_ctx->height
    };

    _cleanup_(app_fini) App app;
    if (!app_init(&app, &wanted_spec, &display_aspect)) {
        exit(1);
    }

    if (!queue_init(&frame_queue)) {
        fprintf(stderr, "Error initializing frame queue\n");
        exit(1);
    }

    SDL_mutex *seek_mtx = SDL_CreateMutex();
    SDL_cond *seek_done = SDL_CreateCond();

    thread_params = (thread_param_t) {
        .avctx = avctx,
        .audio_ctx = audio_ctx,
        .audio_si = audio_si,
        .video_ctx = video_ctx,
        .video_si = video_si,
        .do_seek = false,
        .seek_mtx = seek_mtx,
        .seek_done = seek_done,
        .done = false,
    };

    atexit(main_exit_handler);

    if (!seek_mtx || !seek_done) {
        fprintf(stderr, "Error creating mutex/cond\n");
        exit(1);
    }

    fetch_thread = SDL_CreateThread(
            fetch_frames, "fetch_thread", NULL);
    if (!fetch_thread) {
        fprintf(stderr, "Error launching inferior thread\n");
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

        assert(SDL_LockMutex(frame_queue.mutex) == 0);
        if (frame_queue.count == 0) {
            assert(SDL_UnlockMutex(frame_queue.mutex) == 0);
            SDL_Delay(DEFAULT_FRAME_DELAY);
            continue;
            //assert(SDL_CondWait(frame_queue.fill, frame_queue.mutex) == 0);
        }
        _cleanup_(entry_freep) entry_t entry = queue_dequeue(&frame_queue);
        assert(SDL_CondSignal(frame_queue.empty) == 0);
        assert(SDL_UnlockMutex(frame_queue.mutex) == 0);

        if (entry.stream_index == video_si) {
            rescale_frame(&app, entry.frame);

            int64_t pts = entry.frame->best_effort_timestamp;
            if (app.t_start == -1) {
                app.t_start = SDL_GetTicks64() - pts;
            }
            app.pts = pts;

            int64_t t_elapsed = SDL_GetTicks64() - app.t_start;
            uint32_t delay = max(pts - t_elapsed, 0);
            SDL_Delay(delay);

            update_frame(&app);
        } else {
            _cleanup_(av_frame_free) AVFrame *resampled;
            resampled = resample_frame(&app, entry.frame);
            int sample_size = av_get_bytes_per_sample(resampled->format);
            int datasize = resampled->ch_layout.nb_channels *
                resampled->nb_samples * sample_size;
            float volume = app.muted ? 0 : app.volume;
            for (int i = 0; i < datasize / sample_size; i++) {
                ((float*)resampled->data[0])[i] *= volume;
            }
            assert(SDL_QueueAudio(app.audio_devID, resampled->data[0],
                        datasize) == 0);
            SDL_PauseAudioDevice(app.audio_devID, 0);
        }
    }

    SDL_WaitThread(fetch_thread, NULL);
    fetch_thread = NULL;
    return 0;
}
