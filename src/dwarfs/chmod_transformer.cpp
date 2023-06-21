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

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <tuple>

// #include <sys/stat.h>

#include <fmt/format.h>

#include "dwarfs/chmod_transformer.h"
#include "dwarfs/entry_interface.h"

namespace dwarfs {

namespace fs = std::filesystem;

namespace {

uint16_t constexpr all_perm_bits = 07777;
uint16_t constexpr all_exec_bits = uint16_t(
    fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec);

enum class oper { NONE, ADD_BITS, SUB_BITS, SET_BITS, OCT_BITS };

std::tuple<uint16_t, uint16_t>
compute_perm_and_or(oper op, uint16_t hi_bits, uint16_t setid_bits,
                    uint16_t affected, uint16_t perms, uint16_t umask) {
  uint16_t op_bits = hi_bits;
  uint16_t perm_and;
  uint16_t perm_or;

  if (affected) {
    op_bits |= affected * perms;
  } else {
    affected = all_exec_bits;
    op_bits |= (affected * perms) & ~umask;
  }

  switch (op) {
  case oper::ADD_BITS:
    perm_and = all_perm_bits;
    perm_or = op_bits;
    break;

  case oper::SUB_BITS:
    perm_and = all_perm_bits & ~op_bits;
    perm_or = 0;
    break;

  case oper::SET_BITS:
    perm_and = all_perm_bits &
               ~((affected * uint16_t(fs::perms::others_all)) | setid_bits);
    perm_or = op_bits;
    break;

  case oper::OCT_BITS:
    perm_and = 0;
    perm_or = op_bits;
    break;

  default:
    throw std::runtime_error("missing operation in chmod expression");
  }

  return {perm_and, perm_or};
}

uint16_t modify_perms(uint16_t perm, bool isdir, uint16_t perm_and,
                      uint16_t perm_or, bool flag_X) {
  auto new_perm = perm;

  new_perm &= perm_and;

  if (!flag_X or (perm & all_exec_bits) != 0 or isdir) {
    new_perm |= perm_or;
  } else {
    new_perm |= perm_or & ~all_exec_bits;
  }

  return new_perm;
}

class permission_modifier {
 public:
  virtual ~permission_modifier() = default;

  virtual uint16_t modify(uint16_t perms, bool isdir) const = 0;
};

class static_permission_modifier : public permission_modifier {
 public:
  static_permission_modifier(uint16_t perm_and, uint16_t perm_or, bool flag_X)
      : perm_and_{perm_and}
      , perm_or_{perm_or}
      , flag_X_{flag_X} {}

  uint16_t modify(uint16_t perms, bool isdir) const override {
    return modify_perms(perms, isdir, perm_and_, perm_or_, flag_X_);
  }

 private:
  uint16_t const perm_and_;
  uint16_t const perm_or_;
  bool flag_X_;
};

class dynamic_permission_modifier : public permission_modifier {
 public:
  dynamic_permission_modifier(oper op, uint16_t setid_bits, uint16_t affected,
                              uint16_t perms_shift, uint16_t umask)
      : op_{op}
      , setid_bits_{setid_bits}
      , affected_{affected}
      , perms_shift_{perms_shift}
      , umask_{umask} {}

  uint16_t modify(uint16_t perms, bool isdir) const override {
    uint16_t dyn_perms = (perms >> perms_shift_) & 07;
    auto [perm_and, perm_or] =
        compute_perm_and_or(op_, 0, setid_bits_, affected_, dyn_perms, umask_);
    return modify_perms(perms, isdir, perm_and, perm_or, false);
  }

 private:
  oper const op_;
  uint16_t const setid_bits_;
  uint16_t const affected_;
  uint16_t const perms_shift_;
  uint16_t const umask_;
};

class chmod_transformer : public entry_transformer {
 public:
  chmod_transformer(std::string_view spec, uint16_t umask);

  void transform(entry_interface& ei) override;

