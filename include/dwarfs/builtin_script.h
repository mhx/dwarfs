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

#pragma once

#include <memory>
#include <string>

#include "dwarfs/inode.h"
#include "dwarfs/script.h"

namespace dwarfs {

class logger;

class builtin_script : public script {
 public:
  builtin_script(logger& lgr);
  ~builtin_script();

  void set_root_path(std::string const& path) { impl_->set_root_path(path); }
  void add_filter_rule(std::string const& rule) {
    impl_->add_filter_rule(rule);
  };

  bool has_configure() const override;
  bool has_filter() const override;
  bool has_transform() const override;
  bool has_order() const override;

  void configure(options_interface const& oi) override;
  bool filter(entry_interface const& ei) override;
  void transform(entry_interface& ei) override;
  void order(inode_vector& iv) override;

  class impl {
   public:
    virtual ~impl() = default;

    virtual void set_root_path(std::string const& path) = 0;
    virtual void add_filter_rule(std::string const& rule) = 0;
    virtual bool filter(entry_interface const& ei) = 0;
    virtual bool has_filter() const = 0;
  };

 private:
  std::unique_ptr<impl> impl_;
};

} // namespace dwarfs
