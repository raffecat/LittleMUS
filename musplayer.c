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

With thanks to:
* The DOSBox Team, OPL2/OPL3 emulation library
* Ken Silverman, ADLIBEMU.C
* https://github.com/rofl0r/woody-opl
* https://doomwiki.org/wiki/MUS
* https://moddingwiki.shikadi.net/wiki/MUS_Format
* https://moddingwiki.shikadi.net/wiki/OP2_Bank_Format
* https://cosmodoc.org/topics/adlib-functions/ !
* https://www.doomworld.com/idgames/docs/editing/mus_form
* https://www.phys.unsw.edu.au/jw/notes.html
* id software, for giving us DOOM.
*/

#include "musplayer.h"

#include <stdint.h>
#include <string.h>

#include <stdio.h>

// This must be implemented by the program using musplayer.
void adlib_write(musplayer_t* mp, int reg, int val);

typedef uint8_t byte;

// TO DO:
// ctrl_bank_select
// ctrl_modulation
// ctrl_pan - stereo mix?
// ctrl_expression
// ctrl_reverb
// ctrl_chorus
// ctrl_sustain
// ctrl_soft

typedef struct __attribute__((packed)) MUS_headerS {
    char       ID[4];          // "MUS", 0x1A
    int16_t    scoreLen;       // in bytes
    int16_t    scoreStart;     // offset to start of score
    int16_t    pri_channels;   // number of primary channels used (0-8; excludes percussion #9)
    int16_t    sec_channels;   // number of secondary channels used (10-14; excludes percussion #15) [rarely used]
    int16_t    instrCnt;       // number of instruments
    int16_t    reserved;       // always 0
    int16_t    instruments[1]; // array of instruments (0-127 standard, 135-181 percussion: notes 35-81 on #15)
} MUS_header;

enum mus_flags {
    musf_fixed_note = 1,
    musf_delayed_vibrato = 2,
    musf_double_voice = 4,
};

// MUS body contains only one track.
// Channels numbered from 0, not from 1 (standard midi); percussion on channel #15.
// MUS notes are dynamically mapped to an available OPL channel to allow polyphony.

enum event_type {
    event_release = 0,
    event_note = 1,
    event_pitch_wheel = 2,
    event_system = 3,
    event_controller = 4,
    event_end_of_measure = 5,
    event_end_of_score = 6,
    event_unused = 7,
};

enum controller_enum {
    ctrl_instrument    = 0,  // MIDI -      Change instrument (MIDI event 0xC0)
    ctrl_bank_select   = 1,  // MIDI 0/32   Bank select: 0 by default
    ctrl_modulation    = 2,  // MIDI 1      Modulation (frequency vibrato depth)
    ctrl_volume        = 3,  // MIDI 7      Volume: 0-silent, ~100-normal, 127-loud
    ctrl_pan           = 4,  // MIDI 10     Pan (balance): 0-left, 64-center (default), 127-right
    ctrl_expression    = 5,  // MIDI 11     Expression 
    ctrl_reverb        = 6,  // MIDI 91     Reverb depth
    ctrl_chorus        = 7,  // MIDI 93     Chorus depth
    ctrl_sustain       = 8,  // MIDI 64     Sustain pedal (hold)
    ctrl_soft          = 9,  // MIDI 67     Soft pedal
    ctrl_all_sound_off = 10, // MIDI 120    All sounds off (silence immediately)
    ctrl_all_notes_off = 11, // MIDI 123    All notes off (key off)
    ctrl_mono          = 12, // MIDI 126    Mono (one note per channel)
    ctrl_poly          = 13, // MIDI 127    Poly (multiple notes per channel)
    ctrl_reset_all     = 14, // MIDI 121    Reset all controllers on this channel
    ctrl_event         = 15, //             Never implemented
};

enum opl3_pan {
    opl3_pan_left = 0x10,
    opl3_pan_centre = 0x30,   // at pan=64
    opl3_pan_right = 0x20,

    // -6 dB when panned to centre (routing to both speakers)
    // roughly half loudness to account for two speakers;
    // E2M8 "Nobody Told Me About Id" now sounds right.
    opl3_centre_att = 6,

    // +/-20 must stay centered to match "Dark Halls" recordings,
    // +/-21 has some basis: 128/3 = 42.666; 42/2 = 21 each side.
    opl3_pan_threshold = 21,  // 64-N < centre < 64+N

    // the key-on bit in a hw_cmd from note_cmds.
    // note_cmds all have this bit set.
    opl3_key_on = 1<<13,
    opl3_mask_key_off = ~(uint16_t)(1<<13),

    // number of steps (70ths of a second) in LFO cycle; see lfo.py
    opl3_lfo_steps = 12,

    // vibrato bit in 0x20 register (AM, VIB, EG, KSR, Mult)
    opl3_vibrato_bit = 64,
};

// Adlib HW mapping: 9 channels -> operator 1, operator 2
static int chan_oper1[] = { 0, 1, 2,  8,  9, 10, 16, 17, 18 };
static int chan_oper2[] = { 3, 4, 5, 11, 12, 13, 19, 20, 21 };

// A0 and B0 bytes for each note (fnum, block, key-on)
// GENERATED by note_freq.py (zeros are out of range and won't key-on)
static uint16_t note_cmds[] = { // 256 bytes
  8536, 8557, 8579, 8602, 8626, 8652, 8679, 8708, 
  8739, 8772, 8806, 8843, 8881, 8922, 8966, 9012, 
  9061, 9112, 9167, 9732, 9763, 9796, 9830, 9867, 
  9905, 9946, 9990, 10036, 10085, 10136, 10191, 10756, 
  10787, 10820, 10854, 10891, 10929, 10970, 11014, 11060, 
  11109, 11160, 11215, 11780, 11811, 11844, 11878, 11915, 
  11953, 11994, 12038, 12084, 12133, 12184, 12239, 12804, 
  12835, 12868, 12902, 12939, 12977, 13018, 13062, 13108, 
  13157, 13208, 13263, 13828, 13859, 13892, 13926, 13963, 
  14001, 14042, 14086, 14132, 14181, 14232, 14287, 14852, 
  14883, 14916, 14950, 14987, 15025, 15066, 15110, 15156, 
  15205, 15256, 15311, 15876, 15907, 15940, 15974, 16011, 
  16049, 16090, 16134, 16180, 16229, 16280, 16335, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 
};


