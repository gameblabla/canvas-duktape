#pragma once
#include "common/types.h"
#include <stdint.h>
#include <stddef.h>

/* ============================================================================
 * Audio source structure for mixing multiple sounds
 * ============================================================================ */
#define MAX_AUDIO_SOURCES 128
#define AUDIO_SAMPLE_RATE 44100
#define AUDIO_CHANNELS    2
#define AUDIO_FORMAT      AUDIO_S16LSB
#define AUDIO_BUFFER_SIZE 2048

typedef enum {
    AUDIO_FORMAT_NONE = 0,
    AUDIO_FORMAT_MP3,
    AUDIO_FORMAT_OGG
} AudioFormat;

typedef struct {
    int16_t* samples;      /* interleaved stereo samples */
    size_t   sample_count; /* total samples (stereo frames) */
    int      sample_rate;
    int      channels;
} AudioBuffer;

typedef struct {
    int        active;
    AudioBuffer buffer;
    size_t     position;    /* current read position */
    float      volume;      /* 0.0 - 1.0 */
    int        looping;
    int        paused;
    int        borrowed;    /* if 1, buffer.samples is not owned — do not free */
    AudioFormat format;
    char       src[512];    /* source file path */
} AudioSource;

/* Get the sound interface with extended audio functions */
void sound_sdl2_init_iface(SoundInterface* iface);

/* Set base directory for resolving relative paths */
void sound_set_base_dir(const char* dir);

/* Extended audio control functions (called from JS) */
int  sound_load_audio(const char* src, int* out_index);
void sound_play(int index);
void sound_pause(int index);
void sound_stop(int index);
void sound_set_volume(int index, float volume);
void sound_set_loop(int index, int looping);
int  sound_is_playing(int index);
int  sound_has_ended(int index);
float sound_get_duration(int index);
float sound_get_current_time(int index);
void sound_set_current_time(int index, float time);
void sound_unload(int index);

/* Decode audio to caller-owned PCM (no slot allocated) */
int  sound_decode_to_memory(const char* path, int16_t** out_samples,
                             size_t* out_count, int* out_channels, int* out_rate);

/* Play a borrowed buffer (samples not owned by the slot; slot auto-reclaimed when done) */
int  sound_play_buffer_borrowed(const int16_t* samples, size_t sample_count,
                                int channels, int sample_rate,
                                float volume, int looping);

/* Internal: called by SDL audio callback */
void sound_mix_callback(void* userdata, uint8_t* stream, int len);
