/* vim:set ts=2 sw=2 sts=2 et: */
/**
 * \author     Marcus Holland-Moritz (github@mhxnet.de)
 * \copyright  Copyright (c) Marcus Holland-Moritz
 *
 * This file is part of dwarfs.
 *
 * dwarfs is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * dwarfs is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with dwarfs.  If not, see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <initializer_list>
#include <istream>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <dwarfs/error.h>
#include <dwarfs/file_access.h>
#include <dwarfs/logger.h>
#include <dwarfs/match.h>
#include <dwarfs/os_access.h>
#include <dwarfs/util.h>
#include <dwarfs/writer/input_entry_reader.h>

namespace dwarfs::writer {

namespace {

namespace fs = std::filesystem;

constexpr std::string_view kPlainReaderName = "plain";
constexpr std::string_view kGenInitCpioReaderName = "gen_init_cpio";
constexpr std::string_view kJsonlReaderName = "jsonl";
constexpr std::array kReaderNames{kPlainReaderName, kGenInitCpioReaderName,
                                  kJsonlReaderName};

std::istream& get_input_stream(entry_reader_stream& input) {
  return input | match{
                     [](std::istream* s) -> std::istream& { return *s; },
                     [](std::unique_ptr<input_stream>& s) -> std::istream& {
                       return s->is();
                     },
                 };
}

fs::path resolve_source_path(fs::path const& root, fs::path path) {
  if (!path.is_absolute()) {
    path = root / path;
  }

  path.make_preferred();

  return path;
}

class plain_impl final : public input_entry_reader {
 public:
  plain_impl(entry_reader_stream input, fs::path const& canonical_root,
             os_access const& os, logger& lgr)
      : LOG_PROXY_INIT(lgr)
      , input_{std::move(input)}
      , is_{get_input_stream(input_)}
      , root_{canonical_root}
      , os_{os}
      , aliases_{canonical_root} {}

  std::optional<input_entry> next() override;

  std::optional<input_entry> implicit_root() const override {
    return input_entry{
        .path = ".",
        .source = root_,
        .spec = std::nullopt,
    };
  }

  std::size_t error_count() const noexcept override { return errors_; }

 private:
  fs::path rebase(fs::path const& p);

  LOG_PROXY_DECL(debug_logger_policy);
  entry_reader_stream input_;
  std::istream& is_;
  fs::path root_;
  os_access const& os_;
  std::vector<fs::path> aliases_;
  std::unordered_set<std::string> emitted_parents_;
  std::deque<input_entry> pending_;
  std::size_t errors_{0};
  std::size_t line_{0};
};

fs::path plain_impl::rebase(fs::path const& p) {
  // Rebase only the input path prefix, preserve symlinks in the suffix.
  auto const normalized = p.lexically_normal();

  auto rebase_onto = [&](fs::path const& alias) {
    auto const rel = normalized.lexically_relative(alias);
    return rel.empty() || rel == "." ? root_ : root_ / rel;
  };

  for (auto const& alias : aliases_) {
    if (std::ranges::mismatch(normalized, alias).in2 == alias.end()) {
      return rebase_onto(alias);
    }
  }

  fs::path prefix;

  for (auto const& part : normalized) {
    prefix /= part;

    if (prefix.has_root_directory() && os_.canonical(prefix) == root_) {
      aliases_.push_back(prefix);
      return rebase_onto(prefix);
    }
  }

  return p;
}

std::optional<input_entry> plain_impl::next() {
  for (;;) {
    if (!pending_.empty()) {
      auto entry = std::move(pending_.front());
      pending_.pop_front();
      return entry;
    }

    std::string line;

    if (!std::getline(is_, line)) {
      if (!is_.eof()) {
        DWARFS_THROW(runtime_error, "error reading plain input list");
      }

      return std::nullopt;
    }

    ++line_;

    fs::path input_path(line);

    if (input_path.has_root_directory()) {
      if (!root_.has_root_directory()) {
        auto const root_desc =
            root_.empty()
                ? std::string{"empty"}
                : fmt::format("'{}'", path_to_utf8_string_sanitized(root_));
        DWARFS_THROW(runtime_error,
                     fmt::format("absolute paths in input list require "
                                 "absolute input path, but input path is {}",
                                 root_desc));
      }

      input_path = rebase(input_path);
    }

    fs::path relative;

    if (input_path.has_root_directory()) {
      auto const normalized = input_path.lexically_normal();

      if (std::ranges::mismatch(normalized, root_).in2 != root_.end()) {
        LOG_ERROR << "input list line " << line_ << ": path '"
                  << path_to_utf8_string_sanitized(input_path)
                  << "' is not below '" << path_to_utf8_string_sanitized(root_)
                  << "'";
        ++errors_;
        continue;
      }

      relative = normalized.lexically_relative(root_);
    } else {
      relative = input_path.lexically_normal();
    }

    if (!relative.has_filename()) {
      relative = relative.parent_path();
    }

    // Root records are ordinary reader results. The scanner accepts one
    // only as the first entry and rejects all subsequent root records.
    if (relative == ".") {
      return input_entry{fs::path{"."}, root_, std::nullopt};
    }

    if (relative.empty() || relative.is_absolute() ||
        std::ranges::any_of(relative,
                            [](auto const& c) { return c == ".."; })) {
      LOG_ERROR << "input list line " << line_ << ": invalid path '"
                << path_to_utf8_string_sanitized(input_path) << "'";
      ++errors_;
      continue;
    }

    // Emit each missing ancestor once, since the scanner won't autovivify.
    fs::path ancestor;

    for (auto const& part : relative.parent_path()) {
      ancestor /= part;
      if (emitted_parents_.insert(ancestor.generic_string()).second) {
        pending_.push_back({ancestor, root_ / ancestor, std::nullopt});
      }
    }

    auto const source = root_ / relative;
    emitted_parents_.insert(relative.generic_string());

    pending_.push_back({relative, source, std::nullopt});
  }
}

class gen_init_cpio_impl final : public input_entry_reader {
 public:
  gen_init_cpio_impl(entry_reader_stream input, os_access const& os,
                     logger& lgr, fs::path const& root)
      : LOG_PROXY_INIT(lgr)
      , input_{std::move(input)}
      , is_{get_input_stream(input_)}
      , os_{os}
      , root_{root} {}

  std::optional<input_entry> next() override;

  std::optional<input_entry> implicit_root() const override {
    return std::nullopt;
  }

  std::size_t error_count() const noexcept override { return errors_; }

 private:
  [[noreturn]] void invalid(std::string_view reason) const {
    DWARFS_THROW(runtime_error, fmt::format("gen_init_cpio line {}: {}",
                                            line_number_, reason));
  }

  std::uint32_t
  numeric(std::string const& s, int base, std::string_view field) const;
  fs::path normalized_path(std::string const& s) const;
  std::string expand_variables(std::string const& value);

  LOG_PROXY_DECL(debug_logger_policy);
  entry_reader_stream input_;
  std::istream& is_;
  os_access const& os_;
  fs::path root_;
  std::size_t line_number_{0};
  std::size_t errors_{0};
};

std::uint32_t gen_init_cpio_impl::numeric(std::string const& s, int base,
                                          std::string_view field) const {
  std::uint32_t value{};
  auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), value, base);

  if (ec != std::errc{} || end != s.data() + s.size()) {
    invalid(fmt::format("invalid {} '{}'", field, s));
  }

  return value;
}

fs::path gen_init_cpio_impl::normalized_path(std::string const& s) const {
  auto result = fs::path{s}.relative_path().lexically_normal();

  if (!result.has_filename()) {
    result = result.parent_path();
  }

  if (result.empty() || result == "." ||
      std::ranges::any_of(
          result, [](auto const& component) { return component == ".."; })) {
    invalid(fmt::format("invalid image path '{}'", s));
  }

  return result;
}

std::string gen_init_cpio_impl::expand_variables(std::string const& value) {
  std::string expanded;

  for (std::size_t i = 0; i < value.size();) {
    if (value.compare(i, 2, "${") != 0) {
      expanded.push_back(value[i++]);
      continue;
    }

    auto const end = value.find('}', i + 2);

    if (end == std::string::npos || end == i + 2) {
      invalid("unterminated or empty environment variable");
    }

    auto const name = value.substr(i + 2, end - (i + 2));

    if (auto replacement = os_.getenv(name)) {
      expanded += *replacement;
    } else {
      // Linux gen_init_cpio silently substitutes an empty string.
      // Keep that substitution, but log and count the missing variable.
      LOG_ERROR << "gen_init_cpio line " << line_number_
                << ": environment variable '" << name << "' is not set";
      ++errors_;
    }

    i = end + 1;
  }

  return expanded;
}

std::optional<input_entry> gen_init_cpio_impl::next() {
  // gen_init_cpio uses whitespace-delimited records.
  // Quoted whitespace in paths is not part of the kernel format.

  std::string line;

  while (std::getline(is_, line)) {
    ++line_number_;

    auto const first = line.find_first_not_of(" \t\r");

    if (first == std::string::npos || line[first] == '#') {
      continue;
    }

    std::istringstream fields(line.substr(first));
    std::vector<std::string> args;

    for (std::string arg; fields >> arg;) {
      args.push_back(std::move(arg));
    }

    if (args.empty()) {
      continue;
    }

    auto const& kind = args.front();
    bool const is_file = kind == "file";
    bool const is_link = kind == "slink";
    bool const is_node = kind == "nod";
    std::size_t const expected = is_file ? 6 : is_link ? 6 : is_node ? 8 : 5;

    if ((is_file ? args.size() < expected : args.size() != expected) ||
        (!is_file && !is_link && !is_node && kind != "dir" && kind != "pipe" &&
         kind != "sock")) {
      invalid(fmt::format("invalid '{}' record or field count", kind));
    }

    auto const attr_ix = is_file || is_link ? 3 : 2;
    auto const permissions = numeric(args[attr_ix], 8, "mode");

    if (permissions > 07777) {
      invalid("mode contains bits outside 07777");
    }

    input_entry_spec spec{
        .content = input_entry_spec::directory{},
        .attributes =
            {
                .permissions = permissions,
                .uid = numeric(args[attr_ix + 1], 10, "uid"),
                .gid = numeric(args[attr_ix + 2], 10, "gid"),
            },
    };

    if (is_file) {
      input_entry_spec::file file{
          .source =
              resolve_source_path(root_, fs::path{expand_variables(args[2])}),
          .aliases = {},
      };
      file.aliases.reserve(args.size() - 6);
      for (std::size_t i = 6; i < args.size(); ++i) {
        file.aliases.push_back(normalized_path(args[i]));
      }
      spec.content = std::move(file);
    } else if (is_link) {
      spec.content = input_entry_spec::symlink{args[2]};
    } else if (is_node) {
      if (args[5] != "b" && args[5] != "c") {
        invalid("device type must be 'b' or 'c'");
      }
      spec.content = input_entry_spec::device{
          .type = args[5] == "c" ? posix_file_type::character
                                 : posix_file_type::block,
          .major = numeric(args[6], 10, "major"),
          .minor = numeric(args[7], 10, "minor"),
      };
    } else if (kind == "pipe" || kind == "sock") {
      spec.content = input_entry_spec::special{
          kind == "pipe" ? posix_file_type::fifo : posix_file_type::socket};
    }

    auto const logical = kind == "dir" && args[1] == "/"
                             ? fs::path{"."}
                             : normalized_path(args[1]);
    input_entry result{logical, std::nullopt, std::move(spec)};

    return result;
  }

  if (!is_.eof()) {
    DWARFS_THROW(runtime_error, "error reading gen_init_cpio input list");
  }

  return std::nullopt;
}

class jsonl_impl final : public input_entry_reader {
 public:
  using json = nlohmann::json;
  using spec = input_entry_spec;

  jsonl_impl(entry_reader_stream input, fs::path const& root)
      : input_{std::move(input)}
      , is_{get_input_stream(input_)}
      , root_{root} {}

  std::optional<input_entry> next() override;

  std::optional<input_entry> implicit_root() const override {
    return std::nullopt;
  }

  std::size_t error_count() const noexcept override { return 0; }

 private:
  input_entry parse_entry(json const& record) const;
  input_entry_attributes parse_attributes(json const& record) const;
  file_stat::timespec_type parse_timestamp(json const& value) const;
  fs::path logical_path(json const& value, bool allow_root = false) const;
  fs::path physical_path(json const& value) const;
  std::string string(json const& value, std::string_view field) const;
  std::uint32_t number(json const& value, std::string_view field) const;
  void check_fields(json const& record,
                    std::initializer_list<std::string_view> fields) const;

  entry_reader_stream input_;
  std::istream& is_;
  fs::path root_;
  std::size_t line_{0};
};

std::string
jsonl_impl::string(json const& value, std::string_view field) const {
  if (!value.is_string()) {
    DWARFS_THROW(runtime_error, fmt::format("'{}' must be a string", field));
  }

  auto result = value.get<std::string>();

  if (result.find('\0') != std::string::npos) {
    DWARFS_THROW(runtime_error, fmt::format("'{}' contains a NUL byte", field));
  }

  return result;
}

std::uint32_t
jsonl_impl::number(json const& value, std::string_view field) const {
  if (!value.is_number_integer()) {
    DWARFS_THROW(runtime_error, fmt::format("'{}' must be an integer", field));
  }

  if (value.is_number_unsigned()) {
    auto const n = value.get<std::uint64_t>();

    if (n <= std::numeric_limits<std::uint32_t>::max()) {
      return static_cast<std::uint32_t>(n);
    }
  } else {
    auto const n = value.get<std::int64_t>();

    if (n >= 0 && n <= std::numeric_limits<std::uint32_t>::max()) {
      return static_cast<std::uint32_t>(n);
    }
  }

  DWARFS_THROW(runtime_error, fmt::format("'{}' is out of range", field));
}

fs::path jsonl_impl::logical_path(json const& value, bool allow_root) const {
  auto const name = string(value, "path");

  if (allow_root && (name == "/" || name == ".")) {
    return ".";
  }

  auto path = fs::path{name}.relative_path().lexically_normal();

  if (name.empty() || path.empty() || path == "." ||
      std::ranges::any_of(path,
                          [](auto const& part) { return part == ".."; })) {
    DWARFS_THROW(runtime_error, fmt::format("invalid image path '{}'", name));
  }

  return path;
}

fs::path jsonl_impl::physical_path(json const& value) const {
  auto const name = string(value, "source");

  if (name.empty()) {
    DWARFS_THROW(runtime_error, "'source' must not be empty");
  }

  return resolve_source_path(root_, fs::path{name});
}

file_stat::timespec_type jsonl_impl::parse_timestamp(json const& value) const {
  if (value.is_array()) {
    if (value.size() != 2 || !value[0].is_number_integer()) {
      DWARFS_THROW(runtime_error,
                   "timestamp must be [integer seconds, integer nanoseconds]");
    }

    if (value[0].is_number_unsigned() &&
        value[0].get<std::uint64_t>() >
            static_cast<std::uint64_t>(
                std::numeric_limits<file_stat::time_type>::max())) {
      DWARFS_THROW(runtime_error, "timestamp seconds out of range");
    }

    auto const seconds = value[0].get<file_stat::time_type>();
    auto const nanos = number(value[1], "timestamp nanoseconds");

    if (nanos >= 1'000'000'000) {
      DWARFS_THROW(runtime_error, "timestamp nanoseconds out of range");
    }

    return {seconds, nanos};
  }

  if (value.is_string()) {
    auto const tp = parse_time_point(string(value, "timestamp"));
    auto const elapsed = tp.time_since_epoch();
    auto const seconds = std::chrono::floor<std::chrono::seconds>(elapsed);
    auto const nanos =
        std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed - seconds)
            .count();

    return {seconds.count(), static_cast<std::uint32_t>(nanos)};
  }

  if (value.is_number_integer()) {
    if (value.is_number_unsigned()) {
      auto const n = value.get<std::uint64_t>();

      if (n > static_cast<std::uint64_t>(
                  std::numeric_limits<file_stat::time_type>::max())) {
        DWARFS_THROW(runtime_error, "timestamp seconds out of range");
      }

      return {static_cast<file_stat::time_type>(n), 0};
    }

    return {value.get<file_stat::time_type>(), 0};
  }

  if (value.is_number_float()) {
    auto const seconds = static_cast<long double>(value.get<double>());
    auto const whole = std::floor(seconds);
    auto const limit = std::ldexp(1.0L, 63);

    if (!std::isfinite(seconds) || whole < -limit || whole >= limit) {
      DWARFS_THROW(runtime_error, "timestamp seconds out of range");
    }

    auto sec = static_cast<file_stat::time_type>(whole);
    auto nanos = std::llround((seconds - whole) * 1000000000.0L);

    if (nanos == 1000000000) {
      if (whole >= limit - 1) {
        DWARFS_THROW(runtime_error, "timestamp seconds out of range");
      }

      ++sec;
      nanos = 0;
    }

    return {sec, static_cast<std::uint32_t>(nanos)};
  }

  DWARFS_THROW(runtime_error, "invalid timestamp value");
}

input_entry_attributes jsonl_impl::parse_attributes(json const& record) const {
  input_entry_attributes attributes;

  if (record.contains("mode")) {
    auto const mode = string(record.at("mode"), "mode");
    std::uint32_t value{};
    auto const [end, ec] =
        std::from_chars(mode.data(), mode.data() + mode.size(), value, 8);

    if (ec != std::errc{} || end != mode.data() + mode.size() ||
        value > 07777) {
      DWARFS_THROW(runtime_error, fmt::format("invalid mode '{}'", mode));
    }

    attributes.permissions = value;
  }

  if (record.contains("uid")) {
    attributes.uid = number(record.at("uid"), "uid");
  }

  if (record.contains("gid")) {
    attributes.gid = number(record.at("gid"), "gid");
  }

  if (record.contains("atime")) {
    attributes.atime = parse_timestamp(record.at("atime"));
  }

  if (record.contains("mtime")) {
    attributes.mtime = parse_timestamp(record.at("mtime"));
  }

  if (record.contains("ctime")) {
    attributes.ctime = parse_timestamp(record.at("ctime"));
  }

  return attributes;
}

void jsonl_impl::check_fields(
    json const& record, std::initializer_list<std::string_view> fields) const {
  for (auto it = record.begin(); it != record.end(); ++it) {
    auto const& key = it.key();

    if (key == "type" || key == "path" || key == "mode" || key == "uid" ||
        key == "gid" || key == "atime" || key == "mtime" || key == "ctime" ||
        std::ranges::find(fields, std::string_view{key}) != fields.end()) {
      continue;
    }

    DWARFS_THROW(runtime_error, fmt::format("unknown field '{}'", key));
  }
}

input_entry jsonl_impl::parse_entry(json const& record) const {
  if (!record.is_object()) {
    DWARFS_THROW(runtime_error, "record must be a JSON object");
  }

  if (!record.contains("type") || !record.contains("path")) {
    DWARFS_THROW(runtime_error, "record requires 'type' and 'path'");
  }

  auto const type = string(record.at("type"), "type");
  auto const path = logical_path(record.at("path"), type == "dir");
  spec result{.content = spec::directory{},
              .attributes = parse_attributes(record)};

  if (type == "file") {
    check_fields(record, {"source", "aliases"});

    if (!record.contains("source")) {
      DWARFS_THROW(runtime_error, "file requires 'source'");
    }

    spec::file file{.source = physical_path(record.at("source"))};

    if (record.contains("aliases")) {
      auto const& aliases = record.at("aliases");

      if (!aliases.is_array()) {
        DWARFS_THROW(runtime_error, "'aliases' must be an array");
      }

      file.aliases.reserve(aliases.size());

      for (auto const& alias : aliases) {
        file.aliases.push_back(logical_path(alias));
      }
    }

    result.content = std::move(file);
  } else if (type == "dir") {
    check_fields(record, {});
  } else if (type == "symlink" || type == "hardlink") {
    check_fields(record, {"target"});

    if (!record.contains("target")) {
      DWARFS_THROW(runtime_error, "link requires 'target'");
    }

    if (type == "symlink") {
      result.content = spec::symlink{string(record.at("target"), "target")};
    } else {
      for (auto const& key :
           {"mode", "uid", "gid", "atime", "mtime", "ctime"}) {
        if (record.contains(key)) {
          DWARFS_THROW(runtime_error,
                       "hardlink cannot override file attributes");
        }
      }

      result.content = spec::hardlink{logical_path(record.at("target"))};
    }
  } else if (type == "char" || type == "block") {
    check_fields(record, {"major", "minor"});

    if (!record.contains("major") || !record.contains("minor")) {
      DWARFS_THROW(runtime_error,
                   fmt::format("{} requires 'major' and 'minor'", type));
    }

    result.content = spec::device{
        .type = type == "char" ? posix_file_type::character
                               : posix_file_type::block,
        .major = number(record.at("major"), "major"),
        .minor = number(record.at("minor"), "minor"),
    };
  } else if (type == "fifo" || type == "socket") {
    check_fields(record, {});
    result.content = spec::special{type == "fifo" ? posix_file_type::fifo
                                                  : posix_file_type::socket};
  } else {
    DWARFS_THROW(runtime_error, fmt::format("unknown entry type '{}'", type));
  }

  return {.path = path, .source = std::nullopt, .spec = std::move(result)};
}

std::optional<input_entry> jsonl_impl::next() {
  std::string line;

  while (std::getline(is_, line)) {
    ++line_;

    if (line.find_first_not_of(" \t\r") == std::string::npos) {
      continue;
    }

    try {
      std::unordered_set<std::string> keys;
      auto callback = [&keys](int depth, json::parse_event_t event,
                              json& value) {
        if (depth == 1 && event == json::parse_event_t::key &&
            !keys.insert(value.get<std::string>()).second) {
          DWARFS_THROW(runtime_error, "duplicate JSON object key");
        }
        return true;
      };

      return parse_entry(json::parse(line, callback));
    } catch (std::exception const& e) {
      DWARFS_THROW(runtime_error,
                   fmt::format("jsonl line {}: {}", line_, e.what()));
    }
  }

  if (!is_.eof()) {
    DWARFS_THROW(runtime_error, "error reading jsonl input list");
  }

  return std::nullopt;
}

} // namespace

std::span<std::string_view const> input_entry_reader_factory::names() noexcept {
  return kReaderNames;
}

std::unique_ptr<input_entry_reader>
input_entry_reader_factory::create(std::string_view name, logger& lgr,
                                   os_access const& os,
                                   entry_reader_stream input,
                                   fs::path const& canonical_input_root) {
  if (name == kPlainReaderName) {
    return std::make_unique<plain_impl>(std::move(input), canonical_input_root,
                                        os, lgr);
  }

  if (name == kGenInitCpioReaderName) {
    return std::make_unique<gen_init_cpio_impl>(std::move(input), os, lgr,
                                                canonical_input_root);
  }

  if (name == kJsonlReaderName) {
    return std::make_unique<jsonl_impl>(std::move(input), canonical_input_root);
  }

  DWARFS_THROW(runtime_error,
               fmt::format("invalid input list format '{}'", name));
}

} // namespace dwarfs::writer
