#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#include "../libs/dr_mp3.h"

#define MINIVORBIS_IMPLEMENTATION
#include "../libs/minivorbis.h"

#include "sound_sdl2.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Global audio state
 * ============================================================================ */
static AudioSource g_audio_sources[MAX_AUDIO_SOURCES];
static SDL_AudioSpec g_audio_spec;
static SDL_AudioDeviceID g_audio_device = 0;
static SDL_mutex* g_audio_mutex = NULL;
static char g_base_dir[1024] = {0};

/* ============================================================================
 * Set base directory
 * ============================================================================ */
void sound_set_base_dir(const char* dir) {
    if (dir) {
        strncpy(g_base_dir, dir, sizeof(g_base_dir) - 1);
    }
}

/* ============================================================================
 * Helper: allocate audio buffer
 * ============================================================================ */
static int allocate_audio_buffer(AudioBuffer* buf, size_t sample_count, int channels) {
    buf->sample_count = sample_count;
    buf->channels = channels;
    buf->sample_rate = AUDIO_SAMPLE_RATE;
    buf->samples = (int16_t*)SDL_malloc(sample_count * channels * sizeof(int16_t));
    if (!buf->samples) {
        buf->sample_count = 0;
        return 0;
    }
    SDL_memset(buf->samples, 0, sample_count * channels * sizeof(int16_t));
    return 1;
}

/* ============================================================================
 * Helper: resample audio buffer to target sample rate using SDL
 * ============================================================================ */
static int resample_audio_buffer(AudioBuffer* buf, int target_rate) {
    if (buf->sample_rate == target_rate) {
        return 1; /* No resampling needed */
    }

    /* Set up SDL audio conversion */
    SDL_AudioCVT cvt;
    SDL_AudioFormat format = AUDIO_S16SYS;
    int channels = buf->channels;

    int result = SDL_BuildAudioCVT(&cvt, format, (Uint8)channels, (Uint32)buf->sample_rate,
                                    format, (Uint8)channels, (Uint32)target_rate);
    if (result < 0) {
        fprintf(stderr, "[sound] SDL_BuildAudioCVT failed: %s\n", SDL_GetError());
        return 0;
    }

    if (cvt.needed) {
        /* Allocate buffer for conversion */
        int new_len = buf->sample_count * channels * sizeof(int16_t) * cvt.len_mult;
        cvt.buf = (Uint8*)SDL_malloc(new_len);
        if (!cvt.buf) {
            return 0;
        }

        /* Copy samples to conversion buffer */
        SDL_memcpy(cvt.buf, buf->samples, buf->sample_count * channels * sizeof(int16_t));
        cvt.len = buf->sample_count * channels * sizeof(int16_t);

        /* Perform conversion */
        if (SDL_ConvertAudio(&cvt) < 0) {
            fprintf(stderr, "[sound] SDL_ConvertAudio failed: %s\n", SDL_GetError());
            SDL_free(cvt.buf);
            return 0;
        }

        /* Free old buffer and replace with resampled data */
        SDL_free(buf->samples);
        buf->samples = (int16_t*)SDL_malloc(cvt.len_cvt);
        if (!buf->samples) {
            SDL_free(cvt.buf);
            return 0;
        }

        SDL_memcpy(buf->samples, cvt.buf, cvt.len_cvt);
        SDL_free(cvt.buf);

        /* Update sample count (now in frames) */
        buf->sample_count = cvt.len_cvt / (sizeof(int16_t) * channels);
        buf->sample_rate = target_rate;
    }

    return 1;
}

/* ============================================================================
 * Helper: free audio buffer
 * ============================================================================ */
static void free_audio_buffer(AudioBuffer* buf) {
    if (buf->samples) {
        SDL_free(buf->samples);
        buf->samples = NULL;
    }
    buf->sample_count = 0;
    buf->channels = 0;
    buf->sample_rate = 0;
}

/* ============================================================================
 * Decode MP3 file to audio buffer
 * ============================================================================ */
