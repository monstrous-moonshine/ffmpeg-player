#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <SDL2/SDL.h>
#include <assert.h>
#include "decode.h"
#include "macro.h"
#include "param.h"
#include "queue.h"

/* DONE: add av_strerror() strings to error messages */

extern avparam_t avparam;
extern Queue video_queue;
extern Queue audio_queue;

static inline void unlockp(SDL_mutex **pmtx) {
    ASSERT(SDL_UnlockMutex(*pmtx) == 0);
}

static void seek() {
    // TODO: explicitly pass a stream index instead of -1
    // and adjust the seek pts accordingly
    // NOTE: we hold the lock for avparam
    int err = av_seek_frame(avparam.avctx, -1,
            avparam.seek_pts * 1000,
            avparam.seek_flags);
    if (err < 0) {
        LOG_ERROR("Error seeking to frame: %s\n",
                av_err2str(err));
        return;
    }
    /* avformat_flush(thread_params.avctx); */

    avcodec_flush_buffers(avparam.video_ctx);
    avcodec_flush_buffers(avparam.audio_ctx);
    if (avparam.sub_ctx)
        avcodec_flush_buffers(avparam.sub_ctx);

    // locking the video_queue isn't necessary, since the
    // main thread, which uses it, is stalled waiting for
    // the seek to finish
    queue_flush(&video_queue);

    // locking the audio queue IS necessary, since the
    // audio callback, which uses it, runs asynchronously,
    // and might be trying to pop frames from it
    ASSERT(SDL_LockMutex(audio_queue.mutex) == 0);
    queue_flush(&audio_queue);
    ASSERT(SDL_UnlockMutex(audio_queue.mutex) == 0);
}

static void dump_subtitle(AVPacket *pkt) {
    AVSubtitle *sub = av_malloc(sizeof *sub);
    ASSERT(sub);
    int got_sub;
    int err = avcodec_decode_subtitle2(avparam.sub_ctx,
            sub, &got_sub, pkt);
    if (err < 0 || got_sub == 0) {
        av_freep(&sub);
    } else {
        for (int i = 0; i < (int)sub->num_rects; i++) {
            switch (sub->rects[i]->type) {
            case SUBTITLE_BITMAP:
                printf("BMP: %dx%d\n", sub->rects[i]->w,
                        sub->rects[i]->h);
                break;
            case SUBTITLE_TEXT:
                printf("TXT: %s\n", sub->rects[i]->text);
                break;
            case SUBTITLE_ASS:
                printf("ASS: %s\n", sub->rects[i]->ass);
                break;
            default:
                break;
            }
        }
        avsubtitle_free(sub);
    }
}

static int read_frame(AVCodecContext **pcodec_ctx, AVFrame *frame,
        int *stream_index) {
    int err;

    err = avcodec_receive_frame(*pcodec_ctx, frame);
    while (err == AVERROR(EAGAIN)) {
        _cleanup_(av_packet_free) AVPacket *pkt = av_packet_alloc();
        if (!pkt) {
            LOG_ERROR("Error allocating packet\n");
            return AVERROR(ENOMEM);
        }

        do {
            err = av_read_frame(avparam.avctx, pkt);
            if (err == AVERROR_EOF) {
                return err;
            } else if (err < 0) {
                LOG_ERROR("Error reading frame: %s\n",
                        av_err2str(err));
                return err;
            }
            *pcodec_ctx =
                pkt->stream_index == avparam.video_si
                ? avparam.video_ctx
                : pkt->stream_index == avparam.audio_si
                ? avparam.audio_ctx
                : NULL;
            *stream_index = pkt->stream_index;
            if (pkt->stream_index == avparam.sub_si) {
                dump_subtitle(pkt);
            }
        } while (!*pcodec_ctx);

        err = avcodec_send_packet(*pcodec_ctx, pkt);
        if (err < 0) {
            LOG_ERROR("Error sending packet to decoder: %s\n",
                    av_err2str(err));
            return err;
        }

        err = avcodec_receive_frame(*pcodec_ctx, frame);
    }
    if (err < 0) {
        LOG_ERROR("Error receiving frame from decoder: %s\n",
                av_err2str(err));
        return err;
    }

    return 0;
}

static int put_frame(Queue *queue, AVFrame *frame) {
    _cleanup_(unlockp) SDL_mutex *queue_mtx = queue->mutex;
    ASSERT(SDL_LockMutex(queue_mtx) == 0);

    while (queue->count == QUEUE_MAX) {
        int err;
        do {
            err = SDL_CondWaitTimeout(queue->empty,
                    queue_mtx, DEFAULT_FRAME_DELAY);
            ASSERT(err >= 0);

            if (avparam.do_seek) {
                av_frame_free(&frame);
                return 0;
            }
            if (avparam.done) {
                av_frame_free(&frame);
                return 0;
            }
        } while (err == SDL_MUTEX_TIMEDOUT);
    }
    queue_enqueue(queue, frame);
    ASSERT(SDL_CondSignal(queue->fill) == 0);

    return 0;
}

int fetch_frames(void *ptr) {
    int stream_index = avparam.video_si;
    AVCodecContext *codec_ctx = avparam.video_ctx;
    int err;

    (void)ptr;

    for (;;) {
        if (avparam.done)
            return 0;

        ASSERT(SDL_LockMutex(avparam.seek_mtx) == 0);
        if (avparam.do_seek) {
            seek();
            avparam.do_seek = false;
            ASSERT(SDL_CondSignal(avparam.seek_done) == 0);
        }
        ASSERT(SDL_UnlockMutex(avparam.seek_mtx) == 0);

        _cleanup_(av_frame_free) AVFrame *frame = av_frame_alloc();
        if (!frame) {
            LOG_ERROR("Error allocating frame\n");
            avparam.done = true;
            return AVERROR(ENOMEM);
        }

        err = read_frame(&codec_ctx, frame, &stream_index);
        if (err == AVERROR_EOF) {
            // wait a bit so we don't spin too fast at EOF
            SDL_Delay(DEFAULT_FRAME_DELAY);
            continue;
        }
        else if (err < 0) {
            avparam.done = true;
            return err;
        }

        Queue *queue = stream_index == avparam.video_si
            ? &video_queue : &audio_queue;
        (void)put_frame(queue, TAKE_PTR(frame));
    }

    /* return 0; */
}
