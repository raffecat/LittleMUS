/*
Little MUS Player v0.1 (alpha, it's nearly there)

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
• The DOSBox Team, OPL2/OPL3 emulation library
• Ken Silverman, ADLIBEMU.C
• https://github.com/rofl0r/woody-opl (opl.c)
• https://doomwiki.org/wiki/MUS
• https://moddingwiki.shikadi.net/wiki/MUS_Format
• https://moddingwiki.shikadi.net/wiki/OP2_Bank_Format
• https://cosmodoc.org/topics/adlib-functions/ !
• https://www.doomworld.com/idgames/docs/editing/mus_form
• https://www.phys.unsw.edu.au/jw/notes.html
• id software, for giving us DOOM.
*/

#include <stdint.h>
#include <string.h>

#include <stdio.h>

#include "adlibemu.h"

typedef uint8_t byte;

// TO DO:
// event_pitch_wheel doesn't update playing notes
// a few notes drop out
// volumes aren't quite right (log space)
// E1M2 has some weird tones
// intro track is broken
// separate music output (so sfx work again..)
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

typedef struct __attribute__((packed)) MUS_voiceS {
    byte       modChar;        // 0x20 Modulator characteristic (Mult, KSR, EG, VIB and AM flags)
    byte       modAttack;      // 0x60 Modulator attack/decay level
    byte       modSustain;     // 0x80 Modulator sustain/release level
    byte       modWaveSel;     // 0xE0 Modulator wave select
    byte       modScale;       // 0x40 Modulator key scaling (first two bits)
    byte       modLevel;       // 0x40 Modulator output level (last six bits)
    byte       feedback;       // 0xC0 Feedback/connection
    byte       carChar;        // 0x23 Carrier characteristic (Mult, KSR, EG, VIB and AM flags)
    byte       carAttack;      // 0x63 Carrier attack/decay level
    byte       carSustain;     // 0x83 Carrier sustain/release level
    byte       carWaveSel;     // 0xE3 Carrier wave select
    byte       carScale;       // 0x43 Carrier key scaling (first two bits)
    byte       carLevel;       // 0x43 Carrier output level (last six bits)
    byte       reserved;       // Unused
    int16_t    noteOfs;        // MIDI note offset (ignored when "fixed note" flag is set)
} MUS_voice;

typedef struct __attribute__((packed)) MUS_instrumentS {
    int16_t    flags;          // 0x01 - fixed note, 0x02 - delayed vibrato (unused), 0x04 - Double-voice mode
    byte       fineTune;       // Second voice detune level, 128 center, (fine_tune / 2) - 64
    byte       noteNum;        // Percussion note number, or the note to play when fixed note (between 0 and 127)
    MUS_voice  voice[2];       // Instrument voices, 2nd is for double-voice
} MUS_instrument;

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

// Adlib HW mapping: 9 channels -> operator 1, operator 2
static int chan_oper1[] = { 0, 1, 2,  8,  9, 10, 16, 17, 18 };
static int chan_oper2[] = { 3, 4, 5, 11, 12, 13, 19, 20, 21 };

// A0 and B0 bytes for each note (fnum, block, key-on)
// GENERATED by note_freq.py (zeros are out of range and won't key-on)
static uint16_t note_freq[] = { // 256 bytes
  8536,
  8557,
  8579,
  8602,
  8626,
  8652,
  8679,
  8708,
  8739,
  8772,
  8806,
  8843,
  8881,
  8922,
  8966,
  9012,
  9061,
  9112,
  9167,
  9732,
  9763,
  9796,
  9830,
  9867,
  9905,
  9946,
  9990,
  10036,
  10085,
  10136,
  10191,
  10756,
  10787,
  10820,
  10854,
  10891,
  10929,
  10970,
  11014,
  11060,
  11109,
  11160,
  11215,
  11780,
  11811,
  11844,
  11878,
  11915,
  11953,
  11994,
  12038,
  12084,
  12133,
  12184,
  12239,
  12804,
  12835,
  12868,
  12902,
  12939,
  12977,
  13018,
  13062,
  13108,
  13157,
  13208,
  13263,
  13828,
  13859,
  13892,
  13926,
  13963,
  14001,
  14042,
  14086,
  14132,
  14181,
  14232,
  14287,
  14852,
  14883,
  14916,
  14950,
  14987,
  15025,
  15066,
  15110,
  15156,
  15205,
  15256,
  15311,
  15876,
  15907,
  15940,
  15974,
  16011,
  16049,
  16090,
  16134,
  16180,
  16229,
  16280,
  16335,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
};

