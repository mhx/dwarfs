#!/bin/bash

set -e

output_dir="$1"
duration=5

for bits in 8 16 20 24 32; do
  sox -D -r10240 -b$bits -n $output_dir/test$bits.wav synth $duration sine ${bits}
  for ch in 1 2 3 4 5 6; do
    sox -D -r10240 -b$bits -n $output_dir/test$bits.$ch.wav synth $duration sine ${bits} ${ch}
    sox -D -M $output_dir/test$bits.?.wav $output_dir/test$bits-$ch.wav
  done
  rm $output_dir/test$bits.?.wav
  for fmt in aiff caf w64; do
    sox -D $output_dir/test$bits.wav $output_dir/test$bits.$fmt
    for ch in 2 3 4 5 6; do
      sox -D $output_dir/test$bits-$ch.wav $output_dir/test$bits-$ch.$fmt
    done
  done
done