// volume/attenuation tables
// HW_level = clamp(20 · Σ k_i·log10(vol_i/100) / -0.75, 0, 63)
// where k=2 for channel volume/expression and k=~3 for note velocity
// GENERATED by vol_ramp.py
static int8_t att_log_square[128] = {
    96, 96, 90, 81, 74, 69, 65, 61, 58, 55, 53, 51, 49, 47, 45, 43, 42, 41,
    39, 38, 37, 36, 35, 34, 33, 32, 31, 30, 29, 28, 27, 27, 26, 25, 24, 24,
    23, 23, 22, 21, 21, 20, 20, 19, 19, 18, 17, 17, 17, 16, 16, 15, 15, 14,
    14, 13, 13, 13, 12, 12, 11, 11, 11, 10, 10, 9, 9, 9, 8, 8, 8, 7, 7, 7,
    6, 6, 6, 6, 5, 5, 5, 4, 4, 4, 4, 3, 3, 3, 2, 2, 2, 2, 1, 1, 1, 1, 0, 0,
    0, 0, 0, 0, 0, 0, 0, -1, -1, -1, -1, -1, -2, -2, -2, -2, -3, -3, -3, -3,
    -3, -4, -4, -4, -4, -4, -4, -5, -5, -5
};

// GENERATED by lfo.py
static int8_t lfo_table[12*8] = {
    0,0,1,2,1,0,0,0,-1,-2,-1,-1,
    0,1,3,4,3,1,0,-1,-3,-4,-3,-2,
    0,2,5,6,5,2,0,-2,-5,-6,-5,-3,
    0,3,6,8,6,3,0,-3,-6,-8,-6,-4,
    0,4,8,10,8,4,0,-4,-8,-10,-8,-5,
    0,5,10,12,10,5,0,-5,-10,-12,-10,-6,
    0,6,12,14,12,6,0,-6,-12,-14,-12,-7,
    0,7,13,16,13,7,0,-7,-13,-16,-13,-8
};

static void key_off_hw(musplayer_t* mp, int hw_ch) {
    if (mp->hw_voices[hw_ch].noteid >= 0) {
        // printf("[MUS] *%d key off\n", hw_ch);
        int hw_cmd = mp->hw_voices[hw_ch].hw_cmd & opl3_mask_key_off; // clear key-on bit
        int B=0, ch = hw_ch; if (ch >= mus_bank_two) { ch -= mus_bank_two; B = 0x100; } // OPL3 2nd bank
        adlib_write(mp, (B|0xB0)+ch, (hw_cmd >> 8));    // clear register bit 5 (key-off)
        mp->hw_voices[hw_ch].noteid = -1;
        mp->hw_voices[hw_ch].release = mp->mus_time;    // wait 1 tick before next key-on
        mp->hw_voices[hw_ch].koff_seq = mp->next_kon++; // key-off sequence (oldest key-off)
        mp->hw_voices[hw_ch].hw_cmd = hw_cmd;           // update hw_cmd for bend_channel, modulation
    }
}

static void silence_hw(musplayer_t* mp, int hw_ch) {
    // set release speed to maximum (instant)
    int B=0, ch = hw_ch; if (ch >= mus_bank_two) { ch -= mus_bank_two; B = 0x100; } // OPL3 2nd bank
    adlib_write(mp, (B|0x80)+chan_oper1[ch], 0xFF);   // sustain 15 (-93dB) release 15 (shortest)
    adlib_write(mp, (B|0x80)+chan_oper2[ch], 0xFF);   // sustain 15 (-93dB) release 15 (shortest)
    int hw_cmd = mp->hw_voices[hw_ch].hw_cmd & opl3_mask_key_off; // clear key-on bit
    adlib_write(mp, (B|0xB0)+ch, (hw_cmd >> 8));      // clear register bit 5 (key-off)
    // loaded instrument is no longer valid
    mp->hw_voices[hw_ch].ins_sel = -1;
}

static void key_off_note(musplayer_t* mp, int mus_ch, int note) {
    // find the oldest matching note.
    uint32_t oldest_v0_seq = 0xFFFFFFFF;
    uint32_t oldest_v1_seq = 0xFFFFFFFF;
    int oldest_v0 = -1;
    int oldest_v1 = -1;
    int note_v1 = note | 0x100; // for dual-voice notes (voice=0/1 in bit 8)
    for (int h=0; h<mus_num_voices; h++) {
        // match same channel
        if (mp->hw_voices[h].mus_ch == mus_ch) {
            // match same note
            if (mp->hw_voices[h].noteid == note) {
                if (mp->hw_voices[h].kon_seq < oldest_v0_seq) {
                    oldest_v0_seq = mp->hw_voices[h].kon_seq;
                    oldest_v0 = h;
                }
            } else if (mp->hw_voices[h].noteid == note_v1) {
                if (mp->hw_voices[h].kon_seq < oldest_v1_seq) {
                    oldest_v1_seq = mp->hw_voices[h].kon_seq;
                    oldest_v1 = h;
                }
            }
        }
    }
    if (oldest_v0 >= 0) {
        key_off_hw(mp, oldest_v0);
    } else {
        printf("[MUS] #%d key off note (%d) - not found\n", mus_ch, note);
    }
    if (oldest_v1 >= 0) {
        key_off_hw(mp, oldest_v1);
    }
}