enum constants {
    num_voices = 9,
    num_mus_channels = 16,
};

typedef struct hw_voice_s {
    // playing note:
    int seq;                // note key-on sequence number (to key-off the oldest)
    int release;            // if non-zero, the release finish time
    int16_t note;           // MIDI note playing on this channel (-1 if not playing)
    uint16_t freq;          // Adlib Frequency/Octave/KeyOn from note_freq
    byte vol;               // note key-on volume (for controller updates)
    byte mus_ch;            // MUS channel that owns the playing note (-1 if not playing)
    // instrument config:
    int16_t ins_sel;        // current instrument configured on the HW channel
    byte ksl1, ksl2;        // current instrument KSL values (used to change operator volume)
    byte lvl1, lvl2;        // current instrument Level values (used to change operator volume)
    int8_t fineTune;        // current instrument fine tune (mus->fineTune / 2) - 64
    //byte rel;             // release rate 0-15
} hw_voice_t;

typedef struct channel_s {
    byte mono;              // silence all notes when a new note is played
    byte last_vol;          // volume of the previous note played on the channel
    byte vol;               // channel volume controller
    byte express;           // channel expression controller
    byte ins_idx;           // selected MIDI instrument on this channel (index into op2bank)
    MUS_instrument* ins;    // selected instrument data
} mus_channel;

byte* loop_score = 0;
byte* score = 0;
int delay = 0;
int mus_time = 0;
int next_free = 0;
int next_keyon_seq = 1;
mus_channel channels[num_mus_channels] = {0};
hw_voice_t hw_voices[num_voices] = {0};
MUS_instrument op2bank[175] = {0};  // ~6K

// release time is a "linear change in decibel level"
// static int release_time[16] = {
//     240, 240, 240, 240, 240, 240, 240, 120, 60, 30, 15, 8, 4, 2, 1, 1,
// };

// static inline int max(int a, int b) {
//     return a > b ? a : b;
// }

static void key_off_hw(int hw_ch) {
    if (hw_voices[hw_ch].note >= 0) {
        // printf("[HW] *%d key off\n", hw_ch);
        adlib_write(0xb0+hw_ch, (hw_voices[hw_ch].freq >> 8) & 0xdf);  // clear bit 5 (key-off)
        hw_voices[hw_ch].note = -1;
        hw_voices[hw_ch].release = mus_time + 4; // release_time[hw_voices[hw_ch].rel];
        // XXX need to mark the channel as 'release' phase,
        // delay marking it free - otherwise the channel reuse
        // logic prefers to cut off releasing notes!
    }
}

static void silence_hw(int hw_ch) {
    // set release speed to maximum (instant)
    adlib_write(0x80+chan_oper1[hw_ch], 15);  // sustain 0 release 15
    adlib_write(0x80+chan_oper2[hw_ch], 15);  // sustain 0 release 15
    key_off_hw(hw_ch);
    // loaded instrument is no longer valid
    hw_voices[hw_ch].ins_sel = -1;
}

static void key_off_note(int mus_ch, int note) {
    // find the oldest matching note.
    int found = 0;
    for (int h=0; h<num_voices; h++) {
        // match both voices for double-voice notes (voice=0/1 in bit 8)
        if (hw_voices[h].mus_ch == mus_ch && (hw_voices[h].note & 0xFF) == note) {
            key_off_hw(h);
            found = 1;
        }
    }
    if (!found) {
        printf("[MUS] #%d key off note (%d) - not found\n", mus_ch, note);
    }
}

static void key_off_mus_all(int mus_ch) {
    for (int h=0; h<num_voices; h++) {
        if (hw_voices[h].mus_ch == mus_ch) {
            key_off_hw(h);
        }
    }
}

static void silence_mus_all(int mus_ch) {
    for (int h=0; h<num_voices; h++) {
        if (hw_voices[h].mus_ch == mus_ch) {
            silence_hw(h);
        }
    }
}

