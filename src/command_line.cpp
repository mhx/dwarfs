/* vim:set ts=2 sw=2 sts=2 et: */
/**
 * \author     Marcus Holland-Moritz (github@mhxnet.de)
 * \copyright  Copyright (c) Marcus Holland-Moritz
 *
 * This file is part of dwarfs.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the “Software”), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <ostream>

#include <dwarfs/command_line.h>

namespace dwarfs::cli {

namespace {

bool looks_like_option(std::string_view text) {
  return text.size() > 1 && text.front() == '-';
}

bool can_be_value(std::string_view text) {
  return !looks_like_option(text) || (text[1] >= '0' && text[1] <= '9') ||
         text[1] == '.' ||
         (!text.starts_with("--") && text.find_first_of(" \t") != text.npos);
}

[[noreturn]] void unrecognised(std::string_view arg) {
  throw error("unrecognised option '" + std::string(arg) + "'");
}

std::string quoted(std::string_view name) {
  return "'--" + std::string(name) + "'";
}

void print_description(std::ostream& out, std::string_view text, size_t indent,
                       size_t width) {
  size_t column = indent;

  while (!text.empty()) {
    auto const first = text.find_first_not_of(" \t\n");

    if (first == std::string_view::npos) {
      break;
    }

    bool const newline =
        text.substr(0, first).find('\n') != std::string_view::npos;

    text.remove_prefix(first);

    auto const end = text.find_first_of(" \t\n");
    auto const word = text.substr(0, end);

    if (column > indent) {
      if (newline || word.size() >= width - std::min(column, width)) {
        out << '\n' << std::string(indent, ' ');
        column = indent;
      } else {
        out << ' ';
        ++column;
      }
    }

    out << word;
    column += word.size();
    text.remove_prefix(word.size());
  }

  out << '\n';
}

} // namespace

option::option(std::string_view names, std::string_view description,
               std::string_view group, kind type, setter set)
    : description_(description)
    , group_(group)
    , type_(type)
    , set_(std::move(set)) {
  auto const comma = names.find(',');

  name_ = names.substr(0, comma);

  if (name_.empty() || name_.front() == '-' ||
      name_.find_first_of("= \t\n") != std::string::npos) {
    throw std::logic_error("invalid option name: " + std::string(names));
  }

  if (comma != std::string_view::npos) {
    if (names.size() - comma != 2 || names.back() == '-' ||
        names.back() == '=') {
      throw std::logic_error("invalid option alias: " + std::string(names));
    }

    alias_ = names.back();
  }
}

option& option::value_name(std::string_view name) {
  value_name_ = name;
  return *this;
}

option& option::default_text(std::string_view text) {
  default_text_ = std::string(text);
  return *this;
}

option& option::implicit_value(std::string_view text) {
  if (type_ != kind::single) {
    throw std::logic_error("implicit value requires a single-valued option");
  }
  implicit_ = std::string(text);
  return *this;
}

std::string option::label() const {
  std::string result{"  "};

  if (alias_) {
    result += '-';
    result += alias_;
    result += " [ --";
    result += name_;
    result += " ]";
  } else {
    result += "--";
    result += name_;
  }

  if (type_ != kind::flag) {
    result += ' ';

    if (implicit_) {
      result += "[=" + value_name_ + "(=" + *implicit_ + ")]";
    } else {
      result += value_name_;
    }

    if (default_text_) {
      result += " (=" + *default_text_ + ')';
    }
  }

  return result;
}

option& options::insert(std::string_view names, std::string_view description,
                        option::kind type, option::setter set) {
  option entry(names, description, group_, type, std::move(set));

  if (parsed_ || find_exact(entry.name_) ||
      (entry.alias_ && find_short(entry.alias_))) {
    throw std::logic_error("duplicate or late option definition: " +
                           entry.name_);
  }

  return options_.emplace_back(std::move(entry));
}

option& options::add_value(std::string_view names, std::string_view description,
                           std::function<bool(std::string_view)> set) {
  return insert(names, description, option::kind::single, std::move(set));
}

option* options::find_exact(std::string_view name) {
  for (auto& entry : options_) {
    if (entry.name_ == name) {
      return &entry;
    }
  }

  return nullptr;
}

option* options::find_short(char alias) {
  for (auto& entry : options_) {
    if (entry.alias_ == alias) {
      return &entry;
    }
  }

  return nullptr;
}

option* options::find_long(std::string_view name) {
  if (auto* exact = find_exact(name)) {
    return exact;
  }

  std::vector<std::string_view> matches;
  option* result = nullptr;

  for (auto& entry : options_) {
    if (entry.name_.starts_with(name)) {
      matches.push_back(entry.name_);
      result = &entry;
    }
  }

  if (matches.size() > 1) {
    std::ranges::sort(matches);
    std::string message =
        "option " + quoted(name) + " is ambiguous and matches ";

    for (size_t i = 0; i < matches.size(); ++i) {
      if (i) {
        message += i + 1 == matches.size() ? ", and " : ", ";
      }

      message += quoted(matches[i]);
    }

    throw error(message);
  }

  return result;
}

void options::positional(std::string_view name) {
  auto* entry = find_exact(name);

  if (!entry || entry->type_ == option::kind::flag || parsed_) {
    throw std::logic_error("invalid positional option: " + std::string(name));
  }

  positional_ = entry;
}

bool options::contains(std::string_view name) const {
  return std::ranges::any_of(options_, [name](auto const& entry) {
    return entry.name_ == name && entry.seen_;
  });
}

void options::parse(int argc, sys_char const* const* argv) {
#ifdef _WIN32
  std::vector<std::string> storage;

  for (int i = 1; i < argc; ++i) {
    storage.push_back(sys_string_to_string(argv[i]));
  }
#endif

  std::vector<std::string_view> args;

  for (int i = 1; i < argc; ++i) {
#ifdef _WIN32
    args.emplace_back(storage[i - 1]);
#else
    args.emplace_back(argv[i]);
#endif
  }

  parse(args);
}

void options::parse(std::span<std::string_view const> args) {
  if (std::exchange(parsed_, true)) {
    throw std::logic_error("command line already parsed");
  }

  struct occurrence {
    option* entry;
    std::string_view value;
  };

  std::vector<occurrence> occurrences;

  auto consume = [&](option& entry, std::optional<std::string_view> value,
                     size_t& i) {
    if (entry.type_ == option::kind::flag) {
      if (value) {
        throw error("option " + quoted(entry.name_) +
                    " does not take any arguments");
      }
      occurrences.emplace_back(&entry, std::string_view{});
      return;
    }

    if (!value) {
      if (i + 1 < args.size() && can_be_value(args[i + 1])) {
        value = args[++i];
      } else if (entry.implicit_) {
        value = *entry.implicit_;
      } else {
        throw error("the required argument for option " + quoted(entry.name_) +
                    " is missing");
      }
    }

    occurrences.emplace_back(&entry, *value);

    if (entry.type_ == option::kind::multi) {
      while (i + 1 < args.size() && can_be_value(args[i + 1])) {
        occurrences.emplace_back(&entry, args[++i]);
      }
    }
  };

  bool positional_only = false;

  for (size_t i = 0; i < args.size(); ++i) {
    auto const arg = args[i];

    if (!positional_only && arg == "--") {
      positional_only = true;
    } else if (!positional_only && arg.starts_with("--")) {
      auto const eq = arg.find('=');
      auto const name = arg.substr(2, eq == arg.npos ? arg.npos : eq - 2);
      auto* entry = name.empty() ? nullptr : find_long(name);

      if (!entry) {
        unrecognised(arg);
      }

      std::optional<std::string_view> value;

      if (eq != arg.npos) {
        value = arg.substr(eq + 1);
      }

      consume(*entry, value, i);
    } else if (!positional_only && looks_like_option(arg)) {
      for (size_t j = 1; j < arg.size(); ++j) {
        auto* entry = find_short(arg[j]);

        if (!entry) {
          unrecognised(arg);
        }

        std::optional<std::string_view> value;

        if (entry->type_ != option::kind::flag && j + 1 < arg.size()) {
          value = arg.substr(j + 1);
        }

        consume(*entry, value, i);

        if (entry->type_ != option::kind::flag) {
          break;
        }
      }
    } else {
      if (!positional_) {
        throw error("too many positional options have been specified on the "
                    "command line");
      }

      occurrences.emplace_back(positional_, arg);
    }
  }

  for (auto const& [entry, value] : occurrences) {
    if (entry->seen_ && !entry->repeatable()) {
      throw error("option " + quoted(entry->name_) +
                  " cannot be specified more than once");
    }

    if (!entry->set_(value)) {
      throw error("the argument ('" + std::string(value) + "') for option " +
                  quoted(entry->name_) + " is invalid");
    }

    entry->seen_ = true;
  }
}

void options::help(std::ostream& out, size_t width) const {
  print_help(out, width, std::nullopt);
}

void options::help(std::ostream& out, std::string_view group,
                   size_t width) const {
  print_help(out, width, group);
}

void options::print_help(std::ostream& out, size_t width,
                         std::optional<std::string_view> selected) const {
  auto visible = [&](option const& entry) {
    return !entry.hidden_ && (!selected || entry.group_ == *selected);
  };
  width = std::max(width, size_t{20});
  size_t longest = 0;

  for (auto const& entry : options_) {
    if (visible(entry)) {
      longest = std::max(longest, entry.label().size());
    }
  }

  auto const indent = std::min(longest + 2, width / 2);
  std::optional<std::string_view> group;

  for (auto const& entry : options_) {
    if (!visible(entry)) {
      continue;
    }

    if (!group || *group != entry.group_) {
      if (group) {
        out << '\n';
      }

      group = entry.group_;

      if (!group->empty()) {
        out << *group << ":\n";
      }
    }

    auto const label = entry.label();
    out << label;

    if (entry.description_.empty()) {
      out << '\n';
      continue;
    }

    if (label.size() >= indent) {
      out << '\n' << std::string(indent, ' ');
    } else {
      out << std::string(indent - label.size(), ' ');
    }

    print_description(out, entry.description_, indent, width);
  }
}

} // namespace dwarfs::cli
