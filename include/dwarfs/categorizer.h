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

#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

#include "dwarfs/category_resolver.h"
#include "dwarfs/inode_fragments.h"

namespace boost::program_options {
class options_description;
class variables_map;
} // namespace boost::program_options

namespace dwarfs {

class logger;

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
                                         std::string requirements);
  virtual bool
  subcategory_less(fragment_category a, fragment_category b) const = 0;
};

class random_access_categorizer : public categorizer {
 public:
  virtual inode_fragments
  categorize(std::filesystem::path const& path, std::span<uint8_t const> data,
             category_mapper const& mapper) const = 0;
};

// TODO: add call to check if categorizer can return multiple fragments
//       if it *can* we must run it before we start similarity hashing
class sequential_categorizer_job {
 public:
  virtual ~sequential_categorizer_job() = default;

  virtual void add(std::span<uint8_t const> data) = 0;
  virtual inode_fragments result() = 0;
};

class sequential_categorizer : public categorizer {
 public:
  virtual std::unique_ptr<sequential_categorizer_job>
  job(std::filesystem::path const& path, size_t total_size,
      category_mapper const& mapper) const = 0;
};

class categorizer_job {
 public:
  class impl;

  categorizer_job();
  categorizer_job(std::unique_ptr<impl> impl);

  void set_total_size(size_t total_size) {
    return impl_->set_total_size(total_size);
  }

  void categorize_random_access(std::span<uint8_t const> data) {
    return impl_->categorize_random_access(data);
  }

  void categorize_sequential(std::span<uint8_t const> data) {
    return impl_->categorize_sequential(data);
  }

  inode_fragments result() { return impl_->result(); }

  explicit operator bool() const { return impl_ != nullptr; }

  bool best_result_found() const { return impl_->best_result_found(); }

  class impl {
   public:
    virtual ~impl() = default;

    virtual void set_total_size(size_t total_size) = 0;
    virtual void categorize_random_access(std::span<uint8_t const> data) = 0;
    virtual void categorize_sequential(std::span<uint8_t const> data) = 0;
    virtual inode_fragments result() = 0;
    virtual bool best_result_found() const = 0;
  };

 private:
  std::unique_ptr<impl> impl_;
};

class categorizer_manager : public category_resolver {
 public:
  categorizer_manager(logger& lgr);

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

  void
  set_metadata_requirements(fragment_category::value_type c, std::string req) {
    impl_->set_metadata_requirements(c, std::move(req));
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
                                           std::string req) = 0;
    virtual bool
    deterministic_less(fragment_category a, fragment_category b) const = 0;
  };

 private:
  std::unique_ptr<impl> impl_;
};

std::string category_prefix(std::shared_ptr<categorizer_manager> const& mgr,
                            fragment_category cat);
std::string category_prefix(std::unique_ptr<categorizer_manager> const& mgr,
                            fragment_category cat);
std::string
category_prefix(categorizer_manager const* mgr, fragment_category cat);
std::string category_prefix(std::shared_ptr<categorizer_manager> const& mgr,
                            fragment_category::value_type cat);
std::string category_prefix(std::unique_ptr<categorizer_manager> const& mgr,
                            fragment_category::value_type cat);
std::string category_prefix(categorizer_manager const* mgr,
                            fragment_category::value_type cat);

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
  create(logger& lgr,
         boost::program_options::variables_map const& vm) const = 0;
};

namespace detail {

template <typename T>
class categorizer_factory_registrar {
 public:
  categorizer_factory_registrar();
};

} // namespace detail

class categorizer_registry {
 public:
  template <typename T>
  friend class detail::categorizer_factory_registrar;

  static categorizer_registry& instance();

  std::unique_ptr<categorizer>
  create(logger& lgr, std::string const& name,
         boost::program_options::variables_map const& vm) const;

  void add_options(boost::program_options::options_description& opts) const;

  std::vector<std::string> categorizer_names() const;

 private:
  categorizer_registry();
  ~categorizer_registry();

  void register_factory(std::unique_ptr<categorizer_factory const>&& factory);

  std::map<std::string, std::unique_ptr<categorizer_factory const>> factories_;
};

namespace detail {

template <typename T>
categorizer_factory_registrar<T>::categorizer_factory_registrar() {
  ::dwarfs::categorizer_registry::instance().register_factory(
      std::make_unique<T>());
}

} // namespace detail

#define REGISTER_CATEGORIZER_FACTORY(factory)                                  \
  namespace {                                                                  \
  ::dwarfs::detail::categorizer_factory_registrar<factory>                     \
      the_##factory##_registrar;                                               \
  }

} // namespace dwarfs
