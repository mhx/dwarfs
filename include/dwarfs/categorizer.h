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
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

#include "dwarfs/file_category.h"

namespace boost::program_options {
class options_description;
class variables_map;
} // namespace boost::program_options

namespace dwarfs {

class logger;

class categorizer {
 public:
  virtual ~categorizer() = default;

  virtual std::span<std::string_view const> categories() const = 0;
};

class random_access_categorizer : public categorizer {
 public:
  virtual std::optional<std::string_view>
  categorize(std::filesystem::path const& path,
             std::span<uint8_t const> data) const = 0;
};

class sequential_categorizer_job {
 public:
  virtual ~sequential_categorizer_job() = default;

  virtual void add(std::span<uint8_t const> data) = 0;
  virtual std::optional<std::string_view> result() = 0;
};

class sequential_categorizer : public categorizer {
 public:
  virtual std::unique_ptr<sequential_categorizer_job>
  job(std::filesystem::path const& path, size_t total_size) const = 0;
};

class categorizer_job {
 public:
  class impl;

  categorizer_job();
  categorizer_job(std::unique_ptr<impl> impl);

  void categorize_random_access(std::span<uint8_t const> data) {
    return impl_->categorize_random_access(data);
  }

  void categorize_sequential(std::span<uint8_t const> data) {
    return impl_->categorize_sequential(data);
  }

  file_category result() { return impl_->result(); }

  explicit operator bool() const { return impl_ != nullptr; }

  class impl {
   public:
    virtual ~impl() = default;

    virtual void categorize_random_access(std::span<uint8_t const> data) = 0;
    virtual void categorize_sequential(std::span<uint8_t const> data) = 0;
    virtual file_category result() = 0;
  };

 private:
  std::unique_ptr<impl> impl_;
};

class categorizer_manager {
 public:
  categorizer_manager(logger& lgr);

  void add(std::shared_ptr<categorizer const> c) { impl_->add(std::move(c)); }

  categorizer_job job(std::filesystem::path const& path) const {
    return impl_->job(path);
  }

  std::string_view category_name(file_category c) const {
    return impl_->category_name(c);
  }

  class impl {
   public:
    virtual ~impl() = default;

    virtual void add(std::shared_ptr<categorizer const> c) = 0;
    virtual categorizer_job job(std::filesystem::path const& path) const = 0;
    virtual std::string_view category_name(file_category c) const = 0;
  };

 private:
  std::unique_ptr<impl> impl_;
};

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