static int decode_mp3(const char* filepath, AudioBuffer* out_buffer) {
    FILE* f = fopen(filepath, "rb");
    if (!f) {
        fprintf(stderr, "[sound] Failed to open MP3 file: %s\n", filepath);
        return 0;
    }

    /* Get file size */
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    /* Read file into memory */
    unsigned char* data = (unsigned char*)SDL_malloc(file_size);
    if (!data) {
        fclose(f);
        return 0;
    }
    fread(data, 1, file_size, f);
    fclose(f);

    /* Decode MP3 */
    drmp3 mp3;
    if (!drmp3_init_memory(&mp3, data, file_size, NULL)) {
        fprintf(stderr, "[sound] Failed to initialize MP3 decoder\n");
        SDL_free(data);
        return 0;
    }

    /* Calculate output samples */
    drmp3_uint64 total_frames = drmp3_get_pcm_frame_count(&mp3);
    drmp3_uint64 total_samples = total_frames * mp3.channels;

    out_buffer->samples = (int16_t*)SDL_malloc((size_t)(total_samples * sizeof(int16_t)));
    if (!out_buffer->samples) {
        drmp3_uninit(&mp3);
        SDL_free(data);
        return 0;
    }

    drmp3_uint64 frames_decoded = drmp3_read_pcm_frames_s16(&mp3, total_frames, out_buffer->samples);
    out_buffer->sample_count = (size_t)frames_decoded;
    out_buffer->channels = mp3.channels;
    out_buffer->sample_rate = mp3.sampleRate;

    drmp3_uninit(&mp3);
    SDL_free(data);

    /* Resample to target sample rate */
    if (!resample_audio_buffer(out_buffer, AUDIO_SAMPLE_RATE)) {
        fprintf(stderr, "[sound] MP3 resampling failed\n");
        return 0;
    }

    fprintf(stderr, "[sound] MP3 decoded: %lu frames, %d channels, %d Hz -> %d Hz\n",
            (unsigned long)frames_decoded, out_buffer->channels, mp3.sampleRate, out_buffer->sample_rate);
    return 1;
}

/* ============================================================================
 * Decode OGG file to audio buffer
 * ============================================================================ */
static int decode_ogg(const char* filepath, AudioBuffer* out_buffer) {
    FILE* f = fopen(filepath, "rb");
    if (!f) {
        fprintf(stderr, "[sound] Failed to open OGG file: %s\n", filepath);
        return 0;
    }

    OggVorbis_File vf;
    int result = ov_open(f, &vf, NULL, 0);
    if (result < 0) {
        fprintf(stderr, "[sound] Failed to open OGG decoder: %ld\n", (long)result);
        fclose(f);
        return 0;
    }

    vorbis_info* vi = ov_info(&vf, -1);
    int channels = vi->channels;
    int sample_rate = vi->rate;
    ogg_int64_t total_pcm_samples = ov_pcm_total(&vf, -1); /* This is frames (sample per channel) */

    /* Allocate: total_frames * channels samples */
    ogg_int64_t total_samples = total_pcm_samples * channels;
    out_buffer->samples = (int16_t*)SDL_malloc((size_t)total_samples * sizeof(int16_t));
    if (!out_buffer->samples) {
        ov_clear(&vf);
        return 0;
    }

    /* Read all samples */
    int bytes_read = 0;
    int total_bytes = (int)(total_samples * sizeof(int16_t));
    char* buffer = (char*)out_buffer->samples;
    int section = 0;

    while (bytes_read < total_bytes) {
        int ret = ov_read(&vf, buffer + bytes_read, total_bytes - bytes_read, 0, 2, 1, &section);
        if (ret <= 0) break;
        bytes_read += ret;
    }

    out_buffer->sample_count = bytes_read / (sizeof(int16_t) * channels);
    out_buffer->channels = channels;
    out_buffer->sample_rate = sample_rate;

    ov_clear(&vf);

    /* Resample to target sample rate */
    if (!resample_audio_buffer(out_buffer, AUDIO_SAMPLE_RATE)) {
        fprintf(stderr, "[sound] OGG resampling failed\n");
        return 0;
    }

    fprintf(stderr, "[sound] OGG decoded: %ld frames, %d channels, %d Hz -> %d Hz (%.2f seconds)\n",
            (long)out_buffer->sample_count, out_buffer->channels, sample_rate, out_buffer->sample_rate,
            (float)out_buffer->sample_count / (float)out_buffer->sample_rate);
    return 1;
}

