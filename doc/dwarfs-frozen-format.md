<!--
SPDX-FileCopyrightText: Copyright (c) Marcus Holland-Moritz
SPDX-License-Identifier: MIT
-->

# dwarfs-frozen-format(5) -- Frozen2 Schema and Data Format

## DESCRIPTION

Frozen2 is a schema-driven, memory-mappable serialization format. It stores an
object as two related artifacts:

- a **schema**, which describes the exact layout chosen for the object; and
- a **data region**, which contains the values encoded according to that schema.

The schema is not merely a description of the source-language type. It is a
description of one particular compact representation, derived from both the
type and the values that were frozen. Integer widths, omitted fields, struct
sizes, range strides and relative-distance widths can therefore differ between
two objects of the same C++ or Thrift type.

The principal design goal is direct read access. After loading the schema, a
reader constructs a tree of `Layout<T>` objects. A `View` combines such a layout
with a position in the data region and exposes the original object without
materializing it first. Nested structures, ranges and associative containers can
be traversed directly from memory-mapped storage.

Frozen2 is especially useful for large, mostly immutable structures where:

- compact storage matters
- random access matters
- loading the entire object graph would be expensive
- the serialized data can remain mapped for the lifetime of its views

This document describes the DwarFS fork of Frozen2, including schema and data
validation. It is intended to be detailed enough to serve as the basis for an
independent implementation.

The Thrift IDL of the application object is not serialized as part of Frozen2.
A reader must know the expected root type and have a matching generated
`Layout<T>` implementation.

## TERMINOLOGY

- **Source object**: The ordinary C++ object passed to the freezer.

- **Frozen object**: The object representation in the data region.

- **Schema**: A compact Thrift structure containing a table of layouts and a
  root-layout identifier.

- **Layout**: The description of the frozen representation of one value. A
  layout records its byte size, packed-bit size and, for composite types, its
  fields and child layouts.

- **Layout tree**: The concrete C++ `Layout<T>` object graph reconstructed by
  loading the schema for a known root type. The serialized schema itself is a
  directed acyclic graph because identical layout descriptions can be shared.

- **Embedded field**: A child value stored within the byte or bit extent of its
  parent layout.

- **Out-of-line allocation**: A separate range of bytes referenced by a
  relative distance, as used for strings, ranges and nested dynamic content.

- **Packed-bit region**: The initial bit-addressed part of a non-inlined layout.
  Byte-addressed fields begin after this region, rounded up to a complete byte.

- **Inlined layout**: A bit-only child layout whose bits are allocated directly
  in the packed-bit region of its parent. Its serialized `size` is zero.

- **Empty layout**: A layout with both `size == 0` and `bits == 0`. It occupies
  no data and reads as the type's default value.

- **View**: A lightweight object containing a pointer to a layout and a position
  in the frozen data. It provides read access without thawing the complete
  object.

- **Thawing**: Reconstructing an ordinary mutable C++ object from a frozen view.

- **Logical data size**: The data size excluding the mandatory trailing padding.

- **Physical data size**: The complete mapped data size, including trailing
  padding that packed reads may access.

## HIGH-LEVEL STRUCTURE

A self-contained stream produced by `freezeToString()` has this form:

```
    ┌──────────────────────────────────────┐
    │ Schema encoded with Thrift Compact   │
    ├──────────────────────────────────────┤  schema reader reports bytes consumed
    │ Root object                          │  data offset 0
    │   embedded bits and bytes            │
    ├──────────────────────────────────────┤
    │ Out-of-line allocation               │
    ├──────────────────────────────────────┤
    │ Out-of-line allocation               │
    ├──────────────────────────────────────┤
    │ ...                                  │
    ├──────────────────────────────────────┤
    │ 8 bytes packed-read padding          │
    └──────────────────────────────────────┘
```

DwarFS stores the schema and data in separate sections, but their contents are
the same: the schema is a compact-protocol `Schema`, and the data starts with
the root layout at offset zero.

The data region contains no global header, magic number, schema identifier or
root type name. All interpretation comes from the separately supplied schema
and expected C++/Thrift root type.

## BYTE AND BIT ORDER

Packed bits are numbered from the least significant bit of the first byte:

```
    byte offset N

      bit index:   7 6 5 4 3 2 1 0
                  ┌───────────────┐
      memory:     │h g f e d c b a│
                  └───────────────┘

    packed bit offset 0 reads a
    packed bit offset 1 reads b
```

Multi-byte packed-integer accesses use little-endian byte order. The DwarFS
`bit_view` implementation loads and stores fixed-width words through `memcpy`
and converts them to or from little-endian order before applying shifts and
masks. Thus packed bools, integers, enums, counts, distances and hash-table
block offsets have a canonical little-endian representation.

Signed integers are stored as the low `N` bits of their two's-complement
representation, where `N` is the minimum width that preserves the value under
sign extension. Examples are:

```
    value       width       stored bits, least significant first

       0          0         no bits
       1          2         10
       2          3         010
      -1          1         1
      -2          2         01
```

The textual bit strings above are written in memory traversal order. In normal
most-significant-bit-first binary notation the stored representation of `1`
with width 2 is `01`, and the stored representation of `-2` with width 2 is
`10`.

A zero-width integer or boolean performs no data access and always yields zero
or `false`.

## POSITION MODEL

The implementation uses five closely related position types:

