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
 */

#include <array>
#include <iostream>

#include <fmt/format.h>

#include "dwarfs/file_access_generic.h"

#include "test_helpers.h"

namespace dwarfs::test {

namespace {

class test_input_stream : public input_stream {
 public:
  test_input_stream(std::string content) { is_.str(std::move(content)); }

  std::istream& is() override { return is_; }

  void close(std::error_code& /*ec*/) override {}

  void close() override {}

 private:
  std::istringstream is_;
};

class test_output_stream : public output_stream {
 public:
  test_output_stream(std::filesystem::path const& path, std::error_code& ec,
                     test_file_access const* tfa)
      : path_{path}
      , tfa_{tfa} {
    if (path_.empty()) {
      ec = std::make_error_code(std::errc::invalid_argument);
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

} // namespace

bool test_file_access::exists(std::filesystem::path const& path) const {
  return files_.find(path) != files_.end();
}

std::unique_ptr<input_stream>
test_file_access::open_input(std::filesystem::path const& path,
                             std::error_code& ec) const {
  auto it = files_.find(path);
  if (it != files_.end()) {
    return std::make_unique<test_input_stream>(it->second);
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

test_terminal::test_terminal(std::ostream& out, std::ostream& err)
    : out_{&out}
    , err_{&err} {}

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
          "<red>",
          "<green>",
          "<yellow>",
          "<blue>",
          "<magenta>",
          "<cyan>",
          "<white>",
          "<gray>",
          "<bold-red>",
          "<bold-green>",
          "<bold-yellow>",
          "<bold-blue>",
          "<bold-magenta>",
          "<bold-cyan>",
          "<bold-white>",
          "<bold-gray>",
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
    , term_{std::make_shared<test_terminal>(out_, err_)}
    , fa_{std::move(fa)} {}

test_iolayer::~test_iolayer() = default;

iolayer const& test_iolayer::get() {
  if (!iol_) {
    if (real_term_) {
      iol_ = std::make_unique<iolayer>(iolayer{
          .os = os_,
          .term = real_term_,
          .file = fa_,
          .in = std::cin,
          .out = std::cout,
          .err = std::cerr,
      });
    } else {
      iol_ = std::make_unique<iolayer>(iolayer{
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
  real_term_ = terminal::create();
}

void test_iolayer::set_terminal_is_tty(bool is_tty) {
  term_->set_is_tty(is_tty);
}
void test_iolayer::set_terminal_fancy(bool fancy) { term_->set_fancy(fancy); }
void test_iolayer::set_terminal_width(size_t width) { term_->set_width(width); }
void test_iolayer::set_in(std::string in) { in_.str(std::move(in)); }

std::string test_iolayer::out() const { return out_.str(); }
std::string test_iolayer::err() const { return err_.str(); }

void test_iolayer::set_os_access(std::shared_ptr<os_access_mock> os) {
  if (iol_) {
    throw std::runtime_error("iolayer already created");
  }
  os_ = std::move(os);
}

void test_iolayer::set_file_access(std::shared_ptr<file_access const> fa) {
  if (iol_) {
    throw std::runtime_error("iolayer already created");
  }
  fa_ = std::move(fa);
}

} // namespace dwarfs::test