/* ============================================================================
 * Decode WAV from memory using SDL2
 * ============================================================================ */
static int decode_wav_from_memory(const uint8_t *data, size_t size, AudioBuffer *out_buffer) {
    SDL_RWops *rw = SDL_RWFromConstMem(data, (int)size);
    if (!rw) {
        fprintf(stderr, "[sound] SDL_RWFromConstMem failed: %s\n", SDL_GetError());
        return 0;
    }

    SDL_AudioSpec wav_spec;
    uint8_t *wav_buf = NULL;
    uint32_t wav_len = 0;

    if (!SDL_LoadWAV_RW(rw, 1 /* close rw */, &wav_spec, &wav_buf, &wav_len)) {
        fprintf(stderr, "[sound] SDL_LoadWAV_RW failed: %s\n", SDL_GetError());
        return 0;
    }

    /* Convert to S16SYS stereo at target rate */
    SDL_AudioCVT cvt;
    int ret = SDL_BuildAudioCVT(&cvt,
        wav_spec.format, wav_spec.channels, wav_spec.freq,
        AUDIO_S16SYS, AUDIO_CHANNELS, AUDIO_SAMPLE_RATE);
    if (ret < 0) {
        fprintf(stderr, "[sound] SDL_BuildAudioCVT (wav) failed: %s\n", SDL_GetError());
        SDL_FreeWAV(wav_buf);
        return 0;
    }

    uint8_t *converted = NULL;
    size_t converted_len = 0;

    if (ret == 0) {
        /* No conversion needed */
        converted = (uint8_t *)SDL_malloc(wav_len);
        if (!converted) { SDL_FreeWAV(wav_buf); return 0; }
        SDL_memcpy(converted, wav_buf, wav_len);
        converted_len = wav_len;
    } else {
        cvt.buf = (uint8_t *)SDL_malloc((size_t)wav_len * (size_t)cvt.len_mult);
        if (!cvt.buf) { SDL_FreeWAV(wav_buf); return 0; }
        SDL_memcpy(cvt.buf, wav_buf, wav_len);
        cvt.len = (int)wav_len;
        if (SDL_ConvertAudio(&cvt) < 0) {
            fprintf(stderr, "[sound] SDL_ConvertAudio (wav) failed: %s\n", SDL_GetError());
            SDL_free(cvt.buf);
            SDL_FreeWAV(wav_buf);
            return 0;
        }
        converted = cvt.buf;
        converted_len = (size_t)cvt.len_cvt;
    }

    SDL_FreeWAV(wav_buf);

    out_buffer->samples = (int16_t *)SDL_malloc(converted_len);
    if (!out_buffer->samples) { SDL_free(converted); return 0; }
    SDL_memcpy(out_buffer->samples, converted, converted_len);
    SDL_free(converted);

    out_buffer->channels     = AUDIO_CHANNELS;
    out_buffer->sample_rate  = AUDIO_SAMPLE_RATE;
    out_buffer->sample_count = converted_len / (sizeof(int16_t) * AUDIO_CHANNELS);

    fprintf(stderr, "[sound] WAV decoded: %lu frames, %d Hz\n",
            (unsigned long)out_buffer->sample_count, out_buffer->sample_rate);
    return 1;
}

/* ============================================================================
 * Decode MP3 from memory
 * ============================================================================ */
