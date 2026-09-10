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

#include <array>
#include <map>
#include <stack>
#include <unordered_map>
#include <vector>

#include <boost/program_options.hpp>

#include <fmt/format.h>

#include <magic.h>

#include <dwarfs/binary_literals.h>
#include <dwarfs/error.h>
#include <dwarfs/glob_matcher.h>
#include <dwarfs/logger.h>
#include <dwarfs/string.h>
#include <dwarfs/util.h>
#include <dwarfs/writer/categorizer.h>

#include <dwarfs/internal/synchronized.h>

namespace dwarfs::writer {

namespace {

namespace po = boost::program_options;
using namespace dwarfs::binary_literals;

class magic_wrapper {
 public:
  magic_wrapper(std::size_t max_bytes,
                std::optional<std::string> const& magic_file)
      : max_bytes_{max_bytes}
      , magic_file_{magic_file} {}

  size_t cookie_count() const { return cookies_.rlock()->size(); }

  std::string identify(file_view const& mm) const {
    auto segment =
        mm.segment_at(0, std::min<file_size_t>(mm.size(), max_bytes_));
    auto data = segment.span();
    scoped_cookie m(*this);
    if (auto id = ::magic_buffer(m.get(), data.data(), data.size())) {
      return std::string{id};
    }
    throw std::runtime_error(fmt::format("(magic) {}", ::magic_error(m.get())));
  }

 private:
  using magic_cookie_t =
      std::unique_ptr<struct ::magic_set, decltype(&::magic_close)>;

  magic_cookie_t new_cookie() const {
    magic_cookie_t m(::magic_open(MAGIC_MIME_TYPE | MAGIC_PRESERVE_ATIME |
                                  MAGIC_NO_CHECK_COMPRESS),
                     &::magic_close);
    if (!m) {
      throw std::runtime_error("could not create magic cookie");
    }
    char const* path = magic_file_ ? magic_file_->c_str() : NULL;
    if (::magic_load(m.get(), path) != 0) {
      auto const* errstr = ::magic_error(m.get());
      if (!errstr) {
        errstr = "unknown error";
      }
      if (!path) {
        path = "NULL";
      }
      throw std::runtime_error(
          fmt::format("(magic) magic_load({}): {}", path, errstr));
    }
    return m;
  }

  class scoped_cookie {
   public:
    explicit scoped_cookie(magic_wrapper const& w)
        : cookie_{get_scoped_cookie(w)}
        , w_{w} {}

    ~scoped_cookie() { w_.cookies_.wlock()->push(std::move(cookie_)); }

    ::magic_t get() const { return cookie_.get(); }

   private:
    static magic_cookie_t get_scoped_cookie(magic_wrapper const& w) {
      return w.cookies_.with_wlock([&](auto& cookies) {
        if (cookies.empty()) [[unlikely]] {
          return w.new_cookie();
        }
        auto cookie = std::move(cookies.top());
        cookies.pop();
        return cookie;
      });
    }

    magic_cookie_t cookie_;
    magic_wrapper const& w_;
  };

  mutable dwarfs::internal::synchronized<std::stack<magic_cookie_t>,
                                         std::shared_mutex>
      cookies_;
  std::size_t const max_bytes_{64_KiB};
  std::optional<std::string> const magic_file_;
};

template <typename LoggerPolicy>
class libmagic_categorizer_ final : public random_access_categorizer {
 public:
  libmagic_categorizer_(logger& lgr, std::span<std::string const> categories,
                        std::size_t max_bytes,
                        std::optional<std::string> const& magic_file)
      : LOG_PROXY_INIT(lgr)
      , m_{max_bytes, magic_file}
      , matchers_{build_matchers(categories)}
      , categories_{build_categories(matchers_)} {}

  ~libmagic_categorizer_() {
    LOG_VERBOSE << m_.cookie_count() << " magic cookies were used";
    mimetypes_.with_rlock([&](auto& m) {
      for (auto const& [k, v] : m) {
        LOG_VERBOSE << k << " -> " << v;
      }
    });
  }

  std::span<std::string_view const> categories() const override {
    return categories_;
  }

  inode_fragments categorize(file_path_info const& path, file_view const& mm,
                             category_mapper const& mapper) const override;

  bool
  subcategory_less(fragment_category a, fragment_category b) const override;

 private:
  using category_matcher = std::pair<glob_matcher, std::string>;