static void key_off_mus_all(musplayer_t* mp, int mus_ch) {
    for (int h=0; h<mus_num_voices; h++) {
        if (mp->hw_voices[h].mus_ch == mus_ch) {
            key_off_hw(mp, h);
        }
    }
}

static void silence_mus_all(musplayer_t* mp, int mus_ch) {
    for (int h=0; h<mus_num_voices; h++) {
        if (mp->hw_voices[h].mus_ch == mus_ch) {
            silence_hw(mp, h);
        }
    }
}

static void load_hw_instrument(musplayer_t* mp, mus_hw_voice_t* hw, int hw_ch, int ins_sel, int mus_ch) {
    int ins = ins_sel & 0xFF;
    int vi = ins_sel >> 8;
    if (ins >= 175) {
        printf("[MUS] *%d BAD instrument %d\n", hw_ch, ins);
        return;
    }
    MUS_instrument* in = &mp->op2bank[ins];
    MUS_voice* v = &in->voice[vi]; // voice index 0/1 in bit 8 of instrument selector
    printf("[MUS] *%d load instrument %d-%d\n", hw_ch, ins, vi);
    uint8_t modChar = v->modChar;
    uint8_t carChar = v->carChar;
    // software vibrato
    uint8_t sw_vib = 0;
    if (mp->channels[mus_ch].uses_mod) {
        // Turn off HW vibrato; enable SW vibrato.
        if (v->carChar & opl3_vibrato_bit) {
            sw_vib = 1;
            carChar &= ~opl3_vibrato_bit;
        }
        if ((v->feedback & 1) && (v->modChar & opl3_vibrato_bit)) {
            sw_vib = 1;
            modChar &= ~opl3_vibrato_bit;
        }
        if (sw_vib) printf("[MUS] load instrument *%d with software vibrato\n", hw_ch);
    }
    hw->sw_vib = sw_vib;
    int B=0, ch = hw_ch; if (ch >= mus_bank_two) { ch -= mus_bank_two; B = 0x100; } // OPL3 2nd bank
    adlib_write(mp, (B|0x20)+chan_oper1[ch], modChar);       // Modulator AM, VIB, EG, KSR, Mult
    adlib_write(mp, (B|0x60)+chan_oper1[ch], v->modAttack);  // Modulator attack, decay
    adlib_write(mp, (B|0x80)+chan_oper1[ch], v->modSustain); // Modulator sustain, release
    adlib_write(mp, (B|0xE0)+chan_oper1[ch], v->modWaveSel); // Modulator wave select
    adlib_write(mp, (B|0x20)+chan_oper2[ch], carChar);       // Carrier AM, VIB, EG, KSR, Mult (no VIB)
    adlib_write(mp, (B|0x60)+chan_oper2[ch], v->carAttack);  // Carrier attack, decay
    adlib_write(mp, (B|0x80)+chan_oper2[ch], v->carSustain); // Carrier sustain, release
    adlib_write(mp, (B|0xE0)+chan_oper2[ch], v->carWaveSel); // Carrier wave select
    hw->ksl1 = v->modScale;                                  // Modulator key scaling (top two bits)
    hw->ksl2 = v->carScale;                                  // Carrier key scaling (top two bits)
    hw->lvl1 = v->modLevel;                                  // Modulator output level (low six bits)
    hw->lvl2 = v->carLevel;                                  // Carrier output level (low six bits)
    hw->feedback = v->feedback;                              // Current instrument connection type (0=FM 1=Add)
    hw->fineTune = vi ? (in->fineTune / 2) - 64 : 0;         // Second voice detune level
    hw->ins_sel = ins_sel;                                   // instrument+voice configured on this channel
    if (hw->lvl1 > 63) {
        hw->lvl1 = 63; // bad instrument data
    }
    if (hw->lvl2 > 63) {
        hw->lvl2 = 63; // bad instrument data
    }
}

static void switch_to_sw_vib(musplayer_t* mp, int mus_ch) {
    // Switch mus_ch to software vibrato (modify all voices on mus_ch)
    for (int hw_ch=0; hw_ch<mus_num_voices; hw_ch++) {
        if (mp->hw_voices[hw_ch].mus_ch == mus_ch) {
            // Turn off HW vibrato; enable SW vibrato.
            mus_hw_voice_t* hw = &mp->hw_voices[hw_ch];
            MUS_voice* v = &mp->op2bank[hw->ins_sel & 255].voice[hw->ins_sel >> 8];
            uint8_t sw_vib = 0;
            int B=0, ch = hw_ch; if (ch >= mus_bank_two) { ch -= mus_bank_two; B = 0x100; } // OPL3 2nd bank
            if (v->carChar & opl3_vibrato_bit) {
                // switch carrier to software vibrato.
                sw_vib = 1;
                adlib_write(mp, (B|0x20)+chan_oper2[ch], (uint8_t)(v->carChar & ~opl3_vibrato_bit));
            }
            if ((v->feedback & 1) && (v->modChar & opl3_vibrato_bit)) {
                // switch modulator to software vibrato.
                sw_vib = 1;
                adlib_write(mp, (B|0x20)+chan_oper1[ch], (uint8_t)(v->modChar & ~opl3_vibrato_bit));
            }
            if (sw_vib) printf("[MUS] switch *%d to software vibrato\n", hw_ch);
            mp->hw_voices[hw_ch].sw_vib = sw_vib;
        }
    }
}

