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

- `-q`, `--quiet`:
  Don't produce any output unless there is an error.

- `-O`, `--image-offset=`*value*|`auto`:
  Specify the byte offset at which the filesystem is located in the image.
  Use `auto` to detect the offset automatically. This is also the default.
  This is only useful for images that have some header located before the
  actual filesystem data.

- `-H`, `--print-header`:
  Print the header located before the filesystem image to stdout. If no
  header is present, the program will exit with exit code 2 and emit a
  warning.

- `-n`, `--num-workers=`*value*:
  Number of worker threads used for integrity checking.

- `--check-integrity`:
  Instead of performing a fast checksum check, perform a (much slower)
  integrity check using the embedded SHA-512/256 hashes.

- `--no-check`:
  Don't even perform a fast checksum check on the file system blocks.
  The metadata will still be checked to make sure it can safely be read.
  Use this if you want to quickly query file system information rather
  than performing an actual file system check.

- `-j`, `--json`:
  Print a simple JSON representation of the filesystem metadata. Please
  note that the format is *not* stable. The level of detail also depends
  on the `--detail` switch. This can be useful in conjunction with the
  `jq` tool to extract file system information, for example generate a
  list of all categories in a file system image:

```
$ dwarfsck image.dwarfs --no-check -j | jq -r '.categories | keys .[]'
<default>
incompressible
pcmaudio/metadata
pcmaudio/waveform
```

- `--export-metadata=`*file*:
  Export all filesystem metadata in JSON format.

- `--log-level=`*name*:
  Specify a logging level.

- `--log-with-context`:
  Enable logging context regardless of level. By default, context is enabled
  if the level is `verbose`, `debug` or `trace`.

- `-h`, `--help`:
  Show program help, including option defaults.

- `--man`:
  If the project was built with support for built-in manual pages, this
  option will show the manual page. If supported by the terminal and a
  suitable pager (e.g. `less`) is found, the manual page is displayed
  in the pager.

## AUTHOR

Written by Marcus Holland-Moritz.

## COPYRIGHT

Copyright (C) Marcus Holland-Moritz.

## SEE ALSO

mkdwarfs(1), dwarfs(1), dwarfsextract(1), dwarfs-format(5)
