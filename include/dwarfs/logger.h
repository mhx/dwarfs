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

#include <atomic>
#include <chrono>
#include <cstddef>
#include <ctime>
#include <iosfwd>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include <boost/chrono/thread_clock.hpp>

#include <dwarfs/detail/logging_class_factory.h>
#include <dwarfs/util.h>

namespace dwarfs {

class terminal;

class logger {
 public:
  enum level_type : unsigned {
    FATAL,
    ERROR,
    WARN,
    INFO,
    VERBOSE,
    DEBUG,
    TRACE
  };

  static char level_char(level_type level);

  virtual ~logger() = default;

  virtual void write(level_type level, const std::string& output,
                     char const* file, int line) = 0;

  std::string_view policy_name() const { return policy_name_; }

  static level_type parse_level(std::string_view level);
  static std::string_view level_name(level_type level);

  static std::string all_level_names();

 protected:
  template <class Policy>
  void set_policy() // TODO: construction time arg?
  {
    policy_name_ = Policy::name();
  }

 private:
  std::string_view policy_name_;
};

std::ostream& operator<<(std::ostream& os, logger::level_type const& optval);
std::istream& operator>>(std::istream& is, logger::level_type& optval);

struct logger_options {
  logger::level_type threshold{logger::WARN};
  std::optional<bool> with_context{};
};

class stream_logger : public logger {
 public:
  explicit stream_logger(logger_options const& options = {});
  explicit stream_logger(std::ostream& os, logger_options const& options = {});
  stream_logger(std::shared_ptr<terminal const> term, std::ostream& os,
                logger_options const& options = {});

  void write(level_type level, const std::string& output, char const* file,
             int line) override;

  void set_threshold(level_type threshold);
  void set_with_context(bool with_context) { with_context_ = with_context; }

 protected:
  virtual void preamble(std::ostream& os);
  virtual void postamble(std::ostream& os);
  virtual std::string_view get_newline() const;

  void write_nolock(std::string_view output);
  std::mutex& log_mutex() const { return mx_; }
  bool log_is_colored() const { return color_; }
  level_type log_threshold() const { return threshold_.load(); }
  terminal const& term() const { return *term_; }

 private:
  std::ostream& os_;
  std::mutex mutable mx_;
  std::atomic<level_type> threshold_;
  bool const color_;
  bool const enable_stack_trace_;
  bool with_context_;
  std::shared_ptr<terminal const> term_;
};

class null_logger : public logger {
 public:
  null_logger();

  void write(level_type, const std::string&, char const*, int) override {}
};

class level_log_entry {
 public:
  level_log_entry(logger& lgr, logger::level_type level,
                  char const* file = nullptr, int line = 0)
      : lgr_(lgr)
      , level_(level)
      , file_(file)
      , line_(line) {}

  level_log_entry(level_log_entry const&) = delete;

  ~level_log_entry() { lgr_.write(level_, oss_.str(), file_, line_); }

  template <typename T>
  level_log_entry& operator<<(const T& val) {
    oss_ << val;
    return *this;
  }

 private:
  logger& lgr_;
  std::ostringstream oss_;
  logger::level_type const level_;
  char const* const file_;
  int const line_;
};

class timed_level_log_entry {
 public:
  using thread_clock = boost::chrono::thread_clock;

  timed_level_log_entry(logger& lgr, logger::level_type level,
                        char const* file = nullptr, int line = 0,
                        bool with_cpu = false)
      : lgr_(lgr)
      , level_(level)
      , start_time_(std::chrono::high_resolution_clock::now())
      , with_cpu_(with_cpu)
      , file_(file)
      , line_(line) {
    if (with_cpu) {
      cpu_start_time_ = thread_clock::now();
    }
  }

  timed_level_log_entry(timed_level_log_entry const&) = delete;

  ~timed_level_log_entry() {
    if (output_) {
      std::chrono::duration<double> sec =
          std::chrono::high_resolution_clock::now() - start_time_;
      oss_ << " [" << time_with_unit(sec.count());
      if (with_cpu_) {
        boost::chrono::duration<double> cpu_time_sec =
            thread_clock::now() - cpu_start_time_;
        oss_ << ", " << time_with_unit(cpu_time_sec.count()) << " CPU";
      }
      oss_ << "]";
      lgr_.write(level_, oss_.str(), file_, line_);
    }
  }

  template <typename T>
  timed_level_log_entry& operator<<(const T& val) {
    output_ = true;
    oss_ << val;
    return *this;
  }

