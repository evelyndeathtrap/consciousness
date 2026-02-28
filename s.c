#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <fcntl.h>
#include <unistd.h>
#include <AL/al.h>
#include <AL/alc.h>

// --- Configuration ---
#define BUFFER_SAMPLES 4096  // Increased for stability
#define SAMPLE_RATE 386000    // Lowered for CPU efficiency
#define NUM_BUFFERS 1024
#define ITERATIONS 256        // Lowered to prevent stutter, increase if CPU allows

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Global time tracker
double phase = 0;
// Cache for random data to avoid slow system calls in the loop
unsigned char random_cache[1024];

// --- DSP Effect Function ---
void apply_effect(ALshort *buffer, int num_samples) {
    static int cache_ptr = 0;
    
    // Get fresh random data for this buffer from cache
    unsigned char seed = random_cache[cache_ptr % 1024];
    cache_ptr++;

    // Map seed to frequency jitter (+/- 20Hz)
    float jitter = ((float)(seed % 400) / 10.0f) - 20.0f;
    float current_freq = 120.0f + jitter;
    
    for (int i = 0; i < num_samples; i++) {
        float sum_of_sines = 0.0f;
        float time = (float)i / SAMPLE_RATE;

        // Sum of Sines algorithm
        for (int k = 1; k <= ITERATIONS; k++) {
            sum_of_sines += (1.0f / k) * cos(2.0 * M_PI * (current_freq * k) * (time + phase));
        }

        // Apply modulation
        float modulation = 0.5f + (0.5f * sum_of_sines);
        float sample = (float)buffer[i] / 32768.0f;
        
        // Ring Modulation
        sample *= modulation; 

        // Clamp
        if (sample > 1.0f) sample = 1.0f;
        if (sample < -1.0f) sample = -1.0f;
        buffer[i] = (ALshort)(sample * 32767.0f);
    }
    
    phase += (double)num_samples / SAMPLE_RATE;
}

int main() {
    ALCdevice *captureDev, *playbackDev;
    ALCcontext *ctx;
    ALuint source, buffers[NUM_BUFFERS];
    ALshort tempBuffer[BUFFER_SAMPLES];
    ALint samplesAvailable, processed, state;

    // --- Fill Random Cache ---
    int fd = open("/dev/urandom", O_RDONLY);
    read(fd, random_cache, sizeof(random_cache));
    close(fd);

    // --- Setup Playback ---
    playbackDev = alcOpenDevice(NULL);
    if (!playbackDev) return 1;
    ctx = alcCreateContext(playbackDev, NULL);
    alcMakeContextCurrent(ctx);

    // --- Setup Capture ---
    captureDev = alcCaptureOpenDevice(NULL, SAMPLE_RATE, AL_FORMAT_MONO16, BUFFER_SAMPLES * 2);
    if (!captureDev) return 1;

    // --- Setup OpenAL ---
    alGenSources(1, &source);
    alGenBuffers(NUM_BUFFERS, buffers);

    alcCaptureStart(captureDev);

    // --- Initial Buffering ---
    for (int i = 0; i < NUM_BUFFERS; i++) {
        do {
            alcGetIntegerv(captureDev, ALC_CAPTURE_SAMPLES, 1, &samplesAvailable);
        } while (samplesAvailable < BUFFER_SAMPLES);

        alcCaptureSamples(captureDev, tempBuffer, BUFFER_SAMPLES);
        apply_effect(tempBuffer, BUFFER_SAMPLES);
        alBufferData(buffers[i], AL_FORMAT_MONO16, tempBuffer, BUFFER_SAMPLES * sizeof(ALshort), SAMPLE_RATE);
    }

    alSourceQueueBuffers(source, NUM_BUFFERS, buffers);
    alSourcePlay(source);

    // --- Main Loop ---
    while (1) {
        alGetSourcei(source, AL_BUFFERS_PROCESSED, &processed);
        while (processed > 0) {
            ALuint bufID;
            alSourceUnqueueBuffers(source, 1, &bufID);

            alcGetIntegerv(captureDev, ALC_CAPTURE_SAMPLES, 1, &samplesAvailable);
            
            if (samplesAvailable >= BUFFER_SAMPLES) {
                alcCaptureSamples(captureDev, tempBuffer, BUFFER_SAMPLES);
                apply_effect(tempBuffer, BUFFER_SAMPLES);
                alBufferData(bufID, AL_FORMAT_MONO16, tempBuffer, BUFFER_SAMPLES * sizeof(ALshort), SAMPLE_RATE);
                alSourceQueueBuffers(source, 1, &bufID);
            }
            processed--;
        }

        alGetSourcei(source, AL_SOURCE_STATE, &state);
        if (state != AL_PLAYING) alSourcePlay(source);
    }

    return 0;
}