- `FieldPosition` describes a field relative to its parent layout.
- `LayoutPosition` describes a position relative to the beginning of the
  prospective data region while computing a layout.
- `FreezePosition` contains a writable byte pointer and bit offset.
- `ViewPosition` contains a readable byte pointer and bit offset.
- `DataPosition` contains checked byte and bit offsets without
  forming a pointer.

A field is either byte-addressed or bit-addressed, never both. In C++ this is
represented as:

```
    FieldPosition {
        offset;       // byte offset, or zero
        bitOffset;    // bit offset, or zero
    }
```

In the serialized schema the two forms share one signed `i16`:

```
    encoded offset < 0    bit offset = -encoded offset
    encoded offset >= 0   byte offset = encoded offset
```

Bit offset zero and byte offset zero both encode as zero. The child layout
resolves the ambiguity: a non-empty child with `size > 0` is byte-addressed; a
child with `size == 0` and `bits > 0` is bit-addressed. Empty layouts occupy no
interval and may use either interpretation at offset zero.

Bit offsets are relative to the start byte of the owning object and may exceed
seven. Readers must not normalize a `(byte, bit)` position by carrying complete
bytes from the bit offset into the byte offset unless all subsequent layout
logic uses the same normalization.

## LAYOUT SIZE, BITS AND INLINING

Every serialized layout records:

- `size`: the number of bytes occupied by the layout when it is not inlined
- `bits`: the number of meaningful packed bits in the layout
- child fields, each with its own layout and relative position

For a non-inlined composite layout, the byte representation is arranged as:

```
    object start
         │
         ▼
    ┌──────────────────────────────────┐
    │ packed-bit region                │  ceil(bits / 8) bytes
    ├──────────────────────────────────┤
    │ byte-addressed embedded field    │
    ├──────────────────────────────────┤
    │ byte-addressed embedded field    │
    ├──────────────────────────────────┤
    │ ...                              │
    └──────────────────────────────────┘
         total extent = size bytes
```

The packed-bit region is always first. A byte-addressed embedded field must not
start before `ceil(bits / 8)`.

Scalar layouts can legitimately populate both fields at once. A byte-positioned
or root packed integer or boolean records its value width in `bits` and its
byte-rounded container in `size == ceil(bits / 8)`. In that case `size` does
not introduce a field region; it merely reserves whole bytes for the packed
value. Type-specific validation accepts exactly this combination for scalar
layouts.

A bit-only child is initially considered for inlining. If recursively laying it
out consumes no complete bytes, its bits are placed directly in the parent's
packed-bit region and its own `size` remains zero. If it needs byte storage, it
is placed as a normal byte field instead.

The same child layout can be different in different fields. Each `Field<T>` owns
its own `Layout<T>` object, allowing one integer member to use three bits while
another member of the same C++ type uses seventeen bits.

## EMPTY LAYOUTS AND DEFAULT VALUES

An empty layout has `size == 0` and `bits == 0`. It is the central mechanism for
representing values that require no storage.

Typical examples are:

- an integer field whose values are all zero
- a boolean field whose values are all false
- a struct field absent from an older schema
- an optional value layout when no source object contains the value
- a generated field omitted from the serialized schema

Saving a schema omits empty fields. Loading a schema leaves fields that are not
present in their default empty-layout state.

Reading an empty leaf layout produces the default value. This allows a newer
reader to load an older schema containing none of the newly added field's
layout information.

Empty fields still have compatibility and safety constraints. A loaded empty
field position must remain within its parent because view code can still form a
position for that field.

## LAYOUT CONSTRUCTION

### Fixed-Point Calculation

`LayoutRoot` determines the minimum layout sufficient for the source object. It
runs recursive layout passes until neither a layout size nor a bit width grows.

A fixed-point algorithm is needed because layout decisions feed back into one
another. For example:

- an integer width can grow after observing another array element
- growing a child can change it from inlined bits to a byte layout
- moving a field from bits to bytes changes the parent's byte size
- a larger count or distance requires more bits in the range descriptor
- out-of-line allocation distances depend on the current root and allocation
  sizes

A simplified algorithm is:

```
    repeat:
        changed = false
        out_of_line_cursor = current root byte size
        recursively lay out root at position (0, 0)
        grow root size/bits as required
    until not changed
```

Layouts only grow during this process, so normal inputs converge. The C++
implementation has a defensive limit of 1000 passes.

### Field Placement

For every child field, `LayoutRoot::layoutField()` performs the following:

1. If the child currently has no byte size, try to lay it out as inlined bits at
   the next parent bit offset.
2. If recursive layout consumes bytes, discard the inline attempt.
3. Otherwise, grow the child bit width and advance the parent's next bit
   position.
4. For a byte child, lay it out at the next parent byte offset and advance by
   the child's byte size.
5. Do not advance either region for an empty child.

The parent starts its byte fields at `ceil(parent.bits / 8)` unless the parent
itself is inlined.

### Out-of-Line Allocation Planning

Strings and ranges ask `LayoutRoot` to reserve an out-of-line byte extent.
During layout calculation, exact buffer addresses are not yet known, so the
cursor assumes worst-case alignment padding:

```
    cursor = max(cursor, origin)
    cursor += alignment - 1
    distance = cursor - origin
    cursor += allocation_size
```

The resulting distance is an upper bound used to determine the bit width of the
stored distance field. During actual freezing, the distance can be smaller
because the real destination address and padding are known.

## FREEZING

