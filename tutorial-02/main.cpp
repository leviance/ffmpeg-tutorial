#include "iostream"
#include "SDL.h"
#include "error-code.h"

extern "C" {
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libswscale/swscale.h"
#include "libavutil/imgutils.h"
}

using namespace std;

SDL_Window      *window             = nullptr;
SDL_Renderer    *renderer           = nullptr;
SDL_Texture     *texture            = nullptr;
SDL_Rect        display_rect;
SDL_Event       event;

/**
 * Init SDL library for render video frame on screen
 * @param SCREEN_WIDTH width of window want to create
 * @param SCREEN_HEIGHT height of window want to create
 * @return 0 on success or negative error code on failure
 */
int init_sdl(const int SCREEN_WIDTH, const int SCREEN_HEIGHT) {
    if ((SDL_Init(SDL_INIT_VIDEO)) < 0) {
        cerr << "Can't init SDL library with error: " << SDL_GetError() << endl;
        return INIT_SDL_LIB_ERROR;
    }

    window = SDL_CreateWindow("FFmpeg tutorial", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SCREEN_WIDTH, SCREEN_HEIGHT, 0);
    if (window == nullptr) {
        cerr << "Can't create SDL window with error: " << SDL_GetError() << endl;
        return CREATE_SDL_WINDOW_ERROR;
    }

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC);
    if (renderer == nullptr) {
        cerr << "Can't create SDL renderer with error: " << SDL_GetError() << endl;
        return CREATE_SDL_RENDERER_ERROR;
    }

    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, SCREEN_WIDTH, SCREEN_HEIGHT);
    if (texture == nullptr) {
        cerr << "Can't create SDL texture with error: " << SDL_GetError() << endl;
        return CREATE_SDL_TEXTURE_ERROR;
    }

    display_rect.x = 0;
    display_rect.y = 0;
    display_rect.w = SCREEN_WIDTH;
    display_rect.h = SCREEN_HEIGHT;

    return 0;
}

