#include "iostream"
#include <SDL.h>
#include "stdlib.h"
#include "time.h"

using namespace std;

#define MUS_PATH "audio.wav"

// prototype for our audio callback
// see the implementation for more information
void my_audio_callback(void *userdata, Uint8 *stream, int len);

// variable declarations
static Uint8 *audio_pos; // global pointer to the audio buffer to be played
static Uint32 audio_len; // remaining length of the sample we have to play


/*
** PLAYING A SOUND IS MUCH MORE COMPLICATED THAN IT SHOULD BE
*/
int main(int argc, char* argv[]){
    srand(time(nullptr));

    // Initialize SDL.
    if (SDL_Init(SDL_INIT_AUDIO) < 0)
        return 1;

    // local variables
    static Uint32 wav_length; // length of our sample
    static Uint8 *wav_buffer; // buffer containing our audio file
    static SDL_AudioSpec wav_spec; // the specs of our piece of music


    /* Load the WAV */
    // the specs, length and buffer of our wav are filled
    if( SDL_LoadWAV(MUS_PATH, &wav_spec, &wav_buffer, &wav_length) == nullptr ){
        return 1;
    }
    // set the callback function
    wav_spec.callback = my_audio_callback;
    wav_spec.userdata = nullptr;
    // set our global static variables
    audio_pos = wav_buffer; // copy sound buffer
    audio_len = wav_length; // copy file length

    /* Open the audio device */
    if ( SDL_OpenAudio(&wav_spec, nullptr) < 0 ){
        fprintf(stderr, "Couldn't open audio: %s\n", SDL_GetError());
        exit(-1);
    }

    /* Start playing */
    SDL_PauseAudio(0);

    // wait until we're don't playing
    while ( audio_len > 0 ) {
        SDL_Delay(100);
    }

    // shut everything down
    SDL_CloseAudio();
    SDL_FreeWAV(wav_buffer);

}

// audio callback function
// here you have to copy the data of your audio buffer into the
// requesting audio buffer (stream)
// you should only copy as much as the requested length (len)
void my_audio_callback(void *userdata, Uint8 *stream, int len) {
    for (int i = 0; i < len; ++i) {
        stream[i] = rand() % 255 + 1;
    }
}