`FreezeRoot` mirrors the layout recursion. Embedded fields are written at their
`FieldPosition`; dynamic containers request appended byte ranges and store a
relative distance from the owning object.

Two freezer implementations are provided:

- `ByteRangeFreezer` writes into one caller-supplied contiguous span.
- `MallocFreezer` allocates segments as needed and can append their logical
  concatenation to a string.

For a contiguous buffer, the process is:

1. reserve the root layout's byte size
2. recursively freeze the root into that range
3. append dynamic allocations as requested
4. append eight bytes of packed-read padding
5. return a root `View` positioned at data offset zero

Every stored pointer-like relationship is a nonnegative relative byte distance.
No machine address is serialized.

## TRAILING PACKED-READ PADDING

Packed integers are read through naturally sized words. A field near the end of
the logical data can therefore cause the implementation to read beyond the last
byte containing meaningful bits. Frozen2 appends eight physical bytes for this
purpose:

```
    logical frozen data                 physical-only padding
    ┌──────────────────────────────────┬────────────────────────┐
    │ root and allocations             │ 8 bytes                │
    └──────────────────────────────────┴────────────────────────┘
                                       ▲
                                       logical size ends here
```

The padding is part of the physical mapped range but not part of any object or
allocation. Data validation distinguishes logical bounds from physical packed-
read bounds.

A new implementation can avoid amplified reads by loading only the exact bytes
needed, but it must still accept the serialized padding produced by existing
writers.

## SCHEMA FORMAT

The schema is defined by `frozen.thrift`:

```
    struct Field {
      1: i16 layoutId
      2: i16 offset
    }

    struct Layout {
      1: i32 size
      2: i16 bits
      3: map<i16, Field> fields
      4: string typeName
    }

    struct Schema {
      4: i32 fileVersion
      1: bool relaxTypeChecks
      2: map<i16, Layout> layouts
      3: i16 rootLayout
    }
```

### Schema Layout Table

`rootLayout` indexes the root layout in `layouts`. Every `Field.layoutId`
indexes another entry in the same table.

The DwarFS in-memory representation requires layout IDs to form the dense range:

```
    0, 1, 2, ..., layout_count - 1
```

Identical layout descriptions are deduplicated while saving, so the table is a
DAG rather than necessarily a tree. Cycles are invalid.

A layout's `fields` map is keyed by the stable Thrift field ID. Field names are
not required to read the serialized schema and are supplied by the generated
C++ layout.

The current DwarFS conversion to `MemorySchema` discards `typeName` and forces
`relaxTypeChecks = true` when converting back to the serialized schema. Thus the
current implementation relies on field IDs and concrete generated layout code,
not serialized C++ type names, for compatibility.

### Compact Protocol Encoding

The schema structure is encoded using the Thrift Compact Protocol. Very briefly:

- integer values use variable-length unsigned varints
- signed integers are zigzag-transformed before varint encoding
- struct field headers normally encode the delta from the previous field ID
- booleans can be folded into the field header
- collection and map headers encode element types and lengths compactly
- strings are a varint byte length followed by raw bytes

The compact protocol is self-delimiting at the struct level. `deserializeRootLayout()`
reads one `Schema`, asks the compact reader how many bytes it consumed, and
treats the remaining bytes as the Frozen data region. The compact encoding is
independent of the bit-packing rules used by the Frozen data itself.

### Schema Version

`Schema.fileVersion` is the Frozen format version. The current value is 1.
Readers reject a schema with a version greater than the implementation's
supported version. Backward compatibility is otherwise implemented through
field IDs, omitted empty fields and default values.

## SCHEMA VALIDATION

Schema validation runs unconditionally in `loadRoot()` before a concrete layout
tree is used.

Generic validation checks include:

- the layout table is nonempty
- the root layout ID is in range
- layout IDs are dense when converting from the serialized map
- byte sizes and bit counts are nonnegative
- a nonzero byte layout contains its declared packed-bit region
- field IDs are unique within a layout
- every child layout ID is in range
- the layout-reference graph is acyclic

Type-specific layout validation then checks, among other things:

- packed bools use exactly one bit
- packed integers do not exceed the destination C++ integer width
- trivial layouts have exactly `sizeof(T)` bytes and no bits
- fixed-size strings have their required size
- embedded fields fit in their parent without overlap

Out-of-line `item` layouts are not treated as embedded intervals inside their
range descriptor.

Unknown field IDs are ignored by generated load code. Missing known fields stay
empty. This is the basis of schema evolution.

Because field-placement validation is driven by the reader's generated layout,
unknown fields are exempt from placement and overlap checking. The
memory-safety guarantee therefore covers exactly the set of fields the reader
can access.

## DATA FORMAT BY LAYOUT TYPE

### Packed Bool

`BoolLayout` uses zero or one bit:

```
    bits = 0    value is always false, no data stored
    bits = 1    one packed bit stores false or true
```

### Packed Integer

`PackedIntegerLayout<T>` stores all values described by that layout using one
shared bit width. For a scalar, that is the minimum width of the scalar. For a
field in a range of structs, it is the maximum width needed by that field over
all elements.

Unsigned values use the minimum number of significant bits. Signed values use
the minimum sign-extended two's-complement width.

No per-value tag or length is stored. The width comes entirely from the schema.

### Enum

`EnumLayout<T>` delegates to a packed integer layout using the enum's underlying
C++ type. The numeric value is stored; enum names are not serialized.

