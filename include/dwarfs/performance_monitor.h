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
#include <initializer_list>
#include <iosfwd>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_set>

#include <dwarfs/config.h>
#include <dwarfs/small_vector.h>

namespace dwarfs {

class file_access;

class performance_monitor {
 public:
  using timer_id = size_t;
  using time_type = uint64_t;
  static inline constexpr size_t kNumInlineContext{3};

  static std::unique_ptr<performance_monitor>
  create(std::unordered_set<std::string> const& enabled_namespaces,
         std::shared_ptr<file_access const> fa = nullptr,
         std::optional<std::filesystem::path> trace_file = std::nullopt);

  virtual ~performance_monitor() = default;

  virtual time_type now() const = 0;
  virtual void add_sample(timer_id id, time_type start,
                          std::span<uint64_t const> context) const = 0;
  virtual void summarize(std::ostream& os) const = 0;
  virtual bool is_enabled(std::string const& ns) const = 0;
  virtual timer_id
  setup_timer(std::string const& ns, std::string const& name,
              std::initializer_list<std::string_view> context) const = 0;
  virtual bool wants_context() const = 0;
};

class performance_monitor_proxy {
 public:
  class section_timer {
   public:
    section_timer() = default;

    section_timer(performance_monitor const* mon,
                  performance_monitor::timer_id id)
        : mon_{mon}
        , id_{id}
        , start_{mon_->now()} {
      if (mon_->wants_context()) {
        context_.emplace();
      }
    }

    void set_context(std::initializer_list<uint64_t> ctx) {
      if (context_) {
        context_->assign(ctx);
      }
    }

    ~section_timer() {
      if (mon_) {
        mon_->add_sample(id_, start_,
                         context_
                             // TODO: workaround for older boost small_vector
                             ? std::span{context_->data(), context_->size()}
                             : std::span<uint64_t const>{});
      }
    }

   private:
    performance_monitor const* mon_{nullptr};
    performance_monitor::timer_id id_;
    performance_monitor::time_type start_;
    std::optional<
        small_vector<uint64_t, performance_monitor::kNumInlineContext>>
        context_;
  };

  performance_monitor_proxy() = default;

  performance_monitor_proxy(std::shared_ptr<performance_monitor const> mon,
                            std::string const& proxy_namespace);

  performance_monitor::timer_id
  setup_timer(std::string const& name,
              std::initializer_list<std::string_view> context) const {
    return mon_ ? mon_->setup_timer(namespace_, name, context) : 0;
  }

  section_timer scoped_section(performance_monitor::timer_id id) const {
    return mon_ ? section_timer(mon_.get(), id) : section_timer();
  }

