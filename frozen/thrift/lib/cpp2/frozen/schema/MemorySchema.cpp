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

#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

THRIFT_IMPL_HASH(apache::thrift::frozen::schema::MemoryField)
THRIFT_IMPL_HASH(apache::thrift::frozen::schema::MemoryLayoutBase)
THRIFT_IMPL_HASH(apache::thrift::frozen::schema::MemoryLayout)
THRIFT_IMPL_HASH(apache::thrift::frozen::schema::MemorySchema)

namespace apache::thrift::frozen::schema {

int16_t MemorySchema::Helper::add(MemoryLayout&& layout) {
  // Add distinct layout, bounds check layoutId
  auto const layoutId = layoutTable_.add(std::move(layout));
  if (std::cmp_greater(layoutId, std::numeric_limits<int16_t>::max())) {
    throw std::runtime_error("Layout overflow");
  }
  return static_cast<int16_t>(layoutId);
}

void MemorySchema::initFromSchema(Schema&& schema) {
  if (!schema.layouts()->empty()) {
    layouts.resize(schema.layouts()->size());

    for (const auto& layoutKvp : *schema.layouts()) {
      const auto id = layoutKvp.first;
      const auto& layout = layoutKvp.second;

      // Note: This will throw if there are any id >=
      // schema.layouts.size().
      auto& memLayout = layouts.at(id);

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
    }
  }
  setRootLayoutId(*schema.rootLayout());
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
