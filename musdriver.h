/*
Little MUS Player v0.3

Copyright 2025 Andrew Towers

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the “Software”), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#pragma once

#include "musplayer.h"
#include "opl3.h"

#include <stddef.h>

typedef struct { int reg, val; } mus_reg_wr;
#define mus_max_regs 4096

// private
typedef struct lpf_resample_s {
    float lpf;      // low-pass filter smoothing coefficient
    float inc;      // out_rate / in_rate
    float mu;       // fractional position within current segment [0,1)
    float prev;     // previous filtered sample
    float next;     // next filtered sample
    size_t inN;     // current source offset in samples
} lpf_resample_t;

// private
typedef struct mus_driver_s {
	musplayer_t player;       // LittleMUS player
	opl3_chip opl3;           // Nuked-OPL3 instance
	int16_t *opl_buf;         // OPL3 output buffer
	size_t buf_ofs;           // write offset in oplbuf (samples)
	size_t opl_max_frames;    // max frames that fit in opl_buf
	uint32_t out_sample_rate; // output sample rate
	uint32_t until_tick;      // frames until next music tick
	int playing;              // musplayer is playing (cleared if score ends)
	uint32_t wr_pos;          // write offset in writes
	lpf_resample_t res_left;  // left channel resampling filter
	lpf_resample_t res_right; // right channel resampling filter
	mus_reg_wr writes[mus_max_regs];
} mus_driver_t;

uint32_t musdriver_opl_buf_size( uint32_t out_sample_rate, uint32_t out_max_frames );

void musdriver_init( mus_driver_t* mp, int16_t* opl_buf, uint32_t out_sample_rate, uint32_t out_max_frames, uint32_t out_cutoff_hz );

void musdriver_start(mus_driver_t* mp, void* song, int loop);

void musdriver_stop(mus_driver_t* mp);

int musdriver_generate(mus_driver_t* mp, int16_t* to_buf, uint32_t frames_needed, float volume);
