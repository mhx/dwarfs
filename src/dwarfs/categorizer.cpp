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
#include <unordered_map>

#include <boost/program_options.hpp>

#include <fmt/format.h>

#include <folly/container/Enumerate.h>

#include "dwarfs/categorizer.h"
#include "dwarfs/compiler.h"
#include "dwarfs/error.h"
#include "dwarfs/logger.h"

namespace dwarfs {

using namespace std::placeholders;

namespace po = boost::program_options;

class categorizer_manager_private : public categorizer_manager::impl {
 public:
  virtual std::vector<std::shared_ptr<categorizer const>> const&
  categorizers() const = 0;
  virtual fragment_category::value_type
  category(std::string_view cat) const = 0;
  virtual bool has_multi_fragment_sequential_categorizers() const = 0;
};

template <typename LoggerPolicy>
class categorizer_job_ final : public categorizer_job::impl {
 public:
  categorizer_job_(logger& lgr, categorizer_manager_private const& mgr,
                   std::filesystem::path const& path)
      : LOG_PROXY_INIT(lgr)
      , mgr_{mgr}
      , path_{path}
      , cat_mapper_{std::bind(&categorizer_manager_private::category,
                              std::cref(mgr_), _1)} {}

  void set_total_size(size_t total_size) override;
  void categorize_random_access(std::span<uint8_t const> data) override;
  void categorize_sequential(std::span<uint8_t const> data) override;
  inode_fragments result() override;
  bool has_multi_fragment_sequential_categorizers() const override;

 private:
  LOG_PROXY_DECL(LoggerPolicy);
  categorizer_manager_private const& mgr_;

  inode_fragments best_;
  int index_{-1};
  bool is_global_best_{false};
  size_t total_size_{0};
  std::vector<std::pair<int, std::unique_ptr<sequential_categorizer_job>>>
      seq_jobs_;
  std::filesystem::path const path_;
  category_mapper cat_mapper_;
};

template <typename LoggerPolicy>
void categorizer_job_<LoggerPolicy>::set_total_size(size_t total_size) {
  total_size_ = total_size;
}

template <typename LoggerPolicy>
void categorizer_job_<LoggerPolicy>::categorize_random_access(
    std::span<uint8_t const> data) {
  DWARFS_CHECK(index_ < 0,
               "internal error: index already set in categorize_random_access");

  total_size_ = data.size();

  bool global_best = true;

  for (auto&& [index, cat] : folly::enumerate(mgr_.categorizers())) {
    if (auto p = dynamic_cast<random_access_categorizer const*>(cat.get())) {
      if (auto c = p->categorize(path_, data, cat_mapper_)) {
        best_ = c;
        index_ = index;
        is_global_best_ = global_best;
        break;
      }
    } else {
      global_best = false;
    }
  }
}

template <typename LoggerPolicy>
void categorizer_job_<LoggerPolicy>::categorize_sequential(
    std::span<uint8_t const> data) {
  if (is_global_best_) {
    return;
  }

  if (seq_jobs_.empty()) [[unlikely]] {
    for (auto&& [index, cat] : folly::enumerate(mgr_.categorizers())) {
      if (index_ >= 0 && static_cast<int>(index) >= index_) {
        break;
      }

      if (auto p = dynamic_cast<sequential_categorizer const*>(cat.get())) {
        if (auto job = p->job(path_, total_size_, cat_mapper_)) {
          seq_jobs_.emplace_back(index, std::move(job));
        }
      }
    }
  }

  for (auto&& [index, job] : seq_jobs_) {
    job->add(data);
  }
}

template <typename LoggerPolicy>
inode_fragments categorizer_job_<LoggerPolicy>::result() {
  if (!seq_jobs_.empty()) {
    for (auto&& [index, job] : seq_jobs_) {
      if (auto c = job->result()) {
        assert(index_ < 0 || index < index_);
        best_ = c;
        break;
      }
    }

    seq_jobs_.clear();
  }

  LOG_TRACE << path_ << " -> "
            << best_.to_string([this](fragment_category::value_type c) {
                 return std::string(mgr_.category_name(c));
               });

  return best_;
}

template <typename LoggerPolicy>
bool categorizer_job_<
    LoggerPolicy>::has_multi_fragment_sequential_categorizers() const {
  return mgr_.has_multi_fragment_sequential_categorizers();
}

categorizer_job::categorizer_job() = default;

categorizer_job::categorizer_job(std::unique_ptr<impl> impl)
    : impl_{std::move(impl)} {}

template <typename LoggerPolicy>
class categorizer_manager_ final : public categorizer_manager_private {
 public:
  categorizer_manager_(logger& lgr)
      : lgr_{lgr}
      , LOG_PROXY_INIT(lgr) {}

