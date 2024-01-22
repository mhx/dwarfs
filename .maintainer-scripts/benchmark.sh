#!/bin/bash

set -e

BENCHMARKS=$1
if [ -z "$BENCHMARKS" ]; then
    BENCHMARKS="extract,mkdwarfs"
fi

HF=hyperfine
NPROC=$(nproc)

NWARM=2
NFAST=10
NSLOW=5

ARCH=$(ls -d1 dwarfs-*-Linux-*/ | head -n1)
HASH=$ARCH
ARCH=${ARCH/*-Linux-/}
ARCH=${ARCH/-*/}
HASH=${HASH/-Linux-*/}
HASH=${HASH/*-/}

OUT=reports-$HASH

mkdir -p $OUT

for i in dwarfs-*-Linux-*/; do
    t=${i/*-Linux-/}
    t=${t/\//}

    for n in mkdwarfs dwarfs dwarfsck dwarfsextract; do
        ln -sf ${i}*bin/$n $n-$t
    done
done

DATASETS='audio disk perl video'

MKDWARFS=$(ls -1 ./mkdwarfs-$ARCH-* | paste -sd "," -)
# DWARFSCK=$(ls -1 ./dwarfsck-$ARCH-* | paste -sd "," -)
DWARFSEXTRACT=$(ls -1 ./dwarfsextract-$ARCH-* | paste -sd "," -)
# DWARFS=$(ls -1 ./dwarfs-$ARCH-* | paste -sd "," -)

#############################################
# extract all datasets as TAR archive
#############################################

if [[ ",$BENCHMARKS," == *,extract,* ]]; then
    for ds in $DATASETS; do
        $HF --export-json=$OUT/extract-$ds.json --export-markdown $OUT/extract-$ds.md -w $NWARM -m $NFAST \
            -L bin "$DWARFSEXTRACT" "{bin} -n $NPROC -s 2g -i datasets/$ds.dwarfs -o /dev/null -f gnutar"
    done
fi

#############################################
# build filesystem images
#############################################

declare -A MKDWARFS_ARGS
MKDWARFS_ARGS[audio]="-C zstd:level=5 --categorize -C pcmaudio/waveform::flac:level=1"
MKDWARFS_ARGS[disk]="-C zstd:level=5 --categorize=incompressible --incompressible-fragments --incompressible-block-size=512k --order=revpath -B16 -C incompressible::null"
MKDWARFS_ARGS[perl]="-C zstd:level=5"
MKDWARFS_ARGS[video]="-C null -S 28 -B 3 -W 8 -w 0 --order=revpath"

if [[ ",$BENCHMARKS," == *,mkdwarfs,* ]]; then
    for ds in $DATASETS; do
        if [ ! -d tmp/$ds ]; then
            mkdir -p tmp/$ds
            echo "Extracting $ds dataset..."
            ./dwarfsextract-*-clang -n $NPROC -s 2g -i datasets/$ds.dwarfs -o tmp/$ds
        fi

        $HF --export-json=$OUT/mkdwarfs-$ds.json --export-markdown $OUT/mkdwarfs-$ds.md -w $NWARM -m $NSLOW \
            -L bin "$MKDWARFS" "{bin} -i tmp/$ds -o /dev/null --force --no-progress --log-level=warn ${MKDWARFS_ARGS[$ds]}"
    done
fi

# remove symlinks

for n in mkdwarfs dwarfs dwarfsck dwarfsextract; do
    for l in $n-$ARCH-*; do
        if [ -L $l ]; then
            rm -f $l
        fi
    done
done
