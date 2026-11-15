//
// Created by daran on 1/12/2017 to be used in ECE420 Sp17 for the first time.
// Modified by dwang49 on 1/1/2018 to adapt to Android 7.0 and Shield Tablet updates.
//

#include <jni.h>
#include "ece420_main.h"
#include "ece420_lib.h"
#include "kiss_fft/kiss_fft.h"
#include <sndfile.h>
#include <string.h>

// JNI Function
extern "C" {
JNIEXPORT void JNICALL
Java_com_ece420_lab5_MainActivity_writeNewFreq(JNIEnv *env, jclass, jint);
}

// Student Variables
#define EPOCH_PEAK_REGION_WIGGLE 30
#define VOICED_THRESHOLD 200000000
#define FRAME_SIZE 1024
#define BUFFER_SIZE (3 * FRAME_SIZE)
#define F_S 48000
float bufferIn[BUFFER_SIZE] = {};
float bufferOut[BUFFER_SIZE] = {};
int newEpochIdx = FRAME_SIZE;

// We have two variables here to ensure that we never change the desired frequency while
// processing a frame. Thread synchronization, etc. Setting to 300 is only an initializer.
int GAIN_NEW_ANDROID = 50;
float PCT_GAIN = 0.5;

// libsndfile
// see https://github.com/libsndfile/libsndfile/blob/master/examples/sfprocess.c
// also https://github.com/libsndfile/libsndfile/blob/master/tests/virtual_io_test.c
// use FRAME_SIZE for BUFFER_LEN in example

#define MAX_CHANNELS 2

int READLENGTH = FRAME_SIZE*MAX_CHANNELS; // read from multiple channels to produce a mono buffer of length FRAME_LENGTH
SF_INFO sfinfo = {};

SNDFILE *infile;
char infilename[512] = "";

bool isOpen = false;

int readcount = 0;

static void process_data (int16_t *datain, int16_t *dataout, int count, int channels);

void ece420ProcessFrame(sample_buf *dataBuf) {
    // Keep in mind, we only have 20ms to process each buffer!
    struct timeval start;
    struct timeval end;
    gettimeofday(&start, NULL);

//     Get the new gain from android
    PCT_GAIN = GAIN_NEW_ANDROID / 100;

    if (!isOpen && strlen(infilename) > 0) {
        memset (&sfinfo, 0, sizeof (sfinfo));
        infile = sf_open(infilename, SFM_READ, &sfinfo);
        if (infile == NULL) {
            LOGE("Failed to open sndfile: %s", sf_strerror(NULL));
        } else {
            isOpen = true;
        }
    }

    // Data is encoded in signed PCM-16, little-endian, mono
    int16_t readdata[FRAME_SIZE*MAX_CHANNELS]; // can hold FRAME_LENGTH samples of multiple channels
    int16_t monodata[FRAME_SIZE]; // will hold mono-summed audio

    readcount = sf_read_short(infile, readdata, READLENGTH);
    process_data(readdata, monodata, readcount, sfinfo.channels);

    for (int i = 0; i < FRAME_SIZE; i++) {
        int16_t newVal = (int16_t) bufferOut[i];

        uint8_t lowByte = (uint8_t) (0x00ff & newVal);
        uint8_t highByte = (uint8_t) ((0xff00 & newVal) >> 8);
        dataBuf->buf_[i * 2] = lowByte;
        dataBuf->buf_[i * 2 + 1] = highByte;
    }

    // Shift our old data back to make room for the new data
    for (int i = 0; i < 2 * FRAME_SIZE; i++) {
        bufferIn[i] = bufferIn[i + FRAME_SIZE - 1];
    }

    // Finally, put in our new data.
    for (int i = 0; i < FRAME_SIZE; i++) {
        bufferIn[i + 2 * FRAME_SIZE - 1] = (float) monodata[i];
    }

    // Very last thing, update your output circular buffer!
    for (int i = 0; i < 2 * FRAME_SIZE; i++) {
        bufferOut[i] = bufferOut[i + FRAME_SIZE - 1];
    }

    for (int i = 0; i < FRAME_SIZE; i++) {
        bufferOut[i + 2 * FRAME_SIZE - 1] = 0;
    }

    gettimeofday(&end, NULL);
    LOGD("Time delay: %ld us",  ((end.tv_sec * 1000000 + end.tv_usec) - (start.tv_sec * 1000000 + start.tv_usec)));
}

static void process_data (int16_t *datain, int16_t *dataout, int count, int channels)
{
    int k;
    int framesum = 0;

    // convert to mono
    for (int i = 0 ; i < count; i++){
        framesum += datain[i] / channels;
        k = i%channels;
        if (k == channels-1){ // all channels have been read
            dataout[i%channels] = PCT_GAIN / 100 * framesum; // put summation in matching position in dataout; PCT_GAIN/100 converts from percentage to scaling value
            framesum = 0;
        }
    }
} /* process_data */

void overlapAddArray(float *dest, float *src, int startIdx, int len) {
    int idxLow = startIdx;
    int idxHigh = startIdx + len;

    int padLow = 0;
    int padHigh = 0;
    if (idxLow < 0) {
        padLow = -idxLow;
    }
    if (idxHigh > BUFFER_SIZE) {
        padHigh = BUFFER_SIZE - idxHigh;
    }

    // Finally, reconstruct the buffer
    for (int i = padLow; i < len + padHigh; i++) {
        dest[startIdx + i] += src[i];
    }
}


JNIEXPORT void JNICALL
Java_com_ece420_lab5_MainActivity_writeNewFreq(JNIEnv *env, jclass, jint newFreq) {
    GAIN_NEW_ANDROID = (int) newFreq;
    return;
}


extern "C" {
JNIEXPORT void JNICALL
Java_com_ece420_lab5_MainActivity_setFilePath(JNIEnv *env, jclass, jstring filePath_) {
    const char *filePath = env->GetStringUTFChars(filePath_, NULL);
    strncpy(infilename, filePath, sizeof(infilename) - 1);
    infilename[sizeof(infilename) - 1] = '\0';
    env->ReleaseStringUTFChars(filePath_, filePath);
    LOGD("Native input filename set to: %s", infilename);
}
}