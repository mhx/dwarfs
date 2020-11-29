# Change Log

## Version 0.2.1 - 2020-11-29

- Replace --no-owner and --no-time with more flexible --set-owner,
  --set-group and --set-time options

- Update man pages


## Version 0.2.0 - 2020-11-29

- Complete rewrite of the file system metadata storage using
  fbthrift's `frozen` library


## Version 0.1.1 - 2020-11-23

- Test and fix Debian Buster and Ubuntu Focal builds

- Migrate from `folly::StringPiece` to `std::string_view`

- Documentation updates, list Debian/Ubuntu dependencies


## Version 0.1.0 - 2020-11-22

- Initial release
