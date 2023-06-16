#ifndef TUTORIAL_03_ERROR_CODE_H
#define TUTORIAL_03_ERROR_CODE_H

#include "iostream"

enum ERROR_CODE {
    ALLOC_FMT_CTX_ERROR = INT_MIN,
    OPEN_INPUT_ERROR,
    FIND_STREAM_INFO_ERROR,
    VIDEO_STREAM_NOT_FOUND,
    AUDIO_STREAM_NOT_FOUND,
    FIND_VIDEO_DECODER_ERROR,
    FIND_AUDIO_DECODER_ERROR,
    ALLOC_VIDEO_CODEC_CTX_ERROR,
    ALLOC_AUDIO_CODEC_CTX_ERROR,
    COPY_VIDEO_CODEC_PARAMS_ERROR,
    COPY_AUDIO_CODEC_PARAMS_ERROR,
    OPEN_VIDEO_CODEC_ERROR,
    OPEN_AUDIO_CODEC_ERROR,
    ALLOC_PACKET_ERROR,
    ALLOC_FRAME_ERROR,
    SEND_VIDEO_PACKET_ERROR,
    SEND_AUDIO_PACKET_ERROR,
    RECEIVE_VIDEO_FRAME_ERROR,
    RECEIVE_AUDIO_FRAME_ERROR,
    GET_SWS_CTX_ERROR,
    ALLOC_RGB_FRAME_ERROR,
    INIT_SDL_LIB_ERROR,
    CREATE_SDL_WINDOW_ERROR,
    CREATE_SDL_RENDERER_ERROR,
    CREATE_SDL_TEXTURE_ERROR,
    OPEN_SDL_AUDIO_ERROR,
    ALLOC_SWR_CONTEXT_ERROR,
    INIT_SWR_CONTEXT_ERROR
};

#endif //TUTORIAL_03_ERROR_CODE_H