int main(int argc, char *args[]) {
    int                     ret                         = 0;
    bool                    quit                        = false;
    AVFormatContext         *format_ctx                 = nullptr;
    string                  file_path                   = "../../videos/video.mp4";
    int                     video_stream_index          = -1;
    int                     audio_stream_index          = -1;
    AVStream                *video_stream               = nullptr;
    AVStream                *audio_stream               = nullptr;
    AVCodecParameters       *video_codec_params         = nullptr;
    AVCodecParameters       *audio_codec_params         = nullptr;
    const AVCodec           *video_codec                = nullptr;
    const AVCodec           *audio_codec                = nullptr;
    AVCodecContext          *video_codec_ctx            = nullptr;
    AVCodecContext          *audio_codec_ctx            = nullptr;
    AVPacket                *packet                     = nullptr;
    AVFrame                 *frame                      = nullptr;
    SwsContext              *sws_ctx                    = nullptr;
    uint8_t                 *yuv420_frame[4]            = {nullptr};
    int                     yuv420_frame_linesize[4]    = {0};

    // Alloc format context for store data inside input file
    if ((format_ctx = avformat_alloc_context()) == nullptr) {
        cerr << "Can't alloc memory for AVFormatContext." << endl;
        return ALLOC_FMT_CTX_ERROR;
    }

    // Open input file and store data in format_ctx
    if ((avformat_open_input(&format_ctx, file_path.data(), nullptr, nullptr)) < 0) {
        cerr << "Can't open input file with given path." << endl;
        return OPEN_INPUT_ERROR;
    }

    // Find stream info in input file
    if ((avformat_find_stream_info(format_ctx, nullptr)) < 0) {
        cerr << "Can't find stream info." << endl;
        return FIND_STREAM_INFO_ERROR;
    }

    // Find audio and video stream
    for (int i = 0; i < format_ctx->nb_streams; ++i) {
        if (format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index  = i;
            video_stream        = format_ctx->streams[i];
            video_codec_params  = format_ctx->streams[i]->codecpar;
        }
        if (format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audio_stream_index  = i;
            audio_stream        = format_ctx->streams[i];
            audio_codec_params  = format_ctx->streams[i]->codecpar;
        }
    }

    /* Check for sure we found video and audio stream */
    if (video_stream_index < 0) {
        cerr << "Can't find video stream." << endl;
        return VIDEO_STREAM_NOT_FOUND;
    }

    if (audio_stream_index < 0) {
        cerr << "Can't find audio stream." << endl;
        return AUDIO_STREAM_NOT_FOUND;
    }

    /* Find video and audio decoder */
    video_codec = avcodec_find_decoder(video_codec_params->codec_id);
    if (video_codec == nullptr) {
        cerr << "Can't find decoder for video with codec: " << avcodec_get_name(video_codec_params->codec_id) << endl;
        return FIND_VIDEO_DECODER_ERROR;
    }

    audio_codec = avcodec_find_decoder(audio_codec_params->codec_id);
    if (audio_codec == nullptr) {
        cerr << "Can't find decoder for audio with codec: " << avcodec_get_name(audio_codec_params->codec_id) << endl;
        return FIND_AUDIO_DECODER_ERROR;
    }

    /* Set update video and audio codec context */
    if ((video_codec_ctx = avcodec_alloc_context3(video_codec)) == nullptr) {
        cerr << "Can't alloc video codec context." << endl;
        return ALLOC_VIDEO_CODEC_CTX_ERROR;
    }

    if ((audio_codec_ctx = avcodec_alloc_context3(audio_codec)) == nullptr) {
        cerr << "Can't alloc audio codec context." << endl;
        return ALLOC_AUDIO_CODEC_CTX_ERROR;
    }

    /* Copy video and audio codec params to context */
    if ((avcodec_parameters_to_context(video_codec_ctx, video_codec_params)) < 0) {
        cerr << "Can't copy video codec params to video codec context." << endl;
        return COPY_VIDEO_CODEC_PARAMS_ERROR;
    }

    if ((avcodec_parameters_to_context(audio_codec_ctx, audio_codec_params)) < 0) {
        cerr << "Can't copy audio codec params to audio codec context." << endl;
        return COPY_AUDIO_CODEC_PARAMS_ERROR;
    }

    /* Now we need open video and audio codec for ready to read and decode video and audio packet */
    if (avcodec_open2(video_codec_ctx, video_codec, nullptr) < 0) {
        cerr << "Can't open video codec context." << endl;
        return OPEN_VIDEO_CODEC_ERROR;
    }

    if (avcodec_open2(audio_codec_ctx, audio_codec, nullptr) < 0) {
        cerr << "Can't open audio codec context." << endl;
        return OPEN_AUDIO_CODEC_ERROR;
    }

    /* Alloc packet for read packet from input file and frame for receive frame decoded from packet */
    if ((packet = av_packet_alloc()) == nullptr) {
        cerr << "Can't alloc packet." << endl;
        return ALLOC_PACKET_ERROR;
    }

    if ((frame = av_frame_alloc()) == nullptr) {
        cerr << "Can't alloc frame." << endl;
        return ALLOC_FRAME_ERROR;
    }

    /* Get SwsContext for scaling and converting frame data */
    sws_ctx = sws_getContext(video_codec_ctx->width, video_codec_ctx->height, video_codec_ctx->pix_fmt,
                             video_codec_ctx->width, video_codec_ctx->height, AV_PIX_FMT_YUV420P,
                             SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (sws_ctx == nullptr) {
        cerr << "Can't get sws context." << endl;
        return GET_SWS_CTX_ERROR;
    }

    /* Alloc memory for store yuv420 data */
    ret = av_image_alloc(yuv420_frame, yuv420_frame_linesize, video_codec_ctx->width, video_codec_ctx->height, AV_PIX_FMT_YUV420P, 1);
    if (ret < 0) {
        cerr << "Can't alloc memory for RGB frame" << endl;
        return ALLOC_RGB_FRAME_ERROR;
    }

    // Init SDL library for output frame on screen
    if ((ret = init_sdl(video_codec_ctx->width, video_codec_ctx->height)) < 0) {
        return ret;
    }

    // Read packet and decode into frame
    while (av_read_frame(format_ctx, packet) >= 0) {
        if (quit) break;

        // Video stream
        if (packet->stream_index == video_stream_index) {
            ret = avcodec_send_packet(video_codec_ctx, packet);

            if (ret == AVERROR_EOF) break;

            if (ret < 0 && ret != AVERROR(EAGAIN)) {
                cerr << "Error when sending video packet." << endl;
                return SEND_VIDEO_PACKET_ERROR;
            }

            while (ret >= 0) {
                ret = avcodec_receive_frame(video_codec_ctx, frame);

                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                else if (ret < 0) {
                    cerr << "Error when receive video frame." << endl;
                    return SEND_VIDEO_FRAME_ERROR;
                }

                if (video_codec_ctx->pix_fmt != AV_PIX_FMT_YUV420P) {
                    /* Convert frame pixel format to RGB24 format */
                    sws_scale(sws_ctx, frame->data, frame->linesize, 0, video_codec_ctx->height, yuv420_frame, yuv420_frame_linesize);
                    SDL_UpdateYUVTexture(texture, nullptr,
                                         yuv420_frame[0], yuv420_frame_linesize[0],
                                         yuv420_frame[1], yuv420_frame_linesize[1],
                                         yuv420_frame[2], yuv420_frame_linesize[2]);
                }
                else {
                    SDL_UpdateYUVTexture(texture, nullptr,
                                         frame->data[0], frame->linesize[0],
                                         frame->data[1], frame->linesize[1],
                                         frame->data[2], frame->linesize[2]);
                }

                // Clear renderer
                SDL_RenderClear(renderer);

                // Copy texture to renderer
                SDL_RenderCopy(renderer, texture, nullptr, &display_rect);

                // Rendering video frame
                SDL_RenderPresent(renderer);

                av_frame_unref(frame);
            }

        }

        // Audio stream
        if (packet->stream_index == audio_stream_index) {

        }

        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) quit = true;
        }

        av_packet_unref(packet);
    }

    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&video_codec_ctx);
    avcodec_free_context(&audio_codec_ctx);
    sws_freeContext(sws_ctx);
    avformat_free_context(format_ctx);

    SDL_DestroyRenderer(renderer);
    SDL_DestroyTexture(texture);
    SDL_DestroyWindow(window);
    cout << "COMPLETE" << endl;

    return 0;
}