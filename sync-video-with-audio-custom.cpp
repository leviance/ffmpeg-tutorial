#include "iostream"
#include "SDL.h"
#include "thread"
#include "vector"

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswresample/swresample.h"
}

using namespace std;

#define SDL_AUDIO_BUFFER_SIZE 1024
#define MAX_AUDIO_FRAME_SIZE 192000

vector<AVPacket *> list_video_packet = {};
vector<AVPacket *> list_audio_packet = {};

AVFormatContext *init_format_context();
AVCodecContext *init_video_codec_context(AVCodecParameters *video_codec_params);
AVCodecContext *init_audio_codec_context(AVCodecParameters *audio_codec_params);
void init_sdl(SDL_Window **window, SDL_Renderer **renderer, SDL_Texture **texture, int window_width, int window_height);
void audio_callback(void *userdata, Uint8 *stream, int len);

static int audio_resampling(                                  // 1
        AVCodecContext * audio_decode_ctx,
        AVFrame * decoded_audio_frame,
        enum AVSampleFormat out_sample_fmt,
        int out_channels,
        int out_sample_rate,
        uint8_t * out_buf
)
{
    SwrContext * swr_ctx = nullptr;
    int ret = 0;
    int64_t in_channel_layout = audio_decode_ctx->channel_layout;
    int64_t out_channel_layout = AV_CH_LAYOUT_STEREO;
    int out_nb_channels = 0;
    int out_linesize = 0;
    int in_nb_samples = 0;
    int out_nb_samples = 0;
    int max_out_nb_samples = 0;
    uint8_t ** resampled_data = nullptr;
    int resampled_data_size = 0;

    // get input audio channels
    in_channel_layout = (audio_decode_ctx->channels == av_get_channel_layout_nb_channels(audio_decode_ctx->channel_layout)) ?   // 2
                        audio_decode_ctx->channel_layout :
                        av_get_default_channel_layout(audio_decode_ctx->channels);

    // check input audio channels correctly retrieved
    if (in_channel_layout <= 0)
    {
        printf("in_channel_layout error.\n");
        return -1;
    }

    // set output audio channels based on the input audio channels
    if (out_channels == 1)
    {
        out_channel_layout = AV_CH_LAYOUT_MONO;
    }
    else if (out_channels == 2)
    {
        out_channel_layout = AV_CH_LAYOUT_STEREO;
    }
    else
    {
        out_channel_layout = AV_CH_LAYOUT_SURROUND;
    }

    // retrieve number of audio samples (per channel)
    in_nb_samples = decoded_audio_frame->nb_samples;
    if (in_nb_samples <= 0)
    {
        printf("in_nb_samples error.\n");
        return -1;
    }

    // Set SwrContext parameters for resampling
    swr_ctx = swr_alloc_set_opts(
            nullptr,
            out_channel_layout,
            out_sample_fmt,
            out_sample_rate,
            in_channel_layout,
            audio_decode_ctx->sample_fmt,
            audio_decode_ctx->sample_rate,
            0,
            nullptr
    );

    // Once all values have been set for the SwrContext, it must be initialized
    // with swr_init().
    ret = swr_init(swr_ctx);;
    if (ret < 0)
    {
        printf("Failed to initialize the resampling context.\n");
        return -1;
    }

    max_out_nb_samples = out_nb_samples = av_rescale_rnd(
            in_nb_samples,
            out_sample_rate,
            audio_decode_ctx->sample_rate,
            AV_ROUND_UP
    );

    // check rescaling was successful
    if (max_out_nb_samples <= 0)
    {
        printf("av_rescale_rnd error.\n");
        return -1;
    }

    // get number of output audio channels
    out_nb_channels = av_get_channel_layout_nb_channels(out_channel_layout);

    ret = av_samples_alloc_array_and_samples(
            &resampled_data,
            &out_linesize,
            out_nb_channels,
            out_nb_samples,
            out_sample_fmt,
            0
    );

    if (ret < 0)
    {
        printf("av_samples_alloc_array_and_samples() error: Could not allocate destination samples.\n");
        return -1;
    }

    // retrieve output samples number taking into account the progressive delay
    out_nb_samples = av_rescale_rnd(
            swr_get_delay(swr_ctx, audio_decode_ctx->sample_rate) + in_nb_samples,
            out_sample_rate,
            audio_decode_ctx->sample_rate,
            AV_ROUND_UP
    );

    // check output samples number was correctly retrieved
    if (out_nb_samples <= 0)
    {
        printf("av_rescale_rnd error\n");
        return -1;
    }

    if (out_nb_samples > max_out_nb_samples)
    {
        // free memory block and set pointer to nullptr
        av_free(resampled_data[0]);

        // Allocate a samples buffer for out_nb_samples samples
        ret = av_samples_alloc(
                resampled_data,
                &out_linesize,
                out_nb_channels,
                out_nb_samples,
                out_sample_fmt,
                1
        );

        // check samples buffer correctly allocated
        if (ret < 0)
        {
            printf("av_samples_alloc failed.\n");
            return -1;
        }

        max_out_nb_samples = out_nb_samples;
    }

    if (swr_ctx)
    {
        // do the actual audio data resampling
        ret = swr_convert(
                swr_ctx,
                resampled_data,
                out_nb_samples,
                (const uint8_t **) decoded_audio_frame->data,
                decoded_audio_frame->nb_samples
        );

        // check audio conversion was successful
        if (ret < 0)
        {
            printf("swr_convert_error.\n");
            return -1;
        }

        // Get the required buffer size for the given audio parameters
        resampled_data_size = av_samples_get_buffer_size(
                &out_linesize,
                out_nb_channels,
                ret,
                out_sample_fmt,
                1
        );

        // check audio buffer size
        if (resampled_data_size < 0)
        {
            printf("av_samples_get_buffer_size error.\n");
            return -1;
        }
    }
    else
    {
        printf("swr_ctx null error.\n");
        return -1;
    }

    // copy the resampled data to the output buffer
    memcpy(out_buf, resampled_data[0], resampled_data_size);

    /*
     * Memory Cleanup.
     */
    if (resampled_data)
    {
        // free memory block and set pointer to nullptr
        av_freep(&resampled_data[0]);
    }

    av_freep(&resampled_data);
    resampled_data = nullptr;

    if (swr_ctx)
    {
        // Free the given SwrContext and set the pointer to nullptr
        swr_free(&swr_ctx);
    }

    return resampled_data_size;
}

