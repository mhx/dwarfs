#!/bin/bash

set -e

RELEASESDIR="/mnt/opensource/artifacts/dwarfs/releases"
OUTPUTDIR="/mnt/opensource/benchmarks-gnutar-devnull"

# COMMON_OPTIONS="--output-dir=${OUTPUTDIR}"
COMMON_OPTIONS=(--output-dir="${OUTPUTDIR}" --only=extract_perl_zstd_gnutar_devnull)

./benchmark.py "${COMMON_OPTIONS[@]}" --input-dir "${RELEASESDIR}"/v0.7.0@* --commit-time 1689098220
./benchmark.py "${COMMON_OPTIONS[@]}" --input-dir "${RELEASESDIR}"/v0.7.1@* --commit-time 1689869865
./benchmark.py "${COMMON_OPTIONS[@]}" --input-dir "${RELEASESDIR}"/v0.7.2@* --commit-time 1690232314
./benchmark.py "${COMMON_OPTIONS[@]}" --input-dir "${RELEASESDIR}"/v0.7.3@* --commit-time 1701806960
./benchmark.py "${COMMON_OPTIONS[@]}" --input-dir "${RELEASESDIR}"/v0.7.4@* --commit-time 1703728314
./benchmark.py "${COMMON_OPTIONS[@]}" --input-dir "${RELEASESDIR}"/v0.7.5@* --commit-time 1705419678
./benchmark.py "${COMMON_OPTIONS[@]}" --input-dir "${RELEASESDIR}"/v0.8.0@* --commit-time 1705926138
./benchmark.py "${COMMON_OPTIONS[@]}" --input-dir "${RELEASESDIR}"/v0.9.0@* --commit-time 1707143229
./benchmark.py "${COMMON_OPTIONS[@]}" --input-dir "${RELEASESDIR}"/v0.9.1@* --commit-time 1707217295
./benchmark.py "${COMMON_OPTIONS[@]}" --input-dir "${RELEASESDIR}"/v0.9.2@* --commit-time 1707503486
./benchmark.py "${COMMON_OPTIONS[@]}" --input-dir "${RELEASESDIR}"/v0.9.3@* --commit-time 1707677893
./benchmark.py "${COMMON_OPTIONS[@]}" --input-dir "${RELEASESDIR}"/v0.9.4@* --commit-time 1707758959
./benchmark.py "${COMMON_OPTIONS[@]}" --input-dir "${RELEASESDIR}"/v0.9.5@* --commit-time 1707812309
./benchmark.py "${COMMON_OPTIONS[@]}" --input-dir "${RELEASESDIR}"/v0.9.6@* --commit-time 1708725460
./benchmark.py "${COMMON_OPTIONS[@]}" --input-dir "${RELEASESDIR}"/v0.9.7@* --commit-time 1712777367
./benchmark.py "${COMMON_OPTIONS[@]}" --input-dir "${RELEASESDIR}"/v0.9.8@* --commit-time 1713113173
./benchmark.py "${COMMON_OPTIONS[@]}" --input-dir "${RELEASESDIR}"/v0.9.9@* --commit-time 1714453472
./benchmark.py "${COMMON_OPTIONS[@]}" --input-dir "${RELEASESDIR}"/v0.9.10@* --commit-time 1717076229
./benchmark.py "${COMMON_OPTIONS[@]}" --input-dir "${RELEASESDIR}"/v0.10.0@* --commit-time 1723659383
./benchmark.py "${COMMON_OPTIONS[@]}" --input-dir "${RELEASESDIR}"/v0.10.1@* --commit-time 1723909686
./benchmark.py "${COMMON_OPTIONS[@]}" --input-dir "${RELEASESDIR}"/v0.10.2@* --commit-time 1733153174
./benchmark.py "${COMMON_OPTIONS[@]}" --input-dir "${RELEASESDIR}"/v0.11.0@* --commit-time 1742224287
./benchmark.py "${COMMON_OPTIONS[@]}" --input-dir "${RELEASESDIR}"/v0.11.1@* --commit-time 1742302835
./benchmark.py "${COMMON_OPTIONS[@]}" --input-dir "${RELEASESDIR}"/v0.11.2@* --commit-time 1742444317
./benchmark.py "${COMMON_OPTIONS[@]}" --input-dir "${RELEASESDIR}"/v0.11.3@* --commit-time 1743426274
./benchmark.py "${COMMON_OPTIONS[@]}" --input-dir "${RELEASESDIR}"/v0.12.0@* --commit-time 1744119300
./benchmark.py "${COMMON_OPTIONS[@]}" --input-dir "${RELEASESDIR}"/v0.12.1@* --commit-time 1744316824
./benchmark.py "${COMMON_OPTIONS[@]}" --input-dir "${RELEASESDIR}"/v0.12.2@* --commit-time 1744786952
