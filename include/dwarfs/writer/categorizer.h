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

#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

#include <dwarfs/file_view.h>
#include <dwarfs/writer/category_resolver.h>
#include <dwarfs/writer/inode_fragments.h>

namespace boost::program_options {
class options_description;
class variables_map;
} // namespace boost::program_options

namespace dwarfs {

class file_access;
class logger;

namespace writer {

namespace internal {

class byte_progress;

} // namespace internal

using category_mapper =
    std::function<fragment_category::value_type(std::string_view)>;

class categorizer {
 public:
  static constexpr std::string_view const DEFAULT_CATEGORY{"<default>"};

  virtual ~categorizer() = default;

  virtual std::span<std::string_view const> categories() const = 0;
  virtual std::string
  category_metadata(std::string_view category_name, fragment_category c) const;
  virtual void set_metadata_requirements(std::string_view category_name,
                                         std::string_view requirements);
  virtual bool
  subcategory_less(fragment_category a, fragment_category b) const = 0;
};

class file_path_info {
 public:
  file_path_info(std::filesystem::path const& root_path,
                 std::filesystem::path const& full_path)
      : root_path_{root_path}
      , full_path_{full_path} {}

  std::filesystem::path const& root_path() const { return root_path_; }
  std::filesystem::path const& full_path() const { return full_path_; }
  std::filesystem::path relative_path() const;

 private:
  std::filesystem::path const& root_path_;
  std::filesystem::path const& full_path_;
};

class random_access_categorizer : public categorizer {
 public:
  virtual inode_fragments
  categorize(file_path_info const& path, file_view const& mm,
             category_mapper const& mapper) const = 0;
};

// TODO: add call to check if categorizer can return multiple fragments
//       if it *can* we must run it before we start similarity hashing
class sequential_categorizer_job {
 public:
  virtual ~sequential_categorizer_job() = default;

  virtual void add(file_segment const& seg) = 0;
  virtual inode_fragments result() = 0;
};

class sequential_categorizer : public categorizer {
 public:
  virtual std::unique_ptr<sequential_categorizer_job>
  job(file_path_info const& path, file_size_t total_size,
      category_mapper const& mapper) const = 0;
};

class categorizer_job {
 public:
  class impl;

  categorizer_job();
  categorizer_job(std::unique_ptr<impl> impl);

  void set_total_size(file_size_t total_size) {
    impl_->set_total_size(total_size);
  }

  void categorize_random_access(file_view const& mm) {
    impl_->categorize_random_access(mm);
  }

  void categorize_sequential(file_view const& mm, file_size_t chunk_size,
                             internal::byte_progress* progress = nullptr) {
    impl_->categorize_sequential(mm, chunk_size, progress);
  }

  inode_fragments result() { return impl_->result(); }

  explicit operator bool() const { return impl_ != nullptr; }

  bool best_result_found() const { return impl_->best_result_found(); }

  class impl {
   public:
    virtual ~impl() = default;

    virtual void set_total_size(file_size_t total_size) = 0;
    virtual void categorize_random_access(file_view const& mm) = 0;
    virtual void
    categorize_sequential(file_view const& mm, file_size_t chunk_size,
                          internal::byte_progress* progress) = 0;
    virtual inode_fragments result() = 0;
    virtual bool best_result_found() const = 0;
  };

 private:
  std::unique_ptr<impl> impl_;
};

class categorizer_manager : public category_resolver {
 public:
  categorizer_manager(logger& lgr, std::filesystem::path root);

  static fragment_category default_category();

  void add(std::shared_ptr<categorizer> c) { impl_->add(std::move(c)); }

  categorizer_job job(std::filesystem::path const& path) const {
    return impl_->job(path);
  }

  std::string_view
  category_name(fragment_category::value_type c) const override {
    return impl_->category_name(c);
  }

  std::optional<fragment_category::value_type>
  category_value(std::string_view name) const override {
    return impl_->category_value(name);
  }

  std::string category_metadata(fragment_category c) const {
    return impl_->category_metadata(c);
  }

  void set_metadata_requirements(fragment_category::value_type c,
                                 std::string_view req) {
    impl_->set_metadata_requirements(c, req);
  }

  bool deterministic_less(fragment_category a, fragment_category b) const {
    return impl_->deterministic_less(a, b);
  }

  class impl {
   public:
    virtual ~impl() = default;

    virtual void add(std::shared_ptr<categorizer> c) = 0;
    virtual categorizer_job job(std::filesystem::path const& path) const = 0;
    virtual std::string_view
    category_name(fragment_category::value_type c) const = 0;
    virtual std::optional<fragment_category::value_type>
    category_value(std::string_view name) const = 0;
    virtual std::string category_metadata(fragment_category c) const = 0;
    virtual void set_metadata_requirements(fragment_category::value_type c,
                                           std::string_view req) = 0;
    virtual bool
    deterministic_less(fragment_category a, fragment_category b) const = 0;
  };

 private:
  std::unique_ptr<impl> impl_;
};

std::string category_prefix(std::shared_ptr<categorizer_manager> const& mgr,
                            fragment_category cat);
std::string
category_prefix(categorizer_manager const* mgr, fragment_category cat);

class categorizer_info {
 public:
  virtual ~categorizer_info() = default;

  virtual std::string_view name() const = 0;
  virtual std::shared_ptr<boost::program_options::options_description const>
  options() const = 0;
};

class categorizer_factory : public categorizer_info {
 public:
  virtual std::unique_ptr<categorizer>
  create(logger& lgr, boost::program_options::variables_map const& vm,
         std::shared_ptr<file_access const> const& fa) const = 0;
};

class categorizer_registry {
 public:
  categorizer_registry();
  ~categorizer_registry();

  std::unique_ptr<categorizer>
  create(logger& lgr, std::string const& name,
         boost::program_options::variables_map const& vm,
         std::shared_ptr<file_access const> const& fa) const;

  void add_options(boost::program_options::options_description& opts) const;

  std::vector<std::string> categorizer_names() const;

  void register_factory(std::unique_ptr<categorizer_factory const>&& factory);

 private:
  std::map<std::string, std::unique_ptr<categorizer_factory const>> factories_;
};

namespace detail {

void binary_categorizer_factory_registrar(categorizer_registry&);
void fits_categorizer_factory_registrar(categorizer_registry&);
void hotness_categorizer_factory_registrar(categorizer_registry&);
void incompressible_categorizer_factory_registrar(categorizer_registry&);
void libmagic_categorizer_factory_registrar(categorizer_registry&);
void pcmaudio_categorizer_factory_registrar(categorizer_registry&);

} // namespace detail

} // namespace writer

} // namespace dwarfs

#define REGISTER_CATEGORIZER_FACTORY(factory)                                  \
  void ::dwarfs::writer::detail::factory##_registrar(                          \
      ::dwarfs::writer::categorizer_registry& cr) {                            \
    cr.register_factory(std::make_unique<factory>());                          \
  }