 private:
  logger& lgr_;
  std::ostringstream oss_;
  logger::level_type const level_;
  std::chrono::time_point<std::chrono::high_resolution_clock> start_time_;
  thread_clock::time_point cpu_start_time_;
  bool output_{false};
  bool const with_cpu_;
  char const* const file_;
  int const line_;
};

class no_log_entry {
 public:
  no_log_entry(logger&, logger::level_type) {}
  no_log_entry(logger&, logger::level_type, char const*, int) {}

  template <typename T>
  no_log_entry& operator<<(const T&) {
    return *this;
  }
};

namespace detail {

template <bool LoggingEnabled>
using logger_type = typename std::conditional<LoggingEnabled, level_log_entry,
                                              no_log_entry>::type;

template <bool LoggingEnabled>
using timed_logger_type =
    typename std::conditional<LoggingEnabled, timed_level_log_entry,
                              no_log_entry>::type;
} // namespace detail

template <unsigned MinLogLevel>
class MinimumLogLevelPolicy {
 public:
  template <unsigned Level>
  using logger_type = detail::logger_type<Level <= MinLogLevel>;

  template <unsigned Level>
  using timed_logger_type = detail::timed_logger_type<Level <= MinLogLevel>;

  static constexpr bool is_enabled_for(logger::level_type level) {
    return level <= MinLogLevel;
  }
};

template <typename LogPolicy>
class log_proxy {
 public:
  log_proxy(logger& lgr)
      : lgr_(lgr) {}

  static constexpr bool is_enabled_for(logger::level_type level) {
    return LogPolicy::is_enabled_for(level);
  }

  auto fatal(char const* file, int line) const {
    return level_log_entry(lgr_, logger::FATAL, file, line);
  }

  auto error(char const* file, int line) const {
    return typename LogPolicy::template logger_type<logger::ERROR>(
        lgr_, logger::ERROR, file, line);
  }

  auto warn(char const* file, int line) const {
    return typename LogPolicy::template logger_type<logger::WARN>(
        lgr_, logger::WARN, file, line);
  }

  auto info(char const* file, int line) const {
    return typename LogPolicy::template logger_type<logger::INFO>(
        lgr_, logger::INFO, file, line);
  }

  auto verbose(char const* file, int line) const {
    return typename LogPolicy::template logger_type<logger::VERBOSE>(
        lgr_, logger::VERBOSE, file, line);
  }

  auto debug(char const* file, int line) const {
    return typename LogPolicy::template logger_type<logger::DEBUG>(
        lgr_, logger::DEBUG, file, line);
  }

  auto trace(char const* file, int line) const {
    return typename LogPolicy::template logger_type<logger::TRACE>(
        lgr_, logger::TRACE, file, line);
  }

  auto timed_error(char const* file, int line) const {
    return typename LogPolicy::template timed_logger_type<logger::ERROR>(
        lgr_, logger::ERROR, file, line);
  }

  auto timed_warn(char const* file, int line) const {
    return typename LogPolicy::template timed_logger_type<logger::WARN>(
        lgr_, logger::WARN, file, line);
  }

  auto timed_info(char const* file, int line) const {
    return typename LogPolicy::template timed_logger_type<logger::INFO>(
        lgr_, logger::INFO, file, line);
  }

  auto timed_verbose(char const* file, int line) const {
    return typename LogPolicy::template timed_logger_type<logger::VERBOSE>(
        lgr_, logger::VERBOSE, file, line);
  }

  auto timed_debug(char const* file, int line) const {
    return typename LogPolicy::template timed_logger_type<logger::DEBUG>(
        lgr_, logger::DEBUG, file, line);
  }

  auto timed_trace(char const* file, int line) const {
    return typename LogPolicy::template timed_logger_type<logger::TRACE>(
        lgr_, logger::TRACE, file, line);
  }

  auto cpu_timed_error(char const* file, int line) const {
    return typename LogPolicy::template timed_logger_type<logger::ERROR>(
        lgr_, logger::ERROR, file, line, true);
  }

  auto cpu_timed_warn(char const* file, int line) const {
    return typename LogPolicy::template timed_logger_type<logger::WARN>(
        lgr_, logger::WARN, file, line, true);
  }

  auto cpu_timed_info(char const* file, int line) const {
    return typename LogPolicy::template timed_logger_type<logger::INFO>(
        lgr_, logger::INFO, file, line, true);
  }

