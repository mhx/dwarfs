#include <algorithm>
#include <cassert>
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

template <typename SourceT, typename BlockT>
class block_merger {
 public:
  using source_type = SourceT;
  using block_type = BlockT;

  virtual ~block_merger() = default;

  virtual void add(source_type src, block_type blk, bool is_last) = 0;
};

template <typename SourceT, typename BlockT>
class multi_queue_block_merger : public block_merger<SourceT, BlockT> {
 public:
  using source_type = SourceT;
  using block_type = BlockT;

  multi_queue_block_merger(size_t num_active_slots, size_t max_queued_blocks,
                           std::vector<source_type> const& sources,
                           std::function<void(block_type)> on_block_merged)
      : num_queueable_{max_queued_blocks}
      , sources_{sources.begin(), sources.end()}
      , active_(num_active_slots)
      , on_block_merged_{on_block_merged} {
    for (size_t i = 0; i < active_.size() && !sources_.empty(); ++i) {
      active_[i] = sources_.front();
      sources_.pop_front();
    }
  }

  void add(source_type src, block_type blk, bool is_last) override {
    std::unique_lock lock{mx_};

    cv_.wait(lock,
             [this, &src] { return source_distance(src) < num_queueable_; });

    --num_queueable_;

    queues_[src].emplace(std::move(blk), is_last);

    while (try_merge_block()) {
    }

    cv_.notify_all();
  }

 private:
  size_t source_distance(source_type src) const {
    auto ix = active_index_;
    size_t distance{0};
    while (active_[ix] && active_[ix].value() != src) {
      ++distance;
      ix = (ix + 1) % active_.size();
      if (ix == active_index_) {
        break;
      }
    }
    return distance;
  }

  bool try_merge_block() {
    auto const ix = active_index_;

    assert(active_[ix]);

    auto src = active_[ix].value();
    auto it = queues_.find(src);

    if (it == queues_.end() || it->second.empty()) {
      return false;
    }

    auto [blk, is_last] = std::move(it->second.front());
    it->second.pop();

    on_block_merged_(std::move(blk));

    ++num_queueable_;

    if (is_last) {
      queues_.erase(it);

      if (!sources_.empty()) {
        active_[ix] = sources_.front();
        sources_.pop_front();
      } else {
        active_[ix].reset();
      }
    }

    do {
      active_index_ = (active_index_ + 1) % active_.size();
    } while (active_index_ != ix && !active_[active_index_]);

    return active_index_ != ix || active_[active_index_];
  }

  std::mutex mx_;
  std::condition_variable cv_;
  size_t active_index_{0};
  size_t num_queueable_;
  std::unordered_map<source_type, std::queue<std::pair<block_type, bool>>>
      queues_;
  std::deque<source_type> sources_;
  std::vector<std::optional<source_type>> active_;
  std::function<void(block_type)> on_block_merged_;
};

void emitter(sync_queue<source>& sources, block_merger<size_t, block>& merger) {
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

      merger.add(blk.first, blk, is_last);

      if (is_last) {
        break;
      }
    }
  }
}

std::vector<block> do_run(size_t run, std::mt19937& delay_rng) {
  std::mt19937 rng(run);
  std::exponential_distribution<> sources_dist(0.1);
  std::exponential_distribution<> threads_dist(0.1);
  std::exponential_distribution<> inflight_dist(0.1);
  std::uniform_real_distribution<> speed_dist(0.1, 10.0);
  auto const num_sources{std::max<size_t>(1, sources_dist(rng))};
  auto const num_threads{std::max<size_t>(1, threads_dist(rng))};
  auto const max_in_flight{std::max<size_t>(1, inflight_dist(delay_rng))};

  std::vector<size_t> source_ids;
  sync_queue<source> sources;
  size_t total_blocks{0};
  std::chrono::nanoseconds total_time{};

  for (size_t i = 0; i < num_sources; ++i) {
    auto src = source(i, delay_rng, rng, 30, 5000.0 * speed_dist(delay_rng));
    total_blocks += src.num_blocks();
    total_time += src.total_time();
    source_ids.emplace_back(src.id());
    sources.wlock()->emplace(std::move(src));
  }

  std::vector<block> merged;

  // block_merger merger(num_threads, source_ids);
  multi_queue_block_merger<size_t, block> merger(
      num_threads, max_in_flight, source_ids,
      [&merged](block blk) { merged.emplace_back(std::move(blk)); });

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

  std::cout << fmt::format("sources: {}, threads: {}, max in flight: {} => "
                           "efficiency: {:.2f}%",
                           num_sources, num_threads, max_in_flight,
                           100.0 * efficiency)
            << "\n";

  return merged;
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

  for (size_t run = 0; run < 1000; ++run) {
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