  void add(std::shared_ptr<categorizer const> c) override;
  categorizer_job job(std::filesystem::path const& path) const override;
  std::string_view
  category_name(fragment_category::value_type c) const override;

  folly::dynamic category_metadata(fragment_category c) const override;

  std::vector<std::shared_ptr<categorizer const>> const&
  categorizers() const override {
    return categorizers_;
  }

  fragment_category::value_type category(std::string_view cat) const override {
    auto it = catmap_.find(cat);
    DWARFS_CHECK(it != catmap_.end(), fmt::format("unknown category: {}", cat));
    return it->second;
  }

  bool has_multi_fragment_sequential_categorizers() const override {
    return has_multi_fragment_sequential_categorizers_;
  }

 private:
  void add_category(std::string_view cat, size_t categorizer_index) {
    if (catmap_.emplace(cat, categories_.size()).second) {
      categories_.emplace_back(cat, categorizer_index);
    } else {
      LOG_WARN << "duplicate category: " << cat;
    }
  }

  logger& lgr_;
  LOG_PROXY_DECL(LoggerPolicy);
  std::vector<std::shared_ptr<categorizer const>> categorizers_;
  std::vector<std::pair<std::string_view, size_t>> categories_;
  std::unordered_map<std::string_view, fragment_category::value_type> catmap_;
  bool has_multi_fragment_sequential_categorizers_{false};
};

template <typename LoggerPolicy>
void categorizer_manager_<LoggerPolicy>::add(
    std::shared_ptr<categorizer const> c) {
  for (auto const& c : c->categories()) {
    add_category(c, categorizers_.size());
  }

  if (!c->is_single_fragment() &&
      dynamic_cast<sequential_categorizer const*>(c.get())) {
    has_multi_fragment_sequential_categorizers_ = true;
  }

  categorizers_.emplace_back(std::move(c));
}

template <typename LoggerPolicy>
categorizer_job categorizer_manager_<LoggerPolicy>::job(
    std::filesystem::path const& path) const {
  return categorizer_job(
      make_unique_logging_object<categorizer_job::impl, categorizer_job_,
                                 logger_policies>(lgr_, *this, path));
}

template <typename LoggerPolicy>
std::string_view categorizer_manager_<LoggerPolicy>::category_name(
    fragment_category::value_type c) const {
  return DWARFS_NOTHROW(categories_.at(c)).first;
}

template <typename LoggerPolicy>
folly::dynamic categorizer_manager_<LoggerPolicy>::category_metadata(
    fragment_category c) const {
  auto categorizer =
      DWARFS_NOTHROW(categorizers_.at(categories_.at(c.value()).second));
  return categorizer->category_metadata(c);
}

categorizer_manager::categorizer_manager(logger& lgr)
    : impl_(make_unique_logging_object<impl, categorizer_manager_,
                                       logger_policies>(lgr)) {}

categorizer_registry& categorizer_registry::instance() {
  static categorizer_registry the_instance;
  return the_instance;
}

void categorizer_registry::register_factory(
    std::unique_ptr<categorizer_factory const>&& factory) {
  auto name = factory->name();

  if (!factories_.emplace(name, std::move(factory)).second) {
    std::cerr << "categorizer factory name conflict (" << name << "\n";
    ::abort();
  }
}

std::unique_ptr<categorizer>
categorizer_registry::create(logger& lgr, std::string const& name,
                             po::variables_map const& vm) const {
  auto it = factories_.find(name);

  if (it == factories_.end()) {
    DWARFS_THROW(runtime_error, "unknown categorizer: " + name);
  }

  return it->second->create(lgr, vm);
}

void categorizer_registry::add_options(po::options_description& opts) const {
  for (auto& f : factories_) {
    if (auto f_opts = f.second->options()) {
      opts.add(*f_opts);
    }
  }
}

std::vector<std::string> categorizer_registry::categorizer_names() const {
  std::vector<std::string> rv;
  for (auto& f : factories_) {
    rv.emplace_back(f.first);
  }
  return rv;
}

categorizer_registry::categorizer_registry() = default;
categorizer_registry::~categorizer_registry() = default;

} // namespace dwarfs