static int bend_pitch(musplayer_t* mp, int note, int bend, int fineTune) {
    // 0 is normal, -64 half-tone down, -128 one tone down, 64 half-tone up, 127 one tone up.
    // pitch bend is continous over this range (using linear approximation)
    if (!bend) {
        if (!fineTune) {
            return note_cmds[note] & opl3_mask_key_off; // current note (no key-on)
        }
        int hw_cmd = note_cmds[note];                     // current base note
        // int freq = (hw_cmd & 1023) + fineTune;            // absolute freq (scaled)
        // int scale = (hw_cmd >> 10) & 7;                   // 3-bit scale field
        // if (freq >= 1024) { freq >>= 1; ++scale; }        // crosses into next octave (drop another bit)
        // return (scale << 10)|freq;                        // re-encode A0|B0 hw_cmd (no key-on)
        int scale = (hw_cmd >> 10) & 7;                   // 3-bit scale field
        int freq = ((hw_cmd & 1023) + fineTune) << scale; // absolute freq (scaled)
        if (freq >= 1024) ++scale;                        // crosses into next octave (drop another bit)
        return (scale << 10)|(freq >> scale);             // re-encode A0|B0 hw_cmd (no key-on)
    } else if (bend > 0) {                                // 1-127+ amount to bend up
        while (bend >= 64) { bend -= 64; note += 1; }     // go up N halftones
        int cmd = note_cmds[note];                        // current note (lower note)
        int lscale = (cmd >> 10) & 7;                     // 3-bit scale field (lower scale)
        int lfreq = ((cmd & 1023) + fineTune) << lscale;  // absolute freq (un-scale)
        int next = note_cmds[++note];                     // next halftone above (higher note)
        int hscale = (next >> 10) & 7;                    // 3-bit scale field (higher scale)
        int hfreq = ((next & 1023) + fineTune) << hscale; // absolute freq (higher note)
        lfreq += ((hfreq - lfreq) * bend) >> 6;           // add partial halftone
        if (lfreq >= (1024 << hscale)) ++hscale;          // crosses into next octave (drop another bit)
        return (hscale << 10)|(lfreq >> hscale);          // re-encode A0|B0 hw_cmd (no key-on)
    } else {                                              // bend < 0
        bend = -bend;                                     // 1-127+ amount to bend down
        while (bend >= 64) { bend -= 64; note -= 1; }     // go down N halftones
        int cmd = note_cmds[note];                        // current note (higher note)
        int hscale = (cmd >> 10) & 7;                     // 3-bit scale field (higher scale)
        int hfreq = ((cmd & 1023) + fineTune) << hscale;  // absolute freq (un-scale)
        int next = note_cmds[--note];                     // next halftone below (lower note)
        int lscale = (next >> 10) & 7;                    // 3-bit scale field (lower scale)
        int lfreq = ((next & 1023) + fineTune) << lscale; // absolute freq (lower note)
        hfreq -= ((hfreq - lfreq) * bend) >> 6;           // subtract partial halftone
        if (hfreq >= (1024 << hscale)) ++hscale;          // crosses into next octave (drop another bit)
        return (hscale << 10)|(hfreq >> hscale);          // re-encode A0|B0 hw_cmd (no key-on)
    }
}

static void bend_channel(musplayer_t* mp, int mus_ch, int bend) {
    for (int h=0; h<mus_num_voices; h++) {
        if (mp->hw_voices[h].mus_ch == mus_ch && mp->hw_voices[h].noteid >= 0) {
            mus_hw_voice_t* hw = &mp->hw_voices[h];
            // XXX also needs LFO modulation here..
            int hw_cmd = bend_pitch(mp, hw->p_note, bend, hw->fineTune); // see key_on
            hw_cmd |= (hw->hw_cmd & opl3_key_on); // add current key-on state
            int B=0, ch = h; if (ch >= mus_bank_two) { ch -= mus_bank_two; B = 0x100; } // OPL3 2nd bank
            adlib_write(mp, (B|0xA0)+ch, hw_cmd & 255); // frequency low 8 bits
            adlib_write(mp, (B|0xB0)+ch, hw_cmd >> 8);  // frequency top 2 bits, octave (shift), key-on
        }
    }
}

static inline int clamp(int v, int min_v, int max_v) {
    return v >= min_v ? (v <= max_v ? v : max_v) : min_v;
}

static void set_hw_volume(musplayer_t* mp, mus_hw_voice_t* hw, int hw_ch, int ch_att, int pan_bits) {
    // "Attenuation = 24*d5 + 12*d4 + 6*d3 + 3*d2 + 1.5*d1 + 0.75*d0 (dB)" - Yamaha's YMF262 doc
    // FM mode: operator 1 modulates frequency of operator 2.
    int pan_att = (pan_bits == opl3_pan_centre) ? opl3_centre_att : 0;
    int v_att = clamp(mp->main_att + hw->note_att + ch_att + pan_att, 0, 63);
    int att1 = hw->lvl1;
    int att2 = hw->lvl2 + v_att; if (att2 > 63) att2 = 63; else if (att2 < 0) att2 = 0;
    // Additive mode: operator 1 is summed with operator 2.
    if (hw->feedback&1) { att1 += v_att; if (att1 > 63) att1 = 63; else if (att1 < 0) att1 = 0; }
    // printf("[MUS] *%d volume (%d) lvls %d %d %d #%d\n", hw_ch, hw->noteid, 63-att1, 63-att2, v_att, mus_ch);
    int B=0, ch = hw_ch; if (ch >= mus_bank_two) { ch -= mus_bank_two; B = 0x100; } // OPL3 2nd bank
    adlib_write(mp, (B|0x40)+chan_oper1[ch], hw->ksl1|att1); // operator 1 attenuation + KSL
    adlib_write(mp, (B|0x40)+chan_oper2[ch], hw->ksl2|att2); // operator 2 attenuation + KSL
    adlib_write(mp, (B|0xC0)+ch, hw->feedback | pan_bits); // channel pan + feedback + add/fm mode
}