int audio_decode_frame(AVCodecContext * aCodecCtx, uint8_t * audio_buf, int buf_size)
{
    AVPacket        *avPacket           = nullptr;
    static uint8_t  *audio_pkt_data     = nullptr;
    static int      audio_pkt_size      = 0;

    // allocate a new frame, used to decode audio packets
    static AVFrame * avFrame = nullptr;
    avFrame = av_frame_alloc();
    if (!avFrame)
    {
        printf("Could not allocate AVFrame.\n");
        return -1;
    }

    int len1 = 0;
    int data_size = 0;

    for (;;)
    {
        while (audio_pkt_size > 0)
        {
            int got_frame = 0;

            // [5]
            // len1 = avcodec_decode_audio4(aCodecCtx, avFrame, &got_frame, avPacket);
            int ret = avcodec_receive_frame(aCodecCtx, avFrame);
            if (ret == 0)
            {
                got_frame = 1;
            }
            if (ret == AVERROR(EAGAIN))
            {
                ret = 0;
            }
            if (ret == 0)
            {
                ret = avcodec_send_packet(aCodecCtx, avPacket);
            }
            if (ret == AVERROR(EAGAIN))
            {
                ret = 0;
            }
            else if (ret < 0)
            {
                printf("avcodec_receive_frame error");
                return -1;
            }
            else
            {
                len1 = avPacket->size;
            }

            if (len1 < 0)
            {
                // if error, skip frame
                audio_pkt_size = 0;
                break;
            }

            audio_pkt_data += len1;
            audio_pkt_size -= len1;
            data_size = 0;

            if (got_frame)
            {
                // audio resampling
                data_size = audio_resampling(
                        aCodecCtx,
                        avFrame,
                        AV_SAMPLE_FMT_S16,
                        aCodecCtx->channels,
                        aCodecCtx->sample_rate,
                        audio_buf
                );

            }

            if (data_size <= 0)
            {
                // no data yet, get more frames
                continue;
            }

            // we have the data, return it and come back for more later
            return data_size;
        }

        if (avPacket != nullptr)
        {
            // wipe the packet
            av_packet_unref(avPacket);
        }

        // get more audio AVPacket
        avPacket = list_audio_packet[0];
        list_audio_packet.erase(list_audio_packet.begin());

        audio_pkt_data = avPacket->data;
        audio_pkt_size = avPacket->size;
    }

    return 0;
}