  auto cpu_timed_verbose(char const* file, int line) const {
    return typename LogPolicy::template timed_logger_type<logger::VERBOSE>(
        lgr_, logger::VERBOSE, file, line, true);
  }

  auto cpu_timed_debug(char const* file, int line) const {
    return typename LogPolicy::template timed_logger_type<logger::DEBUG>(
        lgr_, logger::DEBUG, file, line, true);
  }

  auto cpu_timed_trace(char const* file, int line) const {
    return typename LogPolicy::template timed_logger_type<logger::TRACE>(
        lgr_, logger::TRACE, file, line, true);
  }

  logger& get_logger() const { return lgr_; }

 private:
  logger& lgr_;
};

#define LOG_DETAIL_LEVEL(level, lgr, method)                                   \
  if constexpr (std::decay_t<decltype(lgr)>::is_enabled_for(                   \
                    ::dwarfs::logger::level))                                  \
  lgr.method(__FILE__, __LINE__)

#define LOG_PROXY(policy, lgr) ::dwarfs::log_proxy<policy> log_(lgr)
#define LOG_PROXY_DECL(policy) ::dwarfs::log_proxy<policy> log_
#define LOG_PROXY_INIT(lgr) log_(lgr)
#define LOG_GET_LOGGER log_.get_logger()
#define LOG_FATAL log_.fatal(__FILE__, __LINE__)
#define LOG_ERROR LOG_DETAIL_LEVEL(ERROR, log_, error)
#define LOG_WARN LOG_DETAIL_LEVEL(WARN, log_, warn)
#define LOG_INFO LOG_DETAIL_LEVEL(INFO, log_, info)
#define LOG_VERBOSE LOG_DETAIL_LEVEL(VERBOSE, log_, verbose)
#define LOG_DEBUG LOG_DETAIL_LEVEL(DEBUG, log_, debug)
#define LOG_TRACE LOG_DETAIL_LEVEL(TRACE, log_, trace)
#define LOG_TIMED_ERROR log_.timed_error(__FILE__, __LINE__)
#define LOG_TIMED_WARN log_.timed_warn(__FILE__, __LINE__)
#define LOG_TIMED_INFO log_.timed_info(__FILE__, __LINE__)
#define LOG_TIMED_VERBOSE log_.timed_verbose(__FILE__, __LINE__)
#define LOG_TIMED_DEBUG log_.timed_debug(__FILE__, __LINE__)
#define LOG_TIMED_TRACE log_.timed_trace(__FILE__, __LINE__)
#define LOG_CPU_TIMED_ERROR log_.cpu_timed_error(__FILE__, __LINE__)
#define LOG_CPU_TIMED_WARN log_.cpu_timed_warn(__FILE__, __LINE__)
#define LOG_CPU_TIMED_INFO log_.cpu_timed_info(__FILE__, __LINE__)
#define LOG_CPU_TIMED_VERBOSE log_.cpu_timed_verbose(__FILE__, __LINE__)
#define LOG_CPU_TIMED_DEBUG log_.cpu_timed_debug(__FILE__, __LINE__)
#define LOG_CPU_TIMED_TRACE log_.cpu_timed_trace(__FILE__, __LINE__)

class prod_logger_policy : public MinimumLogLevelPolicy<logger::VERBOSE> {
 public:
  static std::string_view name() { return "prod"; }
};

class debug_logger_policy : public MinimumLogLevelPolicy<logger::TRACE> {
 public:
  static std::string_view name() { return "debug"; }
};

using logger_policies = std::tuple<debug_logger_policy, prod_logger_policy>;

template <class Base, template <class> class T, class LoggerPolicyList,
          class... Args>
std::unique_ptr<Base> make_unique_logging_object(logger& lgr, Args&&... args) {
  return detail::logging_class_factory::create<
      T, detail::unique_ptr_policy<Base>, LoggerPolicyList>(
      lgr, std::forward<Args>(args)...);
}

template <class Base, template <class> class T, class LoggerPolicyList,
          class... Args>
std::shared_ptr<Base> make_shared_logging_object(logger& lgr, Args&&... args) {
  return detail::logging_class_factory::create<
      T, detail::shared_ptr_policy<Base>, LoggerPolicyList>(
      lgr, std::forward<Args>(args)...);
}

std::string get_logger_context(char const* path, int line);
std::string get_current_time_string();

} // namespace dwarfs
