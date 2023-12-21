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

#include <algorithm>
#include <limits>
#include <mutex>
#include <numeric>
#include <variant>

#include <folly/container/Enumerate.h>
#include <folly/experimental/Bits.h>

#include "dwarfs/compiler.h"
#include "dwarfs/logger.h"
#include "dwarfs/progress.h"
#include "dwarfs/similarity_ordering.h"
#include "dwarfs/worker_group.h"

namespace dwarfs {

namespace {

// TODO: move out of here
class job_tracker {
 public:
  explicit job_tracker(folly::Function<void()>&& on_jobs_done)
      : on_jobs_done_{std::move(on_jobs_done)} {}

  void start_job() {
    std::lock_guard lock(mx_);
    ++active_;
  }

  void finish_job() {
    bool all_done = false;
    {
      std::lock_guard lock(mx_);
      assert(active_ > 0);
      --active_;
      all_done = active_ == 0;
    }
    if (all_done) {
      on_jobs_done_();
    }
  }

 private:
  std::mutex mx_;
  size_t active_{0};
  folly::Function<void()> on_jobs_done_;
};

template <typename T, size_t N>
int distance(std::array<T, N> const& a, std::array<T, N> const& b) {
  int d = 0;
  for (size_t i = 0; i < N; ++i) {
    d += folly::popcount(a[i] ^ b[i]);
  }
  return d;
}

#ifdef DWARFS_MULTIVERSIONING
#ifdef __clang__
__attribute__((target_clones("avx512vpopcntdq", "popcnt", "default")))
#else
__attribute__((target_clones("popcnt", "default")))
#endif
#endif
int distance(std::array<uint64_t, 4> const& a, std::array<uint64_t, 4> const& b) {
  return distance<uint64_t, 4>(a, b);
}

template <typename GetI, typename GetK, typename Swap>
void order_by_shortest_path(size_t count, GetI&& geti, GetK&& getk,
                            Swap&& swapper) {
  for (size_t i = 0; i < count - 1; ++i) {
    auto bi = geti(i);
    int best_distance = std::numeric_limits<int>::max();
    size_t best_index = 0;

    for (size_t k = i + 1; k < count; ++k) {
      auto bk = getk(k);
      auto d = distance(*bi, *bk);
      if (d < best_distance) {
        best_distance = d;
        best_index = k;
        if (best_distance <= 1) {
          break;
        }
      }
    }

    if (best_index > 0 && i + 1 != best_index) {
      swapper(i + 1, best_index);
    }
  }
}

template <size_t Bits, typename BitsType = uint64_t,
          typename CountsType = uint32_t>
class basic_centroid {
 public:
  static_assert(Bits % (8 * sizeof(BitsType)) == 0);
  static constexpr size_t const array_size = Bits / (8 * sizeof(BitsType));
  using value_type = std::array<BitsType, array_size>;
  using bits_type = folly::Bits<BitsType>;

  basic_centroid() {
    std::fill(centroid_.begin(), centroid_.end(), 0);
    std::fill(bitcounts_.begin(), bitcounts_.end(), 0);
  }

  value_type const& value() const { return centroid_; };

  void add(value_type const& vec) {
    ++veccount_;
    for (size_t bit = 0; bit < Bits; ++bit) {
      bitcounts_[bit] += bits_type::test(vec.data(), bit) ? 1 : 0;
      if (bitcounts_[bit] > veccount_ / 2) {
        bits_type::set(centroid_.data(), bit);
      } else {
        bits_type::clear(centroid_.data(), bit);
      }
    }
  }

  auto distance_to(value_type const& vec) const {
    return distance(centroid_, vec);
  }

 private:
  value_type centroid_;
  std::array<CountsType, Bits> bitcounts_;
  CountsType veccount_;
};

template <size_t Bits, typename BitsType, typename CountsType,
          typename IndexValueType>
struct basic_cluster {
  using centroid_type = basic_centroid<Bits, BitsType, CountsType>;
  using index_value_type = IndexValueType;
  using index_type = std::vector<index_value_type>;

  basic_cluster() = default;
  explicit basic_cluster(index_type&& index)
      : index{std::move(index)} {}

