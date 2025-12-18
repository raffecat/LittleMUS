#!/usr/bin/env python3

# https://cosmodoc.org/topics/adlib-functions/#frequencies-and-amplitudes-mathematically
# https://www.doomworld.com/idgames/docs/editing/mus_form
# https://www.phys.unsw.edu.au/jw/notes.html

blocks = range(0,8) # 0-7
key_on = (1<<13)
debug = 0

# Ablib $A0 and $B0 bytes to key-on frequency (packed in a word)
def adlib_freq(n,f):
	best_fnum = 0
	best_block = -1
	best_error = 0xFFFFFFFF
	for block in blocks:
		fnum = int(f * (2 ** (20 - block)) // 49716)
		if fnum > 1023:
			if debug:
				print(f"  {f} fnum {fnum} block {block} overflows")
			continue
		fout = fnum * 49716 / (2 ** (20 - block))
		err = abs(fout - f)
		if debug:
			print(f"  {f} fnum {fnum} block {block} error {err}")
		if err < best_error:
			best_fnum = fnum
			best_block = block
			best_error = err
	if debug:
		print(f"{n} {f} best fnum {best_fnum} block {best_block} error {best_error}")
	if best_fnum > 1023:
		raise ValueError("bad block")
	if best_block > 7:
		raise ValueError("bad block")
	if best_block < 0:
		return 0  # out of range, do not key-on
	return key_on | (best_block<<10) | best_fnum


# MIDI notes

#  -----------------------------------------------------------------------
#  | Octave |  C  | C# | D  | D# | E  | F  | F# | G  | G# | A  | A# | B  |
#  -----------------------------------------------------------------------
#  |    0   |   0 |  1 |  2 |  3 |  4 |  5 |  6 |  7 |  8 |  9 | 10 | 11 |
#  |    1   |  12 | 13 | 14 | 15 | 16 | 17 | 18 | 19 | 20 | 21 | 22 | 23 |
#  |    2   |  24 | 25 | 26 | 27 | 28 | 29 | 30 | 31 | 32 | 33 | 34 | 35 |
#  |    3   |  36 | 37 | 38 | 39 | 40 | 41 | 42 | 43 | 44 | 45 | 46 | 47 |
#  |    4   |  48 | 49 | 50 | 51 | 52 | 53 | 54 | 55 | 56 | 57 | 58 | 59 |
#  |    5   |  60 | 61 | 62 | 63 | 64 | 65 | 66 | 67 | 68 | 69 | 70 | 71 |
#  |    6   |  72 | 73 | 74 | 75 | 76 | 77 | 78 | 79 | 80 | 81 | 82 | 83 |
#  |    7   |  84 | 85 | 86 | 87 | 88 | 89 | 90 | 91 | 92 | 93 | 94 | 95 |
#  |    8   |  96 | 97 | 98 | 99 |100 |101 |102 |103 |104 |105 |106 |107 |
#  |    9   | 108 |109 |110 |111 |112 |113 |114 |115 |116 |117 |118 |119 |
#  |   10   | 120 |121 |122 |123 |124 |125 |126 |127 |    |    |    |    |
#  -----------------------------------------------------------------------

A4_freq = 880
A4_note = 69

def note_to_freq(note):
	n = note - A4_note  # >0 if note > A4
	freq = 2 ** (n/12) * A4_freq
	return freq

# ok it seems like musplayer is out by an octave in all the doom music,
# bumping A4 up to 880..

print("midi note frequency:")
for n in range(0,128):
	freq = note_to_freq(n)
	print(f"  {n} freq {freq}")

print("\nadlib frequency selector:")
for n in range(0,128):
	f = note_to_freq(n)
	k = adlib_freq(n,f)
	print(f"  {k},")
