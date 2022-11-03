#include "iostream"

using namespace std;

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

int j = 0;

void save_frame(AVFrame *frame_rgb, int width, int height) {
    cout << width << "-" << height << endl;
    if (j++ != 200) return;

    FILE *pFile;
    char szFilename[32];
    int y;

    sprintf(szFilename, "frame.ppm");

    pFile = fopen(szFilename, "wb");

    if (pFile == nullptr) {
        return;
    }

    fprintf(pFile, "P6\n%d %d\n225\n", width, height);

    for (y = 0; y < height; y++) {
        fwrite(frame_rgb->data[0] + y * frame_rgb->linesize[0], 1, width * 3, pFile);
    }

    fclose(pFile);
}

int main() {
    char            video_url[]             = "C:/Users/Levi/Desktop/test c++/video.ts";
    AVFormatContext *format_ctx             = nullptr;
    int video_stream_index                  = -1;
    int audio_stream_index                  = -1;
    AVStream *video_stream                  = nullptr;
    AVStream *audio_stream                  = nullptr;
    AVCodecParameters *video_codec_params   = nullptr;
    AVCodecParameters *audio_codec_params   = nullptr;
    const AVCodec *video_codec              = nullptr;
    const AVCodec *audio_codec              = nullptr;
    AVCodecContext *video_codec_ctx         = nullptr;
    AVCodecContext *audio_codec_ctx         = nullptr;
    AVFrame *frame                          = nullptr;
    AVPacket *packet                        = nullptr;

    if (!(format_ctx = avformat_alloc_context())) {
        fprintf(stderr, "can't alloc memory for AVFormatContext.");
        return -1;
    }

    // open file
    if (avformat_open_input(&format_ctx, video_url, nullptr, nullptr) < 0) {
        fprintf(stderr, "can't open input file.");
        return -1;
    }

    // find stream info
    if (avformat_find_stream_info(format_ctx, nullptr) < 0) {
        fprintf(stderr, "can't find stream info.");
        return -1;
    }

    // find video and audio stream
    video_codec_params = avcodec_parameters_alloc();
    audio_codec_params = avcodec_parameters_alloc();
    for (int i = 0; i < format_ctx->nb_streams; ++i) {
        if (format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index = i;
            video_codec_params = format_ctx->streams[i]->codecpar;
            video_stream = format_ctx->streams[i];
        }
        if (format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audio_stream_index = i;
            audio_codec_params = format_ctx->streams[i]->codecpar;
            audio_stream = format_ctx->streams[i];
        }
    }

    printf("video stream index: %d\n", video_stream_index);
    printf("audio stream index: %d\n", audio_stream_index);

    if (video_stream_index == -1) {
        fprintf(stderr, "can't find video stream.");
        return -1;
    }

    if (audio_stream_index == -1) {
        fprintf(stderr, "can't find audio stream.");
        return -1;
    }

    // find video codec and audio codec
    video_codec = avcodec_find_decoder(video_codec_params->codec_id);
    audio_codec = avcodec_find_decoder(audio_codec_params->codec_id);

    if (video_codec == nullptr) {
        fprintf(stderr, "can't find video codec.");
        return -1;
    }

    if (audio_codec == nullptr) {
        fprintf(stderr, "can't find video codec.");
        return -1;
    }

    // alloc codec context
    video_codec_ctx = avcodec_alloc_context3(video_codec);
    // copy params of codec into video_codec_ctx, we need this for use video_codec_ctx->pix_fmt
    avcodec_parameters_to_context(video_codec_ctx, video_stream->codecpar);
    if (avcodec_open2(video_codec_ctx, video_codec, nullptr) < 0) {
        fprintf(stderr, "can't open video codec.");
        return -1;
    }

    audio_codec_ctx = avcodec_alloc_context3(audio_codec);
    if (avcodec_open2(audio_codec_ctx, audio_codec, nullptr) < 0) {
        fprintf(stderr, "can't open audio codec.");
        return -1;
    }

    // decode
    frame = av_frame_alloc();
    packet = av_packet_alloc();
    AVFrame *frame_rgb = av_frame_alloc();
    int num_bytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, video_codec_ctx->width, video_codec_ctx->height, 1);
    auto *buffer = (unsigned char *)av_malloc(num_bytes * sizeof(unsigned char));
    av_image_fill_arrays(
            frame_rgb->data,
            frame_rgb->linesize,
            buffer,
            AV_PIX_FMT_RGB24,
            video_codec_ctx->width,
            video_codec_ctx->height,
            1);
    struct SwsContext *sws_ctx = sws_getContext(
            video_codec_ctx->width,
            video_codec_ctx->height,
            video_codec_ctx->pix_fmt,
            video_codec_ctx->width,
            video_codec_ctx->height,
            AV_PIX_FMT_RGB24,
            SWS_BILINEAR,
            nullptr,
            nullptr,
            nullptr
            );

    while (av_read_frame(format_ctx, packet) >= 0) {
        if (packet->stream_index == video_stream_index) {
            avcodec_send_packet(video_codec_ctx, packet);
            while (avcodec_receive_frame(video_codec_ctx, frame) >= 0) {
                // video frame
                sws_scale(
                        sws_ctx,
                        (uint8_t const *const *)frame->data,
                        frame->linesize,
                        0,
                        video_codec_ctx->height,
                        frame_rgb->data,
                        frame_rgb->linesize
                        );

                save_frame(frame_rgb, video_codec_ctx->width, video_codec_ctx->height);

                av_frame_unref(frame);
            }
        }
        if (packet->stream_index == audio_stream_index) {
            avcodec_send_packet(audio_codec_ctx, packet);
            while (avcodec_receive_frame(audio_codec_ctx, frame) == 0) {
                // audio frame
                av_frame_unref(frame);
            }
        }

        av_packet_unref(packet);
    }

    cout << format_ctx->duration << endl;
    cout << "run success" << endl;
    return 0;
}