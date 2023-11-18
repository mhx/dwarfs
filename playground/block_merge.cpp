#include <algorithm>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <optional>
#include <queue>
#include <random>
#include <thread>
#include <vector>

#include <fmt/format.h>
#include <folly/Synchronized.h>

namespace {

using block = std::pair<size_t, size_t>;

template <typename T>
using sync_queue = folly::Synchronized<std::queue<T>>;

class source {
 public:
  source(size_t id, std::mt19937& delay_rng, std::mt19937& rng,
         size_t max_blocks = 20, double ips = 5000.0)
      : id_{id}
      , blocks_{init_blocks(delay_rng, rng, max_blocks, ips)} {}

  std::tuple<block, bool, double> next() {
    auto idx = idx_++;
    return {std::make_pair(id_, idx), idx_ >= blocks_.size(), blocks_[idx]};
  }

  size_t id() const { return id_; }

  size_t num_blocks() const { return blocks_.size(); }

  std::chrono::nanoseconds total_time() const {
    auto seconds = std::accumulate(begin(blocks_), end(blocks_), 0.0);
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::duration<double>(seconds));
  }

 private:
  static std::vector<double>
  init_blocks(std::mt19937& delay_rng, std::mt19937& rng, size_t max_blocks,
              double ips) {
    std::uniform_int_distribution<> idist(1, max_blocks);
    std::exponential_distribution<> edist(ips);
    std::vector<double> blocks;
    blocks.resize(idist(rng));
    std::generate(begin(blocks), end(blocks), [&] { return edist(delay_rng); });
    return blocks;
  }

  size_t idx_{0};
  size_t id_;
  std::vector<double> blocks_;
};

class block_merger_interface {
 public:
  virtual ~block_merger_interface() = default;

  virtual void add(block blk, bool is_last) = 0;
  virtual std::vector<block> const& merged() const = 0;
};

class block_merger : public block_merger_interface {
 public:
  static constexpr size_t const kEmpty{std::numeric_limits<size_t>::max()};

  block_merger(size_t num_slots, std::vector<size_t> const& sources)
      : sources_{sources.begin(), sources.end()}
      , active_(num_slots, kEmpty) {
    for (size_t i = 0; i < active_.size() && !sources_.empty(); ++i) {
      active_[i] = sources_.front();
      sources_.pop_front();
      // std::cout << "[" << i << "] -> " << active_[i] << "\n";
    }
  }

  void add(block blk, bool is_last) override {
    std::unique_lock lock{mx_};

    auto it = std::find(begin(active_), end(active_), blk.first);

    // std::cout << "size=" << active_.size() << "\n";
    // std::cout << "{" << blk.first << "," << blk.second << "}\n";
    // for (size_t i = 0; i < active_.size(); ++i) {
    //   std::cout << "[" << i << "] -> " << active_[i] << "\n";
    // }

    if (it == end(active_)) {
      throw std::runtime_error(
          fmt::format("unexpected source {}.{}", blk.first, blk.second));
    }

    auto ix = std::distance(begin(active_), it);

    cv_.wait(lock, [this, ix] { return ix == index_; });

    merged_.emplace_back(blk);

    if (is_last) {
      if (sources_.empty()) {
        active_[ix] = kEmpty;
      } else {
        active_[ix] = sources_.front();
        sources_.pop_front();
      }
    }

    for (;;) {
      index_ = (index_ + 1) % active_.size();
      if (index_ == ix || active_[index_] != kEmpty) {
        break;
      }
    }

    cv_.notify_all();
  }

  std::vector<block> const& merged() const override { return merged_; }

 private:
  std::mutex mx_;
  std::condition_variable cv_;
  size_t index_{0};
  std::deque<size_t> sources_;
  std::vector<size_t> active_;
  std::vector<block> merged_;
};

class block_merger_new : public block_merger_interface {
 public:
  static constexpr size_t const kEmpty{std::numeric_limits<size_t>::max()};

  block_merger_new(size_t num_slots, size_t max_in_flight,
                   std::vector<size_t> const& sources)
      : free_{max_in_flight}
      , sources_{sources.begin(), sources.end()}
      , active_(num_slots, kEmpty) {
    for (size_t i = 0; i < active_.size() && !sources_.empty(); ++i) {
      active_[i] = sources_.front();
      sources_.pop_front();
      // std::cout << "[" << i << "] -> " << active_[i] << "\n";
    }
  }