class Date {
public:
    static int64_t Now() {
        return chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count();
    }
};

[[noreturn]] void render_video_thread(
        AVCodecContext *video_codec_ctx,
        AVCodecParameters *video_codec_params,
        SDL_Renderer *renderer,
        SDL_Texture *texture,
        SDL_Rect *rect,
        AVStream *video_stream
) {
    AVFrame *frame = av_frame_alloc();
    int64_t last_frame_time = 0;
    int64_t init_render_time = 0;

    while (true) {
        if (list_video_packet.empty()) continue;

        auto frame_timestamp = (int64_t)((double)list_video_packet[0]->pts / video_stream->time_base.den * 1000);

        if (init_render_time == 0) {
            last_frame_time = Date::Now();
            init_render_time = frame_timestamp;

            avcodec_send_packet(video_codec_ctx, list_video_packet[0]);
            while (avcodec_receive_frame(video_codec_ctx, frame) >= 0) {
                //SDL  Refresh texture
                SDL_UpdateYUVTexture(texture, nullptr,
                                     frame->data[0], frame->linesize[0],
                                     frame->data[1], frame->linesize[1],
                                     frame->data[2], frame->linesize[2]);

                rect->x = 0;//SDL Set the display area of the render target
                rect->y = 0;
                rect->w = video_codec_params->width;
                rect->h = video_codec_params->height;

                SDL_RenderClear(renderer);//SDL  Empty the contents of the renderer
                SDL_RenderCopy(renderer, texture, nullptr, rect);//SDL  Copy texture to renderer
                SDL_RenderPresent(renderer);//SDL  Rendering

                list_video_packet.erase(list_video_packet.begin());
            }
        }

        else {
            int64_t current_time = Date::Now();
            int64_t elapsed_time = current_time - last_frame_time;
            init_render_time += elapsed_time;

            // while (init_render_time > frame_timestamp + 30 && list_video_packet.size() > 1) {
            //     list_video_packet.erase(list_video_packet.begin());
            //     frame_timestamp = (int64_t)((double)list_video_packet[0]->pts / video_stream->time_base.den * 1000);
            // }

            if (init_render_time >= frame_timestamp) {

                avcodec_send_packet(video_codec_ctx, list_video_packet[0]);
                while (avcodec_receive_frame(video_codec_ctx, frame) >= 0) {
                    //SDL  Refresh texture
                    SDL_UpdateYUVTexture(texture, nullptr,
                                         frame->data[0], frame->linesize[0],
                                         frame->data[1], frame->linesize[1],
                                         frame->data[2], frame->linesize[2]);

                    rect->x = 0;//SDL Set the display area of the render target
                    rect->y = 0;
                    rect->w = video_codec_params->width;
                    rect->h = video_codec_params->height;

                    SDL_RenderClear(renderer);//SDL  Empty the contents of the renderer
                    SDL_RenderCopy(renderer, texture, nullptr, rect);//SDL  Copy texture to renderer
                    SDL_RenderPresent(renderer);//SDL  Rendering

                    list_video_packet.erase(list_video_packet.begin());
                }
            }

            last_frame_time = current_time;
        }

        this_thread::sleep_for(chrono::milliseconds(1000 / 60));
    }
}