 private:
  std::shared_ptr<performance_monitor const> mon_;
  std::string namespace_;
};

#ifndef DWARFS_PERFMON_ENABLED
#define DWARFS_PERFMON_ENABLED 0
#endif

#if DWARFS_PERFMON_ENABLED

#define PERFMON_ARG(monitor) monitor
#define PERFMON_CREATE(monitor, enabled_namespaces)                            \
  std::shared_ptr<performance_monitor> monitor =                               \
      performance_monitor::create(enabled_namespaces);

#define PERFMON_PROXY_DECL(instname) performance_monitor_proxy instname;
#define PERFMON_PROXY_INIT(instname, monitor, name_space)                      \
  , instname { monitor, name_space }
#define PERFMON_TIMER_DECL(id)                                                 \
  performance_monitor::timer_id const perfmon_##id##_id_;
#define PERFMON_TIMER_INIT(instname, id, ...)                                  \
  , perfmon_##id##_id_ { instname.setup_timer(#id, {__VA_ARGS__}) }
#define PERFMON_SCOPED_SECTION(instname, id)                                   \
  auto perfmon_scoped_section_ = instname.scoped_section(perfmon_##id##_id_);
#define PERFMON_PROXY_SETUP(instname, monitor, name_space)                     \
  instname = performance_monitor_proxy(monitor, name_space);

#define PERFMON_SECTION_ARG perfmon_scoped_section_
#define PERFMON_SECTION_ARG_ perfmon_scoped_section_,
#define PERFMON_SECTION_PARAM                                                  \
  ::dwarfs::performance_monitor_proxy::section_timer& perfmon_scoped_section_
#define PERFMON_SECTION_PARAM_ PERFMON_SECTION_PARAM,

#define PERFMON_PROXY_INSTNAME perfmon_inst_

#define PERFMON_EXT_PROXY_DECL PERFMON_PROXY_DECL(PERFMON_PROXY_INSTNAME)
#define PERFMON_EXT_TIMER_DECL(id)                                             \
  performance_monitor::timer_id perfmon_##id##_id_;
#define PERFMON_EXT_TIMER_SETUP(scope, id, ...)                                \
  (scope).perfmon_##id##_id_ =                                                 \
      (scope).PERFMON_PROXY_INSTNAME.setup_timer(#id, {__VA_ARGS__});
#define PERFMON_EXT_SCOPED_SECTION(scope, id)                                  \
  auto perfmon_scoped_section_ =                                               \
      (scope).PERFMON_PROXY_INSTNAME.scoped_section(                           \
          (scope).perfmon_##id##_id_);
#define PERFMON_EXT_PROXY_SETUP(scope, monitor, name_space)                    \
  PERFMON_PROXY_SETUP((scope).PERFMON_PROXY_INSTNAME, monitor, name_space)

#define PERFMON_CLS_PROXY_DECL PERFMON_PROXY_DECL(PERFMON_PROXY_INSTNAME)
#define PERFMON_CLS_PROXY_INIT(monitor, name_space)                            \
  PERFMON_PROXY_INIT(PERFMON_PROXY_INSTNAME, monitor, name_space)
#define PERFMON_CLS_TIMER_DECL(id) PERFMON_TIMER_DECL(id)
#define PERFMON_CLS_TIMER_INIT(id, ...)                                        \
  PERFMON_TIMER_INIT(PERFMON_PROXY_INSTNAME, id, __VA_ARGS__)
#define PERFMON_CLS_SCOPED_SECTION(id)                                         \
  PERFMON_SCOPED_SECTION(PERFMON_PROXY_INSTNAME, id)

#define PERFMON_SET_CONTEXT(...)                                               \
  perfmon_scoped_section_.set_context({__VA_ARGS__});

#else

#define PERFMON_ARG(monitor) nullptr
#define PERFMON_CREATE(monitor, enabled_namespaces)

#define PERFMON_PROXY_DECL(instname)
#define PERFMON_PROXY_INIT(instname, monitor, name_space)
#define PERFMON_TIMER_DECL(id)
#define PERFMON_TIMER_INIT(instname, id, ...)
#define PERFMON_SCOPED_SECTION(instname, id)
#define PERFMON_PROXY_SETUP(instname, monitor, name_space)

#define PERFMON_SECTION_ARG
#define PERFMON_SECTION_ARG_
#define PERFMON_SECTION_PARAM
#define PERFMON_SECTION_PARAM_

#define PERFMON_EXT_PROXY_DECL
#define PERFMON_EXT_TIMER_DECL(id)
#define PERFMON_EXT_TIMER_SETUP(scope, id, ...)
#define PERFMON_EXT_SCOPED_SECTION(scope, id)
#define PERFMON_EXT_PROXY_SETUP(scope, monitor, name_space)

#define PERFMON_CLS_PROXY_DECL
#define PERFMON_CLS_PROXY_INIT(monitor, name_space)
#define PERFMON_CLS_TIMER_DECL(id)
#define PERFMON_CLS_TIMER_INIT(id, ...)
#define PERFMON_CLS_SCOPED_SECTION(id)

#define PERFMON_SET_CONTEXT(...)

#endif

} // namespace dwarfs
