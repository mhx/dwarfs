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
#include <memory>
#include <optional>
#include <span>
#include <string>

namespace dwarfs {

class file_access;
class logger;
class os_access;
class thread_pool;

namespace writer {

struct scanner_options;

class entry_filter;
class entry_transformer;
class entry_factory;
class filesystem_writer;
class writer_progress;
class segmenter_factory;

class scanner {
 public:
  scanner(logger& lgr, thread_pool& pool, segmenter_factory& sf,
          entry_factory& ef, os_access const& os,
          scanner_options const& options);

  void add_filter(std::unique_ptr<entry_filter>&& filter) {
    impl_->add_filter(std::move(filter));
  }

  void add_transformer(std::unique_ptr<entry_transformer>&& transformer) {
    impl_->add_transformer(std::move(transformer));
  }

  void scan(
      filesystem_writer& fsw, std::filesystem::path const& path,
      writer_progress& prog,
      std::optional<std::span<std::filesystem::path const>> list = std::nullopt,
      std::shared_ptr<file_access const> fa = nullptr) {
    impl_->scan(fsw, path, prog, list, std::move(fa));
  }

  class impl {
   public:
    virtual ~impl() = default;

    virtual void add_filter(std::unique_ptr<entry_filter>&& filter) = 0;

    virtual void
    add_transformer(std::unique_ptr<entry_transformer>&& transformer) = 0;

    virtual void
    scan(filesystem_writer& fsw, std::filesystem::path const& path,
         writer_progress& prog,
         std::optional<std::span<std::filesystem::path const>> list,
         std::shared_ptr<file_access const> fa) = 0;
  };

 private:
  std::unique_ptr<impl> impl_;
};

} // namespace writer
} // namespace dwarfs
