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

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

#include "dwarfs/util.h"

namespace dwarfs {

class logger {
 public:
  enum level_type : unsigned { ERROR, WARN, INFO, DEBUG, TRACE };

  virtual ~logger() = default;

  virtual void write(level_type level, const std::string& output) = 0;

  const std::string& policy_name() const { return policy_name_; }

  template <class Policy>
  void set_policy() // TODO: construction time arg?
  {
    policy_name_ = Policy::name();
  }

  void set_policy_name(const std::string& name) // TODO: construction time arg?
  {
    policy_name_ = name;
  }

  static level_type parse_level(const std::string& level);

 private:
  std::string policy_name_; // TODO: const?
};

class stream_logger : public logger {
 public:
  stream_logger(std::ostream& os = std::cerr, level_type threshold = WARN);

  void write(level_type level, const std::string& output) override;

  void set_threshold(level_type threshold);

 private:
  std::ostream& os_;
  std::mutex mx_;
  std::atomic<level_type> threshold_;
};

class level_logger {
 public:
  level_logger(logger& lgr, logger::level_type level)
      : data_(std::make_unique<data>(lgr, level)) {}

  level_logger(level_logger&& ll)
      : data_(std::move(ll.data_)) {}

  ~level_logger() { data_->lgr.write(data_->level, data_->oss.str()); }

  template <typename T>
  level_logger& operator<<(const T& val) {
    data_->oss << val;
    return *this;
  }

 private:
  struct data {
    data(logger& lgr, logger::level_type level)
        : lgr(lgr)
        , level(level) {}

    logger& lgr;
    std::ostringstream oss;
    const logger::level_type level;
  };

  std::unique_ptr<data> data_;
};

class timed_level_logger {
 public:
  timed_level_logger(logger& lgr, logger::level_type level)
      : data_(std::make_unique<data>(lgr, level)) {}

  timed_level_logger(timed_level_logger&& ll)
      : data_(std::move(ll.data_)) {}

  ~timed_level_logger() {
    std::chrono::duration<double> sec =
        std::chrono::high_resolution_clock::now() - data_->start_time;
    data_->oss << " [" << time_with_unit(sec.count()) << "]";
    data_->lgr.write(data_->level, data_->oss.str());
  }

  template <typename T>
  timed_level_logger& operator<<(const T& val) {
    data_->oss << val;
    return *this;
  }

 private:
  struct data {
    data(logger& lgr, logger::level_type level)
        : lgr(lgr)
        , level(level)
        , start_time(std::chrono::high_resolution_clock::now()) {}

    logger& lgr;
    std::ostringstream oss;
    const logger::level_type level;
    std::chrono::time_point<std::chrono::high_resolution_clock> start_time;
  };

  std::unique_ptr<data> data_;
};

class no_logger {
 public:
  no_logger(logger&, logger::level_type) {}

  template <typename T>
  no_logger& operator<<(const T&) {
    return *this;
  }
};

namespace detail {

template <bool LoggingEnabled>
using logger_type =
    typename std::conditional<LoggingEnabled, level_logger, no_logger>::type;

template <bool LoggingEnabled>
using timed_logger_type =
    typename std::conditional<LoggingEnabled, timed_level_logger,
                              no_logger>::type;
} // namespace detail

template <unsigned MinLogLevel>
class MinimumLogLevelPolicy {
 public:
  template <unsigned Level>
  using logger = detail::logger_type<Level <= MinLogLevel>;

  template <unsigned Level>
  using timed_logger = detail::timed_logger_type<Level <= MinLogLevel>;
};

template <typename LogPolicy>
class log_proxy {
 public:
  log_proxy(logger& lgr)
      : lgr_(lgr) {}

  auto error() const {
    return
        typename LogPolicy::template logger<logger::ERROR>(lgr_, logger::ERROR);
  }

  auto warn() const {
    return
        typename LogPolicy::template logger<logger::WARN>(lgr_, logger::WARN);
  }