static void update_chan_vol(int mus_ch, int ctlvol) {
    for (int h=0; h<num_voices; h++) {
        if (hw_voices[h].mus_ch == mus_ch) {
            hw_voice_t* hw = &hw_voices[h];
            int vvol = (hw->vol * ctlvol) >> 7; // new voice volume
            int vol1 = (hw->lvl1 * vvol) >> 7; if (vol1 > 63) vol1 = 63;
            int vol2 = (hw->lvl2 * vvol) >> 7; if (vol2 > 63) vol2 = 63;
            adlib_write(0x40+chan_oper1[h], hw->ksl1|vol1); // operator 1 attenuation + KSL
            adlib_write(0x40+chan_oper2[h], hw->ksl2|vol2); // operator 2 attenuation + KSL
        }
    }
}

static void load_hw_instrument(hw_voice_t* hw, int hwv, int ins_sel) {
    int ins = ins_sel & 0xFF;
    int vi = ins_sel >> 8;
    if (ins >= 175) {
        printf("[HW] *%d BAD instrument %d\n", hwv, ins);
        return;
    }
    MUS_instrument* in = &op2bank[ins];
    MUS_voice* v = &in->voice[vi]; // voice index 0/1 in bit 8 of instrument selector
    //hw->rel = max((v->modSustain&15),(v->carSustain&15)); // max release rate
    printf("[HW] *%d load instrument %d.%d ma %d md %d ca %d cd %d\n", hwv, ins, vi, (v->modAttack>>4), (v->modAttack&15), (v->carAttack>>4), (v->carAttack&15));
    adlib_write(0x20+chan_oper1[hwv], v->modChar);     // Modulator AM, VIB, EG, KSR, Mult
    adlib_write(0x60+chan_oper1[hwv], v->modAttack);   // Modulator attack, decay
    adlib_write(0x80+chan_oper1[hwv], v->modSustain);  // Modulator sustain, release
    adlib_write(0xE0+chan_oper1[hwv], v->modWaveSel);  // Modulator wave select
    adlib_write(0xC0+hwv, v->feedback);                // Channel feedback, connection
    adlib_write(0x20+chan_oper2[hwv], v->carChar);     // Carrier AM, VIB, EG, KSR, Mult
    adlib_write(0x60+chan_oper2[hwv], v->carAttack);   // Carrier attack, decay
    adlib_write(0x80+chan_oper2[hwv], v->carSustain);  // Carrier sustain, release
    adlib_write(0xE0+chan_oper2[hwv], v->carWaveSel);  // Carrier wave select
    hw->ksl1 = v->modScale;                            // Modulator key scaling (top two bits)
    hw->ksl2 = v->carScale;                            // Carrier key scaling (top two bits)
    hw->lvl1 = v->modLevel;                            // Modulator output level (low six bits)
    hw->lvl2 = v->carLevel;                            // Carrier output level (low six bits)
    hw->fineTune = vi ? (in->fineTune / 2) - 64 : 0;   // Second voice detune level
    hw->ins_sel = ins_sel;                             // instrument+voice configured on this channel
}

static void key_on(int voice, int noteid, int note, int noteOfs, int vol, int ctlvol, int mus_ch) {
    printf("[HW] *%d key on (%d) n=%d v=%d #%d\n", voice, noteid, note, vol, mus_ch);
    hw_voice_t* hw = &hw_voices[voice];
    int vol1 = (hw->lvl1 * ctlvol) >> 7; if (vol1 > 63) vol1 = 63; // "0.75 dB of attenuation per step"
    int vol2 = (hw->lvl2 * ctlvol) >> 7; if (vol2 > 63) vol2 = 63;
    adlib_write(0x40+chan_oper1[voice], hw->ksl1|vol1); // operator 1 attenuation + KSL
    adlib_write(0x40+chan_oper2[voice], hw->ksl2|vol2); // operator 2 attenuation + KSL
    // set frequency, key-on note
    uint16_t freq = note_freq[note + noteOfs] + hw->fineTune; // unclipped seems correct (E1M2)
    // int n = note + noteOfs; if (n > 127) n = 127; else if (n < 0) n = 0; // clamp
    // // XXX this table needs a reorg now, multiple arrays
    // int freq = (note_freq[n] & 1023) + hw->fineTune;
    // if (freq > 1023) freq = 1023; else if (freq < 0) freq = 0; // clamp
    // adlib_write(0xa0+voice, freq & 255); // frequency low 8 bits
    // adlib_write(0xb0+voice, (((note_freq[n] & 0xfc00) | freq) >> 8));  // 6 bits octave/key-on | 2 bits freq
    adlib_write(0xa0+voice, freq & 255); // frequency low 8 bits
    adlib_write(0xb0+voice, freq >> 8);  // 6 bits octave/key-on | 2 bits freq
    hw->seq = next_keyon_seq++;     // key-on sequence number (allow key-off oldest)
    hw->note = noteid;              // save midi note for key_off (original key_on note)
    hw->freq = freq;                // save note freq for key_off, effects (modified by noteOfs)
    hw->vol = vol;                  // key-on note volume (before controllers)
    hw->mus_ch = mus_ch;            // save mus_ch for key_off_mus
}

