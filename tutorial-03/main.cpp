#include "iostream"
#include "SDL.h"
#include "SDL_thread.h"
#include "error-code.h"

extern "C" {
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"
#include "libavutil/imgutils.h"
}

using namespace std;

/**
 * Queue implement for store AVPacket.
 */
struct PACKET_QUEUE {
private:
    AVPacketList *first_packet;
    AVPacketList *last_packet;
    SDL_mutex *mutex;
    SDL_cond *cond;
    int _size;
    int _length;

public:
    PACKET_QUEUE() {
        this->first_packet      = nullptr;
        this->last_packet       = nullptr;
        this->mutex             = SDL_CreateMutex();
        this->cond              = SDL_CreateCond();
        this->_size              = 0;
        this->_length            = 0;
    }

    /**
     * Get size in bytes of all packets stored.
     * @return size in bytes.
     */
    int size() const {
        return this->_size;
    }

    /**
     * Get number of packet stored in this queue.
     * @return number of packet.
     */
    int length() const {
        return this->_length;
    }

    /**
     * Push new AVPacket in queue.
     * @note Do not apply "av_packet_unref" or "av_packet_free" with packet passed into this function.
     * @param packet packet want to store.
     */
    void push(AVPacket *packet) {
        SDL_LockMutex(this->mutex);

        auto *next_packet   = (AVPacketList*)(malloc(sizeof(AVPacketList)));
        next_packet->pkt    = *packet;
        next_packet->next   = nullptr;

        if (!this->first_packet) {
            this->first_packet  = next_packet;
            this->last_packet   = next_packet;
        }
        else {
            this->last_packet->next = next_packet;
            this->last_packet = this->last_packet->next;
        }

        this->_size += packet->size;
        this->_length += 1;

        SDL_UnlockMutex(this->mutex);
        SDL_CondSignal(this->cond);
    }

    /**
     * Retrieves a packet that has been pushed to the queue.
     * @note AVPacket receive from this function need to be free with "av_packet_free" when they are no longer needed.
     * @param wait if "wait" is true thread will be blocked if no packet in queue until a new packet pushed.
     * @return "AVPacket" pointer or "nullptr" when no packet in queue ("nullptr" will just return when "wait" is false).
     */
    AVPacket *get(bool wait) {
        SDL_LockMutex(this->mutex);
        AVPacket *transit_packet = nullptr;

        for(;;) {
            if (this->first_packet) {
                /* Update size and length of this queue */
                this->_size -= this->first_packet->pkt.size;
                this->_length -= 1;

                // Alloc memory for a transit packet
                transit_packet = av_packet_alloc();


                // Copy data from packet stored in first_packet to transit_packet
                *transit_packet = this->first_packet->pkt;

                /* Move first_packet to next packet list and free old data */
                AVPacketList *transit_packet_list = this->first_packet;
                this->first_packet = this->first_packet->next;
                free(transit_packet_list);

                break;
            }
            else if (wait) {
                // Unlock mute and freeze this thread until we reached SDL_CondSignal and thread will continue from here
                SDL_CondWait(this->cond, this->mutex);
            }
            else {
                SDL_UnlockMutex(this->mutex);
                return nullptr;
            }
        }

        SDL_UnlockMutex(this->mutex);
        return transit_packet;
    }
};

const int AUDIO_BUFFER_SIZE = 1024;
const int MAX_AUDIO_FRAME_SIZE = 192000;

bool            quit                    = false;
PACKET_QUEUE    *audio_packet_queue     = new PACKET_QUEUE;
SDL_Window      *window                 = nullptr;
SDL_Renderer    *renderer               = nullptr;
SDL_Texture     *texture                = nullptr;
SDL_AudioSpec   audio_spec;
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

void audio_convert() {

}

