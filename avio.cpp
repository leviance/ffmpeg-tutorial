#include "SDL.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/file.h>
}

struct buffer_data {
    uint8_t *ptr;
    size_t size; ///< size left in the buffer
};

static int read_packet(void *opaque, uint8_t *buf, int buf_size)
{
    struct buffer_data *bd = (struct buffer_data *)opaque;
    buf_size = FFMIN(buf_size, bd->size);
    printf("size:%d size:%zu\n", buf_size, bd->size);
    /* copy internal buffer data to buf */
    memcpy(buf, bd->ptr, buf_size);
    bd->ptr  += buf_size;
    bd->size -= buf_size;
    return buf_size;
}

int main(int argc, char *argv[])
{
    AVFormatContext *format_ctx = nullptr;
    AVIOContext *avio_ctx = nullptr;
    uint8_t *buffer = nullptr, *avio_ctx_buffer = nullptr;
    size_t buffer_size, avio_ctx_buffer_size = 4096;
    char *input_filename = nullptr;
    int ret = 0;
    struct buffer_data bd = {nullptr };

    input_filename = argv[1];
    /* register codecs and formats and other lavf/lavc components*/

    /* slurp file content into buffer */
    ret = av_file_map(input_filename, &buffer, &buffer_size, 0, nullptr);
    if (ret < 0)
        return 0;

    /* fill opaque structure used by the AVIOContext read callback */
    bd.ptr  = buffer;
    bd.size = buffer_size;
    if (!(format_ctx = avformat_alloc_context())) {
        fprintf(stderr, "can't alloc AVFormatContext.");
        return 0;
    }

    avio_ctx_buffer = (unsigned char *)av_malloc(avio_ctx_buffer_size);

    if (!avio_ctx_buffer) {
        fprintf(stderr, "can't alloc avio buffer.");
        return 0;
    }

    avio_ctx = avio_alloc_context(avio_ctx_buffer, avio_ctx_buffer_size,
                                  0, &bd, &read_packet, nullptr, nullptr);
    if (!avio_ctx) {
        fprintf(stderr, "can't alloc memory for AVIOContext");
        return 0;
    }

    format_ctx->pb = avio_ctx;
    ret = avformat_open_input(&format_ctx, nullptr, nullptr, nullptr);
    if (ret < 0) {
        fprintf(stderr, "can't open input\n");
        return 0;
    }

    ret = avformat_find_stream_info(format_ctx, nullptr);
    if (ret < 0) {
        fprintf(stderr, "can't find stream information\n");
        return 0;
    }

    // dump video information
//    av_dump_format(format_ctx, 0, input_filename, 0);

    // find video stream, audio stream
    int video_stream_index = -1, audio_stream_index = -1;
    AVStream *video_stream = nullptr, *audio_stream = nullptr;
    AVCodecParameters *video_codec_params = nullptr, *audio_codec_params = nullptr;

    for (int i = 0; i < format_ctx->nb_streams; i++) {
        if (format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index = i;
            video_stream = format_ctx->streams[i];
            video_codec_params = format_ctx->streams[i]->codecpar;
        }
        if (format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audio_stream_index = i;
            audio_stream = format_ctx->streams[i];
            audio_codec_params = format_ctx->streams[i]->codecpar;
        }
    }

    if (video_stream == nullptr) {
        fprintf(stderr, "can't find video stream.");
        return 0;
    }

    if (audio_stream == nullptr) {
        fprintf(stderr, "can't find audio stream.");
        return 0;
    }

    // find the right decoder
    const AVCodec *video_codec = avcodec_find_decoder(video_codec_params->codec_id);
    const AVCodec *audio_codec = avcodec_find_decoder(audio_codec_params->codec_id);

    // decoder information configuration
    AVCodecContext *video_codec_context = avcodec_alloc_context3(video_codec);
    AVCodecContext *audio_codec_context = avcodec_alloc_context3(audio_codec);
    avcodec_parameters_to_context(video_codec_context, video_codec_params);
    avcodec_parameters_to_context(audio_codec_context, audio_codec_params);

    if (avcodec_open2(video_codec_context, video_codec, nullptr) < 0) {
        fprintf(stderr, "can't open video codec.");
        return 0;
    }

    if (avcodec_open2(audio_codec_context, audio_codec, nullptr) < 0) {
        fprintf(stderr, "can't open audio codec.");
        return 0;
    }

    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();

    // init SDL window
    int window_width = video_codec_params->width;
    int window_height = video_codec_params->height;
    SDL_Window *window = SDL_CreateWindow(
            "simple FFmpeg and SDL player.",
            SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED,
            window_width,
            window_height,
            SDL_WINDOW_OPENGL
    );

    if (!window) {
        fprintf(stderr, "can't create SDL window.");
        return 0;
    }

    // init SDL renderer
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, 0);
    if (renderer == nullptr) {
        fprintf(stderr, "can't create SDL renderer.");
        return 0;
    }

    // init SDL texture
    Uint32 pixel_format = SDL_PIXELFORMAT_IYUV;
    SDL_Texture *texture = SDL_CreateTexture(
            renderer,
            pixel_format,
            SDL_TEXTUREACCESS_STREAMING,
            window_width,
            window_height);

    SDL_Rect rect;

    while (av_read_frame(format_ctx, packet) == 0) {
        if (packet->stream_index == video_stream_index) {
            avcodec_send_packet(video_codec_context, packet);
            while (avcodec_receive_frame(video_codec_context, frame) == 0) {
                //SDL  Refresh texture
                SDL_UpdateYUVTexture(texture, nullptr,
                                     frame->data[0], frame->linesize[0],
                                     frame->data[1], frame->linesize[1],
                                     frame->data[2], frame->linesize[2]);
                rect.x = 0;//SDL Set the display area of the render target
                rect.y = 0;
                rect.w = video_codec_params->width;
                rect.h = video_codec_params->height;

                SDL_RenderClear(renderer);//SDL  Empty the contents of the renderer
                SDL_RenderCopy(renderer, texture, nullptr, &rect);//SDL  Copy texture to renderer
                SDL_RenderPresent(renderer);//SDL  Rendering

                av_frame_unref(frame);
            }
        }
        if (packet->stream_index == audio_stream_index) {
            avcodec_send_packet(audio_codec_context, packet);
            while (avcodec_receive_frame(audio_codec_context, frame) == 0) {
                // audio frame
                av_frame_unref(frame);
            }
        }

        av_packet_unref(packet);
    }

    printf("run success.");
    return 0;
}