static int choose_hw_voice(int ins_sel, int mus_ch, int noteid) {
    int best_koff = -1;
    int best_wait = 0x7FFFFFFF;
    int released = -1;
    int match_ins = -1;
    // search for an idle or nearly-idle voice.
    for (int i = 0; i < num_voices; i++) {
        if (hw_voices[i].note < 0) {
            // voice is in release phase.
            int wait = hw_voices[i].release - mus_time;
            if (wait <= 0) {
                // keyed off and released.
                if (hw_voices[i].ins_sel == ins_sel) match_ins = i; // best case
                else released = i; // last fully released channel
            } else if (wait < best_wait) {
                // keyed off, releasing.
                best_wait = wait;
                best_koff = i; // best keyed-off channel (minimum time remaining)
            }
        } else if (hw_voices[i].note == noteid && hw_voices[i].mus_ch == mus_ch) {
            // noteid requires exact match for re-use (voice=0/1 in bit 8)
            printf("[MUS] #%d replaced note (%d) - double key-on\n", mus_ch, noteid);
            key_off_hw(i);
            return i; // found the same note already keyed on (replace it)
        }
    }
    if (match_ins >= 0) return match_ins; // released channel, already configured.
    if (released >= 0) return released;   // released channel, needs config.
    if (best_koff >= 0) return best_koff; // keyed off channel with lowest remaining time.
    // must steal a voice.
    // XXX maybe just drop the note?
    // if there's a short/quiet note playing, use the oldest?
    // just cycle through them (prefer the oldest?)
    int free = next_free;
    next_free = (next_free + 1) & 7;
    // key-off the note if currently playing,
    // otherwise Adlib won't key-on the new note.
    printf("[MUS] #%d killed note (%d) - overflow\n", hw_voices[free].mus_ch, hw_voices[free].note);
    key_off_hw(free);
    return free;
}

static void play_note(int ins_sel, int noteid, int note, int noteOfs, int vol, int ctlvol, int mus_ch) {
    int voice = choose_hw_voice(ins_sel, mus_ch, noteid);
    if (hw_voices[voice].ins_sel != ins_sel) {
        load_hw_instrument(&hw_voices[voice], voice, ins_sel);
    }
    key_on(voice, noteid, note, noteOfs, vol, ctlvol, mus_ch);
}

static void mus_event(int ctrl, int value, int mus_ch, mus_channel* ch) {
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
            ch->ins = &op2bank[value];
            return;
        case ctrl_bank_select:   // Bank select: 0 by default
            break;
        case ctrl_modulation:    // Modulation (frequency vibrato depth)
            printf("[MUS] #%d vibrato depth = %d\n", mus_ch, value);
            // XXX this is a global HW setting; use this, or emulate it? what would PR do?
            // adlib_write(0xbd, (value & 1) << 6);    // Vibrato Depth (bit 6)  XXX using bit 0
            return;
        case ctrl_volume:        // Volume: 0-silent, ~100-normal, 127-loud
            // printf("[MUS] #%d volume = %d\n", mus_ch, value);
            if (value > 127) value = 127; // must limit
            ch->vol = value;
            printf("[MUS] #%d volume = %d\n", mus_ch, value);
            int ctlvol = ch->vol; // (ch->vol * ch->express) >> 7; // combine controllers
            update_chan_vol(mus_ch, ctlvol);
            return;
        case ctrl_pan:           // Pan (balance): 0-left, 64-center (default), 127-right
            // printf("[MUS] #%d pan = %d\n", mus_ch, value);
            return;
        case ctrl_expression:    // Expression
            if (value > 127) value = 127; // must limit
            ch->express = value;
            printf("[MUS] #%d expression = %d\n", mus_ch, value);
            break;
        case ctrl_reverb:        // Reverb depth
            printf("[MUS] #%d REVERB = %d\n", mus_ch, value);
            break;
        case ctrl_chorus:        // Chorus depth
            printf("[MUS] #%d CHORUS = %d\n", mus_ch, value);
            break;
        case ctrl_sustain:       // Sustain pedal (hold)
            printf("[MUS] #%d SUSTAIN = %d\n", mus_ch, value);
            break;
        case ctrl_soft:          // Soft pedal
            printf("[MUS] #%d SOFT = %d\n", mus_ch, value);
            break;

        // SYSTEM messages
        // in midi these are "channel mode" (only affects one channel)
        case ctrl_all_sound_off: // All sounds off (silence immediately)
            printf("[MUS] #%d all sound off\n", mus_ch);
            silence_mus_all(mus_ch);
            return;
        case ctrl_all_notes_off: // All notes off (key off)
            printf("[MUS] #%d all notes key-off\n", mus_ch);
            key_off_mus_all(mus_ch);
            return;
        case ctrl_mono:          // Mono (one note per channel)
            ch->mono = 1;
            break;
        case ctrl_poly:          // Poly (multiple notes per channel)
            ch->mono = 0;
            break;
        case ctrl_reset_all:     // Reset all controllers on this channel
            printf("[MUS] #%d reset all controllers\n", mus_ch);
            ch->vol = 127;       // Volume controller
            ch->express = 127;   // Expression controller
            ch->mono = 0;        // Mono mode (is POLY the default?)
            return;
    }
    printf("[MUS] #%d controller %d = %d\n", mus_ch, ctrl, value);
}

