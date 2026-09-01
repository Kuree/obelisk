//===- OutputItemFlags.h - Shared formatted-output flags ------*- C -*-===//

#ifndef OBELISK_RUNTIME_OUTPUTITEMFLAGS_H
#define OBELISK_RUNTIME_OUTPUTITEMFLAGS_H

#include <stdint.h>

// Serialized simulation output-list flags shared by simulation IR lowering
// and the design-bytecode interpreter. These are distinct from the runtime
// argument flags produced after output-list conversion.
typedef uint32_t obelisk_rt_output_item_flags;
enum {
  OBELISK_RT_OUTPUT_ITEM_SIGNED = 1u << 0,
  OBELISK_RT_OUTPUT_ITEM_OMITTED = 1u << 1,
  OBELISK_RT_OUTPUT_ITEM_REAL = 1u << 2,
  OBELISK_RT_OUTPUT_ITEM_STRING = 1u << 3,
  OBELISK_RT_OUTPUT_ITEM_CONTAINER = 1u << 4,
  OBELISK_RT_OUTPUT_ITEM_DESIGNATED_FORMAT = 1u << 5,
  OBELISK_RT_OUTPUT_ITEM_CLASS = 1u << 6,
  OBELISK_RT_OUTPUT_ITEM_FORMAT = 1u << 7,
  OBELISK_RT_OUTPUT_ITEM_VIRTUAL_INTERFACE = 1u << 8,
  OBELISK_RT_OUTPUT_ITEM_PROCESS = 1u << 9,
  // One logical enum item is carried as two physical operands: its packed
  // value followed by its precomputed mnemonic string.
  OBELISK_RT_OUTPUT_ITEM_ENUM = 1u << 10,
  // One logical direct-net item is carried as its packed value followed by
  // the net handle. The handle is consulted only by the %v strength format;
  // every other conversion uses the packed snapshot normally.
  OBELISK_RT_OUTPUT_ITEM_NET = 1u << 11,
  // One recursively integral unpacked aggregate is carried as three managed
  // strings: its assignment-pattern rendering, followed by its independently
  // leaf-padded two-state and four-state raw encodings.
  OBELISK_RT_OUTPUT_ITEM_RAW_AGGREGATE = 1u << 12,
  // A real value produced directly by $realtime keeps the invoking scope's
  // time precision when it is displayed without an explicit conversion.
  OBELISK_RT_OUTPUT_ITEM_REAL_TIME = 1u << 13,
  OBELISK_RT_OUTPUT_ITEM_ALL = (1u << 14) - 1
};

#endif // OBELISK_RUNTIME_OUTPUTITEMFLAGS_H