### Trivial Layout

`TrivialLayout<T>` stores exactly `sizeof(T)` bytes and has no packed bits. The
current implementation copies the C++ object representation with `memcpy`.
Scalar arithmetic values, both floating-point and the integral values that are
explicitly frozen through `TrivialLayout` such as the hash-block mask, are
byte-swapped on big-endian systems so that their byte sequence is
little-endian.

This layout is discussed further under **PORTABILITY**. It must not be confused
with a struct composed of individually packed fields: generated Thrift structs,
pairs and optionals have explicit composite layouts and are not stored as native
C++ objects.

### Fixed-Size String

`FixedSizeStringLayout<T>` stores exactly `T::kFixedSize` opaque bytes inline.
It contains no count or distance. Freezing fails if the source has a different
size.

### Dynamic String

`StringLayout<T>` is a descriptor plus an out-of-line contiguous allocation:

```
    owning object
       │
       ├─ distance: packed size_t ───────────────┐
       └─ count:    packed size_t                │
                                                 ▼
                                      ┌──────────────────────┐
                                      │ count × sizeof(Item) │
                                      │ contiguous bytes     │
                                      └──────────────────────┘
```

The descriptor uses field IDs `1: distance` and `2: count`.

For an empty string, no out-of-line allocation is created. The descriptor fields
can themselves be absent when their values require no bits.

The default registered string type is `std::string`, whose items are bytes.
Custom `IsString<T>` specializations are permitted for arithmetic or enum item
types.

### Optional

`OptionalLayout<T>` contains:

- `isset` (field ID 1), a packed bool
- `value` (field ID 2), a `Layout<T>`

The value is frozen and validated only when `isset` is true. If no observed
source value is present, the value layout can remain empty.

Schematic representation:

```
    optional
    ├─ isset
    └─ value     present and meaningful only when isset == true
```

### Pair

`PairLayout<First, Second>` is an ordinary two-field composite layout:

```
    field id 1: first
    field id 2: second
```

Each field is independently packed and can be inlined, byte-addressed, empty or
dynamic. A `std::pair<const K, V>` used by a map is therefore not stored using
the native `std::pair` object representation.

### Generated Struct

A generated Thrift layout is a `Layout<T>` containing one `Field` per known
Thrift field. The generated implementation supplies:

- constructor and field IDs
- maximize, layout, freeze and thaw recursion
- a typed `View` with accessors
- schema save and load recursion
- data-validation recursion
- a compile-time summary of whether range elements may contain dynamic data

Struct fields are identified in the schema by numeric Thrift IDs. C++ member
names are used for diagnostics and navigation but are not the compatibility
key.

A struct's embedded representation follows the generic packed-bit-first rule:

```
    ┌──────────────────────────────────────┐
    │ bit fields from all inlined children │
    ├──────────────────────────────────────┤
    │ byte child                           │
    ├──────────────────────────────────────┤
    │ byte child                           │
    └──────────────────────────────────────┘
```

Dynamic children store only their descriptors here; their payloads are
out-of-line.

### Range / List

`ArrayLayout<T, Item>` uses field IDs:

```
    1: distance
    2: count
    3: item layout
```

The `item` field describes the common layout and stride of all elements, but its
data is out-of-line. The range descriptor and item data are:

```
    range object
    ┌──────────────────────────────────────┐
    │ count                                │
    │ distance                             │
    └──────────────────────────────────────┘
                  │
                  │ relative byte distance from range object
                  ▼
    ┌────────────────────────────────────────────────────────┐
    │ item 0 │ item 1 │ item 2 │ ... │ item count-1          │
    └────────────────────────────────────────────────────────┘
```

All items use one layout. The stride is:

```
    item.size bytes       when item.size != 0
    item.bits bits        otherwise
```

For bit-strided items, elements can cross byte boundaries. For byte-strided
items, each element begins at the next fixed byte stride.

A nonempty range whose item layout is empty is valid. It represents `count`
default-valued elements and requires no item allocation. The current writer
stores a distance of zero for such ranges; a reader must accept any distance
whose position remains inside the physical data range, because a view still
forms a pointer from it.

A genuinely blitted item layout requests `alignof(Item)` alignment and can
expose a raw `std::span<const Item>` through `View::range()`. Other item layouts,
including pairs of integers, are packed layouts and require no native `Item`
alignment. Note that DwarFS metadata must not contain blit layouts whose items
require an alignment greater than one byte: the metadata section is relocatable
(for example, a header can be prepended to an image), which shifts the absolute
address of every payload by an arbitrary byte amount. Alignment-requiring blit
payloads would make both zero-copy access and data validation dependent on the
section's placement.

### Ordered Associative Table

Ordered maps and sets derive from `ArrayLayout`. Their items are stored in
strictly increasing key order. Lookup uses binary search directly over the
frozen item range.

A map item is a packed pair:

```
    std::pair<const Key, Value>
       ├─ first  = key
       └─ second = value
```

If the source container is not already ordered, the freezer constructs an
indirect pointer index, sorts it by key, and writes items in that order. Keys
must be distinct.

### Hash Associative Table

A hash map or set extends the range layout with field ID 4:

```
    4: sparseTable, a range of Block
```

Each `Block` represents 64 buckets:

```
    Block {
        mask:   64-bit occupancy bitmap
        offset: index of the first occupied item represented by this block
    }
```