  centroid_type centroid;
  index_type index;
};

template <typename ClusterType>
struct basic_cluster_tree_node {
  using cluster_type = ClusterType;
  using index_type = typename cluster_type::index_type;
  using index_value_type = typename cluster_type::index_value_type;
  using cluster_pointer = std::unique_ptr<cluster_type>;
  using children_vector = std::vector<basic_cluster_tree_node<cluster_type>>;

  basic_cluster_tree_node()
      : v{std::make_unique<cluster_type>()} {}
  explicit basic_cluster_tree_node(index_type&& index)
      : v{std::make_unique<cluster_type>(std::move(index))} {}

  children_vector const& children() const {
    return std::get<children_vector>(v);
  }
  children_vector& children() { return std::get<children_vector>(v); }

  cluster_type const& cluster() const { return *std::get<cluster_pointer>(v); }
  cluster_type& cluster() { return *std::get<cluster_pointer>(v); }

  bool is_leaf() const { return std::holds_alternative<cluster_pointer>(v); }

  std::string description() const {
    if (is_leaf()) {
      return fmt::format("{} items", cluster().index.size());
    } else {
      return fmt::format("{} children", children().size());
    }
  }

  index_value_type first_index() const {
    if (is_leaf()) {
      return cluster().index.front();
    }
    return children().front().first_index();
  }

  index_value_type last_index() const {
    if (is_leaf()) {
      return cluster().index.back();
    }
    return children().back().last_index();
  }

  std::variant<cluster_pointer, children_vector> v;
};

} // namespace

template <typename LoggerPolicy>
class similarity_ordering_ final : public similarity_ordering::impl {
 public:
  using index_value_type = similarity_ordering::index_value_type;
  using index_type = std::vector<index_value_type>;
  using duplicates_map = std::unordered_map<index_value_type, index_type>;
  using nilsimsa_element_view =
      basic_array_similarity_element_view<256, uint64_t>;
  using nilsimsa_cluster =
      basic_cluster<256, uint64_t, uint32_t, index_value_type>;
  using nilsimsa_cluster_tree_node = basic_cluster_tree_node<nilsimsa_cluster>;

  similarity_ordering_(logger& lgr, progress& prog, worker_group& wg,
                       similarity_ordering_options const& opts)
      : LOG_PROXY_INIT(lgr)
      , prog_{prog}
      , wg_{wg}
      , opts_{opts} {}

  void order_nilsimsa(nilsimsa_element_view const& ev, receiver<index_type> rec,
                      std::optional<index_type> index) const override;

 private:
  index_type build_index(similarity_element_view const& ev) const;
  duplicates_map
  find_duplicates(similarity_element_view const& ev, index_type& index) const;

  template <size_t Bits, typename BitsType>
  size_t
  total_distance(basic_array_similarity_element_view<Bits, BitsType> const& ev,
                 index_type const& index) const;

  template <size_t Bits, typename BitsType>
  void
  order_cluster(basic_array_similarity_element_view<Bits, BitsType> const& ev,
                index_type& index) const;

  template <size_t Bits, typename BitsType, typename CountsType>
  size_t order_tree_rec(
      basic_cluster_tree_node<
          basic_cluster<Bits, BitsType, CountsType, index_value_type>>& node,
      basic_array_similarity_element_view<Bits, BitsType> const& ev) const;

  template <size_t Bits, typename BitsType, typename CountsType>
  void cluster_by_distance(
      basic_cluster_tree_node<
          basic_cluster<Bits, BitsType, CountsType, index_value_type>>& node,
      basic_array_similarity_element_view<Bits, BitsType> const& ev,
      int max_distance) const;

  template <size_t Bits, typename BitsType, typename CountsType>
  void cluster_rec(
      basic_cluster_tree_node<
          basic_cluster<Bits, BitsType, CountsType, index_value_type>>& node,
      basic_array_similarity_element_view<Bits, BitsType> const& ev,
      std::shared_ptr<job_tracker> jt, int max_distance) const;

  template <size_t Bits, typename BitsType, typename CountsType>
  void cluster(basic_cluster_tree_node<basic_cluster<Bits, BitsType, CountsType,
                                                     index_value_type>>& root,
               basic_array_similarity_element_view<Bits, BitsType> const& ev,
               std::shared_ptr<job_tracker> jt) const;

