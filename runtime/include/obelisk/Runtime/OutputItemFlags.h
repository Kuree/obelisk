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
  OBELISK_RT_OUTPUT_ITEM_ALL = (1u << 9) - 1
};

#endif // OBELISK_RUNTIME_OUTPUTITEMFLAGS_H
