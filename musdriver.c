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

#include "musdriver.h"

#include <math.h>
#include <string.h>

#define PI 3.1415926535897932384626433832795f

// Hardware OPL chip clockrate (samplerate)
#define OPL_CLOCKRATE		49716

// Player tickrate = 140
// 49716 / 140 = 355.1143   (49716 Hz OPL3; 140 Hz MUS)
//   355 * 140 = 49700      (near enough)
#define SAMPLES_PER_TICK        355

// When downsampling from OPL_SAMPLERATE, we occasionally
// need an extra sample to finish the buffer.
#define OPL_EXTRA_SAMPLES       1

// Number of channels Nuked-OPL generates (always 2)
#define OPL_CHANNELS 2

//static music_player mus_player = {0};


// Called by LittleMUS to write to Nuked-OPL3.
void adlib_write( musplayer_t* mp, int reg, int val ) {
    mus_driver_t* driver = (mus_driver_t*) mp;

    OPL3_WriteRegBuffered(&driver->opl3, reg, val);

    // queue up the reg write
    //if (driver->wr_pos >= mus_max_regs) {
    //    return; // buffer overflow
    //}
    //driver->writes[driver->wr_pos].reg = reg;
    //driver->writes[driver->wr_pos].val = val;
    //driver->wr_pos++;
}


static int musdriver_advance( mus_driver_t* mp, uint32_t opl_frames_needed );

// OPL filter and Linear Interpolator
// 1-pole LPF (first order, -6 dB/octave)

static inline void musdriver_downsample_init(lpf_resample_t *rs, float in_rate, float out_rate, float cutoff_hz) {
    rs->lpf = 1.0f - expf(-2.0f * PI * cutoff_hz / in_rate);
    rs->inc = in_rate / out_rate;  // e.g. 49716/44100 ≈ 1.1279
    rs->mu = 1.0f;
    rs->prev = 0.0f;
    rs->next = 0.0f;
}

// request another out sample (assumes there is space)
static inline int16_t musdriver_downsample_step(mus_driver_t* mp, lpf_resample_t *rs, int16_t *in, size_t *inSize, float volume)
{
        while (rs->mu >= 1.0f) {
            rs->prev = rs->next;

	    // next input sample
            if (__builtin_expect(!!(rs->inN >= *inSize),0)) {
		// this happens occasionally when we need one extra input sample
		// to complete the downsampled output buffer.
		if (!musdriver_advance(mp, 1)) {
			return 0; // buffer overflow
		}
		*inSize += OPL_CHANNELS;
	    }
	    float samp = (float)(in[rs->inN]) * volume;
	    rs->inN += OPL_CHANNELS;

            // 1st-order LPF
            rs->next = rs->next + (samp - rs->next) * rs->lpf;

            rs->mu -= 1.0;
        }

	// linear interpolation
        float y = rs->prev + (rs->next - rs->prev) * rs->mu;
        rs->mu += rs->inc;

	// quantize
	int iy = (int)y;

	// clipping
        if (iy > 0x7fff) {
            iy = 0x7fff;
        } else if (iy < -0x8000) {
            iy = -0x8000;
        }

        return (int16_t)iy;
}

static void musdriver_filter( mus_driver_t* mp, int16_t* mix_out, size_t mix_frames_needed, float volume ) {
	mp->res_left.inN = 0; // left channel sample-offset
	mp->res_right.inN = 1; // right channel sample-offset
	size_t samples_avail = mp->buf_ofs; // downsample_step can increment
	int16_t* leftout = mix_out;
	int16_t* rightout = mix_out+1;
	int16_t* leftend = leftout + mix_frames_needed*OPL_CHANNELS;

	while (leftout != leftend)
	{
		*leftout = musdriver_downsample_step(mp, &mp->res_left, mp->opl_buf, &samples_avail, volume);  // left channel
		*rightout = musdriver_downsample_step(mp, &mp->res_right, mp->opl_buf, &samples_avail, volume);  // right channel
		leftout += OPL_CHANNELS;
		rightout += OPL_CHANNELS;
	}
}

static int musdriver_gen_opl( mus_driver_t* mp, uint32_t num_frames ) {
	uint32_t i;
	size_t ofs = mp->buf_ofs;
	int16_t* buf = mp->opl_buf;
	if (ofs + num_frames * OPL_CHANNELS > mp->opl_max_frames * OPL_CHANNELS) {
		return 0; // buffer overflow
	}
	/*
	if (mp->wr_pos) {
		// there are pending register writes
		uint32_t num_writes = mp->wr_pos <= num_frames ? mp->wr_pos : num_frames;
		mus_reg_wr *writes = mp->writes;
		for (i = 0; i < num_writes; i++) {
			// write the next register
			OPL3_WriteReg(&mp->opl3, writes->reg, writes->val);
			writes++;
			// advance the OPL player one sample
			OPL3_Generate(&mp->opl3, buf + ofs);
			ofs += OPL_CHANNELS;
		}
		if (mp->wr_pos == num_writes) {
			mp->wr_pos = 0; // used all writes
		} else {
			mp->wr_pos -= num_writes;
			// copy down remaining writes (not ideal)
			memcpy(mp->writes, mp->writes + num_writes, sizeof(mus_reg_wr) * mp->wr_pos);
		}
		num_frames -= num_writes;
	}
	*/
	for (i = 0; i < num_frames; i++) {
		// advance the OPL player one sample
		OPL3_Generate(&mp->opl3, buf + ofs);
		ofs += OPL_CHANNELS;
	}
	mp->buf_ofs = ofs;
	return 1;
}

