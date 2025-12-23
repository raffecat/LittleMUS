# LittleMUS player

LittleMUS is a music player library for DMX MUS files/lumps used in the
DOS versions of DOOM, DOOM II, Heretic, Hexen, Chex Quest, Raptor, Strife
to play Adlib/OPL music.

It aims for 100% accurate playback of the original "Sound Blaster" FM synth
music (Adlib/OPL) in DOOM and other games that used the commercial DMX sound
library.

At this stage it plays the DOOM and DOOM II Adlib/OPL music with about 99%
accuracy compared to hardware recordings. (OPL emulation, output filtering and
resampling happen downstream of LittleMUS; they're not included in this 99%!)

I wrote LittleMUS a part of a source port of the original linuxdoom-1.10 release.
Try it out in [the OG, DOOM](https://github.com/raffecat/OG-DOOM) which aims
to re-create the sound and feel of playing DOOM on a 486 with a Sound Blaster.


## Sample Rate

Real Sound Blaster cards have low-pass filtering on the output that
you can approximate using a sample-rate of 22050 or 24000 (a real LPF
would be better.)

It sounds much sharper and cleaner at 44100 or 48000, but some of the high-frequency
notes in DOOM no longer match recordings, and will break your ears.

You can also use the OPL chip's native sample-rate of ~49716 (14318180/288), but this
requires resampling for playback on modern hardware. The DOSBox/Woody-OPL emulator
produces a decent downsampling at the requested sample-rate.


## Tick Rate

Most of the games listed above assume a tick rate of 140 Hz (140 ticks per second)
except for Raptor, which uses 70 Hz.

The library requires your program to call `musplay_update(ticks)` at (or around)
this rate to advance the music and issue register writes to the OPL emulator.
Multiple ticks can be processed at once if necessary.

Caution: if ticks are delayed too much, notes will play "out of place" in the music.
This is quite audible, if you (for example) call this once per video frame...


## OPL Emulator

The library is set up for [Woody-OPL](https://github.com/rofl0r/woody-opl)
which was extracted from [DOSBox](https://www.dosbox.com/wiki/Main_Page) before
it changed over to [Nuked-OPL](https://github.com/nukeykt/Nuked-OPL3).

LittleMUS only calls a single function, `void adlib_write(int reg, int val)`,
which you can instead define in your program to do anything you like.

I tried it with Nuked-OPL, but some notes didn't key on/off correctly,
so I went back to woody-opl.

It seems Nuked requires time to pass, i.e. a call to `OPL3_Generate`, to actually
apply the key-on or key-off (otherwise subsequent key-on/key-off writes just
flip a single bit back and forth, leading to no net change.)

This would admittedly lead to more accurate results, but it implies an entirely
different architecture for LittleMUS: it must drive Nuked itself, filling a
buffer with samples as it processes MUS events.


## Real OPL Hardware?

I haven't tried it on a real OPL chip but it should more or less work,
unless I missed some necessary initialisation registers.

There's no card/chip detection, it currently just assumes OPL3.

It can work with OPL2, just change the `num_voices` constant to `9`.
The DOOM music will drop a few notes on OPL2; it's currently configured
to drop new notes rather than kill old notes, I'm still playing around
with this part (it doesn't happen much on OPL3.)


## Volume Control?

I added a volume control, combined internally with the operator attenuation
factors written to the OPL registers.

There may be some problems with doing this:

* It seems to change the timbre of some instruments (perhaps it changes the height
  of ADSR envelopes without changing attack/decay rates? Not sure. It may only
  be a problem with volumes > 100)
* Some notes in the music score have their note volume (midi velocity) set to match
  some other note that's already playing, and this can change their relative volumes.

So I don't recommend using the player's volume control (leave it at the default 100),
instead I recommend mixing the OPL output with your own (or the OS's) mixer to
change volume.


## Missing Features

Right now I only know of one left to do:

* LFO emulation, e.g. E1M8 uses the midi modulation controller to vary vibrato depth.
  OPL2/3 hardware only has an on/off vibrato switch, so it's likely the original DMX player
  generated its own scaled LFO and modified the operator pitch.

And the following differences when used with Woody-OPL, which (I think) mostly come
down to choice of sample rate, and filtering:

* Slight phase errors in E2M8, leading to different chorus "voicing". May be due
  to the instantaneous nature of programming the OPL emulator, i.e. there's no register
  programming skew. A low-pass filter would also shift phases a bit.
* Pops in E1M5 also happen on real hardware, but to a lesser extent, likely due to
  the presence of a low-pass filter.
* Some differences in timbre, especially on high notes, could be due (again) to the
  lack of a proper low-pass filter.

I might look into adding a low-pass filter.


## Hardware Recordings

These are the hardware recordings on YouTube I've been using:

* [DOOM](https://www.youtube.com/watch?v=DPLSw9uubhE&list=PL44ECABBB2F44C6F7)
* [DOOM II](https://www.youtube.com/watch?v=hXH692FUp3Q)


## Thanks

I used a lot of different references to put this together..

* https://doomwiki.org/wiki/MUS
* https://moddingwiki.shikadi.net/wiki/MUS_Format
* https://moddingwiki.shikadi.net/wiki/OP2_Bank_Format
* https://cosmodoc.org/topics/adlib-functions/ !
* https://www.doomworld.com/idgames/docs/editing/mus_form
* https://www.phys.unsw.edu.au/jw/notes.html
* https://github.com/rofl0r/woody-opl
* The DOSBox Team, OPL2/OPL3 emulation library
* Ken Silverman, ADLIBEMU.C
* id software, for giving us DOOM.


## Building

Drop the files into your project, maybe in a thirdparty directory.
There's no build-system nonsense here.


## Library API


### _musplay_op2bank_

```c
void musplay_op2bank (char* data);
```

Set the [OP2 Instrument Bank](https://moddingwiki.shikadi.net/wiki/OP2_Bank_Format) before playing music.

Pass a pointer to the `BYTE[175][36]` instrument data, which is found after
the 8-byte `#OPL_II#` header in .OP2 files.

The instrument data (175*36 bytes) is copied into library memory.

If you're a DOOM engine, you can get this data from the `GENMIDI` lump:

```c
  int op2lump = W_GetNumForName("GENMIDI");
  char *op2 = W_CacheLumpNum( op2lump, PU_STATIC );
  musplay_op2bank(op2+8); // skip "#OPL_II#" to get BYTE[175][36] instrument data
  Z_Free( op2 );
```


### _musplay_volume_

```c
void musplay_volume (int volume);
```

Set the player volume (0-127, 100 = full volume [>100 boosts])

This is combined into the hardware attenuation levels written to the
adlib registers in the OPL emulator.

Note: I don't recommend using this, as it [seems to] affect the "tone"
of some instruments and/or the relative level of the notes played
(maybe only when volume > 100?)

Instead I recommend leaving this at the default (100) and using your own
or the OS's mixer to change volume.


### _musplay_start_

```c
void musplay_start (char* data, int loop);
```

Start playing a MUS file/lump.

The supplied data must start with [MUS_header](https://moddingwiki.shikadi.net/wiki/MUS_Format) ("MUS", 0x1A).

Only one song can play at a time.
The song will loop if loop is non-zero.

This writes OPL registers (via `adlib_write`) to initialise the hardware.
The first note will be produced later, when `musplay_update` is called.

If you're a DOOM engine, you receive this data via `I_RegisterSong`.


### _musplay_stop_

```c
void musplay_stop (void);
```

Stop playing the MUS file.

This writes OPL registers (via `adlib_write`) to key-off all channels.


### _musplay_update_

```c
int musplay_update (int ticks);
```

Advance time in 140 Hz ticks i.e. send 140 ticks per second (70 for Raptor.)
See the "Tick Rate" section above.

This writes OPL registers by calling `void adlib_write(int reg, int val)`
which must be implemented by the OPL emulator, or your program.

This can be called unconditionally. It won't do anything unless a song is playing.

Returns 1 if the music is still playing, or 0 if the music finished (looped tracks never finish.)


## Then what?

That's it, then you call `adlib_getsample(mixbuffer, SAMPLECOUNT)` in the Woody-OPL
emulator to get some audio samples to play.

It might be a good idea to set up an audio thread to host this player, the OPL
emulator, and sound-effect mixing. Audio subsystems usually provide a callback
mechanism that will take generated audio samples off your hands.


## Speaking of which, is this library thread-safe?

No, not at all. If you're playing fast and loose with threads (i.e. one thread doesn't
own the player) you'll need to lock a mutex before calling any of these functions.
Ideally the same mutex.

Currently the library uses global variables, which is very doom-like.
I'd like to move all that stuff into a library-instance struct..


## License

MIT License.

```
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
```
