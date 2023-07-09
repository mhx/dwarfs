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
#include <fstream>
#include <regex>
#include <unordered_set>

#include <fmt/format.h>

#include "dwarfs/builtin_script.h"
#include "dwarfs/entry_interface.h"
#include "dwarfs/entry_transformer.h"
#include "dwarfs/logger.h"
#include "dwarfs/util.h"

namespace dwarfs {

struct filter_rule {
  enum class rule_type {
    include,
    exclude,
  };

  filter_rule(rule_type type, bool floating, std::string const& re,
              std::string const& rule)
      : type{type}
      , floating{floating}
      , re{re}
      , rule{rule} {}

  rule_type type;
  bool floating;
  std::regex re;
  std::string rule;
};

template <typename LoggerPolicy>
class builtin_script_ : public builtin_script::impl {
 public:
  explicit builtin_script_(logger& lgr);

  void set_root_path(std::filesystem::path const& path) override;
  void add_filter_rule(std::string const& rule) override;
  void add_filter_rules(std::istream& is) override;

  void add_transformer(std::unique_ptr<entry_transformer>&& xfm) override {
    transformer_.emplace_back(std::move(xfm));
  }

  bool filter(entry_interface const& ei) override;
  void transform(entry_interface& ei) override;

  bool has_filter() const override { return !filter_.empty(); }
  bool has_transform() const override { return !transformer_.empty(); }

 private:
  void add_filter_rule(std::unordered_set<std::string>& seen_files,
                       std::string const& rule);

  void add_filter_rules(std::unordered_set<std::string>& seen_files,
                        std::istream& is);

  filter_rule compile_filter_rule(std::string const& rule);

  LOG_PROXY_DECL(LoggerPolicy);
  std::string root_path_;
  std::vector<filter_rule> filter_;
  std::vector<std::unique_ptr<entry_transformer>> transformer_;
};

template <typename LoggerPolicy>
auto builtin_script_<LoggerPolicy>::compile_filter_rule(std::string const& rule)
    -> filter_rule {
  std::string r;
  filter_rule::rule_type type;

  auto* p = rule.c_str();

  switch (*p) {
  case '+':
    type = filter_rule::rule_type::include;
    break;
  case '-':
    type = filter_rule::rule_type::exclude;
    break;
  default:
    throw std::runtime_error("rules must start with + or -");
  }

  while (*++p == ' ')
    ;

  // If the start of the pattern is not explicitly anchored, make it floating.
  bool floating = *p && *p != '/';

  if (floating) {
    r += ".*/";
  }

  while (*p) {
    switch (*p) {
    case '\\':
      r += *p++;
      if (p) {
        r += *p++;
      }
      continue;

    case '*': {
      int nstar = 1;
      while (*++p == '*') {
        ++nstar;
      }
      switch (nstar) {
      case 1:
        if (r.ends_with('/') and (*p == '/' or *p == '\0')) {
          r += "[^/]+";
        } else {
          r += "[^/]*";
        }
        break;
      case 2:
        r += ".*";
        break;
      default:
        throw std::runtime_error("too many *s");
      }
    }
      continue;

    case '?':
      r += "[^/]";
      break;

    case '.':
    case '+':
    case '^':
    case '$':
    case '(':
    case ')':
    case '{':
    case '}':
    case '|':
      r += '\\';
      r += *p;
      break;

    default:
      r += *p;
      break;
    }

    ++p;
  }

  LOG_DEBUG << "'" << rule << "' -> '" << r << "' [floating=" << floating
            << "]";

  return filter_rule(type, floating, r, rule);
}

template <typename LoggerPolicy>
builtin_script_<LoggerPolicy>::builtin_script_(logger& lgr)
    : log_(lgr) {}

template <typename LoggerPolicy>
void builtin_script_<LoggerPolicy>::set_root_path(
    std::filesystem::path const& path) {
  // TODO: this whole thing needs to be windowsized
  root_path_ = u8string_to_string(path.u8string());
}

template <typename LoggerPolicy>
void builtin_script_<LoggerPolicy>::add_filter_rule(std::string const& rule) {
  std::unordered_set<std::string> seen_files;
  add_filter_rule(seen_files, rule);
}

template <typename LoggerPolicy>
void builtin_script_<LoggerPolicy>::add_filter_rules(std::istream& is) {
  std::unordered_set<std::string> seen_files;
  add_filter_rules(seen_files, is);
}

template <typename LoggerPolicy>
void builtin_script_<LoggerPolicy>::add_filter_rule(
    std::unordered_set<std::string>& seen_files, std::string const& rule) {
  if (rule.starts_with('.')) {
    auto file = std::regex_replace(rule, std::regex("^. +"), "");

    if (!seen_files.emplace(file).second) {
      throw std::runtime_error(
          fmt::format("recursion detected while opening file: {}", file));
    }

    std::ifstream ifs(file);

    if (!ifs.is_open()) {
      throw std::runtime_error(fmt::format("error opening file: {}", file));
    }

    add_filter_rules(seen_files, ifs);

    seen_files.erase(file);
  } else {
    filter_.push_back(compile_filter_rule(rule));
  }
}

template <typename LoggerPolicy>
void builtin_script_<LoggerPolicy>::add_filter_rules(
    std::unordered_set<std::string>& seen_files, std::istream& is) {
  std::string line;

  while (std::getline(is, line)) {
    if (line.starts_with('#')) {
      continue;
    }
    if (line.find_first_not_of(" \t") == std::string::npos) {
      continue;
    }
    add_filter_rule(seen_files, line);
  }
}

template <typename LoggerPolicy>
bool builtin_script_<LoggerPolicy>::filter(entry_interface const& ei) {
  std::string path = ei.unix_dpath();
  std::string relpath = path;

  if (relpath.size() >= root_path_.size()) {
    assert(relpath.substr(0, root_path_.size()) == root_path_);
    relpath.erase(0, root_path_.size());
  }

  for (const auto& r : filter_) {
    if (std::regex_match(r.floating ? path : relpath, r.re)) {
      LOG_TRACE << path << " matched rule '" << r.rule << "'";
      switch (r.type) {
      case filter_rule::rule_type::include:
        return true;

      case filter_rule::rule_type::exclude:
        return false;
      }
    }
  }

  LOG_TRACE << path << " matched no rule";

  return true;
}

template <typename LoggerPolicy>
void builtin_script_<LoggerPolicy>::transform(entry_interface& ei) {
  for (auto& xfm : transformer_) {
    xfm->transform(ei);
  }
}

builtin_script::builtin_script(logger& lgr)
    : impl_(make_unique_logging_object<impl, builtin_script_, logger_policies>(
          lgr)) {}

builtin_script::~builtin_script() = default;

bool builtin_script::has_configure() const { return false; }
bool builtin_script::has_filter() const { return impl_->has_filter(); }
bool builtin_script::has_transform() const { return impl_->has_transform(); }
bool builtin_script::has_order() const { return false; }

void builtin_script::configure(options_interface const&) { assert(false); }

bool builtin_script::filter(entry_interface const& ei) {
  return impl_->filter(ei);
}

void builtin_script::transform(entry_interface& ei) { impl_->transform(ei); }

void builtin_script::order(inode_vector&) { assert(false); }

} // namespace dwarfs