int main (int argv, char *args[]) {
    SDL_Event event;
    SDL_AudioSpec wanted_spec;
    AVFormatContext *format_ctx = init_format_context();
    int video_stream_index                              = -1;
    int audio_stream_index                              = -1;
    AVStream *video_stream                              = nullptr;
    AVStream *audio_stream                              = nullptr;
    AVCodecParameters *video_codec_params               = nullptr;
    AVCodecParameters *audio_codec_params               = nullptr;
    AVCodecContext *video_codec_ctx                     = nullptr;
    AVCodecContext *audio_codec_ctx                     = nullptr;
    AVPacket *packet                                    = av_packet_alloc();
    SDL_Window *window                                  = nullptr;
    SDL_Renderer *renderer                              = nullptr;
    SDL_Texture *texture                                = nullptr;
    SDL_Rect rect;
    bool start_audio_render                             = false;
    bool start_video_render                             = false;

    srand(time(nullptr));

    /**
     * find video and audio stream
     */
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

    /**
     * init video and audio codec context
     */
    video_codec_ctx = init_video_codec_context(video_codec_params);
    audio_codec_ctx = init_audio_codec_context(audio_codec_params);

    // init SDL window, renderer, texture
    init_sdl(&window, &renderer, &texture, video_codec_ctx->width, video_codec_ctx->height);

    // Set audio settings from codec info
    wanted_spec.freq = audio_codec_ctx->sample_rate;
    wanted_spec.format = AUDIO_S16SYS;
    wanted_spec.channels = audio_codec_ctx->ch_layout.nb_channels;
    wanted_spec.silence = 0;
    wanted_spec.samples = SDL_AUDIO_BUFFER_SIZE;
    wanted_spec.callback = audio_callback;
    wanted_spec.userdata = audio_codec_ctx;

    if(SDL_OpenAudio(&wanted_spec, nullptr) < 0) {
        fprintf(stderr, "SDL_OpenAudio: %s\n", SDL_GetError());
        return -1;
    }

    while (av_read_frame(format_ctx, packet) >= 0) {
        SDL_PollEvent(&event);

        if (event.type == SDL_QUIT) break;
        if (event.type == SDL_KEYUP && event.key.keysym.sym == SDLK_ESCAPE) break;

        if (packet->stream_index == video_stream_index) {
            AVPacket *copy_packet = av_packet_alloc();
            av_packet_ref(copy_packet, packet);

            list_video_packet.push_back(copy_packet);

            if (!start_video_render) {
                thread t1(render_video_thread, video_codec_ctx, video_codec_params, renderer, texture, &rect, video_stream);
                t1.detach();
                start_video_render = true;
            }
        }
        if (packet->stream_index == audio_stream_index) {
            AVPacket *copy_packet = av_packet_alloc();
            av_packet_ref(copy_packet, packet);

            list_audio_packet.push_back(copy_packet);

            if (!start_audio_render) {
                SDL_PauseAudio(0);
                start_audio_render = true;
            }
        }

        av_packet_unref(packet);
    }

    while (!list_video_packet.empty()) {
        SDL_PollEvent(&event);

        if (event.type == SDL_QUIT) break;
        if (event.type == SDL_KEYUP && event.key.keysym.sym == SDLK_ESCAPE) break;
    }

    SDL_DestroyWindow(window);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyTexture(texture);
    av_packet_free(&packet);
    avcodec_free_context(&video_codec_ctx);
    avcodec_free_context(&audio_codec_ctx);
    avformat_free_context(format_ctx);

    cout << "Run complete." << endl;
    return 1;
}

AVFormatContext *init_format_context() {
    AVFormatContext *format_ctx = nullptr;
    string baseURL = "C:/Users/Levi/Desktop/Test C++/parse flv (mp3 && aac)/";

    // string url = baseURL.append("video.mp4");
    // string url = baseURL.append("193039199_mp4_h264_aac_fhd_7.ts");
    // string url = baseURL.append("bjork-all-is-full-of-love.ts");
    // string url = baseURL.append("h265_high.mp4");
    // string url = baseURL.append("h265-high.ts");
    // string url = baseURL.append("video.mp4");
    // string url = baseURL.append("video.ts");
    // string url = baseURL.append("start.ts");
    // string url = baseURL.append("kenh14.ts");
    // string url = baseURL.append("video-w.ts");
    string url = baseURL.append("aac.flv");

    if ((format_ctx = avformat_alloc_context()) == nullptr) {
        cerr << "can't alloc AVFormatContext." << endl;
        exit(-1);
    }

    // open input file and read it header
    if (avformat_open_input(&format_ctx, url.data(), nullptr, nullptr) != 0) {
        cerr << "can't open input file." << endl;
        exit(-1);
    }

    if (avformat_find_stream_info(format_ctx, nullptr) < 0) {
        cerr << "can't find stream info for given AVFormatContext." << endl;
        exit(-1);
    }

    return format_ctx;
}