static int decode_mp3_from_memory(const uint8_t *data, size_t size, AudioBuffer *out_buffer) {
    drmp3 mp3;
    if (!drmp3_init_memory(&mp3, data, size, NULL)) {
        fprintf(stderr, "[sound] drmp3_init_memory failed\n");
        return 0;
    }

    drmp3_uint64 total_frames = drmp3_get_pcm_frame_count(&mp3);
    drmp3_uint64 total_samples = total_frames * mp3.channels;

    out_buffer->samples = (int16_t *)SDL_malloc((size_t)(total_samples * sizeof(int16_t)));
    if (!out_buffer->samples) { drmp3_uninit(&mp3); return 0; }

    drmp3_uint64 decoded = drmp3_read_pcm_frames_s16(&mp3, total_frames, out_buffer->samples);
    out_buffer->sample_count = (size_t)decoded;
    out_buffer->channels     = mp3.channels;
    out_buffer->sample_rate  = mp3.sampleRate;
    drmp3_uninit(&mp3);

    if (!resample_audio_buffer(out_buffer, AUDIO_SAMPLE_RATE)) {
        fprintf(stderr, "[sound] MP3 resampling failed\n");
        return 0;
    }

    fprintf(stderr, "[sound] MP3(mem) decoded: %lu frames, %d ch, %d Hz\n",
            (unsigned long)decoded, out_buffer->channels, AUDIO_SAMPLE_RATE);
    return 1;
}

/* ============================================================================
 * Decode OGG from memory via ov_open_callbacks
 * ============================================================================ */
typedef struct { const uint8_t *data; size_t size; size_t pos; } OggMem;

static size_t ogg_mem_read(void *ptr, size_t sz, size_t n, void *src) {
    OggMem *m = (OggMem *)src;
    size_t bytes = sz * n;
    size_t avail = m->size - m->pos;
    if (bytes > avail) bytes = avail;
    memcpy(ptr, m->data + m->pos, bytes);
    m->pos += bytes;
    return (sz > 0) ? bytes / sz : 0;
}
static int ogg_mem_seek(void *src, ogg_int64_t off, int whence) {
    OggMem *m = (OggMem *)src;
    size_t np;
    if (whence == SEEK_SET)      np = (size_t)off;
    else if (whence == SEEK_CUR) np = m->pos + (size_t)off;
    else                         np = m->size + (size_t)off;
    if (np > m->size) return -1;
    m->pos = np;
    return 0;
}
static long ogg_mem_tell(void *src) { return (long)((OggMem *)src)->pos; }

static int decode_ogg_from_memory(const uint8_t *data, size_t size, AudioBuffer *out_buffer) {
    OggMem mem = { data, size, 0 };
    ov_callbacks cb = { ogg_mem_read, ogg_mem_seek, NULL /* no close */, ogg_mem_tell };

    OggVorbis_File vf;
    if (ov_open_callbacks(&mem, &vf, NULL, 0, cb) < 0) {
        fprintf(stderr, "[sound] ov_open_callbacks failed\n");
        return 0;
    }

    vorbis_info *vi = ov_info(&vf, -1);
    int channels    = vi->channels;
    int sample_rate = vi->rate;
    ogg_int64_t total_frames  = ov_pcm_total(&vf, -1);
    ogg_int64_t total_samples = total_frames * channels;

    out_buffer->samples = (int16_t *)SDL_malloc((size_t)total_samples * sizeof(int16_t));
    if (!out_buffer->samples) { ov_clear(&vf); return 0; }

    int bytes_read = 0, total_bytes = (int)(total_samples * sizeof(int16_t)), section = 0;
    char *buf = (char *)out_buffer->samples;
    while (bytes_read < total_bytes) {
        int r = ov_read(&vf, buf + bytes_read, total_bytes - bytes_read, 0, 2, 1, &section);
        if (r <= 0) break;
        bytes_read += r;
    }
    out_buffer->sample_count = (size_t)bytes_read / (sizeof(int16_t) * (size_t)channels);
    out_buffer->channels     = channels;
    out_buffer->sample_rate  = sample_rate;
    ov_clear(&vf);

    if (!resample_audio_buffer(out_buffer, AUDIO_SAMPLE_RATE)) {
        fprintf(stderr, "[sound] OGG resampling failed\n");
        return 0;
    }

    fprintf(stderr, "[sound] OGG(mem) decoded: %lu frames, %d ch, %d Hz\n",
            (unsigned long)out_buffer->sample_count, channels, AUDIO_SAMPLE_RATE);
    return 1;
}

