#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <SDL2/SDL.h>
#include <stdio.h>
#include "macro.h"
#include "param.h"

static bool get_codec_context(AVFormatContext *avctx,
        int stream_index, AVCodecContext **out) {
    AVCodecParameters *codec_param;
    const AVCodec *codec;
    AVCodecContext *codec_ctx;
    int err;

    codec_param = avctx->streams[stream_index]->codecpar;
    codec = avcodec_find_decoder(codec_param->codec_id);
    if (!codec) {
        const char *name = avcodec_get_name(codec_param->codec_id);
        LOG_ERROR("Failed to find decoder: %s\n", name);
        return false;
    }
    codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        LOG_ERROR("Error allocating codec context\n");
        return false;
    }
    err = avcodec_parameters_to_context(codec_ctx, codec_param);
    if (err < 0) {
        LOG_ERROR("Error copying codec context: %s\n", av_err2str(err));
        return false;
    }
    err = avcodec_open2(codec_ctx, codec, NULL);
    if (err < 0) {
        LOG_ERROR("Error opening codec context: %s\n", av_err2str(err));
        return false;
    }
#ifdef PLAYER_DISP_MVS
    codec_ctx->export_side_data |= AV_CODEC_EXPORT_DATA_MVS;
#endif
    *out = codec_ctx;
    return true;
}

bool avparam_init(avparam_t *param, const char *url) {
    int err;
    bool ret;

    err = avformat_open_input(&param->avctx, url, NULL, NULL);
    if (err < 0) {
        fprintf(stderr, "Error opening file '%s': %s\n", url,
                av_err2str(err));
        return false;
    }
    err = avformat_find_stream_info(param->avctx, NULL);
    if (err < 0) {
        LOG_ERROR("Error getting stream info: %s\n", av_err2str(err));
        return false;
    }
    // av_dump_format(param->avctx, 0, url, 0);

    param->video_si = av_find_best_stream(
            param->avctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    param->audio_si = av_find_best_stream(
            param->avctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    param->sub_si = av_find_best_stream(
            param->avctx, AVMEDIA_TYPE_SUBTITLE, -1, -1, NULL, 0);
    if (param->video_si < 0 || param->audio_si < 0) {
        LOG_ERROR("No audio/video stream available\n");
        return false;
    }

    ret = get_codec_context(param->avctx,
            param->video_si, &param->video_ctx);
    if (!ret) return false;
    ret = get_codec_context(param->avctx,
            param->audio_si, &param->audio_ctx);
    if (!ret) return false;
    if (param->sub_si >= 0) {
        ret = get_codec_context(param->avctx,
                param->sub_si, &param->sub_ctx);
        if (!ret) return false;
        printf("%.*s\n", param->sub_ctx->subtitle_header_size,
                param->sub_ctx->subtitle_header);
    }

    param->seek_mtx = SDL_CreateMutex();
    param->seek_done = SDL_CreateCond();
    if (!param->seek_mtx || !param->seek_done) {
        LOG_ERROR("Error creating mutex/cond\n");
        return false;
    }

    /*
    param->do_seek = false;
    param->seek_flags = 0;
    param->seek_pts = 0;
    param->done = false;
    */

    return true;
}

void avparam_fini(avparam_t *param) {
    avcodec_free_context(&param->video_ctx);
    avcodec_free_context(&param->audio_ctx);
    avcodec_free_context(&param->sub_ctx);
    avformat_close_input(&param->avctx);
    SDL_DestroyCond(param->seek_done);
    SDL_DestroyMutex(param->seek_mtx);
}