AVCodecContext *init_video_codec_context(AVCodecParameters *video_codec_params) {
    const AVCodec *video_codec              = nullptr;
    AVCodecContext *video_codec_ctx         = nullptr;

    video_codec = avcodec_find_decoder(video_codec_params->codec_id);
    video_codec_ctx = avcodec_alloc_context3(video_codec);
    avcodec_parameters_to_context(video_codec_ctx, video_codec_params);

    if (avcodec_open2(video_codec_ctx, video_codec, nullptr) < 0) {
        cerr << "can't open video codec." << endl;
        exit(-1);
    }

    return video_codec_ctx;
}

AVCodecContext *init_audio_codec_context(AVCodecParameters *audio_codec_params) {
    const AVCodec *audio_codec              = nullptr;
    AVCodecContext *audio_codec_ctx         = nullptr;

    audio_codec = avcodec_find_decoder(audio_codec_params->codec_id);
    audio_codec_ctx = avcodec_alloc_context3(audio_codec);
    avcodec_parameters_to_context(audio_codec_ctx, audio_codec_params);

    if (avcodec_open2(audio_codec_ctx, audio_codec, nullptr) < 0) {
        cerr << "can't open audio codec." << endl;
        exit(-1);
    }

    return audio_codec_ctx;
}

void init_sdl(SDL_Window **window, SDL_Renderer **renderer, SDL_Texture **texture, int window_width, int window_height) {
    // init SDL window
    *window = SDL_CreateWindow("Milan Player",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,window_width,window_height,SDL_WINDOW_OPENGL);
    if (*window == nullptr) {
        cerr << "can't create SDL window." << endl;
        exit(-1);
    }

    // init SDL renderer
    *renderer = SDL_CreateRenderer(*window, -1, 0);
    if (*renderer == nullptr) {
        cerr << "can't create SDL renderer." << endl;
        exit(-1);
    }

    // init SDL texture
    *texture = SDL_CreateTexture(*renderer,SDL_PIXELFORMAT_IYUV,SDL_TEXTUREACCESS_STREAMING,window_width,window_height);
    if (*texture == nullptr) {
        cerr << "Can't create SDL texture." << endl;
        exit(-1);
    }
}

void audio_callback(void *userdata, Uint8 *stream, int len) {
    auto *audio_codec_ctx = (AVCodecContext *)userdata;

    int len1 = -1;
    int audio_size = -1;

    // The size of audio_buf is 1.5 times the size of the largest audio frame
    // that FFmpeg will give us, which gives us a nice cushion.
    static uint8_t  audio_buf[(MAX_AUDIO_FRAME_SIZE * 3) / 2];
    static unsigned int audio_buf_size = 0;
    static unsigned int audio_buf_index = 0;

    while (len > 0)
    {
        if (audio_buf_index >= audio_buf_size)
        {
            // we have already sent all available data; get more
            audio_size = audio_decode_frame(audio_codec_ctx, audio_buf, sizeof(audio_buf));

            // if error
            if (audio_size < 0)
            {
                // output silence
                audio_buf_size = 1024;

                // clear memory
                memset(audio_buf, 0, audio_buf_size);
                printf("audio_decode_frame() failed.\n");
            }
            else
            {
                audio_buf_size = audio_size;
            }

            audio_buf_index = 0;
        }

        len1 = int(audio_buf_size - audio_buf_index);

        if (len1 > len)
        {
            len1 = len;
        }

        // copy data from audio buffer to the SDL stream
        memcpy(stream, (uint8_t *)audio_buf + audio_buf_index, len1);

        len -= len1;

        // for append data next loop if we do not have enough data
        stream += len1;

        audio_buf_index += len1;
    }
}