 private:
  std::unique_ptr<permission_modifier const> modifier_;
  bool flag_D_{false};
  bool flag_F_{false};
};

chmod_transformer::chmod_transformer(std::string_view spec, uint16_t umask) {
  enum class state { PARSE_WHERE, PARSE_PERMS, PARSE_OCTAL };
  state st{state::PARSE_WHERE};
  oper op{oper::NONE};
  bool flag_X{false};
  uint16_t setid_bits{0};
  uint16_t hi_bits{0};
  uint16_t affected{0};
  uint16_t perms{0};
  std::optional<uint16_t> perms_shift;

  for (auto c : spec) {
    switch (st) {
    case state::PARSE_WHERE:
      switch (c) {
      case 'D':
        if (flag_F_) {
          throw std::runtime_error(
              "cannot combine D and F in chmod expression");
        }
        flag_D_ = true;
        break;

      case 'F':
        if (flag_D_) {
          throw std::runtime_error(
              "cannot combine D and F in chmod expression");
        }
        flag_F_ = true;
        break;

      case 'u':
        affected |= uint16_t(fs::perms::owner_exec);
        setid_bits |= uint16_t(fs::perms::set_uid);
        break;

      case 'g':
        affected |= uint16_t(fs::perms::group_exec);
        setid_bits |= uint16_t(fs::perms::set_gid);
        break;

      case 'o':
        affected |= uint16_t(fs::perms::others_exec);
        break;

      case 'a':
        affected |= all_exec_bits;
        break;

      case '+':
        op = oper::ADD_BITS;
        st = state::PARSE_PERMS;
        break;

      case '-':
        op = oper::SUB_BITS;
        st = state::PARSE_PERMS;
        break;

      case '=':
        op = oper::SET_BITS;
        st = state::PARSE_PERMS;
        break;

      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
        if (affected) {
          throw std::runtime_error(
              "unexpected octal digit in chmod expression");
        }
        affected = 1;
        perms = c - '0';
        op = oper::OCT_BITS;
        st = state::PARSE_OCTAL;
        break;

      default:
        throw std::runtime_error(
            fmt::format("unexpected character in chmod expression: {}", c));
      }
      break;

    case state::PARSE_PERMS:
      switch (c) {
      case 'r':
        perms |= uint16_t(fs::perms::others_read);
        break;

      case 'w':
        perms |= uint16_t(fs::perms::others_write);
        break;

      case 'X':
        flag_X = true;
        [[fallthrough]];

      case 'x':
        perms |= uint16_t(fs::perms::others_exec);
        break;

      case 's':
        // default to fs::perms::set_uid unless explicitly specified
        hi_bits |= setid_bits ? setid_bits : uint16_t(fs::perms::set_uid);
        break;

      case 't':
        hi_bits |= uint16_t(fs::perms::sticky_bit);
        break;

      case 'u':
      case 'g':
      case 'o':
        if (perms_shift) {
          throw std::runtime_error(
              "only one of [ugo] allowed in permission specification");
        }

        switch (c) {
        case 'u':
          perms_shift = 6;
          break;

        case 'g':
          perms_shift = 3;
          break;

        case 'o':
          perms_shift = 0;
          break;

        default:
          assert(false);
        }
        break;

      default:
        throw std::runtime_error(
            fmt::format("unexpected character in chmod expression: {}", c));
      }
      break;

    case state::PARSE_OCTAL:
      if (c < '0' || c > '7') {
        throw std::runtime_error(
            fmt::format("unexpected character in chmod expression: {}", c));
      }

      perms <<= 3;
      perms |= c - '0';

      if (perms > all_perm_bits) {
        throw std::runtime_error("octal chmod expression out of range");
      }

      break;
    }
  }

  if (perms_shift && (perms || hi_bits || flag_X)) {
    throw std::runtime_error(
        "[ugo] cannot be combined with other permission specifiers");
  }

  if (perms_shift) {
    modifier_ = std::make_unique<dynamic_permission_modifier>(
        op, setid_bits, affected, *perms_shift, umask);
  } else {
    auto [perm_and, perm_or] =
        compute_perm_and_or(op, hi_bits, setid_bits, affected, perms, umask);

    modifier_ =
        std::make_unique<static_permission_modifier>(perm_and, perm_or, flag_X);
  }
}

void chmod_transformer::transform(entry_interface& ei) {
  // skip entries for which this isn't intended
  if ((flag_D_ and !ei.is_directory()) or (flag_F_ and ei.is_directory())) {
    return;
  }

  ei.set_permissions(
      modifier_->modify(ei.get_permissions(), ei.is_directory()));
}

} // namespace

std::unique_ptr<entry_transformer>
create_chmod_transformer(std::string_view spec, uint16_t umask) {
  return std::make_unique<chmod_transformer>(spec, umask);
}

} // namespace dwarfs