/* ============================================================================
 * Simple base64 decoder
 * ============================================================================ */
static int base64_decode(const char *in, size_t in_len, uint8_t **out, size_t *out_len) {
    static const int8_t T[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1, 0,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    };
    size_t max_out = (in_len / 4 + 1) * 3;
    uint8_t *buf = (uint8_t *)malloc(max_out);
    if (!buf) return 0;
    size_t o = 0;
    uint32_t acc = 0;
    int bits = 0;
    for (size_t i = 0; i < in_len; i++) {
        int8_t v = T[(unsigned char)in[i]];
        if (v < 0) continue;
        acc = (acc << 6) | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            buf[o++] = (uint8_t)(acc >> bits);
        }
    }
    *out = buf;
    *out_len = o;
    return 1;
}

/* ============================================================================
 * Decode a data: URL (data:audio/wav;base64,... etc.)
 * ============================================================================ */
static int load_audio_from_data_url(const char *url, AudioBuffer *out_buffer) {
    /* Skip "data:" prefix */
    const char *p = url + 5;

    /* Find the comma separating header from data */
    const char *comma = strchr(p, ',');
    if (!comma) {
        fprintf(stderr, "[sound] Malformed data URL\n");
        return 0;
    }

    /* Parse MIME type from header (between "data:" and ";base64," or ",") */
    char mime[64] = {0};
    const char *semi = strchr(p, ';');
    size_t mime_len = semi ? (size_t)(semi - p) : (size_t)(comma - p);
    if (mime_len >= sizeof(mime)) mime_len = sizeof(mime) - 1;
    memcpy(mime, p, mime_len);
    mime[mime_len] = '\0';

    /* Base64 decode the payload */
    const char *b64 = comma + 1;
    size_t b64_len = strlen(b64);
    uint8_t *raw = NULL;
    size_t raw_len = 0;
    if (!base64_decode(b64, b64_len, &raw, &raw_len) || raw_len == 0) {
        fprintf(stderr, "[sound] base64 decode failed for data URL\n");
        free(raw);
        return 0;
    }

    int result = 0;

    /* Dispatch by MIME type or magic bytes */
    if (strstr(mime, "wav") || strstr(mime, "wave") ||
        (raw_len >= 4 && raw[0]=='R' && raw[1]=='I' && raw[2]=='F' && raw[3]=='F')) {
        result = decode_wav_from_memory(raw, raw_len, out_buffer);
    } else if (strstr(mime, "mp3") || strstr(mime, "mpeg") ||
               (raw_len >= 3 && raw[0]==0x49 && raw[1]==0x44 && raw[2]==0x33) ||
               (raw_len >= 2 && raw[0]==0xFF && (raw[1]&0xE0)==0xE0)) {
        result = decode_mp3_from_memory(raw, raw_len, out_buffer);
    } else if (strstr(mime, "ogg") ||
               (raw_len >= 4 && raw[0]=='O' && raw[1]=='g' && raw[2]=='g' && raw[3]=='S')) {
        result = decode_ogg_from_memory(raw, raw_len, out_buffer);
    } else {
        /* Try WAV as fallback for unknown MIME */
        result = decode_wav_from_memory(raw, raw_len, out_buffer);
        if (!result)
            fprintf(stderr, "[sound] Unsupported data URL MIME: %s\n", mime);
    }

    free(raw);
    return result;
}

/* ============================================================================
 * Load audio file (auto-detect format)
 * ============================================================================ */