static void update_volume(musplayer_t* mp, int mus_ch, int ch_att, int pan_bits) {
    for (int hw_ch=0; hw_ch<mus_num_voices; hw_ch++) {
        if (mp->hw_voices[hw_ch].mus_ch == mus_ch) {
            mus_hw_voice_t* hw = &mp->hw_voices[hw_ch];
            set_hw_volume(mp, hw, hw_ch, ch_att, pan_bits);
        }
    }
}

static void key_on(musplayer_t* mp, int hw_ch, int noteid, int note, int noteOfs, int note_att, int ch_att, int mus_ch, int bend, int pan_bits) {
    mus_hw_voice_t* hw = &mp->hw_voices[hw_ch];
    hw->note_att = note_att;        // key-on note attenuation (before controllers)
    hw->noteid = noteid;            // save midi note for key_off (original key_on note)
    hw->p_note = note + noteOfs;    // playing midi note (for pitch bend)
    hw->mus_ch = mus_ch;            // save mus_ch for key_off_mus
    hw->kon_seq = mp->next_kon++;   // key-on sequence (oldest key-on)
    set_hw_volume(mp, hw, hw_ch, ch_att, pan_bits);
    uint16_t hw_cmd = bend_pitch(mp, note + noteOfs, bend, hw->fineTune) | opl3_key_on;
    int B=0, ch = hw_ch; if (ch >= mus_bank_two) { ch -= mus_bank_two; B = 0x100; } // OPL3 2nd bank
    adlib_write(mp, (B|0xA0)+ch, hw_cmd & 255); // frequency low 8 bits
    adlib_write(mp, (B|0xB0)+ch, hw_cmd >> 8);  // key-on | 3-bit octave | 2-bit freq high
    hw->hw_cmd = hw_cmd;                        // save last hw_cmd for pitch bend, modulation, key_off
}

// XXX out of date.
// model release time from sustain level (lerp) using Actual Time Tables.
// 1. use the oldest keyed-off voice playing the same instrument (re-trigger),
// 2. use any free (fully released) voice, based on time model;
// 3. use any keyed-off 'almost silent' voice, based on time model;
// 4. hijack keyed-off channel with the lowest level, based on time model;
// 5. hijack the quietest playing note (lowest sustain level)

static int choose_hw_voice(musplayer_t* mp, int ins_sel, int mus_ch, int noteid) {
    // prefers the oldest keyed-off voice,
    // or failing that, the oldest voice.
    uint32_t oldest_koff_seq = 0xFFFFFFFF;
    uint32_t oldest_idle_seq = 0xFFFFFFFF;
    uint32_t oldest_reuse_seq = 0xFFFFFFFF;
    int oldest_koff = -1;
    int oldest_idle = -1;
    int oldest_reuse = -1;
    int lowest_mus = -1; // lowest priority MUS channel
    int lowest = 0; // always valid
    for (int i = 0; i < mus_num_voices; i++) {
        // find the lowest priority voice.
        if (mp->hw_voices[i].mus_ch > lowest_mus && mp->hw_voices[i].mus_ch < 15) { // avoid killing percussion
            lowest_mus = mp->hw_voices[i].mus_ch;
            lowest = i;
        }
        // find keyed-off voices.
        if (mp->hw_voices[i].noteid < 0) {
            uint32_t koff_seq = mp->hw_voices[i].koff_seq;
            if (koff_seq < oldest_koff_seq) {
                // voice is the oldest key-off.
                oldest_koff_seq = koff_seq;
                oldest_koff = i;
            }
            if (mp->mus_time != mp->hw_voices[i].release) {
                if (koff_seq < oldest_idle_seq) {
                    // voice is the oldest fully key-off.
                    oldest_idle_seq = koff_seq;
                    oldest_idle = i;
                }
                if (koff_seq < oldest_reuse_seq && mp->hw_voices[i].ins_sel == ins_sel) {
                    // voice is the oldest fully keyed-off with the same instrument.
                    oldest_reuse_seq = koff_seq;
                    oldest_reuse = i;
                }
            }
        }
    }
    if (oldest_reuse >= 0) { return oldest_reuse; } // oldest fully keyed-off channel with the same instrument.
    if (oldest_idle >= 0) { printf("[MUS] *%d oldest idle\n", oldest_idle); return oldest_idle; } // oldest fully keyed-off channel.
    if (oldest_koff >= 0) { printf("[MUS] *%d oldest koff (not idle)\n", oldest_koff); return oldest_koff; } // oldest keyed-off channel.
    // steal the lowest priority channel.
    // key-off the voice, otherwise OPL HW won't see a key-on.
    printf("[MUS] *%d KILLED #%d - overflow\n", lowest, mp->hw_voices[lowest].mus_ch);
    key_off_hw(mp, lowest);
    return lowest;
}

static void play_note(musplayer_t* mp, int ins_sel, int noteid, int note, int noteOfs, int note_att, int ch_att, int mus_ch, int bend, int pan) {
    int voice = choose_hw_voice(mp, ins_sel, mus_ch, noteid);
    if (voice < 0) {
        printf("[MUS] #%d DROPPED note (%d) %d\n", mus_ch, noteid, note);
        return; // drop the note
    }
    if (mp->hw_voices[voice].ins_sel != ins_sel) {
        load_hw_instrument(mp, &mp->hw_voices[voice], voice, ins_sel, mus_ch);
    }
    key_on(mp, voice, noteid, note, noteOfs, note_att, ch_att, mus_ch, bend, pan);
}

