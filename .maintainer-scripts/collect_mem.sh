#!/bin/bash

# SPDX-FileCopyrightText: Copyright (c) Marcus Holland-Moritz
# SPDX-License-Identifier: MIT

set -e

OUTPUT=${1:-"mem_results"}

if diff mkdwarfs mkdwarfs.su >/dev/null; then
  echo "mkdwarfs and mkdwarfs.su are identical"
else
  echo "mkdwarfs and mkdwarfs.su differ, updating mkdwarfs.su..."
  sudo cp mkdwarfs mkdwarfs.su
  sudo chown root mkdwarfs.su
  sudo chmod u+s mkdwarfs.su
fi

mkdir -p $OUTPUT

for config in hollow_nodedupe similarity_b8 similarity_l9 default_l9 categorize; do
  for jemalloc in default decay; do
    for dataset in perl debian wiki; do
      if [ "$dataset" == "wiki" ] && [ "$config" == "categorize" ]; then
        continue
      fi

      if [ "$jemalloc" == "default" ]; then
        unset MALLOC_CONF
      else
        export MALLOC_CONF="background_thread:true,dirty_decay_ms:0,muzzy_decay_ms:0"
      fi

      MKDWARFS=mkdwarfs

      case "$dataset" in
        "perl")
          DATASET_PATH="/home/mhx/perl-install"
          ;;
        "debian")
          # debian-live-12.9.0-amd64-mate.iso
          DATASET_PATH="/home/mhx/Downloads/iso/debian-root"
          # this run needs root privileges; `mkdwarfs.su` is `chown root` + `chmod u+s`
          MKDWARFS=mkdwarfs.su
          ;;
        "wiki")
          dwarfs wiki-zstd.dwarfs mnt -o cachesize=8g,workers=32,max_threads=32,clone_fd
          DATASET_PATH="mnt"
          ;;
      esac

      OPTIONS="-i $DATASET_PATH -o /dev/null --force -C null --metadata-compression=null --schema-compression=null --log-level=verbose --no-progress"

      case "$config" in
        "hollow_nodedupe")
          OPTIONS="$OPTIONS --hollow --no-dedupe --order=none"
          ;;
        "similarity_b8")
          OPTIONS="$OPTIONS --order=similarity -B8"
          ;;
        "similarity_l9")
          OPTIONS="$OPTIONS --order=similarity -l9"
          ;;
        "categorize")
          OPTIONS="$OPTIONS --categorize"
          ;;
        "default_l9")
          OPTIONS="$OPTIONS -l9"
          ;;
      esac

      if [ "$dataset" == "wiki" ]; then
        OPTIONS="$OPTIONS --num-walk-workers=32"
      fi

      log_basename="mkdwarfs-${dataset}-${config}-${jemalloc}"

      export DWARFS_LOG_MEMORY_USAGE=$PWD/$OUTPUT/$log_basename-memory.tsv
      echo "==============================================================================="
      echo ./$MKDWARFS $OPTIONS
      echo "==============================================================================="
      ./$MKDWARFS $OPTIONS 2>&1 | tee $OUTPUT/$log_basename-console.log

      case "$dataset" in
        "wiki")
          umount mnt
          ;;
      esac
    done
  done
done
