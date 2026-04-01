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

#pragma once

#include <chrono>
#include <concepts>
#include <cstddef>
#include <functional>
#include <future>
#include <limits>
#include <memory>
#include <optional>
#include <string_view>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <dwarfs/internal/move_only_function.h>
#include <dwarfs/internal/worker_group_fwd.h>

namespace dwarfs {

class logger;
class os_access;

namespace internal {

struct worker_group_options {
  size_t num_workers = 1;
  size_t max_queue_len = std::numeric_limits<size_t>::max();
  int niceness = 0;
};

namespace detail {

class worker_context {
 public:
  virtual ~worker_context() = default;
};

template <typename... Args>
class worker_context_model final : public worker_context {
 public:
  using tuple_type = std::tuple<Args...>;

  template <typename... Ts>
    requires(sizeof...(Ts) == sizeof...(Args) &&
             (std::constructible_from<Args, Ts &&> && ...))
  explicit worker_context_model(Ts&&... args)
      : args_(std::forward<Ts>(args)...) {}

  tuple_type& args() noexcept { return args_; }
  tuple_type const& args() const noexcept { return args_; }

 private:
  tuple_type args_;
};

class worker_group_impl {
 public:
  using queued_job = move_only_function<void(worker_context&)>;
  using state_factory =
      move_only_function<std::unique_ptr<worker_context>(size_t)>;

  virtual ~worker_group_impl() = default;

  virtual void stop() = 0;
  virtual void wait() = 0;
  virtual bool running() const = 0;
  virtual bool add_job(queued_job&& job) = 0;
  virtual size_t size() const = 0;
  virtual std::chrono::nanoseconds get_cpu_time(std::error_code& ec) const = 0;
  virtual std::optional<std::chrono::nanoseconds> try_get_cpu_time() const = 0;
  virtual bool set_affinity(std::vector<int> const& cpus) = 0;
};

std::unique_ptr<worker_group_impl>
make_worker_group_impl(logger& lgr, os_access const& os,
                       std::string_view group_name,
                       worker_group_options const& options,
                       worker_group_impl::state_factory&& sf);

template <typename F, typename Job>
concept forwards_to_job = requires(F&& f) { Job(std::forward<F>(f)); };

template <typename Factory, typename StateType>
concept state_factory_for =
    std::invocable<Factory&, size_t> &&
    std::constructible_from<StateType, std::invoke_result_t<Factory&, size_t>>;

} // namespace detail

/**
 * A group of worker threads.
 *
 * Args... are the per-worker arguments passed to every queued job.
 *
 * Example:
 *   basic_worker_group<archive*> wg(lgr, os, "archiver", a);
 *   wg.add_job([](archive* a) { ... });
 */
template <typename... Args>
class basic_worker_group {
 public:
  static_assert((!std::is_rvalue_reference_v<Args> && ...),
                "basic_worker_group arguments must not be rvalue references");

  using state_type = std::tuple<Args...>;
  using job_type = move_only_function<void(Args...)>;
  using options_type = worker_group_options;

  basic_worker_group() = default;
  ~basic_worker_group() = default;

  basic_worker_group(basic_worker_group&&) noexcept = default;
  basic_worker_group& operator=(basic_worker_group&&) noexcept = default;

  explicit operator bool() const noexcept { return static_cast<bool>(impl_); }

  /**
   * Zero-argument worker group.
   */
  basic_worker_group(logger& lgr, os_access const& os,
                     std::string_view group_name, options_type options = {})
    requires(sizeof...(Args) == 0)
      : basic_worker_group(lgr, os, group_name, options,
                           [](size_t) -> state_type { return {}; }) {}

  /**
   * Construct from a shared set of worker arguments.
   *
   * These arguments are copied into each worker's private state.
   * For non-copyable or per-worker-custom state, use the factory overload.
   */
  template <typename... Ts>
    requires(sizeof...(Args) > 0 && sizeof...(Ts) == sizeof...(Args) &&
             (std::constructible_from<Args, Ts &&> && ...) &&
             (std::copy_constructible<Args> && ...))
  basic_worker_group(logger& lgr, os_access const& os,
                     std::string_view group_name, Ts&&... args)
      : basic_worker_group(lgr, os, group_name, options_type{},
                           std::forward<Ts>(args)...) {}

  template <typename... Ts>
    requires(sizeof...(Args) > 0 && sizeof...(Ts) == sizeof...(Args) &&
             (std::constructible_from<Args, Ts &&> && ...) &&
             (std::copy_constructible<Args> && ...))
  basic_worker_group(logger& lgr, os_access const& os,
                     std::string_view group_name, options_type options,
                     Ts&&... args)
      : basic_worker_group(
            lgr, os, group_name, options,
            make_cloning_state_factory(std::forward<Ts>(args)...)) {}

  /**
   * Advanced constructor for per-worker state creation.
   *
   * The factory returns the tuple of arguments for worker i.
   */
  template <detail::state_factory_for<state_type> Factory>
  basic_worker_group(logger& lgr, os_access const& os,
                     std::string_view group_name, Factory&& state_factory)
      : basic_worker_group(lgr, os, group_name, options_type{},
                           std::forward<Factory>(state_factory)) {}

  template <detail::state_factory_for<state_type> Factory>
  basic_worker_group(logger& lgr, os_access const& os,
                     std::string_view group_name, options_type options,
                     Factory&& state_factory)
      : impl_{detail::make_worker_group_impl(
            lgr, os, group_name, options,
            make_state_factory(std::forward<Factory>(state_factory)))} {}

  void stop() { impl_->stop(); }

  void wait() { impl_->wait(); }

  bool running() const { return impl_->running(); }

  template <detail::forwards_to_job<job_type> F>
  bool add_job(F&& job) {
    auto wrapped = job_type(std::forward<F>(job));

    return impl_->add_job(
        [wrapped = std::move(wrapped)](detail::worker_context& ctx) mutable {
          auto& typed_ctx =
              static_cast<detail::worker_context_model<Args...>&>(ctx);
          std::apply(wrapped, typed_ctx.args());
        });
  }

  template <typename T>
    requires(sizeof...(Args) == 0)
  bool add_job(std::packaged_task<T()>&& task) {
    return add_job([task = std::move(task)]() mutable { task(); });
  }

  size_t size() const { return impl_->size(); }

  std::chrono::nanoseconds get_cpu_time(std::error_code& ec) const {
    return impl_->get_cpu_time(ec);
  }

  std::optional<std::chrono::nanoseconds> try_get_cpu_time() const {
    return impl_->try_get_cpu_time();
  }

  bool set_affinity(std::vector<int> const& cpus) {
    return impl_->set_affinity(cpus);
  }

 private:
  template <typename Factory>
  static detail::worker_group_impl::state_factory
  make_state_factory(Factory&& factory) {
    return
        [factory = std::forward<Factory>(factory)](
            size_t index) mutable -> std::unique_ptr<detail::worker_context> {
          state_type state(std::invoke(factory, index));

          return std::apply(
              [](auto&&... args) -> std::unique_ptr<detail::worker_context> {
                return std::make_unique<detail::worker_context_model<Args...>>(
                    std::forward<decltype(args)>(args)...);
              },
              std::move(state));
        };
  }

  template <typename... Ts>
  static auto make_cloning_state_factory(Ts&&... args) {
    auto state = state_type(std::forward<Ts>(args)...);
    return [state = std::move(state)](size_t) -> state_type { return state; };
  }

  std::unique_ptr<detail::worker_group_impl> impl_;
};

} // namespace internal
} // namespace dwarfs
