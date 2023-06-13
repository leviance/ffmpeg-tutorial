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

void save_frame() {

}

int main(int argc, char *args[]) {
    AVFormatContext         *format_ctx             = nullptr;
    string                  file_path               = "../../videos/video.mp4";
    int                     video_stream_index      = -1;
    int                     audio_stream_index      = -1;
    AVStream                *video_stream           = nullptr;
    AVStream                *audio_stream           = nullptr;
    AVCodecParameters       *video_codec_params     = nullptr;
    AVCodecParameters       *audio_codec_params     = nullptr;
    const AVCodec           *video_codec            = nullptr;
    const AVCodec           *audio_codec            = nullptr;
    AVCodecContext          *video_codec_ctx        = nullptr;
    AVCodecContext          *audio_codec_ctx        = nullptr;
    AVPacket                *packet                 = nullptr;
    AVFrame                 *frame                  = nullptr;
    SwsContext              *sws_ctx                = nullptr;
    uint8_t                 *rgb_frame[4]           = {nullptr};
    int                     rgb_frame_linesize[4]   = {0};

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
                             video_codec_ctx->width, video_codec_ctx->height, AV_PIX_FMT_RGB24,
                             SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (sws_ctx == nullptr) {
        cerr << "Can't get sws context." << endl;
        return GET_SWS_CTX_ERROR;
    }

    // Read packet and decode into frame
    while (av_read_frame(format_ctx, packet) >= 0) {
        if (packet->stream_index == video_stream_index) {
            int ret = avcodec_send_packet(video_codec_ctx, packet);

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



                av_frame_unref(frame);
            }

        }

        if (packet->stream_index == audio_stream_index) {

        }

        av_packet_unref(packet);
    }

    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&video_codec_ctx);
    avcodec_free_context(&audio_codec_ctx);
    avformat_free_context(format_ctx);
    cout << "COMPLETE" << endl;

    return 0;
}