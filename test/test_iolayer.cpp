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

#include <array>
#include <filesystem>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>

#include <fmt/format.h>

#include <dwarfs/file_access_generic.h>
#include <dwarfs/terminal_ansi.h>

#include "test_helpers.h"

namespace dwarfs::test {

namespace {

// Classification of an openmode per C++23 [filebuf.members]. Anything not in
// that table is invalid. std::stringbuf only understands in/out/ate, so we
// resolve everything else here and drive the stringbuf accordingly.
struct resolved_mode {
  bool valid = false;
  bool read = false;
  bool write = false;
  bool truncate = false;   // seed the buffer with "" instead of existing data
  bool at_end = false;     // initial put position at end (ate, or append)
  bool append = false;     // every write forced to end, ignoring seekp
  bool must_exist = false; // 'r' family: fail if the file is absent
  bool exclusive = false;  // noreplace: fail if the file exists
};

resolved_mode resolve_mode(std::ios_base::openmode m) {
  using io = std::ios_base;
  auto const none = io::openmode{};

  bool const ate = (m & io::ate) != none;
  bool const excl = (m & io::noreplace) != none;
  auto const base = m & ~(io::ate | io::noreplace);

  auto const in = io::in, out = io::out, trunc = io::trunc, app = io::app;

  resolved_mode r;
  if (base == out || base == (out | trunc)) { // w
    r = {.valid = true, .write = true, .truncate = true};
  } else if (base == app || base == (out | app)) { // a
    r = {.valid = true, .write = true, .at_end = true, .append = true};
  } else if (base == in) { // r
    r = {.valid = true, .read = true, .must_exist = true};
  } else if (base == (in | out)) { // r+
    r = {.valid = true, .read = true, .write = true, .must_exist = true};
  } else if (base == (in | out | trunc)) { // w+
    r = {.valid = true, .read = true, .write = true, .truncate = true};
  } else if (base == (in | app) || base == (in | out | app)) { // a+
    r = {.valid = true,
         .read = true,
         .write = true,
         .at_end = true,
         .append = true};
  } else {
    return {}; // not in the table -> invalid
  }

  if (ate) {
    r.at_end = true;
  }

  if (excl) {
    // noreplace is only meaningful for the creating, truncating modes.
    if (r.must_exist || r.append) {
      return {};
    }
    r.exclusive = true;
  }

  return r;
}

// A stringbuf that emulates O_APPEND: the put area is pinned to the end of the
// buffer, so writes always land at EOF regardless of seekp(). Reads (the get
// area) are unaffected, which is what a+ needs. For non-append modes this is
// just a plain stringbuf.
class mem_stringbuf : public std::stringbuf {
 public:
  mem_stringbuf(std::string init, std::ios_base::openmode mode, bool append)
      : std::stringbuf(std::move(init), mode)
      , append_{append} {}

 protected:
  pos_type seekpos(pos_type sp, std::ios_base::openmode which) override {
    if (auto p = ignore_output_seek(which)) {
      return *p;
    }
    return std::stringbuf::seekpos(sp, which);
  }

  pos_type seekoff(off_type off, std::ios_base::seekdir way,
                   std::ios_base::openmode which) override {
    if (auto p = ignore_output_seek(which)) {
      return *p;
    }
    return std::stringbuf::seekoff(off, way, which);
  }

 private:
  // If this is a pure output seek in append mode, report the current end of the
  // put area without moving anything (a valid position, so the stream does not
  // set failbit). Returns nullopt to fall through to the base implementation.
  std::optional<pos_type> ignore_output_seek(std::ios_base::openmode& which) {
    if (append_ && (which & std::ios_base::out) != std::ios_base::openmode{}) {
      which &= ~std::ios_base::out; // never reposition the put area
      if (which == std::ios_base::openmode{}) {
        return pos_type(off_type(pptr() - pbase()));
      }
    }
    return std::nullopt;
  }

  bool append_;
};

class test_input_stream : public input_stream {
 public:
  explicit test_input_stream(std::filesystem::path const& path,
                             std::string content, std::error_code& ec,
                             test_file_access const* tfa)
      : path_{path}
      , tfa_{tfa} {
    if (auto error = tfa_->get_open_error(path_)) {
      ec = error.value();
    }
    is_.str(std::move(content));
  }

  std::istream& is() override { return is_; }

  void close(std::error_code& ec) override {
    if (auto error = tfa_->get_close_error(path_)) {
      ec = error.value();
    }
  }