`Block` is serialized as an ordinary two-field composite layout with field IDs
`1: mask` and `2: offset`. The mask is stored as a little-endian 64-bit value
occupying eight bytes; the offset is a packed integer.

Only occupied items are stored in the item range. They are ordered by physical
bucket number, not source iteration order.

For block `B`, bucket bit `b` maps to item index:

```
    B.offset + popcount(B.mask & ((1 << b) - 1))
```

The complete structure is:

```
    hash table object
    ├─ normal range descriptor ───────────────► occupied items[]
    └─ sparseTable range ─────────────────────► Block[]

    Block 0                         Block 1
    ┌─────────────────────────┐    ┌─────────────────────────┐
    │ mask                    │    │ mask                    │
    │ offset = 0              │    │ offset = popcount(B0)   │
    └─────────────────────────┘    └─────────────────────────┘
```

The writer chooses approximately 2.5 buckets per item, rounds to 64-bucket
blocks, forces an odd block count and avoids a block count divisible by five.

Bucket placement follows one exactly specified probe sequence. With
`H = hash(key)` and `buckets = block_count * 64`, the bucket probed at probe
number `p = 0, 1, 2, ...` is:

```
    probe(p) = (5 * H + p * (p + 1) / 2)  mod 2^64  mod buckets
```

that is, an initial multiplied hash followed by quadratic probing with
triangular offsets. The writer stores each key in the first unoccupied probed
bucket. A reader follows the same sequence and stops at the first unoccupied
bucket, or fails after `buckets` probes. Both sides must implement the
sequence identically, because bucket placement is fixed in the serialized
data.

Note: the reference writer accumulates the probe value modulo `2^64` without
intermediate reduction, while the reference reader reduces modulo the bucket
count before each probe. The two sequences agree unless the writer's 64-bit
accumulator wraps around during probing, which requires `5 * H` to lie within
roughly `probe_count^2 / 2` of `2^64` and is considered unreachable in
practice. This quirk is inherited from upstream. An independent implementation
should reproduce the writer arithmetic for placement and be aware of the
discrepancy for lookup.

### Key Hash Functions

The sparse index depends on `Layout<Key>::hash()`. The current implementations
are:

- Packed integers, enums and trivial scalars: `std::hash<T>` applied to the
  value. This is implementation-defined; it is the identity function in
  libstdc++ and libc++ but differs in other standard libraries.
- Dynamic strings, including custom `IsString` item types: `XXH3_64bits` over
  the `count * sizeof(Item)` payload bytes. XXH3 is fully specified and stable
  across platforms.
- Fixed-size strings: for `kFixedSize <= 8`, the bytes are zero-extended into
  a `uint64_t` which is hashed with `std::hash<uint64_t>`; larger sizes use
  `XXH3_64bits` over the bytes.
- Generated structs used as keys require an explicit `Layout<T>::hash()`
  definition, which writer and reader must share.

The hash of a frozen view and the hash of the corresponding source value must
be identical, including across schema evolution. See the hash-stability notes
under PORTABILITY for the resulting constraints.

## VIEWS

`Layout<T>::View` is the read interface for a frozen `T`.

A view contains, directly or through `ViewBase`:

- a non-owning pointer to the concrete layout; and
- a non-owning position in the frozen data.

The view does not own either resource. Both must outlive it.

Leaf views such as integers and floats are eagerly returned as values. Composite
views hold the layout and position and compute child views on demand.

Range iterators store an index rather than an end pointer because an item can
have zero byte size. Indexing computes the item position using the shared item
stride.

`thaw()` recursively reconstructs an ordinary C++ object. Thawing is not
required for normal field or container access.

## IMPLEMENTATION CLASSES

### `Layout<T>`

`Layout<T>` is the central type-dispatch mechanism. Each supported source type
has a specialization deriving from `LayoutBase` or one of the reusable layout
classes.

A concrete layout normally implements:

- `maximize()` -- produce a representation sufficient for every value of `T`
- `layout()` -- grow the representation for an observed value
- `freeze()` -- write a value using the fixed layout
- `thaw()` -- reconstruct a value
- `view()` and nested `View` -- expose direct read access
- `save()` and `load()` -- serialize and reconstruct the layout
- `clear()` and `print()`
- `validate()` -- type-specific schema checks
- `validateData()` -- data-access checks and recursive validation

### `LayoutBase`

`LayoutBase` stores the common `size`, `bits`, `inlined` state and runtime type
information. It provides common resize, printing, clearing, schema validation
and basic data-bound validation.

It is polymorphic because a loaded concrete layout tree recursively validates
and prints through common interfaces. The compile-time
`kMayRequirePerItemInspection` property is separate from virtual dispatch and
allows fixed range items to avoid per-element validation entirely.

### `Field<T, Layout>`

A `Field` combines:

- the stable numeric field ID
- the field name used for diagnostics
- the field's relative position
- an independently specialized child layout

`Field::save()` omits empty child layouts. `Field::load()` reconstructs the
position and child layout, validates the child, and registers its embedded
interval with `LoadRoot`.

### `LayoutRoot`

`LayoutRoot` owns one fixed-point layout pass. It tracks:

- whether any layout grew; and
- the simulated cursor for out-of-line allocations.

Its `layoutField()` function implements bit inlining and byte-field placement.
Its `layoutBytesDistance()` function reserves worst-case aligned dynamic
storage.

### `FreezeRoot`

