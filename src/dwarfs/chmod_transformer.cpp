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

#include <charconv>
#include <filesystem>
#include <vector>

#include <fmt/format.h>

#include "dwarfs/chmod_transformer.h"

namespace dwarfs {

namespace fs = std::filesystem;

namespace {

using mode_type = chmod_transformer::mode_type;

constexpr mode_type const kSetUidBit{
    static_cast<mode_type>(fs::perms::set_uid)};
constexpr mode_type const kSetGidBit{
    static_cast<mode_type>(fs::perms::set_gid)};
constexpr mode_type const kStickyBit{
    static_cast<mode_type>(fs::perms::sticky_bit)};
constexpr mode_type const kUserReadBit{
    static_cast<mode_type>(fs::perms::owner_read)};
constexpr mode_type const kUserWriteBit{
    static_cast<mode_type>(fs::perms::owner_write)};
constexpr mode_type const kUserExecBit{
    static_cast<mode_type>(fs::perms::owner_exec)};
constexpr mode_type const kGroupReadBit{
    static_cast<mode_type>(fs::perms::group_read)};
constexpr mode_type const kGroupWriteBit{
    static_cast<mode_type>(fs::perms::group_write)};
constexpr mode_type const kGroupExecBit{
    static_cast<mode_type>(fs::perms::group_exec)};
constexpr mode_type const kOtherReadBit{
    static_cast<mode_type>(fs::perms::others_read)};
constexpr mode_type const kOtherWriteBit{
    static_cast<mode_type>(fs::perms::others_write)};
constexpr mode_type const kOtherExecBit{
    static_cast<mode_type>(fs::perms::others_exec)};

constexpr mode_type const kAllUidBits{kSetUidBit | kSetGidBit};
constexpr mode_type const kAllUserBits{kUserReadBit | kUserWriteBit |
                                       kUserExecBit};
constexpr mode_type const kAllGroupBits{kGroupReadBit | kGroupWriteBit |
                                        kGroupExecBit};
constexpr mode_type const kAllOtherBits{kOtherReadBit | kOtherWriteBit |
                                        kOtherExecBit};
constexpr mode_type const kAllReadBits{kUserReadBit | kGroupReadBit |
                                       kOtherReadBit};
constexpr mode_type const kAllWriteBits{kUserWriteBit | kGroupWriteBit |
                                        kOtherWriteBit};
constexpr mode_type const kAllExecBits{kUserExecBit | kGroupExecBit |
                                       kOtherExecBit};
constexpr mode_type const kAllRWXBits{kAllReadBits | kAllWriteBits |
                                      kAllExecBits};
constexpr mode_type const kAllModeBits{kAllUidBits | kStickyBit | kAllUserBits |
                                       kAllGroupBits | kAllOtherBits};

enum class opmode { normal, promote_exec, copy_from };

struct modifier {
  char oper;
  opmode mode;
  mode_type whom;
  mode_type bits;
  mode_type mask;
};

class chmod_transformer_ : public chmod_transformer::impl {
 public:
  chmod_transformer_(std::string_view spec, mode_type umask);

  std::optional<mode_type> transform(mode_type mode, bool isdir) const override;

 private:
  std::optional<mode_type> parse_oct(std::string_view& spec);
  std::optional<mode_type> parse_whom(std::string_view& spec);
  static constexpr bool is_op(char c) {
    return c == '=' or c == '+' or c == '-';
  }
  static constexpr bool is_ugo(char c) {
    return c == 'u' or c == 'g' or c == 'o';
  }

