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
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#include <filesystem>
#include <iosfwd>
#include <memory>
#include <string_view>

#include <dwarfs/file_stat.h>
#include <dwarfs/writer/entry_filter.h>

namespace dwarfs {

class file_access;
class logger;

namespace writer {

class rule_based_entry_filter : public entry_filter {
 public:
  rule_based_entry_filter(logger& lgr, std::shared_ptr<file_access const> fa);
  ~rule_based_entry_filter() override;

  void set_root_path(std::filesystem::path const& path) {
    impl_->set_root_path(path);
  }

  void add_rule(std::string_view rule) { impl_->add_rule(rule); }

  void add_rules(std::istream& is) { impl_->add_rules(is); }

  filter_action filter(entry_interface const& ei) const override;

  class impl {
   public:
    virtual ~impl() = default;

    virtual void set_root_path(std::filesystem::path const& path) = 0;
    virtual void add_rule(std::string_view rule) = 0;
    virtual void add_rules(std::istream& is) = 0;
    virtual filter_action filter(entry_interface const& ei) const = 0;
  };

 private:
  std::unique_ptr<impl> impl_;
};

} // namespace writer

} // namespace dwarfs