`FreezeRoot` provides recursive field freezing and the abstract
`appendBytes()` operation used by out-of-line containers. Concrete freezers
decide how storage is obtained.

### `ByteRangeFreezer`

`ByteRangeFreezer` consumes a caller-provided writable span. It aligns each
requested allocation against the actual destination address, advances the span,
and returns the relative distance from the allocation's owner.

### `MallocFreezer`

`MallocFreezer` allocates independent segments while preserving the logical
relative offsets that the final concatenated representation will have. It is
useful when one contiguous destination buffer is not allocated in advance.

### `ViewBase`

`ViewBase<Self, Layout, T>` stores the layout pointer and view position. It
provides common validity, position and thawing operations. Concrete views are
friends so their field accessors can reach the layout and position.

### `FieldView` and `OptionalFieldView`

`FieldView` adapts an ordinary frozen field to the Thrift field-reference API.
`OptionalFieldView` presents an optional-like interface whose contained value is
itself usually a frozen view.

### `LoadRoot`

`LoadRoot` is the context for loading a concrete layout tree from a schema. It
collects embedded field intervals, validates placement and detects overlap after
all fields are loaded.

### `MemorySchema`

`MemorySchema` is the validated, vector-based schema form used while constructing
C++ layouts. It replaces serialized map lookups with dense layout indexing and
provides layout deduplication while saving.

### `DataInspectionContext`

`DataInspectionContext` inspects untrusted data without first constructing
unchecked pointers. It stores:

- the complete physical byte span
- the logical size excluding padding
- checked byte/bit positions
- a structured field/index path
- registered out-of-line allocation intervals
- validation options

All failures pass through one formatter and produce diagnostics such as:

```
    at package::Root.entries[17].name
    (byte offset=284, bit offset=0):
    string data extends beyond the frozen data range:
    offset=8192, size=32, logical size=8000
```

### `Bundled`

`Bundled<Base>` attaches owned resources to a view. `freeze()` uses it to keep
the generated layout and allocated storage alive. `mapFrozen()` uses it to keep
the loaded layout, and optionally the mapped or owned bytes, alive.

### Generated Macros and Codegen

`FrozenMacros.h` defines the common generated layout operations. The code
generator lists fields for each operation rather than reproducing the recursive
logic.

Generated layouts include a compile-time expression equivalent to:

```
    kMayRequirePerItemInspection =
        field_1_layout_may_require ||
        field_2_layout_may_require ||
        ...;
```

This lets a range of fixed structs validate its complete allocation in constant
time without visiting every element.

## DATA VALIDATION

`inspectFrozenData(layout, data, options)` proves that all accesses performed
by the known layout remain within the supplied immutable data range.

The baseline validation performs:

- trailing-padding verification
- root extent validation
- checked offset and size arithmetic
- packed-read physical bounds
- logical allocation bounds
- alignment checks for true blit layouts
- global out-of-line allocation overlap detection
- recursive validation of dynamic children
- mandatory hash sparse-index invariants

### Logical and Physical Bounds

Ordinary objects and allocations must fit before the trailing padding. Packed
word reads may extend into the padding but not beyond the physical span.

### Allocation Overlap

The root object and every nonempty out-of-line payload are registered as
half-open intervals:

```
    [offset, offset + size)
```

No two registered intervals may overlap. Embedded fields are not independent
allocations; their non-overlap is established by schema validation.

### Conditional Range Traversal

After validating a range descriptor and complete item extent, the validator
visits individual elements only when the item layout can contain data-dependent
references.

Fixed leaves, fixed composites and generated structs containing only such
fields have:

```
    kMayRequirePerItemInspection == false;
```

Their ranges require no per-element traversal. Strings, ranges and composites
containing them have the property set to true.

This distinction is essential for large packed metadata. A list of millions of
fixed integral structs is fully described by:

- its validated count
- its validated out-of-line distance
- its common validated item layout
- the checked aggregate extent

Visiting each element would prove no additional memory-safety property.

### Associative Consistency

`DataInspectionOptions::checkAssociativeConsistency` enables checks that
affect lookup correctness rather than raw memory safety.

For ordered tables, keys must be strictly increasing.

For hash tables, every stored key must be reachable through the sparse index and
must resolve to its own physical item.

Hash sparse-table structural invariants remain mandatory even when the option is
false because invalid masks or offsets could create out-of-range iterators.
These invariants include:

- a nonempty item range has buckets
- every block offset equals the population of preceding blocks
- no block population exceeds the remaining item count
- total mask population equals the item count

## READING A FROZEN OBJECT

A reader for a self-contained stream performs:

1. Decode one compact-protocol `Schema`.
2. Check `fileVersion`.
3. Convert to `MemorySchema` and validate it.
4. Instantiate the expected `Layout<Root>`.
5. Load the root layout recursively by matching numeric field IDs.
6. Run type-specific and field-placement schema validation.
7. Treat the remaining bytes as the data region.
8. Optionally, and preferably unconditionally, run `inspectFrozenData()`.
9. Construct the root view at data byte offset zero and bit offset zero.

For DwarFS, steps 1--6 use the schema section and steps 7--9 use the metadata
data section.

## WRITING A FROZEN OBJECT

A writer performs:

1. Construct an empty `Layout<Root>`.
2. Run fixed-point layout calculation over the source object.
3. Convert the concrete layout tree to a deduplicated schema table.
4. Encode the schema with the Thrift Compact Protocol.
5. Allocate or reserve the predicted data size, which already includes the
   trailing padding.