  std::vector<modifier> modifiers_;
  bool flag_D_{false};
  bool flag_F_{false};
  mode_type const umask_;
};

chmod_transformer_::chmod_transformer_(std::string_view spec, mode_type umask)
    : umask_{umask} {
  // This is roughly following the implementation of chmod(1) from GNU coreutils

  if (spec.empty()) {
    throw std::invalid_argument("empty mode");
  }

  auto orig_spec{spec};

  if ('0' <= spec[0] and spec[0] <= '7') {
    // octal mode
    auto mode = parse_oct(spec);
    if (!mode or !spec.empty()) {
      throw std::invalid_argument(fmt::format("invalid mode: {}", orig_spec));
    }
    mode_type mask{spec.size() > 4 ? kAllModeBits
                                   : (mode.value() & kAllUidBits) | kStickyBit |
                                         kAllRWXBits};
    modifiers_.push_back(
        {'=', opmode::normal, kAllModeBits, mode.value(), mask});
    return;
  }

  // symbolic mode

  auto whom = parse_whom(spec);
  if (!whom) {
    throw std::invalid_argument(fmt::format("invalid mode: {}", orig_spec));
  }

  mode_type const mask{whom.value() ? whom.value() : kAllModeBits};

  while (!spec.empty() and is_op(spec.front())) {
    auto op = spec.front();
    spec.remove_prefix(1);

    if (spec.empty()) {
      throw std::invalid_argument(fmt::format("invalid mode: {}", orig_spec));
    }

    if (auto mode = parse_oct(spec); mode) {
      if (whom.value() or !spec.empty()) {
        throw std::invalid_argument(fmt::format("invalid mode: {}", orig_spec));
      }
      modifiers_.push_back(
          {op, opmode::normal, kAllModeBits, mode.value(), kAllModeBits});
      break;
    }

    if (is_ugo(spec.front())) {
      mode_type bits{};

      switch (spec.front()) {
      case 'u':
        bits = kAllUserBits;
        break;
      case 'g':
        bits = kAllGroupBits;
        break;
      case 'o':
        bits = kAllOtherBits;
        break;
      }

      modifiers_.push_back(
          {op, opmode::copy_from, whom.value(), bits, bits & mask});
      spec.remove_prefix(1);
    } else {
      auto mode{opmode::normal};
      mode_type bits{};
      bool more{true};

      while (!spec.empty() and more) {
        switch (spec.front()) {
        case 'r':
          bits |= kAllReadBits;
          break;
        case 'w':
          bits |= kAllWriteBits;
          break;
        case 'x':
          bits |= kAllExecBits;
          break;
        case 's':
          bits |= kAllUidBits;
          break;
        case 't':
          bits |= kStickyBit;
          break;
        case 'X':
          mode = opmode::promote_exec;
          break;
        default:
          more = false;
          break;
        }

        if (more) {
          spec.remove_prefix(1);
        }
      }

      modifiers_.push_back({op, mode, whom.value(), bits, bits & mask});
    }
  }

  if (!spec.empty()) {
    throw std::invalid_argument(fmt::format("invalid mode: {}", orig_spec));
  }
}

auto chmod_transformer_::parse_oct(std::string_view& spec)
    -> std::optional<mode_type> {
  mode_type mode;
  if (auto [p, ec] =
          std::from_chars(spec.data(), spec.data() + spec.size(), mode, 8);
      ec == std::errc{} and mode <= kAllModeBits) {
    spec.remove_prefix(p - spec.data());
    return mode;
  }
  return std::nullopt;
}

auto chmod_transformer_::parse_whom(std::string_view& spec)
    -> std::optional<mode_type> {
  mode_type whom{};

  while (!spec.empty()) {
    switch (spec.front()) {
    case 'u':
      whom |= kSetUidBit | kAllUserBits;
      break;

    case 'g':
      whom |= kSetGidBit | kAllGroupBits;
      break;

    case 'o':
      whom |= kStickyBit | kAllOtherBits;
      break;

    case 'a':
      whom = kAllModeBits;
      break;

    case 'D':
      flag_D_ = true;
      break;

    case 'F':
      flag_F_ = true;
      break;

    case '=':
    case '+':
    case '-':
      return whom;

    default:
      return std::nullopt;
    }

    spec.remove_prefix(1);
  }

  return std::nullopt;
}

std::optional<mode_type>
chmod_transformer_::transform(mode_type mode, bool isdir) const {
  // skip entries for which this isn't intended
  if ((flag_D_ and !isdir) or (flag_F_ and isdir)) {
    return std::nullopt;
  }

  // This is roughly following the implementation of chmod(1) from GNU coreutils

  for (auto const& m : modifiers_) {
    mode_type omit{isdir ? kAllUidBits & ~m.mask : 0};
    auto bits = m.bits;

    switch (m.mode) {
    case opmode::normal:
      break;

    case opmode::promote_exec:
      if (isdir or (mode & kAllExecBits)) {
        bits |= kAllExecBits;
      }
      break;

    case opmode::copy_from:
      bits &= mode;
      if (bits & kAllReadBits) {
        bits |= kAllReadBits;
      }
      if (bits & kAllWriteBits) {
        bits |= kAllWriteBits;
      }
      if (bits & kAllExecBits) {
        bits |= kAllExecBits;
      }
      break;
    }

    bits &= (m.whom ? m.whom : ~umask_) & ~omit;

    switch (m.oper) {
    case '=':
      mode = (mode & ((m.whom ? ~m.whom : 0) | omit)) | bits;
      break;

    case '+':
      mode |= bits;
      break;

    case '-':
      mode &= ~bits;
      break;
    }
  }

  return mode;
}

} // namespace

chmod_transformer::chmod_transformer(std::string_view spec, mode_type umask)
    : impl_{std::make_unique<chmod_transformer_>(spec, umask)} {}

} // namespace dwarfs
