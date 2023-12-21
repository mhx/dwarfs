#!/bin/bash

set -e

for bits in 8 16 20 24 32; do
  sox -D -r10000 -b$bits -n audio/test$bits.wav synth 5 sine ${bits}00
  for ch in 1 2 3 4 5 6; do
    sox -D -r10000 -b$bits -n audio/test$bits.$ch.wav synth 5 sine ${bits}${ch}0
    sox -D -M audio/test$bits.?.wav audio/test$bits-$ch.wav
  done
  rm audio/test$bits.?.wav
  for fmt in aiff caf w64; do
    sox -D audio/test$bits.wav audio/test$bits.$fmt
    for ch in 1 2 3 4 5 6; do
      sox -D audio/test$bits-$ch.wav audio/test$bits-$ch.$fmt
    done
  done
done