static void mus_event(musplayer_t* mp, int ctrl, int value, int mus_ch, mus_channel_t* ch) {
    if (ctrl > 14) {
        printf("[MUS] #%d BAD controller %d = %d\n", mus_ch, ctrl, value);
        return;
    }
    switch (ctrl) {
        case ctrl_instrument:    // Change selected instrument (patch)
            // printf("[MUS] #%d instrument = %d\n", mus_ch, value);
            if (value >= 175) {
                printf("[MUS] #%d BAD instrument %d\n", mus_ch, value);
                value = 0;
            }
            ch->ins_idx = value;
            ch->ins = &mp->op2bank[value];
            return;
        case ctrl_bank_select:   // Bank select: 0 by default
            printf("[MUS] #%d bank select = %d\n", mus_ch, value);
            return;
        case ctrl_modulation:    // Modulation (frequency vibrato depth)
            // printf("[MUS] #%d vibrato depth = %d\n", mus_ch, value);
            // XXX this is a global HW setting; use this, or emulate it? what would PR do?
            // adlib_write(0xbd, (value & 1) << 6);    // Vibrato Depth (bit 6)  XXX using bit 0
            // printf("[MUS] #%d modulation = %d\n", mus_ch, value);
            value = value >> 2;              // divide by 4
            if (value > 7) value = 7;        // LFO table contains 8 rows
            ch->modulation = value * opl3_lfo_steps; // LFO table row offset
            if (!ch->uses_mod) {
                ch->uses_mod = 1;                // channel is using modulation controller
                switch_to_sw_vib(mp, mus_ch);    // turn off HW vibrato; enable SW vibrato
            }
            return;
        case ctrl_volume:        // Volume: 0-silent, ~100-normal, 127-loud
            // insight check: volume is attenuation (att = max - vol)
            // from docs, -48 dB (0) to 0 dB (127) in steps of 0.75 dB
            // since HW attenuation is in dB we can just sum attenuations..
            // use reference level 100 as "full volume",
            // this allows up to -27 negative attenuation
            // (I read somewhere that MUS allows >full volume)
            if (value > 127) value = 127; // must limit
            ch->vol_att = att_log_square[value];
            // printf("[MUS] #%d volume = %d\n", mus_ch, value);
            update_volume(mp, mus_ch, ch->vol_att + ch->exp_att, ch->pan_bits);
            return;
        case ctrl_pan:           // Pan (balance): 0-left, 64-center (default), 127-right
            ch->pan_bits = value <= 64-opl3_pan_threshold ? opl3_pan_left : (value >= 64+opl3_pan_threshold ? opl3_pan_right : opl3_pan_centre);
            // printf("[MUS] #%d pan = %d\n", mus_ch, value);
            update_volume(mp, mus_ch, ch->vol_att + ch->exp_att, ch->pan_bits);
            return;
        case ctrl_expression:    // Expression
            if (value > 127) value = 127; // must limit
            ch->exp_att = att_log_square[value];
            printf("[MUS] #%d expression = %d\n", mus_ch, value);
            update_volume(mp, mus_ch, ch->vol_att + ch->exp_att, ch->pan_bits);
            return;
        case ctrl_reverb:        // Reverb depth
            printf("[MUS] #%d reverb = %d\n", mus_ch, value);
            return;
        case ctrl_chorus:        // Chorus depth
            printf("[MUS] #%d chorus = %d\n", mus_ch, value);
            return;
        case ctrl_sustain:       // Sustain pedal (hold)
            printf("[MUS] #%d sustain = %d\n", mus_ch, value);
            return;
        case ctrl_soft:          // Soft pedal
            printf("[MUS] #%d soft = %d\n", mus_ch, value);
            return;

        // SYSTEM messages
        // in midi these are "channel mode" (only affects one channel)
        case ctrl_all_sound_off: // All sounds off (silence immediately)
            printf("[MUS] #%d all sound off\n", mus_ch);
            silence_mus_all(mp, mus_ch);
            return;
        case ctrl_all_notes_off: // All notes off (key off)
            printf("[MUS] #%d all notes key-off\n", mus_ch);
            key_off_mus_all(mp, mus_ch);
            return;
        case ctrl_mono:          // Mono (one note per channel)
            ch->mono = 1;
            return;
        case ctrl_poly:          // Poly (multiple notes per channel)
            ch->mono = 0;
            return;
        case ctrl_reset_all:     // Reset all controllers on this channel
            printf("[MUS] #%d reset all controllers\n", mus_ch);
            if (ch->vol_att != 0 || ch->exp_att != 0) {
                ch->vol_att = 0;
                ch->exp_att = 0;
                ch->pan_bits = opl3_pan_centre;
                update_volume(mp, mus_ch, ch->vol_att + ch->exp_att, ch->pan_bits);
            }
            if (ch->bend != 0) {
                ch->bend = 0;
                bend_channel(mp, mus_ch, 0);
            }
            ch->mono = 0;        // is POLY the default?
            return;
    }
    printf("[MUS] #%d controller %d = %d\n", mus_ch, ctrl, value);
}

static void do_lfo(musplayer_t* mp) {
    // update LFO on every 4th tick,
    // otherwise it generates too many register writes.
    if ((mp->mus_time & 1) != 0) return;
    // apply LFO to voices with channel modulation
    int lfo_ofs = ++mp->lfo_ofs;
    if (lfo_ofs == opl3_lfo_steps) lfo_ofs = 0;
    mp->lfo_ofs = lfo_ofs;
    for (int hw_ch=0; hw_ch<mus_num_voices; hw_ch++) {
        mus_hw_voice_t* hw = &mp->hw_voices[hw_ch];
        if (hw->sw_vib && hw->mus_ch < mus_num_voices) { // mus_ch == 255 if unused
            mus_channel_t* chan = &mp->channels[hw->mus_ch];
            int mod = chan->modulation;
            if (mod % opl3_lfo_steps != 0) {
                    printf("BUG: bad modulation %d on mus_ch %d", (int)mod, (int)hw->mus_ch);
                    continue;
            }
            // apply software vibrato (affects carrier, also modulator in Add-mode)
            int bend = chan->bend + lfo_table[mod + lfo_ofs]; // -1 row (1-based index)
            int hw_cmd = bend_pitch(mp, hw->p_note, bend, hw->fineTune); // see key_on
            hw_cmd |= (hw->hw_cmd & opl3_key_on); // add current key-on state
            // printf("LFO: %d wr %x\n", hw_ch, hw_cmd);
            int B=0, ch = hw_ch; if (ch >= mus_bank_two) { ch -= mus_bank_two; B = 0x100; } // OPL3 2nd bank
            adlib_write(mp, (B|0xA0)+ch, hw_cmd & 255); // frequency low 8 bits
            adlib_write(mp, (B|0xB0)+ch, hw_cmd >> 8);  // frequency top 2 bits, octave (shift), key-on
        }
    }
}

