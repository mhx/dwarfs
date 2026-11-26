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

#pragma once

#include <chrono>
#include <concepts>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace dwarfs::test {

class subprocess {
 public:
  using argument_list = std::vector<std::filesystem::path>;
  using result = std::tuple<std::string, std::string, int>;

  template <typename... Args>
  explicit subprocess(std::filesystem::path const& prog, Args&&... args)
      : subprocess(prog, arguments(std::forward<Args>(args)...)) {}

  subprocess(std::filesystem::path const& prog, argument_list const& args);
  subprocess(std::filesystem::path const& prog, argument_list&& args)
      : subprocess(prog, std::as_const(args)) {}

  ~subprocess();

  subprocess(subprocess const&) = delete;
  subprocess& operator=(subprocess const&) = delete;

  void wait();
  bool wait_for(std::chrono::milliseconds timeout);
  bool interrupt();
  void terminate();

  std::string const& cmdline() const;
  std::string const& out() const;
  std::string const& err() const;
  int exit_code() const;

  template <typename... Args>
  static result run(Args&&... args) {
    subprocess p(std::forward<Args>(args)...);
    p.wait();
    return {p.out(), p.err(), p.exit_code()};
  }

  template <typename... Args>
  static std::optional<std::string> check_run(Args&&... args) {
    return check_result(run(std::forward<Args>(args)...));
  }

  template <typename... Args>
  static argument_list arguments(Args&&... args) {
    argument_list rv;
    (append_arg(rv, std::forward<Args>(args)), ...);
    return rv;
  }

 private:
  static void
  append_arg(argument_list& args, std::filesystem::path const& arg) {
    args.push_back(arg);
  }

  static void
  append_arg(argument_list& args, std::vector<std::string> const& more) {
    for (auto const& arg : more) {
      append_arg(args, arg);
    }
  }

  static void append_arg(argument_list& args, argument_list const& more) {
    args.insert(args.end(), more.begin(), more.end());
  }

  template <std::convertible_to<std::string> T>
  static void append_arg(argument_list& args, T const& arg) {
    args.emplace_back(std::string(arg));
  }

  static std::optional<std::string> check_result(result value);

  class impl;
  std::unique_ptr<impl> impl_;
};

} // namespace dwarfs::test
