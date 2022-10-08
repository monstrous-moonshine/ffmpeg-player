#include <assert.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <SDL2/SDL.h>
#include "decode.h"
#include "macro.h"
#include "param.h"
#include "queue.h"

extern thread_param_t thread_params;
extern Queue frame_queue;

static inline void unlockp(SDL_mutex **pmtx) {
    if (*pmtx)
        assert(SDL_UnlockMutex(*pmtx) == 0);
}

static void seek() {
    if (av_seek_frame(thread_params.avctx, -1,
                thread_params.seek_pts * 1000,
                thread_params.seek_flags) < 0) {
        fprintf(stderr, "Error seeking to frame\n");
        return;
    }
    /* avformat_flush(thread_params.avctx); */
    avcodec_flush_buffers(thread_params.audio_ctx);
    avcodec_flush_buffers(thread_params.video_ctx);

    /* assert(SDL_LockMutex(frame_queue.mutex) == 0); */
    queue_flush(&frame_queue);
    /* assert(SDL_CondSignal(frame_queue.empty) == 0); */
    /* assert(SDL_UnlockMutex(frame_queue.mutex) == 0); */

}

static int read_frame(AVCodecContext **pcodec_ctx, AVFrame *frame,
        int *stream_index) {
    int err;

    err = avcodec_receive_frame(*pcodec_ctx, frame);
    while (err == AVERROR(EAGAIN)) {
        _cleanup_(av_packet_free) AVPacket *pkt = av_packet_alloc();
        if (!pkt) {
            fprintf(stderr, "Error allocating packet\n");
            return AVERROR(ENOMEM);
        }

        do {
            err = av_read_frame(thread_params.avctx, pkt);
            if (err == AVERROR_EOF) {
                return err;
            } else if (err < 0) {
                fprintf(stderr, "Error reading frame\n");
                return err;
            }
            *pcodec_ctx =
                pkt->stream_index == thread_params.video_si
                ? thread_params.video_ctx
                : pkt->stream_index == thread_params.audio_si
                ? thread_params.audio_ctx
                : NULL;
            *stream_index = pkt->stream_index;
        } while (!*pcodec_ctx);

        err = avcodec_send_packet(*pcodec_ctx, pkt);
        if (err < 0) {
            fprintf(stderr, "Error sending packet to decoder\n");
            return err;
        }

        err = avcodec_receive_frame(*pcodec_ctx, frame);
    }
    if (err < 0) {
        fprintf(stderr, "Error receiving frame from decoder\n");
        return err;
    }

    return 0;
}

static int put_frame(AVFrame *frame, int stream_index) {
    _cleanup_(unlockp) SDL_mutex *queue_mtx = frame_queue.mutex;
    assert(SDL_LockMutex(queue_mtx) == 0);

    while (frame_queue.count == QUEUE_MAX) {
        int err;
        do {
            err = SDL_CondWaitTimeout(frame_queue.empty,
                    queue_mtx, DEFAULT_FRAME_DELAY);
            assert(err >= 0);

            if (thread_params.do_seek) {
                av_frame_free(&frame);
                return 0;
            }
            if (thread_params.done) {
                av_frame_free(&frame);
                return 0;
            }
        } while (err == SDL_MUTEX_TIMEDOUT);
    }
    queue_enqueue(&frame_queue, (entry_t) {
            .frame = frame,
            .stream_index = stream_index
            });
    assert(SDL_CondSignal(frame_queue.fill) == 0);

    return 0;
}

int fetch_frames(void *ptr) {
    int stream_index = thread_params.video_si;
    AVCodecContext *codec_ctx = thread_params.video_ctx;
    int err;

    (void)ptr;

    for (;;) {
        if (thread_params.done)
            return 0;

        assert(SDL_LockMutex(thread_params.seek_mtx) == 0);
        if (thread_params.do_seek) {
            seek();
            thread_params.do_seek = false;
            assert(SDL_CondSignal(thread_params.seek_done) == 0);
        }
        assert(SDL_UnlockMutex(thread_params.seek_mtx) == 0);

        _cleanup_(av_frame_free) AVFrame *frame = av_frame_alloc();
        if (!frame) {
            fprintf(stderr, "Error allocating frame\n");
            return AVERROR(ENOMEM);
        }

        err = read_frame(&codec_ctx, frame, &stream_index);
        if (err == AVERROR_EOF)
            continue;
        else if (err < 0)
            return err;

        (void)put_frame(TAKE_PTR(frame), stream_index);
    }

    /* return 0; */
}
