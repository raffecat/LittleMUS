/*
Little MUS Player v0.2

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
void musplay_op2bank (char* data);

/*
Set the player volume (0-127, 100 = _full volume_)

This is combined into the hardware attenuation levels written to the
adlib registers in the OPL emulator.

In other words, the generated samples will come out more loud or quiet.

If volume is greater than 100, it will _boost_ the volume above the
arranged level of the music, within the headroom available.
*/
void musplay_volume (int volume);

/*
Start playing a MUS file/lump.

The supplied data must start with MUS_header ("MUS", 0x1A)
see https://moddingwiki.shikadi.net/wiki/MUS_Format

Only one song can play at a time.

The song will loop if loop is non-zero.

This writes OPL registers (via `adlib_write`) to initialise the hardware.
The first note will be produced later, when `musplay_update` is called.
*/
void musplay_start (char* data, int loop);

/*
Stop playing the MUS file.

This writes OPL registers (via `adlib_write`) to key-off all channels.
*/
void musplay_stop (void);

/*
Advance time in 140 Hz ticks i.e. send 140 ticks per second (70 for Raptor)

This writes OPL registers by calling `void adlib_write(int reg, int val)`
which must be implemented by the OPL emulator, or your program.

This can be called unconditionally. It won't do anything unless a song is playing.

Returns 1 if the music is still playing, or 0 if the music finished (looped tracks never finish.)
*/
int musplay_update (int ticks);
