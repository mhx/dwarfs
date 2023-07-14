#include <algorithm>
#include <array>
#include <cassert>
#include <charconv>
#include <execution>
#include <iostream>
#include <memory>
#include <random>
#include <unordered_map>
#include <unordered_set>

#include <folly/lang/Bits.h>
#include <folly/String.h>

#include <fmt/format.h>

#include "dwarfs/logger.h"
#include "dwarfs/nilsimsa.h"

template <typename It>
void circular_reverse(It beg, It end, size_t i, size_t k) {
  if (i < k) {
    std::reverse(beg + i, beg + k + 1);
  } else if (i > k) {
    size_t const num_front = k + 1;
    size_t const num_back = std::distance(beg, end) - i;

    It f1, f2;
    std::reverse_iterator<It> g1;
    It r1, r2;

    if (num_front > num_back) {
      auto const delta = num_front - num_back;
      f1 = beg + delta;
      f2 = beg + num_front;
      g1 = std::make_reverse_iterator(end);
      r1 = beg;
      r2 = r1 + delta;
    } else {
      auto const delta = num_back - num_front;
      f1 = beg;
      f2 = beg + num_front;
      g1 = std::make_reverse_iterator(end) + delta;
      r1 = end - delta;
      r2 = end;
    }

    std::swap_ranges(f1, f2, g1);
    std::reverse(r1, r2);
  }
}

struct item {
  std::array<uint64_t, 4> vec;
  size_t size;
  std::string name;
};

bool operator<(item const& a, item const& b) {
  return a.vec < b.vec || (a.vec == b.vec && (a.size > b.size || (a.size == b.size && a.name < b.name)));
}

template <typename T, size_t N>
int distance(std::array<T, N> const& a, std::array<T, N> const& b) {
  int d = 0;
  for (size_t i = 0; i < N; ++i) {
    d += folly::popcount(a[i] ^ b[i]);
  }
  return d;
}

int distance(item const& a, item const& b) {
  return distance(a.vec, b.vec);
}

int distance(std::unique_ptr<item> const& a, std::unique_ptr<item> const& b) {
  return distance(a->vec, b->vec);
}

int distance(std::unique_ptr<item const> const& a, std::unique_ptr<item const> const& b) {
  return distance(a->vec, b->vec);
}

int compute_total_energy(std::vector<std::unique_ptr<item>> const& items) {
  int total_energy = 0;

  for (size_t i = 0; i < items.size(); ++i) {
    auto const& it = items[i];
    auto const& prev = items[i > 0 ? i - 1 : items.size() - 1];
    total_energy += distance(prev, it);
  }

  return total_energy;
}

void analyze(std::vector<std::unique_ptr<item>> const& items) {
  int total_energy = 0;
  std::array<int, 255> hist;

  std::fill(hist.begin(), hist.end(), 0);

  for (size_t i = 0; i < items.size(); ++i) {
    auto const& it = items[i];
    auto const& prev = items[i > 0 ? i - 1 : items.size() - 1];
    auto d = distance(prev, it);
    total_energy += d;
    ++hist[d];
  }

  std::cout << "total energy: " << total_energy << " (" << static_cast<double>(total_energy)/items.size() << "/item)\n";

  for (size_t i = 0; i < hist.size(); ++i) {
    if (hist[i]) {
      std::cout << "[" << i << "] " << hist[i] << "\n";
    }
  }

}