  void close() override {}

 private:
  std::istringstream is_;
  std::filesystem::path path_;
  test_file_access const* tfa_;
};

class test_output_stream : public output_stream {
 public:
  test_output_stream(std::filesystem::path const& path, std::error_code& ec,
                     test_file_access const* tfa)
      : path_{path}
      , tfa_{tfa} {
    if (path_.empty()) {
      ec = std::make_error_code(std::errc::no_such_file_or_directory);
    }
    if (auto error = tfa_->get_open_error(path_)) {
      ec = error.value();
    }
  }

  std::ostream& os() override { return os_; }

  void close(std::error_code& ec) override {
    if (auto error = tfa_->get_close_error(path_)) {
      ec = error.value();
    } else {
      tfa_->set_file(path_, os_.str());
    }
  }

  void close() override {
    std::error_code ec;
    close(ec);
    if (ec) {
      throw std::system_error(ec, fmt::format("close('{}')", path_.string()));
    }
  }

 private:
  std::ostringstream os_;
  std::filesystem::path path_;
  test_file_access const* tfa_;
};

class test_input_output_stream : public input_output_stream {
 public:
  test_input_output_stream(std::filesystem::path const& path,
                           std::ios_base::openmode mode, std::error_code& ec,
                           test_file_access const* tfa)
      : path_{path}
      , tfa_{tfa}
      , ios_{nullptr} {
    ec.clear();

    if (path_.empty()) {
      ec = std::make_error_code(std::errc::no_such_file_or_directory);
      return;
    }

    if (auto error = tfa_->get_open_error(path_)) {
      ec = error.value();
      return;
    }

    auto const rm = resolve_mode(mode);
    if (!rm.valid) {
      ec = std::make_error_code(std::errc::invalid_argument);
      return;
    }

    auto content = tfa_->get_file(path_);

    if (rm.exclusive && content) {
      ec = std::make_error_code(std::errc::file_exists);
      return;
    }

    if (rm.must_exist && !content) {
      ec = std::make_error_code(std::errc::no_such_file_or_directory);
      return;
    }

    // Build a sanitized mode the stringbuf actually understands.
    auto sm = std::ios_base::openmode{};
    if (rm.read) {
      sm |= std::ios_base::in;
    }
    if (rm.write) {
      sm |= std::ios_base::out;
    }
    if (rm.at_end) {
      sm |= std::ios_base::ate;
    }

    std::string seed = rm.truncate ? std::string{} : content.value_or("");

    buf_ = std::make_unique<mem_stringbuf>(std::move(seed), sm, rm.append);
    ios_.rdbuf(buf_.get());
  }

  std::iostream& ios() override { return ios_; }

  void close(std::error_code& ec) override {
    ec.clear();
    if (auto error = tfa_->get_close_error(path_)) {
      ec = error.value();
    } else if (buf_) {
      tfa_->set_file(path_, buf_->str());
    }
  }

  void close() override {
    std::error_code ec;
    close(ec);
    if (ec) {
      throw std::system_error(ec, fmt::format("close('{}')", path_.string()));
    }
  }