  template <size_t Bits, typename BitsType, typename CountsType>
  void collect_rec(
      basic_cluster_tree_node<
          basic_cluster<Bits, BitsType, CountsType, index_value_type>>& node,
      basic_array_similarity_element_view<Bits, BitsType> const& ev,
      duplicates_map& dup, index_type& ordered,
      std::string const& indent) const;

  template <size_t Bits, typename BitsType>
  void order_impl(
      receiver<index_type>&& rec, std::optional<index_type> idx,
      basic_array_similarity_element_view<Bits, BitsType> const& ev) const;

  LOG_PROXY_DECL(LoggerPolicy);
  progress& prog_;
  worker_group& wg_;
  similarity_ordering_options const opts_;
};

template <typename LoggerPolicy>
auto similarity_ordering_<LoggerPolicy>::build_index(
    similarity_element_view const& ev) const -> index_type {
  index_type index;

  {
    auto tt = LOG_TIMED_TRACE;

    index.reserve(ev.size());
    for (index_value_type i = 0; i < ev.size(); ++i) {
      if (ev.exists(i)) {
        index.push_back(i);
      }
    }
    index.shrink_to_fit();

    tt << opts_.context << "build index: " << ev.size() << " -> "
       << index.size();
  }

  return index;
}

template <typename LoggerPolicy>
auto similarity_ordering_<LoggerPolicy>::find_duplicates(
    similarity_element_view const& ev, index_type& index) const
    -> duplicates_map {
  duplicates_map dm;

  {
    auto tt = LOG_TIMED_TRACE;

    std::sort(index.begin(), index.end(),
              [&ev](auto a, auto b) { return ev.bitvec_less(a, b); });

    tt << opts_.context << "sort index of " << index.size() << " elements";
  }

  {
    auto tt = LOG_TIMED_TRACE;

    if (!index.empty()) {
      auto src = index.begin();
      auto dst = src;

      while (++src != index.end()) {
        if (ev.bits_equal(*dst, *src)) {
          dm[*dst].push_back(*src);
        } else if (++dst != src) {
          *dst = std::move(*src);
        }
      }

      index.erase(++dst, index.end());
    }

    tt << opts_.context << "find duplicates: " << index.size() << " unique / "
       << dm.size() << " groups";
  }

  return dm;
}

template <typename LoggerPolicy>
template <size_t Bits, typename BitsType>
size_t similarity_ordering_<LoggerPolicy>::total_distance(
    basic_array_similarity_element_view<Bits, BitsType> const& ev,
    index_type const& index) const {
  size_t td = 0;

  if (!index.empty()) {
    auto* prev = &ev.get_bits(index[0]);

    for (size_t i = 1; i < index.size(); ++i) {
      auto& curr = ev.get_bits(index[i]);
      td += distance(*prev, curr);
      prev = &curr;
    }
  }

  return td;
}

template <typename LoggerPolicy>
template <size_t Bits, typename BitsType>
void similarity_ordering_<LoggerPolicy>::order_cluster(
    basic_array_similarity_element_view<Bits, BitsType> const& ev,
    index_type& index) const {
  using bitvec_type =
      typename basic_array_similarity_element_view<Bits, BitsType>::bitvec_type;

  if (!index.empty()) {
    // TODO: try simulated annealing again? reproducibly?

    std::sort(index.begin(), index.end(),
              [&ev](auto a, auto b) { return ev.order_less(a, b); });

    std::vector<bitvec_type const*> bits;
    bits.reserve(index.size());
    for (auto i : index) {
      bits.push_back(&ev.get_bits(i));
    }

    auto getter = [&bits](auto i) { return bits[i]; };
    auto swapper = [&bits, &index](auto i, auto k) {
      std::swap(bits[i], bits[k]);
      std::swap(index[i], index[k]);
    };

    order_by_shortest_path(index.size(), getter, getter, swapper);
  }
}

template <typename LoggerPolicy>
template <size_t Bits, typename BitsType, typename CountsType>
size_t similarity_ordering_<LoggerPolicy>::order_tree_rec(
    basic_cluster_tree_node<
        basic_cluster<Bits, BitsType, CountsType, index_value_type>>& node,
    basic_array_similarity_element_view<Bits, BitsType> const& ev) const {
  using node_type = std::decay_t<decltype(node)>;
  using bitvec_type =
      typename basic_array_similarity_element_view<Bits, BitsType>::bitvec_type;

  if (node.is_leaf()) {
    auto& cluster = node.cluster();
    return std::accumulate(
        cluster.index.begin(), cluster.index.end(), size_t(0),
        [&ev](size_t acc, size_t i) { return acc + ev.weight(i); });
  }

  auto& children = node.children();
  std::vector<
      std::tuple<bitvec_type const*, bitvec_type const*, node_type*, size_t>>
      bits;
  bits.reserve(children.size());
  size_t total_weight = 0;

  for (auto& cn : children) {
    auto weight = order_tree_rec(cn, ev);
    bits.emplace_back(&ev.get_bits(cn.first_index()),
                      &ev.get_bits(cn.last_index()), &cn, weight);
    total_weight += weight;
  }

  // all children of this node are ordered now

  std::stable_sort(bits.begin(), bits.end(), [](auto const& a, auto const& b) {
    return std::get<3>(a) > std::get<3>(b);
  });

  order_by_shortest_path(
      bits.size(), [&bits](auto i) { return std::get<1>(bits[i]); },
      [&bits](auto k) { return std::get<0>(bits[k]); },
      [&bits](auto i, auto k) { std::swap(bits[i], bits[k]); });

  std::vector<node_type> ordered_children;
  ordered_children.reserve(children.size());

  for (auto& b : bits) {
    ordered_children.emplace_back(std::move(*std::get<2>(b)));
  }

  children.swap(ordered_children);

  return total_weight;
}

template <typename LoggerPolicy>
template <size_t Bits, typename BitsType, typename CountsType>
void similarity_ordering_<LoggerPolicy>::cluster_by_distance(
    basic_cluster_tree_node<
        basic_cluster<Bits, BitsType, CountsType, index_value_type>>& node,
    basic_array_similarity_element_view<Bits, BitsType> const& ev,
    int max_distance) const {
  using node_type = std::decay_t<decltype(node)>;
  using cluster_type = typename node_type::cluster_type;
  typename node_type::children_vector children;

  auto td = LOG_TIMED_DEBUG;

  for (auto i : node.cluster().index) {
    auto const& vec = ev.get_bits(i);
    cluster_type* match = nullptr;
    int best_distance = std::numeric_limits<int>::max();
    cluster_type* best_match = nullptr;

    for (auto& c : children) {
      auto& cluster = c.cluster();
      auto d = cluster.centroid.distance_to(vec);

      if (d <= max_distance) {
        match = &cluster;
        break;
      } else if (d < best_distance) {
        best_distance = d;
        best_match = &cluster;
      }
    }

    if (!match) {
      if (children.size() < opts_.max_children) {
        auto& nn = children.emplace_back();
        match = &nn.cluster();
      } else {
        match = best_match;
      }
    }

    match->centroid.add(vec);
    match->index.push_back(i);
  }

  td << opts_.context << "cluster_by_distance: " << node.cluster().index.size()
     << " -> " << children.size() << ")";

  node.v = std::move(children);
}

template <typename LoggerPolicy>
template <size_t Bits, typename BitsType, typename CountsType>
void similarity_ordering_<LoggerPolicy>::cluster_rec(
    basic_cluster_tree_node<
        basic_cluster<Bits, BitsType, CountsType, index_value_type>>& node,
    basic_array_similarity_element_view<Bits, BitsType> const& ev,
    std::shared_ptr<job_tracker> jt, int max_distance) const {
  cluster_by_distance(node, ev, max_distance);

  for (auto& cn : node.children()) {
    if (max_distance > 1 &&
        cn.cluster().index.size() > opts_.max_cluster_size) {
      jt->start_job();
      wg_.add_job([this, &cn, &ev, jt, md = max_distance / 2] {
        cluster_rec(cn, ev, jt, md);
        jt->finish_job();
      });
    } else if (cn.cluster().index.size() > 1) {
      jt->start_job();
      wg_.add_job([this, &index = cn.cluster().index, &ev, jt] {
        order_cluster(ev, index);
        jt->finish_job();
      });
    }
  }
}

template <typename LoggerPolicy>
template <size_t Bits, typename BitsType, typename CountsType>
void similarity_ordering_<LoggerPolicy>::cluster(
    basic_cluster_tree_node<
        basic_cluster<Bits, BitsType, CountsType, index_value_type>>& root,
    basic_array_similarity_element_view<Bits, BitsType> const& ev,
    std::shared_ptr<job_tracker> jt) const {
  jt->start_job();
  wg_.add_job([this, &root, &ev, jt] {
    cluster_rec(root, ev, jt, Bits / 2);
    jt->finish_job();
  });
}

template <typename LoggerPolicy>
template <size_t Bits, typename BitsType, typename CountsType>
void similarity_ordering_<LoggerPolicy>::collect_rec(
    basic_cluster_tree_node<
        basic_cluster<Bits, BitsType, CountsType, index_value_type>>& node,
    basic_array_similarity_element_view<Bits, BitsType> const& ev,
    duplicates_map& dup, index_type& ordered, std::string const& indent) const {
  if (node.is_leaf()) {
    for (auto e : node.cluster().index) {
      LOG_TRACE << opts_.context << indent << "  " << ev.description(e)
                << " -> "
                << node.cluster().centroid.distance_to(ev.get_bits(e));

      ordered.push_back(e);

      if (auto it = dup.find(e); it != dup.end()) {
        auto& dupvec = it->second;

        std::sort(dupvec.begin(), dupvec.end(),
                  [&ev](auto a, auto b) { return ev.order_less(a, b); });

        for (auto i : dupvec) {
          LOG_TRACE << opts_.context << indent << "  + " << ev.description(i)
                    << " -> "
                    << node.cluster().centroid.distance_to(ev.get_bits(i));
          ordered.push_back(i);
        }
      }
    }
  } else {
    // TODO: order children, probably do this as a separate (parallel)
    //       step before collecting

    for (auto const& [i, cn] : folly::enumerate(node.children())) {
      LOG_TRACE << opts_.context << indent << "[" << i << "] "
                << cn.description();
      collect_rec(cn, ev, dup, ordered, indent + "  ");
    }
  }
}

template <typename LoggerPolicy>
template <size_t Bits, typename BitsType>
void similarity_ordering_<LoggerPolicy>::order_impl(
    receiver<index_type>&& rec, std::optional<index_type> idx,
    basic_array_similarity_element_view<Bits, BitsType> const& ev) const {
  index_type index;

  if (idx) {
    index = *idx;
  } else {
    index = build_index(ev);
  }

  LOG_DEBUG << opts_.context
            << "total distance before ordering: " << total_distance(ev, index);

  size_t size_hint = index.size();
  auto duplicates = find_duplicates(ev, index);
  auto root = std::make_shared<nilsimsa_cluster_tree_node>(std::move(index));

  auto jt = std::make_shared<job_tracker>(
      [this, size_hint, &ev, rec = std::move(rec), root,
       dup = std::move(duplicates)]() mutable {
        {
          auto tv = LOG_TIMED_VERBOSE;
          order_tree_rec(*root, ev);
          tv << opts_.context << "nilsimsa recursive ordering finished";
        }
        index_type rv;
        rv.reserve(size_hint);
        collect_rec(*root, ev, dup, rv, "");
        LOG_DEBUG << opts_.context << "total distance after ordering: "
                  << total_distance(ev, rv);
        rec.set_value(std::move(rv));
      });

  cluster(*root, ev, jt);
}

template <typename LoggerPolicy>
void similarity_ordering_<LoggerPolicy>::order_nilsimsa(
    nilsimsa_element_view const& ev, receiver<index_type> rec,
    std::optional<index_type> index) const {
  wg_.add_job(
      [this, rec = std::move(rec), idx = std::move(index), &ev]() mutable {
        order_impl(std::move(rec), std::move(idx), ev);
      });
}

similarity_ordering::similarity_ordering(
    logger& lgr, progress& prog, worker_group& wg,
    similarity_ordering_options const& opts)
    : impl_(make_unique_logging_object<impl, similarity_ordering_,
                                       logger_policies>(lgr, prog, wg, opts)) {}

} // namespace dwarfs