void annealing(std::vector<std::unique_ptr<item>> items) {
  int total_energy = compute_total_energy(items);
  std::cout << "total energy: " << total_energy << "\n";

  std::mt19937 rng;
  std::uniform_int_distribution<> uidist(0, items.size() - 1);
  std::uniform_real_distribution<> urdist(0.0, 1.0);

  double T = 1;
  double alpha = 0.99999999;
  size_t accepted = 0, rejected = 0;

  bool neigh_only = false;

  while (true) {
    bool accept = false;
    int delta;

    int i, k;

    if (!neigh_only) {
      i = uidist(rng);
      k = uidist(rng);

      auto& i0 = items[i > 0 ? i - 1 : items.size() - 1];
      auto& i1 = items[i];
      auto& i2 = items[i + 1 < items.size() ? i + 1 : 0];

      auto& k0 = items[k > 0 ? k - 1 : items.size() - 1];
      auto& k1 = items[k];
      auto& k2 = items[k + 1 < items.size() ? k + 1 : 0];

      int cur_distance, new_distance;

                        //    a  b  c  d
      if (&i1 == &k0) { //    i0 i1 i2
                        //       k0 k1 k2
        cur_distance = distance(i0, i1) + distance(i2, k2);
        new_distance = distance(i0, i2) + distance(i1, k2);
                               // a  b  c  d
      } else if (&i1 == &k2) { //    i0 i1 i2
                               // k0 k1 k2
        cur_distance = distance(k0, k1) + distance(k2, i2);
        new_distance = distance(k0, k2) + distance(k1, i2);

      } else {
        cur_distance = distance(i0, i1) + distance(i1, i2)
                     + distance(k0, k1) + distance(k1, k2);
        new_distance = distance(i0, k1) + distance(k1, i2)
                     + distance(k0, i1) + distance(i1, k2);
      }
      delta = new_distance - cur_distance;

      if (delta < 0) {
        accept = true;
      } else {
        // TODO: replace std::exp with cheaper function
        accept = urdist(rng) < std::exp(-delta/T);
      }

      if (accept) {
        std::swap(i1, k1);
      }
    } else {
      i = uidist(rng);
      auto& a = items[i];
      auto& b = items[(i + 1) % items.size()]; // TODO: optimize...
      auto& c = items[(i + 2) % items.size()];
      auto& d = items[(i + 3) % items.size()];

      // we want to swap b and c
      // the distance between b and c won't be affected

      auto cur_distance = distance(a, b) + distance(c, d);
      auto new_distance = distance(a, c) + distance(b, d);
      delta = new_distance - cur_distance;

      if (delta < 0) {
        accept = true;
      } else {
        // TODO: replace std::exp with cheaper function
        accept = urdist(rng) < std::exp(-delta/T);
      }

      if (accept) {
        std::swap(b, c);
      }
    }

    if (accept) {
      total_energy += delta;
      ++accepted;

    } else {
      ++rejected;
    }

    if ((accepted + rejected) % 65536 == 0) {
      int te2 = 0;

      std::cout << "T=" << T << ", total energy/item: " << static_cast<double>(total_energy)/items.size() << " (a=" << accepted << ", r=" << rejected <<  ") -> " << total_energy << "/" << te2 << " [" << i << "/" << k << "]\n";

      // if (total_energy != te2) {
      //   return 0;
      // }
    }

    T *= alpha;
  }
}

void annealing2(std::vector<std::unique_ptr<item>> items) {
  int total_energy = compute_total_energy(items);
  std::cout << "total energy: " << total_energy << "\n";

  std::mt19937 rng;
  std::uniform_int_distribution<> uidist(0, items.size() - 1);
  std::uniform_real_distribution<> urdist(0.0, 1.0);

  double T = 1;
  double alpha = 0.99999999;
  size_t accepted = 0, rejected = 0;

  while (true) {
    auto i = uidist(rng);
    auto k = uidist(rng);

    if (i == k) [[unlikely]] {
      continue;
    }

    auto const ip = i > 0 ? i - 1 : items.size() - 1;
    auto const ks = k + 1 < items.size() ? k + 1 : 0;

    if (ks == i) [[unlikely]] {
      continue;
    }

    auto& i_pred = items[ip];
    auto& i_item = items[i];
    auto& k_item = items[k];
    auto& k_succ = items[ks];

    //       i_p i ...... k k_s
    //       i_p k ...... i k_s
    auto cur_distance = distance(i_pred, i_item) + distance(k_item, k_succ);
    auto new_distance = distance(i_pred, k_item) + distance(i_item, k_succ);
    auto delta = new_distance - cur_distance;

    bool accept = false;

    if (delta < 0) {
      accept = true;
    } else {
      // TODO: replace std::exp with cheaper function
      accept = urdist(rng) < std::exp(-delta/T);
    }

    if (accept) {
      total_energy += delta;
      circular_reverse(items.begin(), items.end(), i, k);
      ++accepted;
    } else {
      ++rejected;
    }

    if (/*accept ||*/ (accepted + rejected) % 65536 == 0) {
      int te2 = 0; // compute_total_energy(items);

      std::cout << "T=" << T << ", total energy/item: " << static_cast<double>(total_energy)/items.size() << " (a=" << accepted << ", r=" << rejected <<  ") -> " << total_energy << "/" << te2 << " [" << ip << "/" << i << "/" << k << "/" << ks << "]\n";

      // if (total_energy != te2) {
      //   return;
      // }
    }

    T *= alpha;
  }
}

