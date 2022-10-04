#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>

#define _cleanup_(x) __attribute__((cleanup(x)))

static void avctx_freep(AVFormatContext **pformat_ctx) {
    avformat_close_input(pformat_ctx);
    avformat_free_context(*pformat_ctx);
}

static const char *my_strerror(int err) {
    static char buffer[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(err, buffer, AV_ERROR_MAX_STRING_SIZE);
    return buffer;
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
                my_strerror(err));
        exit(1);
    }
    if (avformat_find_stream_info(avctx, NULL) < 0) {
        fprintf(stderr, "Error getting stream information\n");
        exit(1);
    }
    av_dump_format(avctx, 0, argv[1], 0);

    const AVCodec *codec = NULL;
    AVCodecParameters *codec_param = NULL;
    int video_si = -1; // video stream index
    for (int i = 0; i < (int)avctx->nb_streams; i++) {
        AVStream *stream = avctx->streams[i];
        AVCodecParameters *codecpar = stream->codecpar;
        switch (codecpar->codec_type) {
        case AVMEDIA_TYPE_VIDEO:
            if (video_si == -1) {
                codec = avcodec_find_decoder(codecpar->codec_id);
                if (!codec) {
                    fprintf(stderr, "Failed to find a video codec\n");
                } else {
                    codec_param = codecpar;
                    video_si = i;
                }
            }
            break;
        case AVMEDIA_TYPE_AUDIO:
            break;
        case AVMEDIA_TYPE_SUBTITLE:
            break;
        default:
            ;
        }
        //print_dict(avctx->streams[i]->metadata);
    }
    if (video_si == -1) {
        fprintf(stderr, "No video stream available\n");
        exit(1);
    }
    _cleanup_(avcodec_free_context)
    AVCodecContext *codec_ctx = avcodec_alloc_context3(codec);
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
    while (1) {
        _cleanup_(av_packet_free) AVPacket *pkt = av_packet_alloc();
        if (!pkt) {
            fprintf(stderr, "Error allocating packet\n");
            exit(1);
        }
        _cleanup_(av_frame_free) AVFrame *frame = av_frame_alloc();
        if (!frame) {
            fprintf(stderr, "Error allocating frame\n");
            exit(1);
        }
        err = av_read_frame(avctx, pkt);
        if (err == AVERROR_EOF)
            break;
        else if (err < 0) {
            fprintf(stderr, "Error reading frame\n");
            exit(1);
        }
        if (pkt->stream_index != video_si)
            continue;
        err = avcodec_send_packet(codec_ctx, pkt);
        if (err < 0) {
            fprintf(stderr, "Error sending packet to decoder\n");
            exit(1);
        }
        while (1) {
            err = avcodec_receive_frame(codec_ctx, frame);
            if (err == AVERROR(EAGAIN))
                break;
            else if (err < 0) {
                fprintf(stderr, "Error receiving frame from decoder\n");
                exit(1);
            }
        }
    }
}