static int load_audio_file(const char* filepath, AudioBuffer* out_buffer) {
    /* Handle data: URLs */
    if (strncmp(filepath, "data:", 5) == 0) {
        return load_audio_from_data_url(filepath, out_buffer);
    }

    const char* ext = strrchr(filepath, '.');
    if (!ext) {
        fprintf(stderr, "[sound] Unknown audio format: %s\n", filepath);
        return 0;
    }

    int result = 0;
    if (SDL_strcasecmp(ext, ".mp3") == 0) {
        result = decode_mp3(filepath, out_buffer);
    } else if (SDL_strcasecmp(ext, ".ogg") == 0) {
        result = decode_ogg(filepath, out_buffer);
    } else if (SDL_strcasecmp(ext, ".wav") == 0) {
        /* Read file then decode via memory path */
        FILE *f = fopen(filepath, "rb");
        if (!f) { fprintf(stderr, "[sound] Cannot open WAV: %s\n", filepath); return 0; }
        fseek(f, 0, SEEK_END); long fsz = ftell(f); fseek(f, 0, SEEK_SET);
        uint8_t *data = (uint8_t *)SDL_malloc((size_t)fsz);
        if (!data) { fclose(f); return 0; }
        fread(data, 1, (size_t)fsz, f); fclose(f);
        result = decode_wav_from_memory(data, (size_t)fsz, out_buffer);
        SDL_free(data);
    } else {
        fprintf(stderr, "[sound] Unsupported audio format: %s\n", ext);
    }

    return result;
}

/* ============================================================================
 * Find free audio source slot (slot with no buffer allocated)
 * ============================================================================ */
static int find_free_source(void) {
    for (int i = 0; i < MAX_AUDIO_SOURCES; i++) {
        if (!g_audio_sources[i].buffer.samples) {
            return i;
        }
    }
    return -1;
}

/* ============================================================================
 * SDL Audio Callback - Mixes all active audio sources
 * ============================================================================ */
void sound_mix_callback(void* userdata, uint8_t* stream, int len) {
    (void)userdata;

    SDL_LockMutex(g_audio_mutex);

    /* Clear output buffer */
    SDL_memset(stream, 0, len);

    int samples_to_write = len / (sizeof(int16_t) * AUDIO_CHANNELS);

    for (int i = 0; i < MAX_AUDIO_SOURCES; i++) {
        AudioSource* src = &g_audio_sources[i];

        if (!src->active || src->paused || !src->buffer.samples) {
            continue;
        }

        int16_t* out_samples = (int16_t*)stream;

        for (int s = 0; s < samples_to_write; s++) {
            if (src->position >= src->buffer.sample_count) {
                if (src->looping) {
                    src->position = 0;
                } else {
                    src->active = 0;
                    break;
                }
            }

            /* Get sample from source */
            int sample_idx = (int)src->position * src->buffer.channels;
            int32_t left = 0, right = 0;

            if (src->buffer.channels == 1) {
                /* Mono to stereo */
                left = right = src->buffer.samples[sample_idx];
            } else {
                /* Stereo */
                left = src->buffer.samples[sample_idx];
                right = src->buffer.samples[sample_idx + 1];
            }

            /* Apply volume */
            left = (int32_t)(left * src->volume);
            right = (int32_t)(right * src->volume);

            /* Mix into output (with clipping) */
            int idx = s * 2;
            int32_t mixed_left = out_samples[idx] + left;
            int32_t mixed_right = out_samples[idx + 1] + right;

            out_samples[idx] = (int16_t)SDL_max(SDL_min(mixed_left, 32767), -32768);
            out_samples[idx + 1] = (int16_t)SDL_max(SDL_min(mixed_right, 32767), -32768);

            src->position++;
        }
    }

    SDL_UnlockMutex(g_audio_mutex);
}

/* ============================================================================
 * Sound Interface Implementation
 * ============================================================================ */
static void s_init(void) {
    SDL_memset(g_audio_sources, 0, sizeof(g_audio_sources));

    /* Initialize SDL audio subsystem */
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
        fprintf(stderr, "[sound] Failed to init SDL audio: %s\n", SDL_GetError());
        return;
    }

    g_audio_mutex = SDL_CreateMutex();
    if (!g_audio_mutex) {
        fprintf(stderr, "[sound] Failed to create mutex\n");
        return;
    }

    /* Set up SDL audio spec */
    g_audio_spec.freq = AUDIO_SAMPLE_RATE;
    g_audio_spec.format = AUDIO_S16SYS;
    g_audio_spec.channels = AUDIO_CHANNELS;
    g_audio_spec.silence = 0;
    g_audio_spec.samples = AUDIO_BUFFER_SIZE;
    g_audio_spec.callback = sound_mix_callback;
    g_audio_spec.userdata = NULL;

    /* Open audio device */
    g_audio_device = SDL_OpenAudioDevice(NULL, 0, &g_audio_spec, NULL, 0);
    if (g_audio_device == 0) {
        fprintf(stderr, "[sound] Failed to open audio device: %s\n", SDL_GetError());
        SDL_DestroyMutex(g_audio_mutex);
        g_audio_mutex = NULL;
        return;
    }

    fprintf(stderr, "[sound] Audio initialized: %d Hz, %d channels\n",
            g_audio_spec.freq, g_audio_spec.channels);

    /* Start audio playback */
    SDL_PauseAudioDevice(g_audio_device, 0);
}

