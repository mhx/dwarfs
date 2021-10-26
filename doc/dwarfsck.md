# dwarfsck(1) -- check DwarFS image

## SYNOPSIS

`dwarfsck` [`-i`] *image* [*options*...]

## DESCRIPTION

**dwarfsck** will perform a check of a DwarFS filesystem image.

If successful, it will show details about the filesystem depending
on the detail level specified. If an error is found, it will exit
with a non-zero exit code.

## OPTIONS

- `-i`, `--input=`*file*:
  Path to the filesystem image.

- `-d`, `--detail=`*value*:
  Level of filesystem information detail. The default is 2. Higher values
  mean more output. Values larger than 6 will currently not provide any
  further detail.

- `-O`, `--image-offset=`*value*|`auto`:
  Specify the byte offset at which the filesystem is located in the image.
  Use `auto` to detect the offset automatically. This is also the default.
  This is only useful for images that have some header located before the
  actual filesystem data.

- `-H`, `--print-header`:
  Print the header located before the filesystem image to stdout. If no
  header is present, the program will exit with a non-zero exit code.

- `-n`, `--num-workers=`*value*:
  Number of worker threads used for integrity checking.

- `--check-integrity`:
  In addition to performing a fast checksum check, also perform a (much
  slower) verification of the embedded SHA-512/256 hashes.

- `--json`:
  Print a simple JSON representation of the filesystem metadata. Please
  note that the format is *not* stable.

- `--export-metadata=`*file*:
  Export all filesystem meteadata in JSON format.

- `--log-level=`*name*:
  Specifiy a logging level.

- `--help`:
  Show program help, including option defaults.

## AUTHOR

Written by Marcus Holland-Moritz.

## COPYRIGHT

Copyright (C) Marcus Holland-Moritz.

## SEE ALSO

mkdwarfs(1), dwarfs(1), dwarfsextract(1), dwarfs-format(5)
