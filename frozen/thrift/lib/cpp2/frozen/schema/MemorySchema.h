/*
 * SPDX-FileCopyrightText: Copyright (c) Meta Platforms, Inc. and affiliates.
 * SPDX-FileCopyrightText: Copyright (c) Marcus Holland-Moritz
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * This file is derived from fbthrift and has been modified by
 * Marcus Holland-Moritz for use in dwarfs.
 */

#pragma once

#include <cassert>
#include <cstdint>
#include <vector>

#include <boost/container_hash/hash.hpp>

#include <thrift/lib/cpp/DistinctTable.h>
#include <thrift/lib/thrift/gen-cpp-lite/frozen_types.h>

#define THRIFT_DECLARE_HASH(T)           \
  namespace std {                        \
  template <>                            \
  struct hash<T> {                       \
    size_t operator()(const T& x) const; \
  };                                     \
  }
#define THRIFT_IMPL_HASH(T)                      \
  namespace std {                                \
  size_t hash<T>::operator()(const T& x) const { \
    return x.hash();                             \
  }                                              \
  }

namespace apache::thrift::frozen::schema {

class MemoryField;
class MemoryLayoutBase;
class MemoryLayout;
class MemorySchema;

} // namespace apache::thrift::frozen::schema

THRIFT_DECLARE_HASH(apache::thrift::frozen::schema::MemoryField)
THRIFT_DECLARE_HASH(apache::thrift::frozen::schema::MemoryLayoutBase)
THRIFT_DECLARE_HASH(apache::thrift::frozen::schema::MemoryLayout)
THRIFT_DECLARE_HASH(apache::thrift::frozen::schema::MemorySchema)

namespace apache::thrift::frozen::schema {

// Trivially copyable, hashed bytewise.
class MemoryField {
 public:
  MemoryField() = default;

  size_t hash() const noexcept {
    size_t seed = 0;
    boost::hash_combine(seed, id);
    boost::hash_combine(seed, layoutId);
    boost::hash_combine(seed, offset);
    return seed;
  }

  friend std::size_t hash_value(MemoryField const& obj) noexcept {
    return obj.hash();
  }

  bool operator==(const MemoryField& other) const {
    return id == other.id && layoutId == other.layoutId &&
        offset == other.offset;
  }

  void setId(int16_t i) { id = i; }

  int16_t getId() const { return id; }

  void setLayoutId(int16_t lid) { layoutId = lid; }

  int16_t getLayoutId() const { return layoutId; }

  void setOffset(int16_t o) { offset = o; }

  int16_t getOffset() const { return offset; }

 private:
  // Thrift field index
  int16_t id;

  // Index into MemorySchema::layouts
  int16_t layoutId;

  // field offset:
  //  < 0: -(bit offset)
  //  >= 0: byte offset
  int16_t offset;
};

static_assert(
    sizeof(MemoryField) == 3 * sizeof(int16_t), "Memory Field is not padded.");

class MemoryLayoutBase {
 public:
  MemoryLayoutBase() = default;
  virtual ~MemoryLayoutBase() = default;

  size_t hash() const noexcept {
    size_t seed = 0;
    boost::hash_combine(seed, bits);
    boost::hash_combine(seed, size);
    return seed;
  }

  friend std::size_t hash_value(MemoryLayoutBase const& obj) noexcept {
    return obj.hash();
  }

  bool operator==(const MemoryLayoutBase& other) const {
    return bits == other.bits && size == other.size;
  }

  void setSize(int32_t s) { size = s; }

  int32_t getSize() const { return size; }

  void setBits(int16_t b) { bits = b; }

  int16_t getBits() const { return bits; }

 private:
  int32_t size;
  int16_t bits;
};

class MemoryLayout : public MemoryLayoutBase {
 public:
  using MemoryLayoutBase::MemoryLayoutBase;

  // TODO: check why these two classes are separate at all...
  // NOLINTNEXTLINE(bugprone-derived-method-shadowing-base-method)
  size_t hash() const noexcept {
    size_t seed = MemoryLayoutBase::hash();
    boost::hash_combine(seed, boost::hash_range(fields.begin(), fields.end()));
    return seed;
  }

  friend std::size_t hash_value(MemoryLayout const& obj) noexcept {
    return obj.hash();
  }

  bool operator==(const MemoryLayout& other) const {
    return MemoryLayoutBase::operator==(other) && fields == other.fields;
  }

  void addField(MemoryField const& field) { fields.push_back(field); }

  void setFields(std::vector<MemoryField>&& fs) { fields = std::move(fs); }

  const std::vector<MemoryField>& getFields() const { return fields; }

 private:
  std::vector<MemoryField> fields;
};

class Schema;
class MemorySchema {
 public:
  MemorySchema() = default;
  ~MemorySchema() = default;

  size_t hash() const noexcept {
    size_t seed = 0;
    boost::hash_combine(
        seed, boost::hash_range(layouts.begin(), layouts.end()));
    boost::hash_combine(seed, rootLayout);
    return seed;
  }

  friend std::size_t hash_value(MemorySchema const& obj) noexcept {
    return obj.hash();
  }

  bool operator==(const MemorySchema& other) const {
    return layouts == other.layouts;
  }

  void setRootLayoutId(int16_t rootId) {
    assert(rootId < static_cast<int16_t>(layouts.size()));
    rootLayout = rootId;
  }

  int16_t getRootLayoutId() const { return rootLayout; }

  const MemoryLayout& getRootLayout() const { return layouts.at(rootLayout); }

  const MemoryLayout& getLayoutForField(const MemoryField& field) const {
    return layouts.at(field.getLayoutId());
  }

  const std::vector<MemoryLayout>& getLayouts() const { return layouts; }

  class Helper {
    // Add helper structures here to help minimize size of schema during
    // save() operations.
   public:
    explicit Helper(MemorySchema& schema) : layoutTable_(&schema.layouts) {}

    int16_t add(MemoryLayout&& layout);

   private:
    DistinctTable<MemoryLayout> layoutTable_;
  };

  void initFromSchema(Schema&& schema);

 private:
  std::vector<MemoryLayout> layouts;
  int16_t rootLayout;
};

struct SchemaInfo {
  using Field = MemoryField;
  using Layout = MemoryLayout;
  using Schema = MemorySchema;
  using Helper = MemorySchema::Helper;
};

void convert(Schema&& schema, MemorySchema& memSchema);
void convert(const MemorySchema& memSchema, Schema& schema);

} // namespace apache::thrift::frozen::schema
