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

#include <stdint.h>

// private
typedef struct __attribute__((packed)) MUS_voiceS {
    uint8_t    modChar;        // 0x20 Modulator characteristic (Mult, KSR, EG, VIB and AM flags)
    uint8_t    modAttack;      // 0x60 Modulator attack/decay level
    uint8_t    modSustain;     // 0x80 Modulator sustain/release level
    uint8_t    modWaveSel;     // 0xE0 Modulator wave select
    uint8_t    modScale;       // 0x40 Modulator key scaling (first two bits)
    uint8_t    modLevel;       // 0x40 Modulator output level (last six bits)
    uint8_t    feedback;       // 0xC0 Feedback/connection (low 4 bits)
    uint8_t    carChar;        // 0x23 Carrier characteristic (Mult, KSR, EG, VIB and AM flags)
    uint8_t    carAttack;      // 0x63 Carrier attack/decay level
    uint8_t    carSustain;     // 0x83 Carrier sustain/release level
    uint8_t    carWaveSel;     // 0xE3 Carrier wave select
    uint8_t    carScale;       // 0x43 Carrier key scaling (first two bits)
    uint8_t    carLevel;       // 0x43 Carrier output level (last six bits)
    uint8_t    reserved;       // Unused
    int16_t    noteOfs;        // MIDI note offset (ignored when "fixed note" flag is set)
} MUS_voice;

// private
typedef struct __attribute__((packed)) MUS_instrumentS {
    int16_t    flags;          // 0x01 - fixed note, 0x02 - delayed vibrato (unused), 0x04 - Double-voice mode
    uint8_t    fineTune;       // Second voice detune level, 128 center, (fine_tune / 2) - 64
    uint8_t    noteNum;        // Percussion note number, or the note to play when fixed note (between 0 and 127)
    MUS_voice  voice[2];       // Instrument voices, 2nd is for double-voice
} MUS_instrument;

// private
typedef struct mus_hw_voice_s {
    // playing note:
    int seq;                // note key-on sequence number (to key-off the oldest)
    int release;            // if non-zero, the release finish time
    int16_t noteid;         // MIDI note from the key-on command, for key-off (-1 if not playing)
    int8_t note_att;        // key-on note attenuation level (for volume update)
    uint16_t hw_cmd;        // Last hw_cmd written to HW, for key-off
    uint8_t p_note;         // Playing MIDI note, inc. noteOfs (for pitch bend)
    uint8_t mus_ch;         // MUS channel that owns the playing note (-1 if not playing)
    // instrument config:
    int16_t ins_sel;        // current instrument configured on the HW channel
    uint8_t ksl1, ksl2;     // current instrument KSL values (used to change operator volume)
    uint8_t lvl1, lvl2;     // current instrument Level values (used to change operator volume)
    uint8_t sumMode;        // current instrument connection type (0=FM 1=Add)
    int8_t fineTune;        // current instrument fine tune (mus->fineTune / 2) - 64
} mus_hw_voice_t;

// private
typedef struct mus_channel_s {
    uint8_t mono;           // silence all notes when a new note is played
    uint8_t last_vol;       // volume of the previous note played on the channel
    int8_t vol_att;         // channel volume (attenuation level)
    int8_t exp_att;         // channel expression (attenuation level)
    int8_t bend;            // channel pitch bend (+/- 127)
    uint8_t ins_idx;        // selected MIDI instrument on this channel (index into op2bank)
    MUS_instrument* ins;    // selected instrument data
} mus_channel_t;

enum mus_constants {
    mus_num_voices = 18,       // OPL2=9 OPL3=18
    mus_bank_two = 9,          // for OPL3
    mus_num_channels = 16,     // MUS file has 16 channels
};

typedef struct musplayer_s {
    uint8_t* loop_score;
    uint8_t* score;
    int delay;
    int mus_time;
    int next_free;
    int next_keyon_seq;
    int main_att;
    mus_channel_t channels[mus_num_channels];
    mus_hw_voice_t hw_voices[mus_num_voices];
    MUS_instrument op2bank[175];  // ~6K
} musplayer_t;


/*
Set the OP2 Instrument Bank (https://moddingwiki.shikadi.net/wiki/OP2_Bank_Format)
before playing music.

Pass a pointer to the BYTE[175][36] instrument data, which is found after
the 8-byte "#OPL_II#" header in .OP2 files.

The instrument data (175*36 bytes) is copied into library memory.

If you're a DOOM engine, you can get this data from the `GENMIDI` lump:

```c
  int op2lump = W_GetNumForName("GENMIDI");
  char *op2 = W_CacheLumpNum( op2lump, PU_STATIC );
  musplay_op2bank(op2+8); // skip "#OPL_II#" to get BYTE[175][36] instrument data
  Z_Free( op2 );
```
*/
void musplay_op2bank (musplayer_t* player, char* data);

/*
Set the player volume (0-127, 100 = _full volume_)

This is combined into the hardware attenuation levels written to the
adlib registers in the OPL emulator.

In other words, the generated samples will come out more loud or quiet.

If volume is greater than 100, it will _boost_ the volume above the
arranged level of the music, within the headroom available.
*/
void musplay_volume (musplayer_t* player, int volume);

/*
Start playing a MUS file/lump.

The supplied data must start with MUS_header ("MUS", 0x1A)
see https://moddingwiki.shikadi.net/wiki/MUS_Format

Only one song can play at a time.

The song will loop if loop is non-zero.

This writes OPL registers (via `adlib_write`) to initialise the hardware.
The first note will be produced later, when `musplay_update` is called.
*/
void musplay_start (musplayer_t* player, char* data, int loop);

/*
Stop playing the MUS file.

This writes OPL registers (via `adlib_write`) to key-off all channels.
*/
void musplay_stop (musplayer_t* player);

/*
Advance time in 140 Hz ticks i.e. send 140 ticks per second (70 for Raptor)

This writes OPL registers by calling `void adlib_write(int reg, int val)`
which must be implemented by the OPL emulator, or your program.

This can be called unconditionally. It won't do anything unless a song is playing.

Returns 1 if the music is still playing, or 0 if the music finished (looped tracks never finish.)
*/
int musplay_update (musplayer_t* player, int ticks);
