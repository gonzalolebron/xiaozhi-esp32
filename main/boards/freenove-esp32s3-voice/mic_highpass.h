// High-pass filter for the microphone, 80 Hz, second order (Butterworth).
//
// The INMP441 picks up rumble below ~60 Hz: air on the capsule, or vibration of
// whatever the board rests on. It cannot be heard, but on 2026-10-10 it was more
// than half the energy of the "silence" between words, and the server's VAD never
// found the end of a sentence. Speech starts around 100 Hz, so cutting at 80 Hz
// takes the rumble out (and any DC offset with it) without touching the voice.
//
// Kept free of ESP-IDF on purpose: tests/test_mic_highpass.py compiles it on the
// host and measures its response.
#ifndef MIC_HIGHPASS_H
#define MIC_HIGHPASS_H

#include <cmath>
#include <cstddef>
#include <cstdint>

class MicHighPass {
public:
    explicit MicHighPass(float cutoff_hz = 80.0f, float sample_rate = 16000.0f) {
        // RBJ cookbook biquad, Q = 1/sqrt(2).
        const float w0 = 2.0f * static_cast<float>(M_PI) * cutoff_hz / sample_rate;
        const float c = std::cos(w0);
        const float alpha = std::sin(w0) / (2.0f * 0.70710678f);
        const float a0 = 1.0f + alpha;
        b0_ = (1.0f + c) / 2.0f / a0;
        b1_ = -(1.0f + c) / a0;
        b2_ = b0_;
        a1_ = -2.0f * c / a0;
        a2_ = (1.0f - alpha) / a0;
    }

    // Filters `n` samples in place; the state carries over between calls.
    void Process(int16_t* data, size_t n) {
        for (size_t i = 0; i < n; ++i) {
            const float x = data[i];
            const float y = b0_ * x + b1_ * x1_ + b2_ * x2_ - a1_ * y1_ - a2_ * y2_;
            x2_ = x1_;
            x1_ = x;
            y2_ = y1_;
            y1_ = y;
            data[i] = y > 32767.0f ? 32767 : (y < -32767.0f ? -32767 : static_cast<int16_t>(std::lround(y)));
        }
    }

private:
    float b0_, b1_, b2_, a1_, a2_;
    float x1_ = 0, x2_ = 0, y1_ = 0, y2_ = 0;
};

#endif  // MIC_HIGHPASS_H
