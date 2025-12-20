#pragma once

// Set the OP2 (GENMIDI.OP2) instrument bank.
// Pass a pointer to the BYTE[175][36] instrument data,
// which is after the 8-byte "#OPL_II#" header in .OP2 files.
// The instrument data (175*36 bytes) is copied.
void musplay_op2bank (char* data);

// Set the player volume (0-127)
void musplay_volume (int volume);

// Start playing a MUS file.
// Data must start with MUS_header ("MUS", 0x1A)
// The song will loop if loop is non-zero.
void musplay_play (char* data, int loop);

// Stop playing the MUS file.
void musplay_stop (void);

// Advance time in 140 Hz ticks (i.e. send 140 ticks per second [70 for Raptor])
// This writes to Adlib (adlibemu.c) by calling adlib0(register, value)
int musplay_update (int ticks);
