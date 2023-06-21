#include "iostream"
#include "time.h"

using namespace std;

const int MAX_AUDIO_FRAME_SIZE = 192000;

int audio_decode(uint8_t *AUDIO_BUFFER) {
    int decoded_data_len = rand() % 30;

    for (int i = 0; i < decoded_data_len; ++i) {
        AUDIO_BUFFER[i] = i;
    }

    cout << "decoded data length: " << decoded_data_len << endl;

    return decoded_data_len;
}

void audio_callback(uint8_t *stream, int len) {
    static uint8_t AUDIO_BUFFER[MAX_AUDIO_FRAME_SIZE * 3 / 2] = {0};
    static int first = 0, last = 0;

    int stream_first = 0;

    while (len > 0) {
        int buffer_len = last - first;

        // When we do not have any data in "AUDIO_BUFFER" start decoding.
        if (buffer_len == 0) {
            first = 0;

            int ret = audio_decode(AUDIO_BUFFER);

            if (last < 0) {
                fill(stream, stream + len, 0);
                break;
            }

            last = ret;
            buffer_len = last - first;
        }

        if (buffer_len >= len) {
            memcpy(stream + stream_first, AUDIO_BUFFER + first, len);
            first += len;
            break;
        }

        else {
            memcpy(stream + stream_first, AUDIO_BUFFER + first, buffer_len);
            len -= buffer_len;
            stream_first += buffer_len;
            first = 0;
            last = 0;
        }
    }
}

int main() {
    srand(time(nullptr));
    uint8_t stream[10] = {0};

    for (int j = 0; j < 3; ++j) {
        audio_callback(stream, 10);
        for (unsigned char i : stream) {
            cout << (int)i << " ";
        }
        cout << endl;
    }

}