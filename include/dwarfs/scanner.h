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

#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>

namespace dwarfs {

struct scanner_options;

class entry_factory;
class file_access;
class filesystem_writer;
class logger;
class os_access;
class progress;
class script;
class segmenter_factory;
class worker_group;

class scanner {
 public:
  scanner(logger& lgr, worker_group& wg, std::shared_ptr<segmenter_factory> sf,
          std::shared_ptr<entry_factory> ef,
          std::shared_ptr<os_access const> os, std::shared_ptr<script> scr,
          const scanner_options& options);

  void scan(
      filesystem_writer& fsw, const std::filesystem::path& path, progress& prog,
      std::optional<std::span<std::filesystem::path const>> list = std::nullopt,
      std::shared_ptr<file_access const> fa = nullptr) {
    impl_->scan(fsw, path, prog, list, fa);
  }

  class impl {
   public:
    virtual ~impl() = default;

    virtual void
    scan(filesystem_writer& fsw, const std::filesystem::path& path,
         progress& prog,
         std::optional<std::span<std::filesystem::path const>> list,
         std::shared_ptr<file_access const> fa) = 0;
  };

 private:
  std::unique_ptr<impl> impl_;
};
} // namespace dwarfs
