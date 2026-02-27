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
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <limits>
#include <ostream>
#include <random>

#include <fmt/format.h>

#include <dwarfs/thrift_lite/compact_writer.h>

#include "thrift_lite_test_message.h"

namespace dwarfs::test {

namespace gen = dwarfs::thrift_lite::test;

gen::TestMessage make_random_thrift_lite_test_message(unsigned seed) {
  std::mt19937 rng{seed};
  auto msg = gen::TestMessage{};

  auto bool_dist = std::bernoulli_distribution{};
  auto random_string = [&rng] {
    auto len = std::uniform_int_distribution<std::size_t>{0, 10}(rng);
    std::string str(len, '\0');
    std::ranges::generate(str, [&rng] {
      return static_cast<char>(
          std::uniform_int_distribution<int>{'a', 'z'}(rng));
    });
    return str;
  };
  auto random_binary = [&rng] {
    auto len = std::uniform_int_distribution<std::size_t>{0, 10}(rng);
    std::vector<std::byte> data(len);
    std::ranges::generate(data, [&rng] {
      return static_cast<std::byte>(
          std::uniform_int_distribution<unsigned>{0, 255}(rng));
    });
    return data;
  };
  auto fill_small_strings = [&](gen::SmallStrings& ref) {
    if (bool_dist(rng)) {
      return;
    }
    if (bool_dist(rng)) {
      ref.name() = random_string();
    }
    if (bool_dist(rng)) {
      ref.comment() = random_string();
    }
    if (bool_dist(rng)) {
      ref.tag().emplace(random_string());
    }
    if (bool_dist(rng)) {
      ref.payload().emplace(random_binary());
    }
  };
  auto fill_big_record = [&](gen::BigRecord& rec) {
    if (bool_dist(rng)) {
      return;
    }
    if (bool_dist(rng)) {
      rec.id() = std::uniform_int_distribution<std::int64_t>{}(rng);
    }
    if (bool_dist(rng)) {
      rec.kind() = static_cast<gen::RecordKind>(
          std::uniform_int_distribution<int>{0, 100}(rng));
    }
    if (bool_dist(rng)) {
      rec.checksum() = std::uniform_int_distribution<std::uint32_t>{}(rng);
    }
    if (bool_dist(rng)) {
      rec.offset() = std::uniform_int_distribution<std::int64_t>{}(rng);
    }
    if (bool_dist(rng)) {
      rec.size() = std::uniform_int_distribution<std::int64_t>{}(rng);
    }
    rec.deleted() = bool_dist(rng);
    if (bool_dist(rng)) {
      rec.extents()->resize(
          std::uniform_int_distribution<std::size_t>{0, 5}(rng));
      std::ranges::generate(rec.extents().value(), [&rng] {
        return std::uniform_int_distribution<std::int64_t>{}(rng);
      });
    }
    if (bool_dist(rng)) {
      rec.indices()->resize(
          std::uniform_int_distribution<std::size_t>{0, 500}(rng));
      std::ranges::generate(rec.indices().value(), [&rng] {
        return std::uniform_int_distribution<std::uint32_t>{}(rng);
      });
    }
  };
  auto fill_containers = [&](gen::Containers& cont) {
    if (bool_dist(rng)) {
      return;
    }
    if (bool_dist(rng)) {
      cont.small_ints()->resize(
          std::uniform_int_distribution<std::size_t>{0, 5}(rng));
      std::ranges::generate(cont.small_ints().value(), [&rng] {
        return std::uniform_int_distribution<std::int32_t>{}(rng);
      });
    }
    if (bool_dist(rng)) {
      auto& tags = cont.small_tags().value();
      auto const size = std::uniform_int_distribution<std::size_t>{0, 5}(rng);
      for (std::size_t i = 0; i < size; ++i) {
        tags.insert(random_string());
      }
    }
    if (bool_dist(rng)) {
      auto& i2n = cont.id_to_name().value();
      auto const size = std::uniform_int_distribution<std::size_t>{0, 5}(rng);
      for (std::size_t i = 0; i < size; ++i) {
        i2n.emplace(std::uniform_int_distribution<std::int32_t>{}(rng),
                    random_string());
      }
    }
    if (bool_dist(rng)) {
      auto& n2v = cont.name_to_value().value();
      auto const size = std::uniform_int_distribution<std::size_t>{0, 5}(rng);
      for (std::size_t i = 0; i < size; ++i) {
        n2v.emplace(random_string(),
                    std::uniform_int_distribution<std::int32_t>{}(rng));
      }
    }
    if (bool_dist(rng)) {
      auto& nested = cont.nested_int_lists().value();
      nested.resize(std::uniform_int_distribution<std::size_t>{0, 5}(rng));
      for (auto& inner : nested) {
        inner.resize(std::uniform_int_distribution<std::size_t>{0, 5}(rng));
        std::ranges::generate(inner, [&rng] {
          return std::uniform_int_distribution<std::int32_t>{}(rng);
        });
      }
    }
    if (bool_dist(rng)) {
      auto& i2e = cont.id_to_extents().value();
      auto const size = std::uniform_int_distribution<std::size_t>{0, 5}(rng);
      for (std::size_t i = 0; i < size; ++i) {
        auto& extents = i2e[std::uniform_int_distribution<std::int32_t>{}(rng)];
        extents.resize(std::uniform_int_distribution<std::size_t>{0, 5}(rng));
        std::ranges::generate(extents, [&rng] {
          return std::uniform_int_distribution<std::int64_t>{}(rng);
        });
      }
    }
    if (bool_dist(rng)) {
      auto& ol = cont.opt_list().emplace();
      ol.resize(std::uniform_int_distribution<std::size_t>{0, 5}(rng));
      std::ranges::generate(ol, [&rng] {
        return std::uniform_int_distribution<std::int32_t>{}(rng);
      });
    }
    if (bool_dist(rng)) {
      auto& om = cont.opt_map().emplace();
      auto const size = std::uniform_int_distribution<std::size_t>{0, 5}(rng);
      for (std::size_t i = 0; i < size; ++i) {
        om.emplace(std::uniform_int_distribution<std::int32_t>{}(rng),
                   random_string());
      }
    }
    if (bool_dist(rng)) {
      auto& os = cont.opt_set().emplace();
      auto const size = std::uniform_int_distribution<std::size_t>{0, 5}(rng);
      for (std::size_t i = 0; i < size; ++i) {
        os.insert(std::uniform_int_distribution<std::int32_t>{}(rng));
      }
    }
  };

  if (bool_dist(rng)) {
    auto& hdr = msg.header().value();
    if (bool_dist(rng)) {
      hdr.version() = std::uniform_int_distribution<std::uint32_t>{}(rng);
    }
    if (bool_dist(rng)) {
      hdr.created_ts().emplace(
          std::uniform_int_distribution<std::int64_t>{}(rng));
    }
    if (bool_dist(rng)) {
      fill_small_strings(hdr.meta().emplace());
    }
    if (bool_dist(rng)) {
      hdr.flags() = std::uniform_int_distribution<std::uint16_t>{}(rng);
    }
    if (bool_dist(rng)) {
      hdr.default_kind().emplace(gen::RecordKind::file);
    }
  }

  if (bool_dist(rng)) {
    fill_small_strings(msg.labels().emplace());
  }

  if (bool_dist(rng)) {
    auto& recs = msg.records().value();
    recs.resize(std::uniform_int_distribution<std::size_t>{0, 2}(rng));
    for (auto& r : recs) {
      fill_big_record(r);
    }
  }

  if (bool_dist(rng)) {
    fill_containers(msg.containers().value());
  }

  if (bool_dist(rng)) {
    auto& v1 = msg.v1().value();
    if (bool_dist(rng)) {
      v1.a() = std::uniform_int_distribution<std::int32_t>{}(rng);
    }
    if (bool_dist(rng)) {
      v1.b().emplace(std::uniform_int_distribution<std::int32_t>{}(rng));
    }
    if (bool_dist(rng)) {
      v1.c() = std::uniform_int_distribution<std::int64_t>{}(rng);
    }
    if (bool_dist(rng)) {
      v1.d().emplace(random_string());
    }
    if (bool_dist(rng)) {
      fill_containers(v1.containers().value());
    }
    if (bool_dist(rng)) {
      v1.removed_later().emplace(
          std::uniform_int_distribution<std::int32_t>{}(rng));
    }
  }

  if (bool_dist(rng)) {
    auto& v2 = msg.v2().value();
    if (bool_dist(rng)) {
      v2.a() = std::uniform_int_distribution<std::int32_t>{}(rng);
    }
    if (bool_dist(rng)) {
      v2.b().emplace(std::uniform_int_distribution<std::int32_t>{}(rng));
    }
    if (bool_dist(rng)) {
      v2.c() = std::uniform_int_distribution<std::int64_t>{}(rng);
    }
    if (bool_dist(rng)) {
      v2.d().emplace(random_string());
    }
    if (bool_dist(rng)) {
      fill_containers(v2.containers().value());
    }
    if (bool_dist(rng)) {
      v2.e().emplace(std::uniform_int_distribution<std::int64_t>{}(rng));
    }
    if (bool_dist(rng)) {
      fill_small_strings(v2.f().emplace());
    }
    if (bool_dist(rng)) {
      auto& future = v2.future_field().emplace();
      auto const size = std::uniform_int_distribution<std::size_t>{0, 5}(rng);
      for (std::size_t i = 0; i < size; ++i) {
        auto& rec = future[std::uniform_int_distribution<std::int32_t>{}(rng)];
        rec.resize(std::uniform_int_distribution<std::size_t>{0, 5}(rng));
        for (auto& inner : rec) {
          fill_small_strings(inner);
        }
      }
    }
  }

  if (bool_dist(rng)) {
    // don't use NaNs since they won't compare equal after a round trip
    switch (std::uniform_int_distribution<int>{0, 2}(rng)) {
    case 0:
      msg.float_value() = 1.5;
      break;
    case 1:
      msg.float_value() = std::numeric_limits<double>::infinity();
      break;
    case 2:
      msg.float_value() = -std::numeric_limits<double>::infinity();
      break;
    }
  }

  if (bool_dist(rng)) {
    msg.far_optional().emplace(
        std::uniform_int_distribution<std::int32_t>{}(rng));
  }

  if (bool_dist(rng)) {
    msg.far_regular() = std::uniform_int_distribution<std::int32_t>{}(rng);
  }

  return msg;
}

void generate_thrift_lite_test_message_data(std::ostream& out, size_t count) {
  out << "constexpr size_t num_msgs = " << count << ";\n\n";

  for (size_t i = 0; i < count; ++i) {
    auto msg = dwarfs::test::make_random_thrift_lite_test_message(i);
    auto bytes = std::vector<std::byte>{};
    auto w = dwarfs::thrift_lite::compact_writer{bytes};

    msg.write(w);

    out << "constexpr std::array<std::uint8_t, " << bytes.size() << "> msg" << i
        << "{{";

    for (auto b : bytes) {
      out << fmt::format("0x{:02x},", static_cast<std::uint8_t>(b));
    }

    out << "}};\n";
  }

  out << "\nconstexpr std::array<std::span<std::uint8_t const>, "
         "num_msgs> messages{\n";

  for (size_t i = 0; i < count; ++i) {
    out << "    msg" << i << ",\n";
  }

  out << "};\n";
}

} // namespace dwarfs::test
