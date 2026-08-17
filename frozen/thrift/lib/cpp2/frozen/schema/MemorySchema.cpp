/*
 * SPDX-FileCopyrightText: Copyright (c) Meta Platforms, Inc. and affiliates.
 * SPDX-FileCopyrightText: Copyright (c) Marcus Holland-Moritz
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * This file is derived from fbthrift and has been modified by
 * Marcus Holland-Moritz for use in dwarfs.
 */

#include <thrift/lib/cpp2/frozen/schema/MemorySchema.h>

#include <deque>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <utility>

THRIFT_IMPL_HASH(apache::thrift::frozen::schema::MemoryField)
THRIFT_IMPL_HASH(apache::thrift::frozen::schema::MemoryLayoutBase)
THRIFT_IMPL_HASH(apache::thrift::frozen::schema::MemoryLayout)
THRIFT_IMPL_HASH(apache::thrift::frozen::schema::MemorySchema)

namespace apache::thrift::frozen::schema {

namespace {

[[noreturn]] void invalidSchema(std::string const& message) {
  throw SchemaValidationException(message);
}

std::string layoutPrefix(size_t layoutId) {
  return "invalid frozen schema layout " + std::to_string(layoutId) + ": ";
}

} // namespace

int16_t MemorySchema::Helper::add(MemoryLayout&& layout) {
  // Add distinct layout, bounds check layoutId
  auto const layoutId = layoutTable_.add(std::move(layout));
  if (std::cmp_greater(layoutId, std::numeric_limits<int16_t>::max())) {
    throw std::runtime_error("Layout overflow");
  }
  return static_cast<int16_t>(layoutId);
}

void MemorySchema::initFromSchema(Schema&& schema) {
  auto tmp = std::move(schema);
  const auto& serializedLayouts = *tmp.layouts();

  constexpr auto kMaxLayoutCount =
      static_cast<size_t>(std::numeric_limits<int16_t>::max()) + 1;
  if (serializedLayouts.size() > kMaxLayoutCount) {
    invalidSchema("too many layouts");
  }

  layouts.clear();
  layouts.resize(serializedLayouts.size());

  size_t expectedId = 0;
  for (const auto& layoutKvp : serializedLayouts) {
    const auto id = layoutKvp.first;
    const auto& layout = layoutKvp.second;

    if (id < 0 || std::cmp_not_equal(id, expectedId)) {
      invalidSchema("layout ids must form the dense range [0, layout_count)");
    }

    auto& memLayout = layouts[expectedId];
    memLayout.setSize(*layout.size());
    memLayout.setBits(*layout.bits());

    std::vector<MemoryField> fields;
    fields.reserve(layout.fields()->size());
    for (const auto& fieldKvp : *layout.fields()) {
      MemoryField& memField = fields.emplace_back();
      const auto& fieldId = fieldKvp.first;
      const auto& field = fieldKvp.second;

      memField.setId(fieldId);
      memField.setLayoutId(*field.layoutId());
      memField.setOffset(*field.offset());
    }
    memLayout.setFields(std::move(fields));
    ++expectedId;
  }

  rootLayout = *tmp.rootLayout();
}

void MemorySchema::validate() const {
  if (layouts.empty()) {
    invalidSchema("layout table is empty");
  }

  if (rootLayout < 0 || static_cast<size_t>(rootLayout) >= layouts.size()) {
    invalidSchema("root layout id is out of range");
  }

  std::vector<size_t> incoming(layouts.size());

  for (size_t layoutId = 0; layoutId < layouts.size(); ++layoutId) {
    const auto& layout = layouts[layoutId];
    const auto size = layout.getSize();
    const auto bits = layout.getBits();
    const auto prefix = layoutPrefix(layoutId);

    if (size < 0) {
      invalidSchema(prefix + "negative byte size");
    }
    if (bits < 0) {
      invalidSchema(prefix + "negative bit size");
    }
    if (size > 0 &&
        static_cast<uint64_t>(bits) > static_cast<uint64_t>(size) * 8) {
      invalidSchema(prefix + "bit region does not fit in byte size");
    }

    std::unordered_set<int16_t> fieldIds;
    fieldIds.reserve(layout.getFields().size());
    for (const auto& field : layout.getFields()) {
      if (!fieldIds.insert(field.getId()).second) {
        invalidSchema(
            prefix + "duplicate field id " + std::to_string(field.getId()));
      }

      const auto childId = field.getLayoutId();
      if (childId < 0 || static_cast<size_t>(childId) >= layouts.size()) {
        invalidSchema(
            prefix + "field " + std::to_string(field.getId()) +
            " references an out-of-range layout id");
      }

      ++incoming[static_cast<size_t>(childId)];
    }
  }

  std::deque<size_t> ready;
  for (size_t layoutId = 0; layoutId < incoming.size(); ++layoutId) {
    if (incoming[layoutId] == 0) {
      ready.push_back(layoutId);
    }
  }

  size_t visited = 0;
  while (!ready.empty()) {
    const auto layoutId = ready.front();
    ready.pop_front();
    ++visited;

    for (const auto& field : layouts[layoutId].getFields()) {
      auto& childIncoming = incoming[static_cast<size_t>(field.getLayoutId())];
      if (--childIncoming == 0) {
        ready.push_back(static_cast<size_t>(field.getLayoutId()));
      }
    }
  }

  if (visited != layouts.size()) {
    invalidSchema("layout reference graph contains a cycle");
  }
}

void convert(Schema&& schema, MemorySchema& memSchema) {
  memSchema.initFromSchema(std::move(schema));
}

void convert(const MemorySchema& memSchema, Schema& schema) {
  using LayoutsType = std::decay_t<decltype(*schema.layouts())>;
  LayoutsType layouts;
  for (const auto& memLayout : memSchema.getLayouts()) {
    Layout newLayout;
    newLayout.size() = memLayout.getSize();
    newLayout.bits() = memLayout.getBits();

    using FieldsType = std::decay_t<decltype(*newLayout.fields())>;
    FieldsType fields;
    for (const auto& field : memLayout.getFields()) {
      Field newField;
      newField.layoutId() = field.getLayoutId();
      newField.offset() = field.getOffset();
      fields.emplace(field.getId(), std::move(newField));
    }
    newLayout.fields() = std::move(fields);

    layouts.emplace(layouts.size(), std::move(newLayout));
  }
  schema.layouts() = std::move(layouts);

  //
  // Type information is discarded when transforming from memSchema to
  // schema, so force this bit to true.
  //
  *schema.relaxTypeChecks() = true;
  *schema.rootLayout() = memSchema.getRootLayoutId();
}

} // namespace apache::thrift::frozen::schema
