# dwarfsextract(1) -- extract DwarFS image

## SYNOPSIS

`dwarfsextract` `-i` *image* [`-o` *dir*] [*options*...]  
`dwarfsextract` `-i` *image* -f *format* [`-o` *file*] [*options*...]

## DESCRIPTION

**dwarfsextract** allows you to extract a DwarFS image, either directly
into another archive file, or to a directory on disk.

To extract the filesystem image to a directory, you can use:

    dwarfsextract -i image.dwarfs -o output-directory

The output directory must exist.

You can also rewrite the contents of the filesystem image as another
archive type, for example, to write a tar archive, you can use:

    dwarfsextract -i image.dwarfs -o output.tar -f ustar

For a list of supported formats, see libarchive-formats(5).

If you want to compress the output archive, you can use a pipeline:

    dwarfsextract -i image.dwarfs -f ustar | gzip > output.tar.gz

You could also use this as an alternative way to extract the files
to disk:

    dwarfsextract -i image.dwarfs -f cpio | cpio -id

## OPTIONS

- `-i`, `--input=`*file*:
  Path to the source filesystem.

- `-o`, `--output=`*directory*|*file*:
  If no format is specified, this is the directory to which the contents
  of the filesystem should be extracted. If a format is specified, this
  is the name of the output archive. This option can be omitted, in which
  case the default is to extract the files to the current directory, or
  to write the archive data to stdout.

- `-O`, `--image-offset=`*value*|`auto`:
  Specify the byte offset at which the filesystem is located in the image.
  Use `auto` to detect the offset automatically. This is also the default.
  This is only useful for images that have some header located before the
  actual filesystem data.

- `-f`, `--format=`*format*:
  The archive format to produce. If this is left empty or unspecified,
  files will be extracted to the output directory (or the current directory
  if no output directory is specified). For a full list of supported formats,
  see libarchive-formats(5).

- `--continue-on-error`:
  Try to continue with extraction even when errors are encountered. This
  only applies to errors when reading from the file system image. Errors
  when writing the extracted files will still be fatal.

- `--disable-integrity-check`:
  This option disables all block integrity checks on the file system data.
  There is a non-zero chance that this allows further data to be read from
  corrupted file systems. However, there's also a non-zero chance that it
  will completely crash the program. So please don't use this unless you
  know what you're doing.

- `--stdout-progress`:
  Write progress percentage to stdout. Useful for piping to tools like
  `zenity`.

- `-n`, `--num-workers=`*value*:
  Number of worker threads used for extracting the filesystem.

- `-s`, `--cache-size=`*value*:
  Size of the block cache, in bytes. You can append suffixes (`k`, `m`, `g`)
  to specify the size in KiB, MiB and GiB, respectively. Note that this is
  not the upper memory limit of the process, as there may be blocks in
  flight that are not stored in the cache. Also, each block that hasn't been
  fully decompressed yet will carry decompressor state along with it, which
  can use a significant amount of additional memory.

- `-l`, `--log-level=`*name*:
  Specify a logging level.

- `--log-with-context`:
  Enable logging context regardless of level. By default, context is enabled
  if the level is `verbose`, `debug` or `trace`.

- `--perfmon=`*name*:
  Enable performance monitoring for the list of comma-separated components.
  This option is only available if the project was built with performance
  monitoring enabled. Available components include `fuse`, `filesystem_v2`
  and `inode_reader_v2`.

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

mkdwarfs(1), dwarfs(1), dwarfsck(1), dwarfs-format(5), libarchive-formats(5)
