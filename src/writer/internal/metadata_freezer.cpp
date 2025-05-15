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
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <thrift/lib/cpp2/frozen/FrozenUtil.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include <dwarfs/logger.h>
#include <dwarfs/malloc_byte_buffer.h>

#include <dwarfs/writer/internal/metadata_freezer.h>

#include <dwarfs/gen-cpp2/metadata_layouts.h>
#include <dwarfs/gen-cpp2/metadata_types.h>

#include <thrift/lib/thrift/gen-cpp2/frozen_types_custom_protocol.h>

namespace dwarfs::writer::internal {

namespace {

template <class T>
std::pair<shared_byte_buffer, shared_byte_buffer> freeze_to_buffer(T const& x) {
  using namespace ::apache::thrift::frozen;

  Layout<T> layout;
  size_t content_size = LayoutRoot::layout(x, layout);

  std::string schema;
  serializeRootLayout(layout, schema);

  auto schema_buffer = malloc_byte_buffer::create(schema);

  auto data_buffer = malloc_byte_buffer::create_zeroed(content_size);

  folly::MutableByteRange content_range(data_buffer.data(), data_buffer.size());
  ByteRangeFreezer::freeze(layout, x, content_range);

  data_buffer.resize(data_buffer.size() - content_range.size());
  data_buffer.shrink_to_fit();

  return {schema_buffer.share(), data_buffer.share()};
}

template <typename LoggerPolicy>
class metadata_freezer_ : public metadata_freezer::impl {
 public:
  explicit metadata_freezer_(logger& lgr)
      : LOG_PROXY_INIT(lgr) {}

  std::pair<shared_byte_buffer, shared_byte_buffer>
  freeze(thrift::metadata::metadata const& data) const override {
    auto ti = LOG_TIMED_VERBOSE;
    auto rv = freeze_to_buffer(data);

    ti << "freezing metadata to " << rv.second.size() << " bytes...";

    return rv;
  }

 private:
  LOG_PROXY_DECL(LoggerPolicy);
};

} // namespace

metadata_freezer::metadata_freezer(logger& lgr)
    : impl_{
          make_unique_logging_object<impl, metadata_freezer_, logger_policies>(
              lgr)} {}

metadata_freezer::~metadata_freezer() = default;

} // namespace dwarfs::writer::internal