static void s_quit(void) {
    SDL_LockMutex(g_audio_mutex);

    if (g_audio_device) {
        SDL_PauseAudioDevice(g_audio_device, 1);
        SDL_CloseAudioDevice(g_audio_device);
        g_audio_device = 0;
    }

    /* Free all audio sources */
    for (int i = 0; i < MAX_AUDIO_SOURCES; i++) {
        if (g_audio_sources[i].buffer.samples) {
            SDL_free(g_audio_sources[i].buffer.samples);
            g_audio_sources[i].buffer.samples = NULL;
        }
        g_audio_sources[i].active = 0;
    }

    SDL_UnlockMutex(g_audio_mutex);

    if (g_audio_mutex) {
        SDL_DestroyMutex(g_audio_mutex);
        g_audio_mutex = NULL;
    }

    fprintf(stderr, "[sound] Audio shutdown complete\n");
}

/* ============================================================================
 * Extended Audio Control Functions
 * ============================================================================ */
int sound_load_audio(const char* src, int* out_index) {
    /* Resolve relative path */
    char resolved_path[1024];
    if (src[0] != '/' && strncmp(src, "data:", 5) != 0) {
        if (g_base_dir[0] != '\0') {
            snprintf(resolved_path, sizeof(resolved_path), "%s/%s", g_base_dir, src);
        } else {
            strncpy(resolved_path, src, sizeof(resolved_path) - 1);
        }
    } else {
        strncpy(resolved_path, src, sizeof(resolved_path) - 1);
    }

    SDL_LockMutex(g_audio_mutex);

    int slot = find_free_source();
    if (slot < 0) {
        SDL_UnlockMutex(g_audio_mutex);
        fprintf(stderr, "[sound] No free audio source slots\n");
        return -1;
    }

    AudioSource* audio_src = &g_audio_sources[slot];

    /* Try to load the audio file */
    if (!load_audio_file(resolved_path, &audio_src->buffer)) {
        SDL_UnlockMutex(g_audio_mutex);
        return -1;
    }

    /* Initialize source state - NOT active, sounds only play when .play() is called */
    audio_src->active = 0;
    audio_src->position = 0;
    audio_src->volume = 1.0f;
    audio_src->looping = 0;
    audio_src->paused = 1;
    audio_src->format = AUDIO_FORMAT_NONE;

    /* Detect format from extension */
    const char* ext = strrchr(src, '.');
    if (ext) {
        if (SDL_strcasecmp(ext, ".mp3") == 0) {
            audio_src->format = AUDIO_FORMAT_MP3;
        } else if (SDL_strcasecmp(ext, ".ogg") == 0) {
            audio_src->format = AUDIO_FORMAT_OGG;
        }
    }

    SDL_strlcpy(audio_src->src, src, sizeof(audio_src->src));

    *out_index = slot;
    SDL_UnlockMutex(g_audio_mutex);

    fprintf(stderr, "[sound] Loaded audio into slot %d: %s\n", slot, src);
    return slot;
}

void sound_play(int index) {
    if (index < 0 || index >= MAX_AUDIO_SOURCES) return;

    SDL_LockMutex(g_audio_mutex);
    AudioSource* src = &g_audio_sources[index];

    if (src->buffer.samples) {
        src->active = 1;
        src->paused = 0;
        if (src->position >= src->buffer.sample_count) {
            src->position = 0;
        }
    }

    SDL_UnlockMutex(g_audio_mutex);
}