int musplay_update(int ticks) {
    if (!score) return 0; // stopped
    int cmd, note, vol;
    do {
        // wait for current delay to elapse.
        if (delay > 0) {
            if (delay > ticks) { delay -= ticks; mus_time += ticks; return 1; } // use all ticks.
            mus_time += delay;
            ticks -= delay; // discard ticks used by delay.
            delay = 0;
        }
        // execute commands until the next delay.
        do {
            cmd = *score++;
            int mus_ch = cmd & 15;
            int event = (cmd >> 4) & 7;
            mus_channel* ch = &channels[mus_ch];
            switch (event) {
                case event_release: {
                    note = *score++; // note number, top bit 0
                    key_off_note(mus_ch, note);
                    // printf("[MUS] #%d release %d\n", chan, note);
                    break;
                }
                case event_note: {
                    note = *score++; // note number, top bit 'vol'
                    if (note & 0x80) {
                        vol = *score++; // note volume
                        ch->last_vol = vol; // update last volume
                    } else {
                        vol = ch->last_vol; // use last volume
                    }
                    int ctlvol = (vol * ch->vol) >> 7; // apply channel volume
                    // ctlvol = (ctlvol * ch->express) >> 7; // apply channel expression
                    note &= 0x7F; // low 7 bits are the MIDI note
                    // key-off all notes on the channel if mono mode
                    if (ch->mono) {
                        key_off_mus_all(mus_ch);
                    }
                    if (mus_ch==15) {
                        // notes 35-81 on #15 plays percussion instrument 135-181 (midi?)
                        if (note >= 35 && note <= 81) {
                            // precussion: note selects the instrument
                            int ins_sel = 128-35+note; // percussion bank starts at 128
                            MUS_instrument* ins = &op2bank[ins_sel];
                            // yeah these can't use noteOfs, otherwise it breaks a bunch of tunes
                            play_note(ins_sel, note, ins->noteNum, 0, vol, ctlvol, mus_ch); // ins->voice[0].noteOfs
                            if (ins->flags & musf_double_voice) {
                                // double-voice note: voice=1 in bit 8 of ins_sel and noteid
                                play_note(ins_sel|(1<<8), note|(1<<8), ins->noteNum, 0, vol, ctlvol, mus_ch); // ins->voice[1].noteOfs
                            }
                        }
                    } else {
                        MUS_instrument* ins = ch->ins;
                        int ins_sel = ch->ins_idx; // instrument selector: voice=0 in bit 8
                        if (ins->flags & musf_fixed_note) {
                            // play a fixed note, ignoring noteOfs (as per format doc)
                            play_note(ins_sel, note, ins->noteNum, 0, vol, ctlvol, mus_ch); // noteOfs=0
                            if (ins->flags & musf_double_voice) {
                                // double-voice note: voice=1 in bit 8 of ins_sel and noteid
                                play_note(ins_sel|(1<<8), note|(1<<8), ins->noteNum, 0, vol, ctlvol, mus_ch); // noteOfs=0
                            }
                        } else {
                            // play a normal note.
                            play_note(ins_sel, note, note, ins->voice[0].noteOfs, vol, ctlvol, mus_ch);
                            if (ins->flags & musf_double_voice) {
                                // double-voice note: voice=1 in bit 8 of ins_sel and noteid
                                play_note(ins_sel|(1<<8), note|(1<<8), note, ins->voice[1].noteOfs, vol, ctlvol, mus_ch);
                            }
                        }
                    }
                    // printf("[MUS] #%d play %d vol %d\n", chan, note, vol);
                    break;
                }
                case event_pitch_wheel: {
                    // 0 is one tone down, 255 is one tone up. 64 is a half-tone down, 192 is a half-tone up, and 128 is none.
                    int bend = *score++; // 8-bit value (top 3 bits significant)
                    // XXX map to a freq offset (multiplier)
                    // XXX apply to every note in the channel
                    // if (ch->note) {
                    //     bend = ch->freq + bend; // XXX HACK TODO
                    //     // adlib_write(0xa0+chan, bend & 255); // frequency low 8 bits
                    //     // adlib_write(0xb0+chan, bend >> 8);  // frequency top 2 bits, octave (shift), key-on
                    // }
                    printf("[MUS] #%d bend %d\n", mus_ch, bend);
                    break;
                }
                case event_system: {
                    int ctrl = (*score++) & 0x7F;  // only low 4 bits used (ignore top bit?)
                    mus_event(ctrl, 0, mus_ch, ch);
                    break;
                }
                case event_controller: {
                    int ctrl = (*score++) & 0x7F;  // only low 4 bits used (ignore top bit?)
                    int value = *score++;          // full 8 bits are used
                    // system evenst are "silently skipped" if used via event_controller.
                    if (ctrl < 10) {
                        mus_event(ctrl, value, mus_ch, ch);
                    }
                    break;
                }
                case event_end_of_measure:
                    break;
                case event_end_of_score: {
                    printf("[MUS] end of score\n");
                    if (loop_score) {
                        score = loop_score;
                        return 1; // still playing (ignore remaining ticks)
                    }
                    return 0; // stopped
                }
                case event_unused:
                    score++; // must contain a single data byte
                    break;
            }
        } while (!(cmd & 0x80));
        // parse the next delay.
        do {
            cmd = *score++;
            delay <<= 7;         // *= 128
            delay += cmd & 0x7F; // low 7 bits
        } while (cmd & 0x80);
    } while (ticks > 0);
    return 1;
}