  auto info() const {
    return
        typename LogPolicy::template logger<logger::INFO>(lgr_, logger::INFO);
  }

  auto debug() const {
    return
        typename LogPolicy::template logger<logger::DEBUG>(lgr_, logger::DEBUG);
  }

  auto trace() const {
    return
        typename LogPolicy::template logger<logger::TRACE>(lgr_, logger::TRACE);
  }

  auto timed_error() const {
    return typename LogPolicy::template timed_logger<logger::ERROR>(
        lgr_, logger::ERROR);
  }

  auto timed_warn() const {
    return typename LogPolicy::template timed_logger<logger::WARN>(
        lgr_, logger::WARN);
  }

  auto timed_info() const {
    return typename LogPolicy::template timed_logger<logger::INFO>(
        lgr_, logger::INFO);
  }

  auto timed_debug() const {
    return typename LogPolicy::template timed_logger<logger::DEBUG>(
        lgr_, logger::DEBUG);
  }

  auto timed_trace() const {
    return typename LogPolicy::template timed_logger<logger::TRACE>(
        lgr_, logger::TRACE);
  }

 private:
  logger& lgr_;
};

class prod_logger_policy : public MinimumLogLevelPolicy<logger::INFO> {
 public:
  static std::string name() { return "prod"; }
};

class debug_logger_policy : public MinimumLogLevelPolicy<logger::TRACE> {
 public:
  static std::string name() { return "debug"; }
};

using logger_policies = std::tuple<debug_logger_policy, prod_logger_policy>;

template <class T>
struct unique_ptr_policy {
  using return_type = std::unique_ptr<T>;

  template <class U, class... Args>
  static return_type create(Args&&... args) {
    return std::make_unique<U>(std::forward<Args>(args)...);
  }
};

template <class T>
struct shared_ptr_policy {
  using return_type = std::shared_ptr<T>;

  template <class U, class... Args>
  static return_type create(Args&&... args) {
    return std::make_shared<U>(std::forward<Args>(args)...);
  }
};

template <template <class> class T, class CreatePolicy, class LoggerPolicyList,
          size_t N>
struct logging_class_factory {
  template <class... Args>
  static typename CreatePolicy::return_type
  create(logger& lgr, Args&&... args) {
    if (std::tuple_element<N - 1, LoggerPolicyList>::type::name() ==
        lgr.policy_name()) {
      using obj_type =
          T<typename std::tuple_element<N - 1, LoggerPolicyList>::type>;
      return CreatePolicy::template create<obj_type>(
          lgr, std::forward<Args>(args)...);
    }

    return logging_class_factory<T, CreatePolicy, LoggerPolicyList,
                                 N - 1>::create(lgr,
                                                std::forward<Args>(args)...);
  }
};

template <template <class> class T, class CreatePolicy, class LoggerPolicyList>
struct logging_class_factory<T, CreatePolicy, LoggerPolicyList, 0> {
  template <class... Args>
  static typename CreatePolicy::return_type create(logger& lgr, Args&&...) {
    throw std::runtime_error("no such logger policy: " + lgr.policy_name());
  }
};

template <class Base, template <class> class T, class LoggerPolicyList,
          class... Args>
std::unique_ptr<Base> make_unique_logging_object(logger& lgr, Args&&... args) {
  return logging_class_factory<
      T, unique_ptr_policy<Base>, LoggerPolicyList,
      std::tuple_size<LoggerPolicyList>::value>::create(lgr,
                                                        std::forward<Args>(
                                                            args)...);
}

template <class Base, template <class> class T, class LoggerPolicyList,
          class... Args>
std::shared_ptr<Base> make_shared_logging_object(logger& lgr, Args&&... args) {
  return logging_class_factory<
      T, shared_ptr_policy<Base>, LoggerPolicyList,
      std::tuple_size<LoggerPolicyList>::value>::create(lgr,
                                                        std::forward<Args>(
                                                            args)...);
}
} // namespace dwarfs
