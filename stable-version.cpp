#include "iostream"
#include "SDL.h"

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
}

using namespace std;

int main (int argv, char *args[]) {
    char url[] = "C:/Users/Levi/Desktop/FFmpeg and SDL decoder/video.ts";
//    char url[] = "C:/Users/Levi/Desktop/test c++/video.ts";
//    char url[] = "http://flv.bdplay.nodemedia.cn/live/bbb.flv";

    // alloc AVFormatContext for contain information of video
    AVFormatContext *format_ctx = avformat_alloc_context();
    if (format_ctx == nullptr) {
        cerr << "can't alloc AVFormatContext." << endl;
        return 0;
    }

    // open input file and read it header
    if (avformat_open_input(&format_ctx, url, nullptr, nullptr) != 0) {
        cerr << "can't open input file." << endl;
        return 0;
    }

    if (avformat_find_stream_info(format_ctx, nullptr) < 0) {
        cerr << "can't find stream info for given AVFormatContext." << endl;
        return 0;
    }

    // looking for video stream, audio stream
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
        cerr << "can't find video stream." << endl;
        return 0;
    }

    if (audio_stream == nullptr) {
        cerr << "can't find audio stream." << endl;
        return false;
    }

    // find the right decoder
    const AVCodec *video_codec = avcodec_find_decoder(video_codec_params->codec_id);
    const AVCodec *audio_codec = avcodec_find_decoder(audio_codec_params->codec_id);

    // decoder information configuration
    AVCodecContext *video_codec_ctx = avcodec_alloc_context3(video_codec);
    AVCodecContext *audio_codec_ctx = avcodec_alloc_context3(audio_codec);

    if (avcodec_open2(video_codec_ctx, video_codec, nullptr) < 0) {
        cerr << "can't open video codec." << endl;
        return 0;
    }

    if (avcodec_open2(audio_codec_ctx, audio_codec, nullptr) < 0) {
        cerr << "can't open audio codec." << endl;
        return 0;
    }

    //Output Info-----------------------------
    // printf("---------------- File Information ---------------\n");
    // av_dump_format(format_ctx, 0, url,0);
    // printf("-------------------------------------------------\n");

    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();

    if (packet == nullptr) {
        cerr << "can't alloc AVPacket." << endl;
        return 0;
    }

    if (frame == nullptr) {
        cerr << "can't alloc AVFrame." << endl;
    }

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
        cerr << "can't create SDL window." << endl;
        return 0;
    }

    // init SDL renderer
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, 0);
    if (renderer == nullptr) {
        cerr << "can't create SDL renderer" << endl;
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

    while (av_read_frame(format_ctx, packet) >= 0) {
        if (packet->stream_index == video_stream_index) {
            avcodec_send_packet(video_codec_ctx, packet);
            while (avcodec_receive_frame(video_codec_ctx, frame) == 0) {
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
            }
        }
        if (packet->stream_index == audio_stream_index) {
            avcodec_send_packet(audio_codec_ctx, packet);
            while (avcodec_receive_frame(audio_codec_ctx, frame) == 0) {
                // audio frame
            }
        }

        av_packet_unref(packet);
        av_frame_unref(frame);
    }

    cout << "run success" << endl;
    return 1;
}