void musplay_op2bank(char* data) {
    memcpy(&op2bank, data, sizeof(op2bank));
}

void musplay_play(char* data, int loop) {
    MUS_header* hdr = (MUS_header*) data;
    for (int i=0; i<hdr->instrCnt; i++) {
        printf("%d, ", hdr->instruments[i]);
    }
    printf("\n");
    score = (byte*)(data + hdr->scoreStart);
    loop_score = loop ? score : 0;
    delay = 0;
    // clear all MUS channels
    memset(channels, 0, sizeof(channels));
    for (int m=0; m<num_mus_channels; m++) {
        channels[m].last_vol = 127;    // volume of prior note on the channel  XXX check
        channels[m].vol = 127;         // default channel volume               XXX check
        channels[m].express = 127;     // default channel expression           XXX check
        channels[m].ins = &op2bank[0]; // must be a valid pointer
    }
    // clear all HW channels
    memset(hw_voices, 0, sizeof(hw_voices));
    for (int h=0; h<num_voices; h++) {
        hw_voices[h].note = -1;    // no note playing
        hw_voices[h].mus_ch = -1;  // no channel owner
        hw_voices[h].ins_sel = -1; // no instrument selected
    }
    // clear all writeable registers on the card
    for (int reg = 0x01; reg <= 0xf5; reg++) {
        adlib_write(reg, 0);
    }
    // enable OPL2 features on the card
    adlib_write(0x01, 0x20);
    printf("\n\n\n[MUS] started playing\n");
}

void musplay_stop (void) {
    adlib_write(0xbd, 0); // stop rhythm instruments, clear rhythm mode, vibrato, tremelo
    for (int i=0; i<num_voices; i++) {
        silence_hw(i); // set release rates to max, and key-off
    }
    score = 0;
}