6. Freeze the root and all requested out-of-line allocations.
7. Append eight bytes of padding.

A fixed layout can be reused to freeze another object only if every observed
value fits its existing widths and extents. Otherwise freezing throws
`LayoutException`.

## COMPATIBILITY

Frozen compatibility is based primarily on stable field IDs and empty-layout
defaults.

A newer reader can normally consume an older schema when:

- known old fields retain their numeric IDs and compatible types
- new fields have new IDs
- absent new fields have sensible default values
- the Frozen schema version is not newer than the reader supports

Unknown fields in a loaded schema are ignored by generated layout code. Their
serialized layout nodes still have to pass generic schema validation, but no C++
view accesses them.

Removing or renaming a field while preserving other IDs does not change the
position of those fields because positions come from the schema, not the
current declaration order.

Changing a field ID changes its compatibility identity.

The current DwarFS schema conversion does not enforce serialized type names.
Consequently, changing the C++ type associated with an existing ID can load far
enough to reach type-specific layout validation, but such a change must not be
considered generally compatible.

### Upstream Compatibility

This fork intentionally implements a subset of upstream Frozen2 (fbthrift).
Reference-field support (`cpp.ref`, `unique_ptr`, `shared_ptr` and `IOBuf`
layouts) and folly-specific functionality have been removed. The schema wire
format, the data encoding, all built-in field IDs and the trailing-padding
rule are unchanged for everything the fork supports.

The intended compatibility relationship is one-directional: data produced by
this fork is readable by upstream Frozen2 given the same IDL definitions,
while upstream data is readable by the fork only if it uses no removed
features. Schemas produced by the fork always set `relaxTypeChecks` and omit
`typeName`, which upstream accepts.

Known divergences:

- On big-endian hosts, the fork byte-swaps scalar `TrivialLayout` values to
  little-endian. Upstream Frozen2 is little-endian only; on little-endian
  hosts the outputs are identical.

- The fork requests out-of-line item alignment based on the resolved layout
  rather than on `IsBlitType`. For trivially copyable item types that use
  generated composite layouts, the fork may emit less padding than upstream.
  Both variants are read correctly by both sides through the stored
  distances.

## PORTABILITY

### Intended Portable Core

The generated-Thrift path is designed to be independent of host endianness and
native struct layout:

- bools, integers and enums are bit-packed in canonical little-endian order
- generated structs are composed field by field, not copied as C++ objects
- pairs and optionals are composed field by field
- byte strings and fixed-size byte strings are opaque byte sequences
- ranges use schema-defined fixed strides
- all references are relative byte distances
- the schema itself uses the architecture-neutral Thrift Compact Protocol

A reader on a different-endian machine can therefore interpret these core
representations by following the schema and using little-endian packed-bit
operations.

### Address-Space Width

Counts, distances and hash block offsets are represented by
`PackedIntegerLayout<size_t>` in the current C++ implementation. Their stored
width is schema-defined, but the target layout validator rejects a width greater
than the target machine's `sizeof(size_t) * 8`.

Therefore a representation can be read on a narrower architecture only when all
serialized counts and relative offsets fit that architecture's `size_t` and
address space. This is normally unavoidable for directly memory-mapping the
complete object, but it is a portability condition rather than an entirely
fixed-width wire contract.

A fully architecture-neutral independent implementation should decode these
fields into a fixed-width unsigned type first, check them against its local
address-space limits, and only then convert to native indices.

### Alignment and Relocation

All references are relative, but the current zero-copy C++ implementation still
requires native alignment for genuine blit layouts. The freezer aligns such
payloads against the actual destination address, and validation checks the
resulting absolute address.

Relocating a complete byte sequence to a base address with a different alignment
modulus can therefore make a previously aligned blit payload misaligned even
though every relative distance remains correct.

Memory mappings and ordinary allocator results are generally sufficiently
aligned, but arbitrary byte-subspan relocation is not guaranteed. An
independent portable reader can avoid this restriction by reading blitted values
with `memcpy` rather than typed pointer access.

### Known Portability Limitations

The following current implementation paths are not fully architecture-neutral.
They should either be restricted or redesigned if complete portability is a hard
format requirement.

#### Generic `TrivialLayout<T>`

For an arbitrary trivially copyable non-integral type, `TrivialLayout<T>` stores
its native object representation. This can depend on:

- `sizeof(T)`
- member offsets and padding
- padding-byte contents
- implementation-defined object representations
- endianness of non-floating members

Only floating-point objects receive an explicit big-endian byte swap. Even for
floats, the implementation assumes matching width and binary representation,
typically IEEE-754.

Generated Thrift structs do not use this path, but custom types can. A portable
format should allow `TrivialLayout` only for explicitly defined canonical byte
representations, or replace it with type-specific encode/decode logic.

Copying native padding can also harm reproducibility and can expose
uninitialized bytes.

#### Custom Multi-Byte `IsString` Types

`StringLayout<T>` copies its contiguous `Item` array byte-for-byte. This is
portable for the default `std::string` specialization because `Item` is `char`.
A custom string-like type with `uint16_t`, `uint32_t`, floating or enum items uses
native item representation and byte order.

Such types need per-item canonicalization for cross-endian portability.

#### Hash Block Mask Byte Order