void sound_pause(int index) {
    if (index < 0 || index >= MAX_AUDIO_SOURCES) return;

    SDL_LockMutex(g_audio_mutex);
    g_audio_sources[index].paused = 1;
    SDL_UnlockMutex(g_audio_mutex);
}

void sound_stop(int index) {
    if (index < 0 || index >= MAX_AUDIO_SOURCES) return;

    SDL_LockMutex(g_audio_mutex);
    AudioSource* src = &g_audio_sources[index];
    src->active = 0;
    src->position = 0;
    SDL_UnlockMutex(g_audio_mutex);
}

void sound_set_volume(int index, float volume) {
    if (index < 0 || index >= MAX_AUDIO_SOURCES) return;

    SDL_LockMutex(g_audio_mutex);
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
    g_audio_sources[index].volume = volume;
    SDL_UnlockMutex(g_audio_mutex);
}

void sound_set_loop(int index, int looping) {
    if (index < 0 || index >= MAX_AUDIO_SOURCES) return;

    SDL_LockMutex(g_audio_mutex);
    g_audio_sources[index].looping = looping;
    SDL_UnlockMutex(g_audio_mutex);
}

int sound_is_playing(int index) {
    if (index < 0 || index >= MAX_AUDIO_SOURCES) return 0;

    SDL_LockMutex(g_audio_mutex);
    AudioSource* src = &g_audio_sources[index];
    int playing = src->active && !src->paused;
    SDL_UnlockMutex(g_audio_mutex);

    return playing;
}

int sound_has_ended(int index) {
    if (index < 0 || index >= MAX_AUDIO_SOURCES) return 0;

    SDL_LockMutex(g_audio_mutex);
    AudioSource* src = &g_audio_sources[index];
    /* ended = was started (position > 0) and is now inactive and not explicitly paused */
    int ended = !src->active && !src->paused && src->position > 0;
    SDL_UnlockMutex(g_audio_mutex);

    return ended;
}

float sound_get_duration(int index) {
    if (index < 0 || index >= MAX_AUDIO_SOURCES) return 0.0f;

    SDL_LockMutex(g_audio_mutex);
    AudioSource* src = &g_audio_sources[index];
    float duration = 0.0f;
    if (src->buffer.sample_count > 0 && src->buffer.sample_rate > 0) {
        duration = (float)src->buffer.sample_count / (float)src->buffer.sample_rate;
    }
    SDL_UnlockMutex(g_audio_mutex);

    return duration;
}

float sound_get_current_time(int index) {
    if (index < 0 || index >= MAX_AUDIO_SOURCES) return 0.0f;

    SDL_LockMutex(g_audio_mutex);
    AudioSource* src = &g_audio_sources[index];
    float time = 0.0f;
    if (src->buffer.sample_rate > 0) {
        time = (float)src->position / (float)src->buffer.sample_rate;
    }
    SDL_UnlockMutex(g_audio_mutex);

    return time;
}

void sound_set_current_time(int index, float time) {
    if (index < 0 || index >= MAX_AUDIO_SOURCES) return;

    SDL_LockMutex(g_audio_mutex);
    AudioSource* src = &g_audio_sources[index];
    if (src->buffer.sample_rate > 0) {
        src->position = (size_t)(time * src->buffer.sample_rate);
        if (src->position > src->buffer.sample_count) {
            src->position = src->buffer.sample_count;
        }
    }
    SDL_UnlockMutex(g_audio_mutex);
}

void sound_unload(int index) {
    if (index < 0 || index >= MAX_AUDIO_SOURCES) return;

    SDL_LockMutex(g_audio_mutex);
    AudioSource* src = &g_audio_sources[index];
    free_audio_buffer(&src->buffer);
    src->active = 0;
    src->src[0] = '\0';
    SDL_UnlockMutex(g_audio_mutex);
}

/* ============================================================================
 * Interface initialization
 * ============================================================================ */
void sound_sdl2_init_iface(SoundInterface* iface) {
    iface->init = s_init;
    iface->quit = s_quit;
}
