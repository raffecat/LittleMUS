#!/usr/bin/env python3

# MIDI channel volume (CC7), expression (CC11), note velocity (volume)
# are all values in the range 0 - 127
# convert to a logarithmic amplitude, by MIDI convention:
# L(dB) = 40 log₁₀(vol/127)  with factor 40 giving a "natural" feel

# we can convert this to a linear gain:
# gain = 10^(LdB/20)   [ inverse: 20·log₁₀(LdB) ]
# but notice we're cancelling out most of the L(dB) calculation (see inverse)
# gain = (vol/127)²    [ divide 40/20 ; cancel log₁₀ and 10^ ]

# we can multiply linear gains:
# gain = (volume/127)² x (expression/127)² x (velocity/127)³
# then convert the result back to dB
# giving an intuitive model for combining volumes

# however if we stay in log-space, we can add them for the same effect!
# L(dB) = 40·log₁₀(volume/127) + 40·log₁₀(expression/127) + 60·log₁₀(velocity/127)

# that looks expensive, but we can bake most of the work
# into lookup tables, and just add the results:
# L(dB) = logSqr[volume] + logSqr[expression] + logCube[velocity]

# finally we need to convert to an attenuation value to put in the
# OPL2/3 Operator Level register: 0-63
#      0 dB attenuation (level = 0)   full volume
# -47.25 dB attenuation (level = 63)  almost silent
#  -0.75 dB attenuation per step

# since L(dB) is already a dB calculation, we divide its result by the
# hardware step size, -0.75 dB, yielding a number of steps above 0 dB,
# then clamp between 0 and 63, the hardware limits.


# set 0dB pivot at pv = 100 (rather than 127)
# MUS tracks seem to be authored with this assumption

# HW_level = clamp(20 · Σ k_i·log10(vol_i/pv) / -0.75, 0, 63)
# where k=2 for volume/expression and k=~3 for note velocity
# (these are conventional MIDI powers)

# attenuation = clamp(logSqr[vol] + logSqr[expr] + logCube[vel], 0, 63)
# logSqr[n]  = (2*20/-0.75) · log10(max(n,1)/100)   # k=2
# logCube[n] = (3*20/-0.75) · log10(max(n,1)/100)   # k=3

# I ended up boosting quiet notes a bit, down to k=2.6
# otherwise a lot of nice background instruments hide in the mix

import math

maxAtt = 96 # maximum attenuation steps per control

dbStep = 0.75 # HW attenuates in 0.75 dB steps

logSqT = [maxAtt] # n=0 has infinite attenuation
logCbT = [maxAtt] 

for n in range(1,128):
    log = math.log10(n / 100)
    logSq = (2 * 20.0 / -0.75) * log
    logCu = (2.6 * 20.0 / -0.75) * log
    if (logSq > maxAtt): logSq = maxAtt
    if (logCu > maxAtt): logCu = maxAtt
    logSqT.append(int(logSq))
    logCbT.append(int(logCu))

print("static int8_t att_log_square[128] =",logSqT)
print("static int8_t att_log_cube[128] =",logCbT)
