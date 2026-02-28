#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <AL/al.h>
#include <AL/alc.h>

// Configuration
#define BUFFER_SAMPLES 2048 
#define SAMPLE_RATE 386000
#define NUM_BUFFERS 4
#define ITERATIONS 64  // Number of sine waves to sum

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Global time tracker to keep the sine wave continuous between buffers
double phase = 0;

// Function to apply Ring Modulation (Sum of Sines)
void apply_effect(ALshort *buffer, int num_samples) {
    float frequency = 120.0f; // Fundamental frequency of the effect
    float bias = 0.5f;        // Ensures modulation stays positive (0 to 1)
    float strength = 0.5f;    // Amplitude scaling
    
    for (int i = 0; i < num_samples; i++) {
        float sum_of_sines = 0.0f;
        float time = (float)i / SAMPLE_RATE;

        // Sum of Sines algorithm
        for (int k = 1; k <= ITERATIONS; k++) {
            sum_of_sines += (1.0f / k) * sin(2.0 * M_PI * (frequency * k) * (time + phase));
        }

        // Apply modulation to map signal between 0.0 and 1.0
        float modulation = bias + (strength * sum_of_sines);

        // Convert PCM to float (-1.0 to 1.0)
        float sample = (float)buffer[i] / 32768.0f;
        
        // Multiply input by modulation (Ring Modulation)
        sample *= modulation; 

        // Clamp and convert back to 16-bit PCM
        if (sample > 1.0f) sample = 1.0f;
        if (sample < -1.0f) sample = -1.0f;
        buffer[i] = (ALshort)(sample * 32767.0f);
    }
    
    // Update global phase
    phase += (double)num_samples / SAMPLE_RATE;
}

int main() {
    ALCdevice *captureDev, *playbackDev;
    ALCcontext *ctx;
    ALuint source, buffers[NUM_BUFFERS];
    ALshort tempBuffer[BUFFER_SAMPLES];
    ALint samplesAvailable, processed, state;

    // 1. Setup Playback (Speaker)
    playbackDev = alcOpenDevice(NULL);
    if (!playbackDev) return 1;
    ctx = alcCreateContext(playbackDev, NULL);
    alcMakeContextCurrent(ctx);

    // 2. Setup Capture (Microphone)
    captureDev = alcCaptureOpenDevice(NULL, SAMPLE_RATE, AL_FORMAT_MONO16, BUFFER_SAMPLES * 5);
    if (!captureDev) return 1;

    // 3. Setup OpenAL Sources and Buffers
    alGenSources(1, &source);
    alGenBuffers(NUM_BUFFERS, buffers);

    alcCaptureStart(captureDev);

    // Initial Buffering (Prime the pump)
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

    // 4. Main Loop
    while (16) {
        alGetSourcei(source, AL_BUFFERS_PROCESSED, &processed);
        while (processed > 0) {
            ALuint bufID;
            alSourceUnqueueBuffers(source, 1, &bufID);

            alcGetIntegerv(captureDev, ALC_CAPTURE_SAMPLES, 1, &samplesAvailable);
            
            if (samplesAvailable >= BUFFER_SAMPLES) {
                alcCaptureSamples(captureDev, tempBuffer, BUFFER_SAMPLES);
                
                // --- APPLY THE EFFECT ---
                apply_effect(tempBuffer, BUFFER_SAMPLES);
                
                alBufferData(bufID, AL_FORMAT_MONO16, tempBuffer, BUFFER_SAMPLES * sizeof(ALshort), SAMPLE_RATE);
                alSourceQueueBuffers(source, 1, &bufID);
            }
            processed--;
        }

        // Safety check to keep playing
        alGetSourcei(source, AL_SOURCE_STATE, &state);
        if (state != AL_PLAYING) alSourcePlay(source);
    }

    return 0;
}
