#include "iostream"

using namespace std;

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

void SavaFrame(AVFrame *pFrame, int width, int height) {
    FILE *pFile;
    char szFilename[32];
    int y;

    //生成文件名称
    sprintf(szFilename, "frame.ppm");

    //创建或打开文件
    pFile = fopen(szFilename, "wb");

    if (pFile == nullptr) {
        return;
    }

    //写入头信息
    fprintf(pFile, "P6\n%d %d\n225\n", width, height);

    //写入数据
    for (y = 0; y < height; y++) {
        fwrite(pFrame->data[0] + y * pFrame->linesize[0], 1, width * 3, pFile);
    }

    fclose(pFile);
}

int main() {
    AVFormatContext *pFormatCtx = nullptr;
    char url[] = "https://test-streams.mux.dev/x36xhzz/url_8/url_592/193039199_mp4_h264_aac_fhd_7.ts";

    if (avformat_open_input(&pFormatCtx, url, nullptr, nullptr) != 0) {
        fprintf(stderr, "can't open input file.");
        return -1;  // Couldn't open file
    }

    if (avformat_find_stream_info(pFormatCtx, nullptr) < 0) {
        fprintf(stderr, "can't find stream info with given AVFormatContext.");
        return -2;
    }

    av_dump_format(pFormatCtx, 0, url, 0);
    AVCodecParameters *codecPar = nullptr;
    int videoStream = -1;

    for (int i = 0; i < pFormatCtx->nb_streams; i++) {
        if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            codecPar = pFormatCtx->streams[i]->codecpar;
            videoStream = i;
            break;
        }
    }

    if (videoStream == -1) {
        return -1;
    }

    const AVCodec *pCodec = avcodec_find_decoder(codecPar->codec_id);
    if (pCodec == nullptr) {
        fprintf(stderr, "Unsupported codec!");
        return -1;
    }

    AVCodecContext *pCodecCtx = nullptr;
    pCodecCtx = avcodec_alloc_context3(pCodec);
    avcodec_parameters_to_context(pCodecCtx, codecPar);

    if (avcodec_open2(pCodecCtx, pCodec, nullptr) < 0) {
        return -1;
    }

    AVFrame *pFrame = av_frame_alloc();

    AVFrame *pFrameRGB = av_frame_alloc();
    if (pFrameRGB == nullptr) {
        return -1;
    }

    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, pCodecCtx->width, pCodecCtx->height, 1);
    auto *buffer = (unsigned char *)av_malloc(numBytes * sizeof(unsigned char));

    av_image_fill_arrays(pFrameRGB->data, pFrameRGB->linesize, buffer, AV_PIX_FMT_RGB24, pCodecCtx->width, pCodecCtx->height, 1);

    AVPacket packet;
    struct SwsContext *sws_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_RGB24, SWS_BILINEAR, nullptr, nullptr, nullptr);

    int ms = 0 * 1000;
    int timeStamp = ((double)ms / (double)1000) * pFormatCtx->streams[videoStream]->time_base.den / pFormatCtx->streams[videoStream]->time_base.num;

    printf("timestamp: %d \n", timeStamp);
    printf("num: %d \n", pFormatCtx->streams[videoStream]->time_base.num);
    printf("den: %d \n", pFormatCtx->streams[videoStream]->time_base.den);

    int code = av_seek_frame(pFormatCtx, videoStream, timeStamp, AVSEEK_FLAG_BACKWARD);

    if (code < 0) {
        fprintf(stderr, "av_seek_frame failed");
        return -1;
    }

    while (av_read_frame(pFormatCtx, &packet) >= 0) {
        if (packet.stream_index == videoStream) {
            if (avcodec_send_packet(pCodecCtx, &packet) != 0) {
                fprintf(stderr, "there is something wrong with avcodec_send_packet\n");
                av_packet_unref(&packet);
                continue;
            }

            if (avcodec_receive_frame(pCodecCtx, pFrame) == 0) {
                sws_scale(sws_ctx, (uint8_t const *const *)pFrame->data, pFrame->linesize, 0, pCodecCtx->height, pFrameRGB->data, pFrameRGB->linesize);

                SavaFrame(pFrameRGB, pCodecCtx->width, pCodecCtx->height);

                av_packet_unref(&packet);
                break;
            }
        }

        av_packet_unref(&packet);
    }

    av_free(buffer);
    av_frame_unref(pFrameRGB);
    av_frame_unref(pFrame);
    av_free(pFrame);
    avcodec_free_context(&pCodecCtx);
    avformat_close_input(&pFormatCtx);

    return 0;
}