  void add(block blk, bool is_last) override {
    std::unique_lock lock{mx_};

    cv_.wait(lock, [this, &blk] {
      auto ix = index_;
      size_t dist{0};
      while (active_[ix] != blk.first) {
        ++dist;
        ix = (ix + 1) % active_.size();
        if (ix == index_) {
          break;
        }
      }
      // std::cout << "free: " << free_ << ", dist: " << dist << "\n";
      return free_ > dist;
    });

    --free_;

    queues_[blk.first].emplace(blk, is_last);

    // auto it = std::find(begin(active_), end(active_), blk.first);

    // if (it == end(active_)) {
    //   throw std::runtime_error(
    //       fmt::format("unexpected source {}.{}", blk.first, blk.second));
    // }

    // auto ix = std::distance(begin(active_), it);

    for (;;) {
      auto const ix = index_;

      auto src = active_[ix];

      if (src == kEmpty) {
        throw std::runtime_error("active source is empty");
      }

      auto it = queues_.find(src);

      if (it == queues_.end()) {
        // nothing yet...
        break;
      }

      if (it->second.empty()) {
        // nothing yet...
        break;
      }

      auto [q_blk, q_is_last] = it->second.front();
      it->second.pop();

      ++free_;

      merged_.emplace_back(q_blk);

      if (q_is_last) {
        // gone forever
        queues_.erase(it);

        if (sources_.empty()) {
          active_[ix] = kEmpty;
        } else {
          active_[ix] = sources_.front();
          sources_.pop_front();
        }
      }

      for (;;) {
        index_ = (index_ + 1) % active_.size();
        if (index_ == ix || active_[index_] != kEmpty) {
          break;
        }
      }

      if (index_ == ix && active_[index_] == kEmpty) {
        break;
      }
    }

    cv_.notify_all();
  }

  std::vector<block> const& merged() const override { return merged_; }

 private:
  std::mutex mx_;
  std::condition_variable cv_;
  size_t index_{0};
  size_t free_;
  std::unordered_map<size_t, std::queue<std::pair<block, bool>>> queues_;
  std::deque<size_t> sources_;
  std::vector<size_t> active_;
  std::vector<block> merged_;
};

void emitter(sync_queue<source>& sources, block_merger_interface& merger) {
  for (;;) {
    auto src = sources.withWLock([](auto&& q) {
      std::optional<source> src;

      if (!q.empty()) {
        src = std::move(q.front());
        q.pop();
      }

      return src;
    });

    if (!src) {
      break;
    }

    for (;;) {
      auto [blk, is_last, wait] = src->next();

      std::this_thread::sleep_for(std::chrono::duration<double>(wait));

      merger.add(blk, is_last);

      if (is_last) {
        break;
      }
    }
  }
}

std::vector<block> do_run(size_t run, std::mt19937& delay_rng) {
  constexpr size_t const num_sources{100};
  constexpr size_t const num_threads{16};
  constexpr size_t const max_in_flight{16};
  std::mt19937 rng(run);

  std::vector<size_t> source_ids;
  sync_queue<source> sources;
  size_t total_blocks{0};
  std::chrono::nanoseconds total_time{};

  for (size_t i = 0; i < num_sources; ++i) {
    auto src = source(i, delay_rng, rng);
    total_blocks += src.num_blocks();
    total_time += src.total_time();
    source_ids.emplace_back(src.id());
    sources.wlock()->emplace(std::move(src));
  }

  // block_merger merger(num_threads, source_ids);
  block_merger_new merger(num_threads, max_in_flight, source_ids);

  std::vector<std::thread> thr;

  auto t0 = std::chrono::steady_clock::now();

  for (size_t i = 0; i < num_threads; ++i) {
    thr.emplace_back([&] { emitter(sources, merger); });
  }

  for (auto& t : thr) {
    t.join();
  }

  auto t1 = std::chrono::steady_clock::now();

  auto elapsed = num_threads * (t1 - t0);
  auto efficiency =
      std::chrono::duration_cast<std::chrono::duration<double>>(total_time)
          .count() /
      std::chrono::duration_cast<std::chrono::duration<double>>(elapsed)
          .count();

  std::cout << fmt::format("efficiency: {:.2f}%", 100.0 * efficiency) << "\n";

  return merger.merged();
}

[[maybe_unused]] void dump(std::vector<block> const& blocks) {
  for (size_t i = 0; i < blocks.size(); ++i) {
    if (i > 0) {
      std::cout << ", ";
    }
    auto const& b = blocks[i];
    std::cout << b.first << "." << b.second;
  }
  std::cout << "\n";
}

} // namespace

int main() {
  std::random_device rd;
  std::mt19937 delay_rng(rd());

  for (size_t run = 0; run < 10; ++run) {
    std::cout << "[" << run << "] ref\n";
    auto ref = do_run(run, delay_rng);
    // dump(ref);
    for (size_t rep = 0; rep < 9; ++rep) {
      std::cout << "[" << run << "] test\n";
      auto test = do_run(run, delay_rng);
      // dump(test);
      if (test != ref) {
        throw std::runtime_error("boo");
      }
    }
  }

  return 0;
}