  static std::vector<category_matcher>
  build_matchers(std::span<std::string const> categories) {
    std::vector<category_matcher> matchers;
    std::unordered_map<std::string, std::size_t> cat_index;

    for (auto const& def : categories) {
      auto const eq_pos = def.find('=');

      if (eq_pos == std::string::npos) {
        throw std::runtime_error(
            fmt::format("invalid libmagic category definition: {}", def));
      }

      auto const cat = def.substr(0, eq_pos);

      if (cat.empty()) {
        throw std::runtime_error(
            fmt::format("invalid libmagic category definition: {}", def));
      }

      auto const patterns =
          split_to<std::vector<std::string>>(def.substr(eq_pos + 1), ':');

      if (patterns.empty()) {
        throw std::runtime_error(
            fmt::format("invalid libmagic category definition: {}", def));
      }

      glob_matcher* matcher = nullptr;

      if (auto it = cat_index.find(cat); it != cat_index.end()) {
        matcher = &matchers[it->second].first;
      } else {
        cat_index[cat] = matchers.size();
        matchers.emplace_back(glob_matcher{}, cat);
        matcher = &matchers.back().first;
      }

      for (auto const& pattern : patterns) {
        matcher->add_pattern(pattern);
      }
    }

    return matchers;
  }

  static std::vector<std::string_view>
  build_categories(std::vector<category_matcher> const& matchers) {
    std::vector<std::string_view> categories;
    categories.reserve(matchers.size());
    for (auto const& m : matchers) {
      categories.emplace_back(m.second);
    }
    return categories;
  }

  LOG_PROXY_DECL(LoggerPolicy);
  magic_wrapper m_;
  std::vector<category_matcher> const matchers_;
  std::vector<std::string_view> const categories_;
  dwarfs::internal::synchronized<std::map<std::string, size_t>,
                                 std::shared_mutex> mutable mimetypes_;
};

template <typename LoggerPolicy>
inode_fragments libmagic_categorizer_<LoggerPolicy>::categorize(
    file_path_info const& path, file_view const& mm,
    category_mapper const& mapper) const {
  inode_fragments fragments;
  auto id = m_.identify(mm);

  LOG_TRACE << path.full_path() << " -> (magic) " << id;

  mimetypes_.with_wlock([&](auto& m) { ++m[id]; });

  for (auto const& [matcher, cat] : matchers_) {
    if (matcher.match(id)) {
      fragments.emplace_back(fragment_category(mapper(cat)), mm.size());
      break;
    }
  }

  return fragments;
}

template <typename LoggerPolicy>
bool libmagic_categorizer_<LoggerPolicy>::subcategory_less(
    fragment_category, fragment_category) const {
  // No subcategories for now, just return false
  return false;
}

class libmagic_categorizer_factory : public categorizer_factory {
 public:
  libmagic_categorizer_factory()
      : opts_{std::make_shared<po::options_description>(
            "LibMagic categorizer options")} {
    // clang-format off
    opts_->add_options()
      ("libmagic-category",
          po::value<std::vector<std::string>>(&categories_)
            ->multitoken()->composing(),
          "define category based on mime types "
          "(accepts globs, can be specified multiple times)")
      ("libmagic-max-bytes",
          po::value<std::string>(&max_bytes_str_)->default_value("64K"),
          "maximum number of bytes to read from a file for libmagic to "
          "identify it")
      ("libmagic-database",
          po::value<std::string>(&magic_db_path_),
          "path to libmagic database (default: use system default)")
      ;
    // clang-format on
  }

  std::string_view name() const override { return "libmagic"; }

  std::shared_ptr<po::options_description const> options() const override {
    return opts_;
  }

  std::unique_ptr<categorizer>
  create(logger& lgr, po::variables_map const& vm,
         std::shared_ptr<file_access const> const& /*fa*/) const override {
    std::optional<std::string> magic_file;
    if (vm.count("libmagic-database")) {
      magic_file = magic_db_path_;
    }
    return make_unique_logging_object<categorizer, libmagic_categorizer_,
                                      default_logger_policy>(
        lgr, categories_, parse_size_with_unit(max_bytes_str_), magic_file);
  }

 private:
  std::shared_ptr<po::options_description> opts_;
  std::vector<std::string> categories_;
  std::string max_bytes_str_;
  std::string magic_db_path_;
};

} // namespace

REGISTER_CATEGORIZER_FACTORY(libmagic_categorizer_factory)

} // namespace dwarfs::writer
