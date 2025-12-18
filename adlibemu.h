#pragma once

#include <stdint.h>

// general functions
void adlib_init(uint32_t samplerate);
void adlib_write(uintptr_t idx, uint8_t val);
void adlib_getsample(int16_t* sndptr, intptr_t numsamples);