int musplay_tick(musplayer_t* mp) {
    if (!mp->score) return 0; // stopped
    int cmd, note, vol;
    // wait for current delay to elapse.
    if (mp->delay > 1) { mp->delay--; mp->mus_time++; do_lfo(mp); return 1; } // use tick.
    // execute commands until the next delay.
    mp->mus_time++;
    // printf("mus_time: %d\n", mp->mus_time);
    do {
        cmd = *mp->score++;
        int mus_ch = cmd & 15;
        int event = (cmd >> 4) & 7;
        mus_channel_t* ch = &mp->channels[mus_ch];
        switch (event) {
            case event_release: {
                note = *mp->score++; // note number, top bit 0
                key_off_note(mp, mus_ch, note);
                // printf("[MUS] #%d release %d\n", chan, note);
                break;
            }
            case event_note: {
                note = *mp->score++; // note number, top bit 'vol'
                if (note & 0x80) {
                    vol = *mp->score++; // note volume
                    ch->last_vol = vol; // update last volume
                } else {
                    vol = ch->last_vol; // use last volume
                }
                note &= 0x7F; // low 7 bits are the MIDI note
                // key-off all notes on the channel if mono mode
                if (ch->mono) {
                    key_off_mus_all(mp, mus_ch);
                }
                // note volume, not real sure how this is handled in MUS
                // (midi velocity / strike-intensity)
                // tweaked this until the music sounds about right..
                int note_att = att_log_square[vol];
                int ch_att = ch->vol_att; // current volume attenuation (0.75 dB steps)
                if (mus_ch==15) {
                    // notes 35-81 on #15 plays percussion instrument 135-181 (midi?)
                    if (note >= 35 && note <= 81) {
                        // precussion: note selects the instrument
                        int ins_sel = 128-35+note; // percussion bank starts at 128
                        MUS_instrument* ins = &mp->op2bank[ins_sel];
                        // yeah these can't use noteOfs, otherwise it breaks a bunch of tunes
                        play_note(mp, ins_sel, note, ins->noteNum, 0, note_att, ch_att, mus_ch, ch->bend, ch->pan_bits); // ins->voice[0].noteOfs
                        if (ins->flags & musf_double_voice) {
                            // double-voice note: voice=1 in bit 8 of ins_sel and noteid
                            play_note(mp, ins_sel|(1<<8), note|(1<<8), ins->noteNum, 0, note_att, ch_att, mus_ch, ch->bend, ch->pan_bits); // ins->voice[1].noteOfs
                        }
                    }
                } else {
                    MUS_instrument* ins = ch->ins;
                    int ins_sel = ch->ins_idx; // instrument selector: voice=0 in bit 8
                    if (ins->flags & musf_fixed_note) {
                        // play a fixed note, ignoring noteOfs (as per format doc)
                        play_note(mp, ins_sel, note, ins->noteNum, 0, note_att, ch_att, mus_ch, ch->bend, ch->pan_bits); // noteOfs=0
                        if (ins->flags & musf_double_voice) {
                            // double-voice note: voice=1 in bit 8 of ins_sel and noteid
                            play_note(mp, ins_sel|(1<<8), note|(1<<8), ins->noteNum, 0, note_att, ch_att, mus_ch, ch->bend, ch->pan_bits); // noteOfs=0
                        }
                    } else {
                        // play a normal note.
                        play_note(mp, ins_sel, note, note, ins->voice[0].noteOfs, note_att, ch_att, mus_ch, ch->bend, ch->pan_bits);
                        if (ins->flags & musf_double_voice) {
                            // double-voice note: voice=1 in bit 8 of ins_sel and noteid
                            play_note(mp, ins_sel|(1<<8), note|(1<<8), note, ins->voice[1].noteOfs, note_att, ch_att, mus_ch, ch->bend, ch->pan_bits);
                        }
                    }
                }
                // printf("[MUS] #%d play %d vol %d\n", chan, note, vol);
                break;
            }
            case event_pitch_wheel: {
                // 128 is normal, 64 half-tone down, 0 one tone down, 192 half-tone up, 255 one tone up.
                int bend = (*mp->score++) - 128; // 8-bit value
                ch->bend = bend;
                bend_channel(mp, mus_ch, bend);
                // printf("[MUS] #%d bend %d\n", mus_ch, bend);
                break;
            }
            case event_system: {
                int ctrl = (*mp->score++) & 0x7F;  // only low 4 bits used (ignore top bit?)
                mus_event(mp, ctrl, 0, mus_ch, ch);
                break;
            }
            case event_controller: {
                int ctrl = (*mp->score++) & 0x7F;  // only low 4 bits used (ignore top bit?)
                int value = *mp->score++;          // full 8 bits are used
                // system evenst are "silently skipped" if used via event_controller.
                if (ctrl < 10) {
                    mus_event(mp, ctrl, value, mus_ch, ch);
                }
                break;
            }
            case event_end_of_measure:
                break;
            case event_end_of_score: {
                printf("[MUS] end of score\n");
                if (mp->loop_score) {
                    mp->score = mp->loop_score;
                    return 1; // still playing (ignore remaining ticks)
                } else {
                    // key off all channels
                    for (int i=0; i<mus_num_voices; i++) {
                        key_off_hw(mp, i);
                    }
                    mp->score = 0; // ignore future updates
                }
                return 0; // stopped
            }
            case event_unused:
                mp->score++; // must contain a single data byte
                break;
        }
    } while (!(cmd & 0x80));
    // parse the next delay.
    mp->delay = 0;
    do {
        cmd = *mp->score++;
        mp->delay <<= 7;         // *= 128
        mp->delay += cmd & 0x7F; // low 7 bits
    } while (cmd & 0x80);
    do_lfo(mp);
    return 1;
}