static int musdriver_advance( mus_driver_t* mp, uint32_t opl_frames_needed ) {
	// alternate between generating OPL samples and ticking the music player
	while (opl_frames_needed >= mp->until_tick) {
		// use up all frames left, then do a music tick
		if (mp->until_tick) {
			if (!musdriver_gen_opl(mp, mp->until_tick)) {   // advances music_bufofs
				return 0; // buffer overflow
			}
			opl_frames_needed -= mp->until_tick;  // used up frames
		}
		// perform a music tick
		mp->until_tick = SAMPLES_PER_TICK;
		if (mp->playing) {
			// the music player calls adlib_write (above)
			mp->playing = musplay_update(&mp->player, 1);
		}
	}
	if (opl_frames_needed) {
		// use up all of remain; can't do another music tick
		mp->until_tick -= opl_frames_needed;
		if (!musdriver_gen_opl(mp, opl_frames_needed)) { // advances music_bufofs
			return 0; // buffer overflow
		}
	}
	return 1; // ok
}

// Calculate the required opl_buf size for `out_sample_rate` and `out_max_frames`.
// out_sample_rate is the sample rate you want from musdriver_generate, typically 48000 or 44100,
// or another rate determined by the audio subsystem you're using.
// out_max_frames is the maximum chunk size in frames (e.g. pairs of samples for stereo) you want
// from musdriver_generate, which will be determined by the audio subsystem you're using (typically 512-2048)
uint32_t musdriver_opl_buf_size( uint32_t out_sample_rate, uint32_t out_max_frames ) {
	uint32_t opl_max_frames = ((out_max_frames * OPL_CLOCKRATE + (out_sample_rate-1)) / out_sample_rate) + OPL_EXTRA_SAMPLES;
	return opl_max_frames * sizeof(int16_t) * OPL_CHANNELS;
}

// Initialise mus_driver_t ready to start playing via musplay_start.
// opl_buf is an allocated buffer of size musdriver_opl_buf_size() in bytes.
// out_sample_rate is the sample rate you want from musdriver_generate (same as musdriver_opl_buf_size call)
// out_max_frames is the maximum chunk size in frames (same as musdriver_opl_buf_size call)
// out_cutoff_hz is the low-pass filter cutoff frequency, typically 8000 (8 KHz)
void musdriver_init( mus_driver_t* mp, int16_t* opl_buf, uint32_t out_sample_rate, uint32_t out_max_frames, uint32_t out_cutoff_hz ) {
	mp->opl_buf = opl_buf;
	mp->buf_ofs = 0;
	mp->opl_max_frames = ((out_max_frames * OPL_CLOCKRATE + (out_sample_rate-1)) / out_sample_rate) + OPL_EXTRA_SAMPLES;
	mp->out_sample_rate = out_sample_rate;
	mp->until_tick = 0;
	mp->playing = 0;
	musdriver_downsample_init(&mp->res_left, OPL_CLOCKRATE, out_sample_rate, out_cutoff_hz);
	musdriver_downsample_init(&mp->res_right, OPL_CLOCKRATE, out_sample_rate, out_cutoff_hz);
}

void musdriver_start(mus_driver_t* mp, void* song, int loop) {
    OPL3_Reset(&mp->opl3, OPL_CLOCKRATE);
    musplay_start(&mp->player, song, loop);
    mp->playing = 1;
    mp->until_tick = 0;
    mp->wr_pos = 0;
}

void musdriver_stop(mus_driver_t* mp) {
    if (mp->playing) {
        musplay_stop(&mp->player);
	mp->playing = 0;
    }
}

// Generate sample-frames to fill `to_buf` with `frames_needed` samples.
// volume is between 0 and 1 (on a logarithmic curve)
int musdriver_generate(mus_driver_t* mp, int16_t* to_buf, uint32_t frames_needed, float volume) {
	uint32_t opl_frames_needed = frames_needed * OPL_CLOCKRATE / mp->out_sample_rate; // floor
	if (opl_frames_needed > mp->opl_max_frames) {
		return 0; // buffer overflow
	}
	mp->buf_ofs = 0; // reset for this sample chunk
	if (!musdriver_advance(mp, opl_frames_needed)) {
		return 0; // buffer overflow
	}
	if (mp->buf_ofs != opl_frames_needed*OPL_CHANNELS) {
		return 0; // didn't fill the buffer
	}
	musdriver_filter(mp, to_buf, frames_needed, volume);
	return 1;
}