int audio_decode(AVCodecContext *audio_codec_ctx, uint8_t audio_buffer[]) {
    AVPacket *audio_packet = audio_packet_queue->get(true);
    AVFrame *audio_frame = av_frame_alloc();
    if (audio_frame == nullptr) {
        cerr << "Can't alloc memory for audio frame." << endl;
        return ALLOC_FRAME_ERROR;
    }

    int ret = avcodec_send_packet(audio_codec_ctx, audio_packet);
    if (ret < 0 && ret != AVERROR(EAGAIN)) {
        cerr << "Can't send audio packet." << endl;
        return SEND_AUDIO_PACKET_ERROR;
    }

    while (ret >= 0) {
        ret = avcodec_receive_frame(audio_codec_ctx, audio_frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        else if (ret < 0) {
            cerr << "Can't receive audio frame." << endl;
            return RECEIVE_AUDIO_FRAME_ERROR;
        }
    }

    av_frame_free(&audio_frame);
    av_packet_free(&audio_packet);
    return 0;
}

void audio_callback(void *userdata, Uint8 *stream, int len) {
    // auto audio_codec_ctx = (AVCodecContext*)userdata;
    // static uint8_t audio_buffer[(MAX_AUDIO_FRAME_SIZE * 3) / 2];
    //
    // int length = audio_decode(audio_codec_ctx, audio_buffer);
    // if (length < 0) {
    //     quit = true;
    //     return;
    // }
}

int main(int argc, char *args[]) {
    int                     ret                         = 0;
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
    SwrContext              *swr_ctx                    = nullptr;
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

    /* Setup SDL audio */
    audio_spec.freq = audio_codec_ctx->sample_rate;
    audio_spec.format = AUDIO_S16SYS;
    audio_spec.channels = video_codec_ctx->ch_layout.nb_channels;
    audio_spec.silence = 0;
    audio_spec.samples = AUDIO_BUFFER_SIZE;
    audio_spec.callback = audio_callback;
    audio_spec.userdata = audio_codec_ctx;

    if (SDL_OpenAudio(&audio_spec, nullptr) < 0) {
        cerr << "Can't open SDL audio with error: " << SDL_GetError() << endl;
        return OPEN_SDL_AUDIO_ERROR;
    }

    SDL_PauseAudio(0);

    /*  */
    AVChannelLayout out_ch_layout = AV_CHANNEL_LAYOUT_MONO;

    ret = swr_alloc_set_opts2(&swr_ctx,
                              &out_ch_layout, AV_SAMPLE_FMT_S16, audio_codec_ctx->sample_rate,
                              &audio_codec_ctx->ch_layout, audio_codec_ctx->sample_fmt, audio_codec_ctx->sample_rate,
                              0, nullptr);
    if (ret < 0) {
        cerr << "Can't alloc SwrContext." << endl;
        return ALLOC_SWR_CONTEXT_ERROR;
    }

    ret = swr_init(swr_ctx);
    if (ret < 0) {
        cerr << "Can't init SwrContext." << endl;
        return INIT_SWR_CONTEXT_ERROR;
    }

    uint8_t *audio_data[4] = {nullptr};
    int audio_linesize[4] = {0};

    // Read packet and decode into frame
    while (av_read_frame(format_ctx, packet) >= 0) {
        if (quit) break;

        // Video stream
        if (packet->stream_index == video_stream_index) {
            ret = avcodec_send_packet(video_codec_ctx, packet);
            if (ret < 0 && ret != AVERROR(EAGAIN)) {
                cerr << "Error when sending video packet." << endl;
                return SEND_VIDEO_PACKET_ERROR;
            }

            while (ret >= 0) {
                ret = avcodec_receive_frame(video_codec_ctx, frame);

                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                else if (ret < 0) {
                    cerr << "Error when receive video frame." << endl;
                    return RECEIVE_VIDEO_FRAME_ERROR;
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

            av_packet_unref(packet);
        }

        // Audio stream
        else if (packet->stream_index == audio_stream_index) {
            // audio_packet_queue->push(packet);

            ret = avcodec_send_packet(audio_codec_ctx, packet);
            if (ret < 0 && ret != AVERROR(EAGAIN)) {
                cerr << "Error when sending audio packet." << endl;
                return SEND_AUDIO_PACKET_ERROR;
            }

            while (ret >= 0) {
                ret = avcodec_receive_frame(audio_codec_ctx, frame);

                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                else if (ret < 0) {
                    cerr << "Error when receive audio frame." << endl;
                    return RECEIVE_AUDIO_FRAME_ERROR;
                }

                if (audio_codec_ctx->sample_fmt == AV_SAMPLE_FMT_S16) {

                }
                else {
                    int out_samples = av_rescale_rnd(swr_get_delay(swr_ctx, audio_codec_ctx->sample_rate) + frame->nb_samples,
                                                     audio_codec_ctx->sample_rate, audio_codec_ctx->sample_rate, AV_ROUND_UP);

                    cout << "Calculate number of out sample: " << out_samples << endl;

                    // [ERROR]: memory leak in here!
                    av_samples_alloc(audio_data, audio_linesize, audio_codec_ctx->channels, out_samples, AV_SAMPLE_FMT_S16, 0);

                    out_samples = swr_convert(swr_ctx, audio_data, out_samples, (const uint8_t**)frame->data, frame->nb_samples);

                    cout << "number of out sample after converted: " << out_samples << endl;
                    cout << "audio frame linesize: " << audio_linesize[0] << endl;
                }

                av_frame_unref(frame);
            }

            av_packet_unref(packet);
        }

        else {
            av_packet_unref(packet);
        }

        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) quit = true;

            if (event.type == SDL_KEYUP) {
                switch (event.key.keysym.sym) {
                    case SDLK_ESCAPE:
                        quit = true;
                        break;

                    default:
                        cout << "Unhandled key" << endl;
                        break;
                }
            }
        }
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