`BlockLayout` stores its 64-bit occupancy mask through
`TrivialLayout<uint64_t>`. The mask is specified as a little-endian 64-bit
value; the DwarFS fork byte-swaps scalar `TrivialLayout` values on big-endian
hosts to produce and consume this canonical representation.

Upstream Frozen2 copies the native object representation without a swap and is
therefore little-endian only for this field. Since effectively all existing
data was produced on little-endian hosts, the little-endian definition matches
existing files, and outputs on little-endian hosts remain byte-identical
between the fork and upstream.

#### Hash Function Stability

The serialized sparse hash index depends on `Layout<Key>::hash()`. Several leaf
layouts currently delegate to `std::hash<T>` and return `size_t`.

The C++ standard does not require `std::hash` results to be stable across:

- architectures
- standard-library implementations
- process executions
- `size_t` widths

If a reader computes a different hash from the writer, a valid key can become
unreachable even though the bytes and schema are otherwise correct. Enabling
associative-consistency validation detects this after loading but does not make
the representation portable.

A fully portable Frozen hash table needs a specified fixed-width hash algorithm,
for example a defined `uint64_t` hash for every supported key layout, with
exactly specified field combination and byte order.

Ordered maps and sets do not have this hash-stability issue.

#### Raw Blit Range Access

`ArrayLayout::View::range()` returns `std::span<const Item>` only for layouts
resolved as `TrivialLayout<Item>`. On big-endian systems, raw floating-point
range access is intentionally rejected because the stored bytes are
little-endian. Element-wise `view()`/`thaw()` can perform conversion.

This API restriction is correct, but it illustrates that a portable serialized
representation does not imply every zero-copy native view is portable.

#### Pointer-to-Integer Diagnostics

`ViewPosition::toBits()` converts a pointer to `int64_t` for comparisons. This is
an implementation detail rather than serialized data, but it assumes pointers
fit in 64 bits. A maximally portable implementation should compare normalized
offsets or use `uintptr_t` with checked arithmetic.

## IMPLEMENTATION REQUIREMENTS FOR A NEW READER

A new implementation should, at minimum:

1. Decode the compact schema without trusting layout IDs, sizes or offsets.
2. Validate the schema graph and all numeric ranges.
3. Bind known field IDs to the expected root type.
4. Preserve unknown-field tolerance and missing-field defaults.
5. Interpret packed bits least-significant-bit first and multi-byte words as
   little-endian.
6. Distinguish byte layouts from inlined bit layouts using child `size` and
   `bits`.
7. Treat range `item` layouts as out-of-line, not embedded in the descriptor.
8. Check all count, distance, stride and extent arithmetic for overflow.
9. Require every logical object and allocation to fit before the trailing
   padding.
10. Ensure out-of-line allocations do not overlap.
11. Validate hash block populations and offsets before exposing lookup.
12. Avoid forming pointers until the relevant range has been checked.
13. Keep the schema/layout object alive as long as any view uses it.
14. Handle unaligned and opposite-endian data explicitly rather than relying on
   native casts.
15. Use a specified stable hash if cross-architecture hash-table compatibility
   is required.

## IMPLEMENTATION REQUIREMENTS FOR A NEW WRITER

A new writer should:

1. Build one independent child layout per field.
2. Grow widths over every observed value sharing that field layout.
3. Iterate layout calculation to a fixed point.
4. Place inlined bits before embedded bytes.
5. Omit empty fields from the schema.
6. Deduplicate identical serialized layouts.
7. Assign dense nonnegative layout IDs.
8. Calculate out-of-line distances relative to the owning object.
9. Use the exact item order required by ordered or hash lookup.
10. Append eight bytes of physical padding.
11. Produce canonical bytes for every type claimed to be portable.

## DEBUGGING

`operator<<(LayoutBase)` recursively prints a human-readable layout tree. It is
often the fastest way to connect a generated type to raw schema entries. A
layout dump shows:

- total byte and bit size
- field names
- byte or bit positions
- range descriptor fields
- item layouts
- omitted empty fields

Schema-validation errors identify malformed layout graph or field placement.
Data-validation errors include a structured root/field/index path, current byte
and bit position, and actual versus expected numeric values where useful.

For example:

```
    at dwarfs::thrift::metadata::metadata.reg_file_size_cache.value.size_lookup
    (byte offset=0, bit offset=393):
    range data is not correctly aligned:
    offset=6408, address=..., actual remainder=4,
    expected alignment=8 and remainder=0
```

The path identifies the logical object containing the malformed descriptor.
Payload offsets in the remainder of the message identify the referenced
allocation.

## SECURITY CONSIDERATIONS

A Frozen view is effectively an interpreter for untrusted offsets and bit
widths. Loading a schema and directly constructing a view without validation can
turn malformed data into out-of-bounds reads, invalid typed pointers or
incorrect associative lookup.

Recommended practice is:

- always validate the schema while loading it
- always validate the complete data region before exposing the root view
- enable associative consistency when lookup correctness is security-relevant
- verify the outer container or section integrity separately
- retain the physical trailing padding in the mapped range

Validation is designed to be inexpensive for the common case. Large ranges of
fixed packed structs are checked as aggregate extents and are not traversed per
element.

## AUTHOR

Written by Marcus Holland-Moritz.

## COPYRIGHT

Copyright (C) Marcus Holland-Moritz.

## SEE ALSO

[dwarfs-format(5)](dwarfs-format.md)
