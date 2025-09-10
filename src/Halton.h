#pragma once
#include <cstdint>

inline float Halton(uint32_t index, uint32_t base) {
    float f = 1.0f;
    float r = 0.0f;
    while (index > 0) {
        f /= (float)base;
        r += f * (index % base);
        index /= base;
    }
    return r;
}