void brute_force(std::vector<std::unique_ptr<item>> items) {
  int total_energy = compute_total_energy(items);
  std::cout << "total energy: " << total_energy << "\n";

  for (size_t i = 0; i < items.size() - 1; ++i) {
    int min_d = 256;
    size_t min_k = 0;
    for (size_t k = i + 1; k < items.size(); ++k) {
      auto d = distance(items[i], items[k]);
      if (d < min_d) {
        min_d = d;
        min_k = k;
        if (d == 1) {
          break;
        }
      }
    }
    if (min_k > i + 1) {
      std::swap(items[i + 1], items[min_k]);
    }

    if (i % 256 == 0) {
      total_energy = compute_total_energy(items);
      std::cout << "[" << i << "/" << items.size() << "] total energy: " << total_energy << "\n";
    }
  }

  total_energy = compute_total_energy(items);
  std::cout << "final total energy: " << total_energy << "\n";
  std::cout << "final total energy: " << static_cast<double>(total_energy)/items.size() << "/item\n";
}

void reverse_test() {
  std::vector<std::tuple<size_t, size_t, std::vector<int>>> const test_cases {
    {3, 7, {1, 2, 3, 8, 7, 6, 5, 4, 9}},
    {7, 3, {2, 1, 9, 8, 5, 6, 7, 4, 3}},
    {5, 1, {7, 6, 3, 4, 5, 2, 1, 9, 8}},
    {6, 2, {9, 8, 7, 4, 5, 6, 3, 2, 1}},
  };

  for (auto& [i, k, ref] : test_cases) {
    std::vector<int> in(9);
    std::iota(in.begin(), in.end(), 1);

    circular_reverse(in.begin(), in.end(), i, k);

    std::cout << "[" << i << "," << k << "] -> " << folly::join(", ", in) << " -> " << (in == ref ? "OK" : "FAIL") << "\n";
  }
}

template <typename It>
void bitwise_rotate_left(It beg, It end, size_t count) {
  using value_type = std::decay_t<decltype(*beg)>;
  constexpr auto const value_bits = 8*sizeof(value_type);
  constexpr value_type const one{1};
  assert(count < std::distance(beg, end)*value_bits);
  if (count >= value_bits) {
    auto distance = count / value_bits;
    std::rotate(beg, beg + distance, end);
    count %= value_bits;
  }
  if (count > 0) {
    auto const shift = value_bits - count;
    value_type const mask = (one << count) - one;
    value_type const leftmost_bits = *beg >> shift;
    assert(beg != end);
    auto next = beg + 1;
    while (next != end) {
      *beg = (*beg << count) | (*next >> shift);
      beg = next++;
    }
    *beg = (*beg << count) | leftmost_bits;
  }
}

#define EXPECT_EQ(a, b) do { auto av = (a); auto bv = (b); if (av != bv) { throw std::runtime_error(fmt::format("{}:{}: {} != {}", __FILE__, __LINE__, av, bv)); } } while (false)

void bitwise_rotate_left_test() {
  std::array<uint8_t, 1> a1{{ 0b10100100 }};
  std::array<uint8_t, 2> a2{{ 0b10010100, 0b10100101 }};
  std::array<uint8_t, 3> a3{{ 0b01001000, 0b10100100, 0b00100101 }};

  bitwise_rotate_left(a1.begin(), a1.end(), 0);
  EXPECT_EQ(a1[0], 0b10100100);
  bitwise_rotate_left(a1.begin(), a1.end(), 1);
  EXPECT_EQ(a1[0], 0b01001001);
  bitwise_rotate_left(a1.begin(), a1.end(), 3);
  EXPECT_EQ(a1[0], 0b01001010);
  bitwise_rotate_left(a1.begin(), a1.end(), 7);
  EXPECT_EQ(a1[0], 0b00100101);

  bitwise_rotate_left(a2.begin(), a2.end(), 0);
  EXPECT_EQ(a2[0], 0b10010100);
  EXPECT_EQ(a2[1], 0b10100101);
  bitwise_rotate_left(a2.begin(), a2.end(), 1);
  EXPECT_EQ(a2[0], 0b00101001);
  EXPECT_EQ(a2[1], 0b01001011);
  bitwise_rotate_left(a2.begin(), a2.end(), 3);
  EXPECT_EQ(a2[0], 0b01001010);
  EXPECT_EQ(a2[1], 0b01011001);
  bitwise_rotate_left(a2.begin(), a2.end(), 15);
  EXPECT_EQ(a2[0], 0b10100101);
  EXPECT_EQ(a2[1], 0b00101100);

  bitwise_rotate_left(a3.begin(), a3.end(), 13);
  EXPECT_EQ(a3[0], 0b10000100);
  EXPECT_EQ(a3[1], 0b10101001);
  EXPECT_EQ(a3[2], 0b00010100);

  for (int i = 0; i < 25; ++i) {
    bitwise_rotate_left(a3.begin(), a3.end(), 1);
  }

  EXPECT_EQ(a3[0], 0b00001001);
  EXPECT_EQ(a3[1], 0b01010010);
  EXPECT_EQ(a3[2], 0b00101001);
}