 private:
  std::filesystem::path path_;
  test_file_access const* tfa_;
  std::unique_ptr<mem_stringbuf> buf_;
  std::iostream ios_;
};

} // namespace

bool test_file_access::exists(std::filesystem::path const& path) const {
  return files_.find(path) != files_.end();
}

std::unique_ptr<input_stream>
test_file_access::open_input(std::filesystem::path const& path,
                             std::error_code& ec) const {
  ec.clear();
  auto it = files_.find(path);
  if (it != files_.end()) {
    return std::make_unique<test_input_stream>(path, it->second, ec, this);
  }
  ec = std::make_error_code(std::errc::no_such_file_or_directory);
  return nullptr;
}

std::unique_ptr<input_stream>
test_file_access::open_input(std::filesystem::path const& path) const {
  std::error_code ec;
  auto rv = open_input(path, ec);
  if (ec) {
    throw std::system_error(ec, fmt::format("open_input('{}')", path.string()));
  }
  return rv;
}

std::unique_ptr<input_stream>
test_file_access::open_input_binary(std::filesystem::path const& path,
                                    std::error_code& ec) const {
  return open_input(path, ec);
}

std::unique_ptr<input_stream>
test_file_access::open_input_binary(std::filesystem::path const& path) const {
  std::error_code ec;
  auto rv = open_input_binary(path, ec);
  if (ec) {
    throw std::system_error(
        ec, fmt::format("open_input_binary('{}')", path.string()));
  }
  return rv;
}

std::unique_ptr<output_stream>
test_file_access::open_output(std::filesystem::path const& path,
                              std::error_code& ec) const {
  ec.clear();
  auto rv = std::make_unique<test_output_stream>(path, ec, this);
  if (ec) {
    rv.reset();
  }
  return rv;
}

std::unique_ptr<output_stream>
test_file_access::open_output(std::filesystem::path const& path) const {
  std::error_code ec;
  auto rv = open_output(path, ec);
  if (ec) {
    throw std::system_error(ec,
                            fmt::format("open_output('{}')", path.string()));
  }
  return rv;
}

std::unique_ptr<output_stream>
test_file_access::open_output_binary(std::filesystem::path const& path,
                                     std::error_code& ec) const {
  ec.clear();
  auto rv = std::make_unique<test_output_stream>(path, ec, this);
  if (ec) {
    rv.reset();
  }
  return rv;
}

std::unique_ptr<output_stream>
test_file_access::open_output_binary(std::filesystem::path const& path) const {
  std::error_code ec;
  auto rv = open_output_binary(path, ec);
  if (ec) {
    throw std::system_error(
        ec, fmt::format("open_output_binary('{}')", path.string()));
  }
  return rv;
}

std::unique_ptr<input_output_stream>
test_file_access::open(std::filesystem::path const& path,
                       std::ios_base::openmode mode,
                       std::error_code& ec) const {
  ec.clear();

  auto rv = std::make_unique<test_input_output_stream>(path, mode, ec, this);

  if (ec) {
    rv.reset();
  }

  return rv;
}

std::unique_ptr<input_output_stream>
test_file_access::open(std::filesystem::path const& path,
                       std::ios_base::openmode mode) const {
  std::error_code ec;
  auto rv = open(path, mode, ec);
  if (ec) {
    throw std::system_error(
        ec, fmt::format("open_input_binary('{}')", path.string()));
  }
  return rv;
}

void test_file_access::set_file(std::filesystem::path const& path,
                                std::string content) const {
  files_[path] = std::move(content);
}

void test_file_access::set_open_error(std::filesystem::path const& path,
                                      std::error_code ec) const {
  open_errors_[path] = ec;
}

void test_file_access::set_close_error(std::filesystem::path const& path,
                                       std::error_code ec) const {
  close_errors_[path] = ec;
}

std::optional<std::error_code>
test_file_access::get_open_error(std::filesystem::path const& path) const {
  if (auto it = open_errors_.find(path); it != open_errors_.end()) {
    return it->second;
  }
  return std::nullopt;
}

std::optional<std::error_code>
test_file_access::get_close_error(std::filesystem::path const& path) const {
  if (auto it = close_errors_.find(path); it != close_errors_.end()) {
    return it->second;
  }
  return std::nullopt;
}

std::optional<std::string>
test_file_access::get_file(std::filesystem::path const& path) const {
  auto it = files_.find(path);
  if (it != files_.end()) {
    return it->second;
  }
  return std::nullopt;
}

test_terminal::test_terminal() = default;

size_t test_terminal::width() const { return width_; }

bool test_terminal::is_tty(std::ostream& /*os*/) const { return is_tty_; }

bool test_terminal::is_fancy() const { return fancy_; }

std::string_view test_terminal::carriage_return() const { return "<cr>"; }

std::string_view test_terminal::rewind_line() const { return "<rewind>"; }

std::string_view test_terminal::clear_line() const { return "<clear>"; }

std::string_view test_terminal::color(termcolor color, termstyle style) const {
  static constexpr std::array<std::string_view,
                              static_cast<size_t>(termcolor::NUM_COLORS)>
      // clang-format off
      colors = {{
          "<normal>",
          "<black>",
          "<red>",
          "<green>",
          "<yellow>",
          "<blue>",
          "<magenta>",
          "<cyan>",
          "<white>",
          "<gray>",
          "<bold-black>",
          "<bold-red>",
          "<bold-green>",
          "<bold-yellow>",
          "<bold-blue>",
          "<bold-magenta>",
          "<bold-cyan>",
          "<bold-white>",
          "<bold-gray>",
          "<dim-black>",
          "<dim-red>",
          "<dim-green>",
          "<dim-yellow>",
          "<dim-blue>",
          "<dim-magenta>",
          "<dim-cyan>",
          "<dim-white>",
          "<dim-gray>",
      }}; // clang-format on

  static constexpr size_t const kBoldOffset{
      static_cast<size_t>(termcolor::BOLD_RED) -
      static_cast<size_t>(termcolor::RED)};
  static constexpr size_t const kDimOffset{
      static_cast<size_t>(termcolor::DIM_RED) -
      static_cast<size_t>(termcolor::RED)};

  switch (style) {
  case termstyle::BOLD:
  case termstyle::DIM: {
    auto ix = static_cast<size_t>(color);
    if (ix < static_cast<size_t>(termcolor::BOLD_RED)) {
      color = static_cast<termcolor>(
          ix + (style == termstyle::BOLD ? kBoldOffset : kDimOffset));
    }
  } break;

  default:
    break;
  }

  return colors.at(static_cast<size_t>(color));
}

std::string_view test_terminal::bgcolor(termcolor color) const {
  static constexpr std::array<std::string_view,
                              static_cast<size_t>(termcolor::NUM_COLORS)>
      // clang-format off
      colors = {{
          "<bg-normal>",
          "<bg-black>",
          "<bg-red>",
          "<bg-green>",
          "<bg-yellow>",
          "<bg-blue>",
          "<bg-magenta>",
          "<bg-cyan>",
          "<bg-white>",
          "<bg-gray>",
          "<bg-bright-black>",
          "<bg-bright-red>",
          "<bg-bright-green>",
          "<bg-bright-yellow>",
          "<bg-bright-blue>",
          "<bg-bright-magenta>",
          "<bg-bright-cyan>",
          "<bg-bright-white>",
          "<bg-bright-gray>",
          "<bg-dim-black>",
          "<bg-dim-red>",
          "<bg-dim-green>",
          "<bg-dim-yellow>",
          "<bg-dim-blue>",
          "<bg-dim-magenta>",
          "<bg-dim-cyan>",
          "<bg-dim-white>",
          "<bg-dim-gray>",
      }}; // clang-format on

  return colors.at(static_cast<size_t>(color));
}

std::string test_terminal::colored(std::string text, termcolor color,
                                   bool enable, termstyle style) const {
  std::string result;

  if (enable) {
    auto preamble = this->color(color, style);
    auto postamble = this->color(termcolor::NORMAL, termstyle::NORMAL);

    result.reserve(preamble.size() + text.size() + postamble.size());
    result.append(preamble);
    result.append(text);
    result.append(postamble);
  } else {
    result.append(text);
  }

  return result;
}

test_iolayer::test_iolayer()
    : test_iolayer{os_access_mock::create_test_instance()} {}

test_iolayer::test_iolayer(std::shared_ptr<os_access const> os)
    : test_iolayer{std::move(os), create_file_access_generic()} {}

test_iolayer::test_iolayer(std::shared_ptr<os_access const> os,
                           std::shared_ptr<file_access const> fa)
    : os_{std::move(os)}
    , term_{std::make_shared<test_terminal>()}
    , fa_{std::move(fa)} {}

test_iolayer::~test_iolayer() = default;

tool::iolayer const& test_iolayer::get() {
  if (!iol_) {
    if (real_term_) {
      iol_ = std::make_unique<tool::iolayer>(tool::iolayer{
          .os = os_,
          .term = real_term_,
          .file = fa_,
          .in = std::cin,
          .out = std::cout,
          .err = std::cerr,
      });
    } else {
      iol_ = std::make_unique<tool::iolayer>(tool::iolayer{
          .os = os_,
          .term = term_,
          .file = fa_,
          .in = in_,
          .out = out_,
          .err = err_,
      });
    }
  }
  return *iol_;
}

void test_iolayer::use_real_terminal(bool use) {
  if (use) {
    real_term_ = std::make_shared<terminal_ansi>();
  } else {
    real_term_.reset();
  }
}

void test_iolayer::set_terminal_is_tty(bool is_tty) {
  term_->set_is_tty(is_tty);
}
void test_iolayer::set_terminal_fancy(bool fancy) { term_->set_fancy(fancy); }
void test_iolayer::set_terminal_width(size_t width) { term_->set_width(width); }
void test_iolayer::set_in(std::string in) { in_.str(std::move(in)); }

std::string test_iolayer::out() const { return out_.str(); }
std::string test_iolayer::err() const { return err_.str(); }

} // namespace dwarfs::test