void musplay_op2bank(musplayer_t* mp, char* data) {
    memcpy(&mp->op2bank, data, sizeof(mp->op2bank));
}

void musplay_volume (musplayer_t* mp, int volume) {
    if (volume > 127) volume = 127; // must limit
    else if (volume < 0) volume = 0;
    mp->main_att = att_log_square[volume];
}

void musplay_start(musplayer_t* mp, char* data, int loop) {
    MUS_header* hdr = (MUS_header*) data;
    mp->score = (byte*)(data + hdr->scoreStart); // order matters!
    mp->loop_score = loop ? mp->score : 0;
    mp->delay = 0;
    mp->mus_time = 0;
    mp->next_kon = 0;
    mp->lfo_ofs = 0;
    // clear all MUS channels
    memset(mp->channels, 0, sizeof(mp->channels));
    for (int m=0; m<mus_num_channels; m++) {
        mp->channels[m].ins = &mp->op2bank[0]; // must be a valid pointer
        mp->channels[m].pan_bits = opl3_pan_centre;
    }
    // clear all HW channels
    memset(mp->hw_voices, 0, sizeof(mp->hw_voices));
    for (int h=0; h<mus_num_voices; h++) {
        mp->hw_voices[h].noteid = -1;  // no note playing
        mp->hw_voices[h].mus_ch = -1;  // no channel owner
        mp->hw_voices[h].ins_sel = -1; // no instrument selected
    }
    // clear Test/IRQ/CSW registers
    adlib_write(mp, 0x01, 0x00);
    adlib_write(mp, 0x04, 0x00);
    adlib_write(mp, 0x08, 0x00);
    // enable OPL2 features
    adlib_write(mp, 0x01, 0x20);
    // enable OPL3 features
    adlib_write(mp, 0x105, 0x01);
    // clear OPL3 four-operator enable
    adlib_write(mp, 0x104, 0x00);
    // clear depth / percussion modes
    adlib_write(mp, 0xBD, 0x00);
    // silence all voices (key-off with instant release)
    for (int b = 0x00; b <= 0x100; b += 0x100) {
        for (int ch=0; ch<=9; ch++) {
            adlib_write(mp, (b|0x80)+chan_oper2[ch], 0xFF); // carrier, sustain -93dB; release max
            adlib_write(mp, (b|0x80)+chan_oper1[ch], 0xFF); // modulator, sustain -93dB; release max
            adlib_write(mp, (b|0xB0)+ch, 0); // key off, clear freq high
            adlib_write(mp, (b|0xA0)+ch, 0); // clear freq low
        }
    }
    // reset remaining channel/voice registers
    for (int b = 0x00; b <= 0x100; b += 0x100) {
        for (int ch=0; ch<=9; ch++) {
            adlib_write(mp, (b|0x20)+chan_oper2[ch], 0); // carrier, tremolo/vibratio/sustain etc
            adlib_write(mp, (b|0x20)+chan_oper1[ch], 0); // modulator, tremolo/vibratio/sustain etc
            adlib_write(mp, (b|0x60)+chan_oper2[ch], 0); // carrier, attack/decay rates
            adlib_write(mp, (b|0x60)+chan_oper1[ch], 0); // modulator, attack/decay rates
            adlib_write(mp, (b|0xC0)+ch, 0); // clear feedback / stereo modes
            adlib_write(mp, (b|0xE0)+chan_oper2[ch], 0); // carrier, waveform select
            adlib_write(mp, (b|0xE0)+chan_oper1[ch], 0); // modulator, waveform select
        }
    }
    // attenuate all channels (allow time for key-off above, to avoid click)
    for (int b = 0x00; b <= 0x100; b += 0x100) {
        for (int ch=0; ch<=9; ch++) {
            adlib_write(mp, (b|0x40)+chan_oper2[ch], 63); // carrier, maximum attenuation
            adlib_write(mp, (b|0x40)+chan_oper1[ch], 63); // modulator, maximum attenuation
        }
    }
    printf("\n\n\n[MUS] started playing\n");
}

void musplay_stop (musplayer_t* mp) {
    adlib_write(mp, 0xbd, 0); // stop rhythm instruments, clear rhythm mode, vibrato, tremelo
    for (int hw_ch=0; hw_ch<mus_num_voices; hw_ch++) {
        // ensure release > 0 so the note will end.
        int ins = mp->hw_voices[hw_ch].ins_sel;
        if (ins >= 0) {
            MUS_voice* v = &mp->op2bank[ins & 0xFF].voice[ins >> 8];
            if ((v->carSustain & 15) < 4) {
                int B=0, ch = hw_ch; if (ch >= mus_bank_two) { ch -= mus_bank_two; B = 0x100; } // OPL3 2nd bank
                adlib_write(mp, (B|0x80)+chan_oper2[ch], (v->carSustain & 0xF0)|4); // release = 4
            }
            if ((v->feedback & 1) && ((v->modSustain & 15) < 4)) { // Additive mode
                int B=0, ch = hw_ch; if (ch >= mus_bank_two) { ch -= mus_bank_two; B = 0x100; } // OPL3 2nd bank
                adlib_write(mp, (B|0x80)+chan_oper1[ch], (v->modSustain & 0xF0)|4); // release = 4
            }
        }
        key_off_hw(mp, hw_ch);
    }
    mp->score = 0;
}