void rot_hash_by_one(std::vector<std::unique_ptr<item>>& items) {
  for (auto& it : items) {
    auto& v = it->vec;
    bitwise_rotate_left(v.begin(), v.end(), 1);
  }
}

void find_neighbours(dwarfs::logger& lgr, std::vector<std::unique_ptr<item>> items) {
  LOG_PROXY(dwarfs::debug_logger_policy, lgr);

  std::vector<uint32_t> index(items.size());
  std::unordered_map<uint32_t, std::unordered_set<uint32_t>> distance_one_map;
  size_t last_map_size = 0;
  size_t comparisons = 0;

  for (int shift = 0; shift < 256; ++shift) {
    {
      auto ti = LOG_TIMED_INFO;
      rot_hash_by_one(items);
      ti << "rotate";
    }

    std::iota(index.begin(), index.end(), 0);

    {
      auto ti = LOG_TIMED_INFO;
      sort(index.begin(), index.end(), [&](uint32_t a, uint32_t b){
        ++comparisons;
        return *items[a] < *items[b];
      });
      ti << "sort";
    }

    {
      auto ti = LOG_TIMED_INFO;
      for (size_t i = 1; i < index.size(); ++i) {
        auto const& a = items[index[i - 1]];
        auto const& b = items[index[i]];
        auto d = distance(a, b);
        if (d == 0) {
          throw std::runtime_error("distance is unexpectedly zero");
        }
        if (d == 1) {
          distance_one_map[index[i - 1]].insert(index[i]);
          distance_one_map[index[i]].insert(index[i - 1]);
        }
      }
      ti << "find neighbours";
    }

    LOG_INFO << "[" << shift << "] map size: " << distance_one_map.size() << " (+" << (distance_one_map.size() - last_map_size) << ")";
    last_map_size = distance_one_map.size();
  }

  size_t total = 0;
  for (auto const& [i, s] : distance_one_map) {
    total += s.size();
  }

  LOG_INFO << "total direct neighbours found: " << total << " (" << distance_one_map.size() << ", " << items.size() << ")";
  LOG_INFO << "total comparisons: " << comparisons;
}

void find_neighbours2(dwarfs::logger& lgr, std::vector<std::unique_ptr<item>> items) {
  LOG_PROXY(dwarfs::debug_logger_policy, lgr);

  std::unordered_map<uint32_t, std::unordered_set<uint32_t>> distance_one_map;

  for (int bit = 0; bit < 256; ++bit) {
    int const v_idx = bit / 64;
    int const v_bit = bit % 64;
    uint64_t const mask = UINT64_C(1) << v_bit;

    for (size_t i = 0; i < items.size(); ++i) {
      auto v = items[i]->vec;

      if (v[v_idx] & mask) {
        v[v_idx] &= ~mask;

        auto it = std::lower_bound(items.begin(), items.end(), v, [&](auto const& a, auto const& b){
          return a->vec < b;
        });

        if (it != items.end() && (*it)->vec == v) {
          int k = std::distance(items.begin(), it);
          distance_one_map[i].insert(k);
          distance_one_map[k].insert(i);
        }
      }
    }
  }

  size_t total = 0;
  for (auto const& [i, s] : distance_one_map) {
    total += s.size();
  }

  LOG_INFO << "total direct neighbours found: " << total << " (" << distance_one_map.size() << ", " << items.size() << ")";
}

int main() {
  using namespace dwarfs;
  stream_logger lgr(std::cout, logger::level_type::INFO);
  LOG_PROXY(debug_logger_policy, lgr);

  reverse_test();

  std::string line;
  std::vector<std::unique_ptr<item>> items;

  {
    auto ti = LOG_TIMED_INFO;

    while (std::getline(std::cin, line)) {
      auto it = std::make_unique<item>();
      for (size_t i = 0; i < it->vec.size(); ++i) {
        auto start = line.data() + 16*i;
        auto res = std::from_chars(start, start + 16, it->vec[i], 16);
        assert(res.ec == std::errc());
      }
      char const* const end = line.data() + line.size();
      auto res = std::from_chars(line.data() + 65, end, it->size);
      assert(res.ptr != nullptr);
      it->name.assign(res.ptr, end);
      items.emplace_back(std::move(it));
    }

    ti << "reading input data";
  }

  // brute_force(std::move(items));

  // annealing(std::move(items));

  // annealing2(std::move(items));

  // analyze(items);

  bitwise_rotate_left_test();

  {
    auto ti = LOG_TIMED_INFO;

    find_neighbours2(lgr, std::move(items));

    ti << "find_neighbours";
  }

  return 0;
}
