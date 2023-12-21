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

#include <array>
#include <map>
#include <shared_mutex>
#include <stack>
#include <unordered_set>
#include <vector>

#include <boost/program_options.hpp>

#include <fmt/format.h>

#include <folly/Synchronized.h>

#include <magic.h>

#include "dwarfs/categorizer.h"
#include "dwarfs/error.h"
#include "dwarfs/logger.h"

namespace dwarfs {

namespace {

namespace po = boost::program_options;

constexpr std::string_view const SOME_CATEGORY{"bla"};

std::unordered_set<std::string_view> executable_mime_types{
    "application/x-executable",
    "application/x-sharedlib",
};

class magic_wrapper {
 public:
  magic_wrapper() = default;

  size_t cookie_count() const {
    auto rlock = cookies_.rlock();
    return rlock->size();
  }

  std::string identify(std::span<uint8_t const> data) const {
    std::string rv;
    scoped_cookie m(*this);
    if (auto id = ::magic_buffer(m.get(), data.data(), data.size())) {
      rv.assign(id);
    }
    if (rv.starts_with("application/")) {
      ::magic_setflags(m.get(), MAGIC_NONE);
      if (auto id = ::magic_buffer(m.get(), data.data(), data.size())) {
        rv += "; " + std::string(id);
      }
      ::magic_setflags(m.get(), MAGIC_MIME_TYPE);
    }
    return rv;
  }

 private:
  using magic_cookie_t =
      std::unique_ptr<struct ::magic_set, decltype(&::magic_close)>;

  magic_cookie_t new_cookie() const {
    magic_cookie_t m(::magic_open(MAGIC_MIME_TYPE), &::magic_close);
    if (!m) {
      throw std::runtime_error("could not create magic cookie");
    }
    if (::magic_load(m.get(), NULL) != 0) {
      throw std::runtime_error(
          fmt::format("(magic) {}", ::magic_error(m.get())));
    }
    return m;
  }

  class scoped_cookie {
   public:
    explicit scoped_cookie(magic_wrapper const& w)
        : cookie_{get_scoped_cookie(w)}
        , w_{w} {}

    ~scoped_cookie() {
      auto wlock = w_.cookies_.wlock();
      wlock->push(std::move(cookie_));
    }

    ::magic_t get() const { return cookie_.get(); }

   private:
    static magic_cookie_t get_scoped_cookie(magic_wrapper const& w) {
      auto wlock = w.cookies_.wlock();
      if (wlock->empty()) [[unlikely]] {
        return w.new_cookie();
      }
      auto cookie = std::move(wlock->top());
      wlock->pop();
      return cookie;
    }

    magic_cookie_t cookie_;
    magic_wrapper const& w_;
  };

  mutable folly::Synchronized<std::stack<magic_cookie_t>, std::shared_mutex>
      cookies_;
};

class libmagic_categorizer_base : public random_access_categorizer {
 public:
  std::span<std::string_view const> categories() const override;
};

template <typename LoggerPolicy>
class libmagic_categorizer_ final : public libmagic_categorizer_base {
 public:
  explicit libmagic_categorizer_(logger& lgr)
      : LOG_PROXY_INIT(lgr) {}

  ~libmagic_categorizer_() {
    LOG_INFO << m_.cookie_count() << " magic cookies were used";
    {
      auto rlock = mimetypes_.rlock();
      for (auto const& [k, v] : *rlock) {
        LOG_INFO << k << " -> " << v;
      }
    }
  }

  inode_fragments
  categorize(std::filesystem::path const& path, std::span<uint8_t const> data,
             category_mapper const& mapper) const override;

  bool
  subcategory_less(fragment_category a, fragment_category b) const override;

 private:
  LOG_PROXY_DECL(LoggerPolicy);
  magic_wrapper m_;
  mutable folly::Synchronized<std::map<std::string, size_t>, std::shared_mutex>
      mimetypes_;
};

std::span<std::string_view const>
libmagic_categorizer_base::categories() const {
  static constexpr std::array const s_categories{
      SOME_CATEGORY,
  };
  return s_categories;
}

template <typename LoggerPolicy>
inode_fragments libmagic_categorizer_<LoggerPolicy>::categorize(
    std::filesystem::path const& path, std::span<uint8_t const> data,
    category_mapper const& /*mapper*/) const {
  inode_fragments fragments; // TODO: actually fill this :-)
  auto id = m_.identify(data);
  LOG_DEBUG << path << " -> (magic) " << id;
  {
    auto wlock = mimetypes_.wlock();
    ++(*wlock)[id];
  }
  return fragments;
}

template <typename LoggerPolicy>
bool libmagic_categorizer_<LoggerPolicy>::subcategory_less(
    fragment_category, fragment_category) const {
  return false; // TODO
}

class libmagic_categorizer_factory : public categorizer_factory {
 public:
  std::string_view name() const override { return "libmagic"; }

  std::shared_ptr<boost::program_options::options_description const>
  options() const override {
    return nullptr;
  }

  std::unique_ptr<categorizer>
  create(logger& lgr, po::variables_map const& /*vm*/) const override {
    return make_unique_logging_object<categorizer, libmagic_categorizer_,
                                      logger_policies>(lgr);
  }

 private:
};

} // namespace

REGISTER_CATEGORIZER_FACTORY(libmagic_categorizer_factory)

} // namespace dwarfs
