//===- Runtime.h - Obelisk native runtime C ABI -----------------*- C -*-===//

#ifndef OBELISK_RUNTIME_RUNTIME_H
#define OBELISK_RUNTIME_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// All in-tree runtime records and generated artifacts use this single
// prototype schema version. Names ending in _v1 identify the same schema.
// This is not a backward-compatibility promise: generated code, bytecode, and
// libobelisk_rt must come from the same Obelisk source revision.
#define OBELISK_RT_VERSION 1u

typedef struct obelisk_rt_context obelisk_rt_context;
typedef struct obelisk_rt_gc_lane_v1 obelisk_rt_gc_lane_v1;
typedef struct obelisk_rt_object_v1 obelisk_rt_object_v1;

// Functional-coverage handles are context-local monotonically allocated IDs.
// Zero is the language null value and never names a live instance.
typedef uint64_t obelisk_rt_covergroup_v1;

// A managed word is the common 64-bit storage unit used by values which may
// either contain an aligned heap handle or an immediate representation.
// Strings use the low two bits as a tag:
//
//   00  zero (empty) or an aligned heap-string handle
//   01  seven-byte SSO; bits 2..4 are the length and bits 8..63 are bytes
//
// Tags 10 and 11, nonzero reserved bits in an SSO control byte, and SSO
// lengths greater than seven are invalid. Heap handles are always at least
// 16-byte aligned.
typedef uint64_t obelisk_rt_managed_word_v1;
typedef obelisk_rt_managed_word_v1 obelisk_rt_string_v1;

typedef int32_t obelisk_rt_status;
#define OBELISK_RT_OK INT32_C(0)
#define OBELISK_RT_EOF INT32_C(1)
#define OBELISK_RT_INVALID_ARGUMENT INT32_C(2)
#define OBELISK_RT_INVALID_HANDLE INT32_C(3)
#define OBELISK_RT_IO_ERROR INT32_C(4)
#define OBELISK_RT_OUT_OF_MEMORY INT32_C(5)
#define OBELISK_RT_OUT_OF_RESOURCES INT32_C(6)
#define OBELISK_RT_FORMAT_ERROR INT32_C(7)
#define OBELISK_RT_ARGUMENT_MISMATCH INT32_C(8)
#define OBELISK_RT_INVALID_BYTECODE INT32_C(9)
#define OBELISK_RT_STEP_LIMIT INT32_C(10)
#define OBELISK_RT_LAYOUT_MISMATCH INT32_C(11)
#define OBELISK_RT_INVALID_CONTINUATION INT32_C(12)
#define OBELISK_RT_TIER_UNAVAILABLE INT32_C(13)
#define OBELISK_RT_INVALID_LIFECYCLE INT32_C(14)
#define OBELISK_RT_INVALID_FRAME INT32_C(15)
#define OBELISK_RT_INVALID_DESIGN INT32_C(16)
#define OBELISK_RT_PERMISSION_DENIED INT32_C(17)
#define OBELISK_RT_DPI_DISABLE_UNSUPPORTED INT32_C(18)
#define OBELISK_RT_FATAL INT32_C(19)
// Generated run_until reached an enabled runtime synchronization boundary.
// Unlike TIER_UNAVAILABLE, this is a resumable handoff and must not deopt the
// native schedule permanently.
#define OBELISK_RT_AOT_CHECKPOINT INT32_C(20)
// Generated run_until stopped before a proven future runtime deadline. The
// runtime advances to that deadline, drains the slot, and resumes the saved
// periodic phase. Same-slot branch checkpoints continue to use 20.
#define OBELISK_RT_AOT_TIMED_CHECKPOINT INT32_C(21)
// Internal generated-branch handoff. The generated run loop consumes this
// status into an exact callback transaction before returning ordinary status
// 20 to the runtime scheduler.
#define OBELISK_RT_AOT_GENERATED_CHECKPOINT INT32_C(22)

// Managed SystemVerilog value ABI. Object handles are nullable pointers into
// the context-owned, non-moving heap. Collector and synchronization metadata
// live outside the language-visible object. Class objects retain an immutable
// class descriptor in their first word; runtime-owned variable-sized values
// use an opaque runtime descriptor word and never pass through class
// validation.
typedef uint32_t obelisk_rt_managed_kind_v1;
enum {
  OBELISK_RT_MANAGED_INVALID = 0,
  OBELISK_RT_MANAGED_CLASS = 1,
  OBELISK_RT_MANAGED_STRING = 2,
  OBELISK_RT_MANAGED_CONTAINER = 3,
  OBELISK_RT_MANAGED_BUFFER = 4,
  OBELISK_RT_MANAGED_KEY_BLOB = 5,
  OBELISK_RT_MANAGED_REFERENCE_PATH = 6
};

typedef uint32_t obelisk_rt_trace_kind;
enum {
  OBELISK_RT_TRACE_STRONG = 1,
  OBELISK_RT_TRACE_WEAK = 2,
  OBELISK_RT_TRACE_EMBEDDED = 3
};

typedef uint32_t obelisk_rt_managed_slot_kind_v1;
enum {
  OBELISK_RT_MANAGED_SLOT_INVALID = 0,
  OBELISK_RT_MANAGED_SLOT_CLASS = 1,
  OBELISK_RT_MANAGED_SLOT_STRING = 2,
  OBELISK_RT_MANAGED_SLOT_CONTAINER = 3
};

struct obelisk_rt_trace_layout_v1;

// One entry describes either repeated object-handle slots or repeated embedded
// values. For scalar fields count is one and stride may be zero. Embedded
// entries recurse through child_layout; handle entries require it to be null.
typedef struct obelisk_rt_trace_entry_v1 {
  uint64_t offset;
  uint64_t stride;
  uint64_t count;
  obelisk_rt_trace_kind kind;
  obelisk_rt_managed_slot_kind_v1 slot_kind;
  const struct obelisk_rt_trace_layout_v1 *child_layout;
} obelisk_rt_trace_entry_v1;

typedef struct obelisk_rt_trace_layout_v1 {
  uint32_t version;
  uint32_t reserved;
  uint64_t size;
  uint64_t alignment;
  const obelisk_rt_trace_entry_v1 *entries;
  uint64_t entry_count;
} obelisk_rt_trace_layout_v1;

typedef struct obelisk_rt_method_argument_v1 {
  const void *data;
  uint64_t size;
} obelisk_rt_method_argument_v1;

typedef obelisk_rt_status (*obelisk_rt_native_method_v1)(
    obelisk_rt_context *context, obelisk_rt_gc_lane_v1 *lane,
    obelisk_rt_object_v1 *receiver,
    const obelisk_rt_method_argument_v1 *arguments, uint32_t argument_count,
    void *result, uint64_t result_size);

#define OBELISK_RT_METHOD_TASK (UINT32_C(1) << 0)
#define OBELISK_RT_METHOD_PURE (UINT32_C(1) << 1)
#define OBELISK_RT_METHOD_NO_BYTECODE UINT32_MAX

typedef struct obelisk_rt_method_descriptor_v1 {
  uint64_t signature_id;
  uint32_t flags;
  uint32_t bytecode_function;
  obelisk_rt_native_method_v1 native_entry;
  const void *environment;
} obelisk_rt_method_descriptor_v1;

// One flattened interface dispatch table for a managed class. Method slots
// are indexed by the declaring interface's stable method ordinal and select
// entries in the containing class descriptor's effective method table.
typedef struct obelisk_rt_interface_descriptor_v1 {
  uint64_t interface_id;
  const uint32_t *method_slots;
  uint64_t method_count;
} obelisk_rt_interface_descriptor_v1;

#define OBELISK_RT_CLASS_ABSTRACT (UINT32_C(1) << 0)
#define OBELISK_RT_CLASS_INTERFACE (UINT32_C(1) << 1)
#define OBELISK_RT_CLASS_FINAL (UINT32_C(1) << 2)
#define OBELISK_RT_CLASS_WEAK_WRAPPER (UINT32_C(1) << 3)

typedef struct obelisk_rt_class_descriptor_v1 {
  uint32_t version;
  uint32_t flags;
  uint64_t class_id;
  uint64_t instance_size;
  uint64_t instance_alignment;
  const struct obelisk_rt_class_descriptor_v1 *base;
  const obelisk_rt_interface_descriptor_v1 *interfaces;
  uint64_t interface_count;
  const obelisk_rt_trace_layout_v1 *layout;
  const obelisk_rt_method_descriptor_v1 *methods;
  uint64_t method_count;
  const char *debug_name;
  uint64_t debug_name_size;
} obelisk_rt_class_descriptor_v1;

// Compiler-emitted erased element metadata. Four-state storage places an
// equally sized unknown plane immediately after the value plane. type_id is a
// stable, nonzero design identifier so pointer-free bytecode can resolve the
// native descriptor registered during context startup.
typedef uint32_t obelisk_rt_element_kind_v1;
enum {
  OBELISK_RT_ELEMENT_BITS = 1,
  OBELISK_RT_ELEMENT_LOGIC = 2,
  OBELISK_RT_ELEMENT_REAL = 3,
  OBELISK_RT_ELEMENT_CLASS_HANDLE = 4,
  OBELISK_RT_ELEMENT_STRING = 5,
  OBELISK_RT_ELEMENT_CONTAINER_HANDLE = 6,
  OBELISK_RT_ELEMENT_AGGREGATE = 7,
  OBELISK_RT_ELEMENT_EVENT = 8
};

#define OBELISK_RT_ELEMENT_FOUR_STATE (UINT32_C(1) << 0)
#define OBELISK_RT_ELEMENT_SIGNED (UINT32_C(1) << 1)

typedef struct obelisk_rt_element_trace_slot_v1 {
  uint64_t offset;
  obelisk_rt_managed_slot_kind_v1 kind;
  uint32_t reserved;
} obelisk_rt_element_trace_slot_v1;

typedef struct obelisk_rt_element_type_v1 {
  uint32_t version;
  obelisk_rt_element_kind_v1 kind;
  uint64_t type_id;
  uint32_t flags;
  uint32_t reserved;
  uint64_t value_size;
  uint64_t alignment;
  uint64_t bit_width;
  const obelisk_rt_trace_layout_v1 *trace;
} obelisk_rt_element_type_v1;

// Registered descriptors and every trace-layout object reachable from them
// are immutable and must remain alive until their context is destroyed.

// Stack-owned precise root record. Push/pop must be properly nested on one
// registered lane. Keeping the record caller-owned avoids an allocation at
// every native call boundary.
typedef struct obelisk_rt_gc_root_v1 {
  obelisk_rt_object_v1 **slot;
  struct obelisk_rt_gc_root_v1 *previous;
  uint64_t cookie;
} obelisk_rt_gc_root_v1;

// Stack-owned contiguous root range. Generated activations use one range
// record and one pointer slot per live managed SSA value, independent of the
// number of roots.
typedef struct obelisk_rt_gc_root_range_v1 {
  obelisk_rt_object_v1 **slots;
  uint64_t count;
  struct obelisk_rt_gc_root_range_v1 *previous;
  uint64_t cookie;
} obelisk_rt_gc_root_range_v1;

// Tagged managed roots are separate from legacy object-pointer roots so
// native class/container ABI users remain source compatible. Immediate words
// are validated but have no outgoing heap edge.
typedef struct obelisk_rt_gc_managed_root_v1 {
  obelisk_rt_managed_word_v1 *slot;
  struct obelisk_rt_gc_managed_root_v1 *previous;
  uint64_t cookie;
} obelisk_rt_gc_managed_root_v1;

typedef struct obelisk_rt_gc_managed_root_range_v1 {
  obelisk_rt_managed_word_v1 *slots;
  uint64_t count;
  struct obelisk_rt_gc_managed_root_range_v1 *previous;
  uint64_t cookie;
} obelisk_rt_gc_managed_root_range_v1;

typedef struct obelisk_rt_gc_statistics_v1 {
  uint64_t live_objects;
  uint64_t live_bytes;
  uint64_t allocated_objects;
  uint64_t reclaimed_objects;
  uint64_t collection_count;
  uint64_t chunk_allocation_count;
  uint64_t large_allocation_count;
  uint64_t cached_empty_chunks;
  uint64_t next_collection_bytes;
} obelisk_rt_gc_statistics_v1;

typedef struct obelisk_rt_buffer_v1 {
  uint8_t *data;
  uint64_t size;
} obelisk_rt_buffer_v1;

// Stable simulator handles never contain native addresses. Generation is
// incremented when a dynamic descriptor slot is reused; static descriptors use
// generation zero.
typedef uint32_t obelisk_rt_descriptor_kind;
enum {
  OBELISK_RT_DESCRIPTOR_INVALID = 0,
  OBELISK_RT_DESCRIPTOR_SCOPE = 1,
  OBELISK_RT_DESCRIPTOR_STORAGE = 2,
  OBELISK_RT_DESCRIPTOR_NET = 3,
  OBELISK_RT_DESCRIPTOR_DRIVER = 4,
  OBELISK_RT_DESCRIPTOR_EVENT = 5,
  OBELISK_RT_DESCRIPTOR_PROCESS = 6,
  OBELISK_RT_DESCRIPTOR_FRAGMENT = 7,
  OBELISK_RT_DESCRIPTOR_FUNCTION = 8,
  OBELISK_RT_DESCRIPTOR_OBSERVER = 9
};

typedef struct obelisk_rt_handle_v1 {
  obelisk_rt_descriptor_kind kind;
  uint32_t generation;
  uint64_t id;
} obelisk_rt_handle_v1;

// Design-wide executable metadata is deliberately split from the optional
// reflection image used by VPI and waveform dumping. Both payloads are
// immutable, pointer-free byte images; the only native pointers are this
// linker-resolved pointer/size pair. A descriptor with no reflection consumers
// has a null image.
#define OBELISK_RT_EXECUTION_HAS_BYTECODE (UINT32_C(1) << 0)
#define OBELISK_RT_EXECUTION_HAS_DESIGN_DATABASE (UINT32_C(1) << 1)
#define OBELISK_RT_EXECUTION_VPI_READ (UINT32_C(1) << 2)
#define OBELISK_RT_EXECUTION_VPI_WRITE (UINT32_C(1) << 3)
#define OBELISK_RT_EXECUTION_REQUIRE_BYTECODE (UINT32_C(1) << 4)
#define OBELISK_RT_EXECUTION_PREPONED_SNAPSHOT (UINT32_C(1) << 5)
// The design database is also the immutable hierarchy/type image consumed by
// waveform dumping. This grants no VPI access; the VPI permission bits remain
// the sole authority for VPI handles and reads.
#define OBELISK_RT_EXECUTION_WAVEFORM_METADATA (UINT32_C(1) << 6)

// Executable event-region ordinals. The eight PLI callback regions remain
// compiler-only until the callback ABI can populate them. Preponed is serviced
// by a slot-boundary hook rather than a process queue.
typedef uint32_t obelisk_rt_event_region_v1;
enum {
  OBELISK_RT_REGION_ACTIVE = 0,
  OBELISK_RT_REGION_INACTIVE = 1,
  OBELISK_RT_REGION_NBA = 2,
  OBELISK_RT_REGION_OBSERVED = 3,
  OBELISK_RT_REGION_REACTIVE = 4,
  OBELISK_RT_REGION_RE_INACTIVE = 5,
  OBELISK_RT_REGION_RE_NBA = 6,
  OBELISK_RT_REGION_POSTPONED = 7
};

// Scheduler registration flags. FINAL deliberately retains the old phase-one
// encoding. The home region is encoded directly so Observed and Postponed do
// not require future ABI changes.
#define OBELISK_RT_SCHEDULE_FINAL (UINT32_C(1) << 0)
#define OBELISK_RT_SCHEDULE_HOME_SHIFT 1u
#define OBELISK_RT_SCHEDULE_HOME_MASK                                          \
  (UINT32_C(7) << OBELISK_RT_SCHEDULE_HOME_SHIFT)
#define OBELISK_RT_SCHEDULE_HOME(region)                                       \
  ((uint32_t)(region) << OBELISK_RT_SCHEDULE_HOME_SHIFT)
#define OBELISK_RT_SCHEDULE_INITIAL (UINT32_C(1) << 4)
#define OBELISK_RT_SCHEDULE_STARTUP (UINT32_C(1) << 5)
#define OBELISK_RT_SCHEDULE_DETACHED_CONTROLS (UINT32_C(1) << 6)
// A signal-resumed process with this flag runs before ordinary work already
// queued in the same event region. Unlike STARTUP, this does not change the
// process's event region or make its initial activation urgent.
#define OBELISK_RT_SCHEDULE_PRIORITY_SIGNAL (UINT32_C(1) << 7)

// The bytecode SPAWN intrinsic uses its three high flag bits for scheduler
// classifications and the remaining bits for the callee function index.
#define OBELISK_RT_INTRINSIC_SPAWN_STARTUP (UINT32_C(1) << 31)
#define OBELISK_RT_INTRINSIC_SPAWN_DETACHED_CONTROLS (UINT32_C(1) << 30)
#define OBELISK_RT_INTRINSIC_SPAWN_PRIORITY_SIGNAL (UINT32_C(1) << 29)
#define OBELISK_RT_INTRINSIC_SPAWN_FUNCTION_MASK UINT32_C(0x1fffffff)

// Serialized design-bytecode function flags. Process functions encode their
// canonical frame size shifted left by one. Bits 60-62 encode the executable
// home region and the high bit records final-phase scheduling.
#define OBELISK_RT_DESIGN_FUNCTION_PROCESS UINT64_C(0x0000000000000001)
#define OBELISK_RT_DESIGN_FUNCTION_FRAME_SIZE_MASK UINT64_C(0x0ffffffffffffffe)
#define OBELISK_RT_DESIGN_FUNCTION_FRAME_SIZE_LIMIT UINT64_C(0x0800000000000000)
#define OBELISK_RT_DESIGN_FUNCTION_HOME_SHIFT 60u
#define OBELISK_RT_DESIGN_FUNCTION_HOME_MASK UINT64_C(0x7000000000000000)
#define OBELISK_RT_DESIGN_FUNCTION_HOME(region)                                \
  ((uint64_t)(region) << OBELISK_RT_DESIGN_FUNCTION_HOME_SHIFT)
#define OBELISK_RT_DESIGN_FUNCTION_FINAL UINT64_C(0x8000000000000000)

// DPI scope records are immutable execution metadata and remain available
// independently of the optional VPI design database. IDs are dense from zero;
// UINT64_MAX denotes the root's absent parent. Time unit and precision are
// decimal exponents in seconds, as returned by svGetTimeUnit and
// svGetTimePrecision.
typedef struct obelisk_rt_dpi_scope_v1 {
  uint64_t id;
  uint64_t parent_id;
  const char *name;
  uint64_t name_size;
  int32_t time_unit;
  int32_t time_precision;
  uint32_t reserved;
} obelisk_rt_dpi_scope_v1;

struct obelisk_rt_process_descriptor_v1;

#define OBELISK_RT_ACTIVATION_HAS_NATIVE (UINT32_C(1) << 0)
#define OBELISK_RT_ACTIVATION_HAS_BYTECODE (UINT32_C(1) << 1)
#define OBELISK_RT_ACTIVATION_NO_BYTECODE UINT32_MAX

// Direct activation inventory. Records are sorted by stable code-unit ID and
// identify both executable representations of a process, fork branch, or
// task. This remains independent of optional VPI reflection metadata.
typedef struct obelisk_rt_activation_descriptor_v1 {
  uint64_t code_unit_id;
  const struct obelisk_rt_process_descriptor_v1 *native_entry;
  uint32_t bytecode_function;
  uint32_t flags;
} obelisk_rt_activation_descriptor_v1;

typedef uint32_t obelisk_rt_observer_capture_kind;
enum {
  OBELISK_RT_OBSERVER_CAPTURE_STORAGE = 1,
  OBELISK_RT_OBSERVER_CAPTURE_NET = 2,
  OBELISK_RT_OBSERVER_CAPTURE_EVENT = 3,
  OBELISK_RT_OBSERVER_CAPTURE_DRIVER = 4
};

typedef struct obelisk_rt_observer_capture_abi_v1 {
  obelisk_rt_observer_capture_kind kind;
  uint32_t width;
} obelisk_rt_observer_capture_abi_v1;

#define OBELISK_RT_OBSERVER_FOUR_STATE (UINT32_C(1) << 0)
#define OBELISK_RT_OBSERVER_REAL32 (UINT32_C(1) << 1)
#define OBELISK_RT_OBSERVER_REAL64 (UINT32_C(1) << 2)
#define OBELISK_RT_OBSERVER_NO_BYTECODE UINT32_MAX

typedef obelisk_rt_status (*obelisk_rt_native_observer_v1)(
    obelisk_rt_context *context, const uint64_t *captures,
    uint32_t capture_count, uint64_t *value, uint64_t *unknown,
    uint32_t limb_count);

typedef struct obelisk_rt_observer_descriptor_v1 {
  uint64_t code_unit_id;
  const obelisk_rt_observer_capture_abi_v1 *capture_abi;
  uint32_t capture_count;
  uint32_t result_width;
  uint32_t flags;
  uint32_t bytecode_function;
  obelisk_rt_native_observer_v1 native_evaluator;
  uint64_t reserved;
} obelisk_rt_observer_descriptor_v1;

// One compiler-coalesced canonical state range retained in the dense
// Preponed snapshot. Each range begins at a byte boundary in the snapshot;
// source offsets and widths remain bit-granular.
typedef struct obelisk_rt_sampled_range_v1 {
  uint64_t source_bit_offset;
  uint64_t snapshot_byte_offset;
  uint64_t bit_width;
} obelisk_rt_sampled_range_v1;

// Optional data carried by execution_descriptor_v1::reserved when the
// Preponed snapshot capability is set. The descriptor keeps its original ABI
// size while allowing sampled-state metadata to evolve independently.
#define OBELISK_RT_EXECUTION_EXTENSION_VERSION UINT32_C(1)
typedef struct obelisk_rt_execution_extension_v1 {
  uint32_t version;
  uint32_t size;
  const obelisk_rt_sampled_range_v1 *sampled_ranges;
  uint64_t sampled_range_count;
} obelisk_rt_execution_extension_v1;

typedef struct obelisk_rt_execution_descriptor_v1 {
  uint32_t version;
  uint32_t flags;
  uint64_t reserved;
  const uint8_t *bytecode;
  uint64_t bytecode_size;
  const uint8_t *design_database;
  uint64_t design_database_size;
  uint64_t state_bit_count;
  uint64_t checksum;
  const obelisk_rt_dpi_scope_v1 *dpi_scopes;
  uint64_t dpi_scope_count;
  int32_t dpi_time_precision;
  uint32_t dpi_reserved;
  const obelisk_rt_activation_descriptor_v1 *activations;
  uint64_t activation_count;
  const obelisk_rt_observer_descriptor_v1 *observers;
  uint64_t observer_count;
} obelisk_rt_execution_descriptor_v1;

// One of these immutable records is attached to the canonical process
// descriptor for every process encoded in the design image.
typedef struct obelisk_rt_design_bytecode_entry_v1 {
  const obelisk_rt_execution_descriptor_v1 *execution;
  uint32_t function;
  uint32_t reserved;
} obelisk_rt_design_bytecode_entry_v1;

// Pointer-free serialized image headers shared by the encoder and runtime
// validator. Fields are read and written explicitly as little-endian values;
// the structs provide one typed source of truth for field offsets.
typedef struct obelisk_rt_design_bytecode_header_v1 {
  uint8_t magic[8];
  uint32_t version;
  uint32_t reserved;
  uint32_t header_size;
  uint32_t flags;
  uint64_t image_size;
  uint64_t checksum;
  uint64_t function_offset;
  uint64_t function_count;
  uint64_t layout_offset;
  uint64_t layout_count;
  uint64_t code_offset;
  uint64_t instruction_count;
  uint64_t operand_offset;
  uint64_t operand_count;
  uint64_t constant_offset;
  uint64_t constant_size;
  uint64_t continuation_offset;
  uint64_t continuation_count;
  uint64_t intrinsic_offset;
  uint64_t intrinsic_count;
  uint64_t site_offset;
  uint64_t site_count;
  uint64_t state_offset;
  uint64_t state_count;
  uint64_t connectivity_offset;
  uint64_t connectivity_count;
  uint64_t tail_reserved;
} obelisk_rt_design_bytecode_header_v1;

typedef struct obelisk_rt_design_database_header_v1 {
  uint8_t magic[8];
  uint32_t version;
  uint32_t reserved;
  uint32_t profile;
  uint32_t header_size;
  uint64_t image_size;
  uint64_t checksum;
  uint64_t root_offset;
  uint64_t scope_offset;
  uint64_t scope_count;
  uint64_t object_offset;
  uint64_t object_count;
  uint64_t type_offset;
  uint64_t type_count;
  uint64_t string_offset;
  uint64_t string_size;
  uint64_t index_offset;
  uint64_t index_count;
} obelisk_rt_design_database_header_v1;

#define OBELISK_RT_DESIGN_BYTECODE_HEADER_SIZE 208u
#define OBELISK_RT_DESIGN_DATABASE_HEADER_SIZE 128u
#define OBELISK_RT_DESIGN_BYTECODE_INSTRUCTION_SIZE 32u
#define OBELISK_RT_DESIGN_BYTECODE_MAGIC "OBBCDS1"
#define OBELISK_RT_DESIGN_DATABASE_MAGIC "OBDSGN1"

typedef uint8_t obelisk_rt_design_register_kind;
enum {
  OBELISK_RT_DBREG_INVALID = 0,
  OBELISK_RT_DBREG_BITS = 1,
  OBELISK_RT_DBREG_LOGIC = 2,
  OBELISK_RT_DBREG_HANDLE = 3,
  OBELISK_RT_DBREG_STATUS = 4,
  OBELISK_RT_DBREG_RESOURCE = 5,
  OBELISK_RT_DBREG_BYTES = 6,
  // Native object pointers are confined to these precisely traced register
  // kinds. MANAGED_REF is {object, byte offset}; ARGUMENT_REF uses tag 0 for
  // ordinary state, tag 1 for {class owner, field offset}, and tag 2 for a
  // ReferencePath in its rooted owner word. Their first words are roots.
  OBELISK_RT_DBREG_MANAGED = 7,
  OBELISK_RT_DBREG_MANAGED_REF = 8,
  OBELISK_RT_DBREG_ARGUMENT_REF = 9,
  // Managed strings are tagged words, not native object pointers. Floating
  // registers retain their semantic width so the validator cannot admit
  // integer opcodes over IEEE payloads.
  OBELISK_RT_DBREG_STRING = 10,
  OBELISK_RT_DBREG_REAL32 = 11,
  OBELISK_RT_DBREG_REAL64 = 12
};

#define OBELISK_RT_DBREG_SIGNED (UINT8_C(1) << 0)

typedef uint16_t obelisk_rt_design_bytecode_opcode;
enum {
  OBELISK_RT_DB_NOP = 0,
  OBELISK_RT_DB_CONSTANT = 1,
  OBELISK_RT_DB_MOVE = 2,
  OBELISK_RT_DB_NOT = 3,
  OBELISK_RT_DB_AND = 4,
  OBELISK_RT_DB_OR = 5,
  OBELISK_RT_DB_XOR = 6,
  OBELISK_RT_DB_ADD = 7,
  OBELISK_RT_DB_SUB = 8,
  OBELISK_RT_DB_MUL = 9,
  OBELISK_RT_DB_UDIV = 10,
  OBELISK_RT_DB_SDIV = 11,
  OBELISK_RT_DB_UREM = 12,
  OBELISK_RT_DB_SREM = 13,
  OBELISK_RT_DB_SHL = 14,
  OBELISK_RT_DB_LSHR = 15,
  OBELISK_RT_DB_ASHR = 16,
  OBELISK_RT_DB_COMPARE = 17,
  OBELISK_RT_DB_SELECT = 18,
  OBELISK_RT_DB_REDUCE = 19,
  OBELISK_RT_DB_CONCAT = 20,
  OBELISK_RT_DB_EXTRACT = 21,
  OBELISK_RT_DB_INSERT = 22,
  OBELISK_RT_DB_LOAD_FRAME = 23,
  OBELISK_RT_DB_STORE_FRAME = 24,
  OBELISK_RT_DB_MAKE_HANDLE = 25,
  OBELISK_RT_DB_HANDLE_OFFSET = 26,
  OBELISK_RT_DB_LOAD_STATE = 27,
  OBELISK_RT_DB_STORE_STATE = 28,
  OBELISK_RT_DB_JUMP = 29,
  OBELISK_RT_DB_BRANCH = 30,
  OBELISK_RT_DB_CALL = 31,
  OBELISK_RT_DB_RETURN = 32,
  OBELISK_RT_DB_CONTINUE = 33,
  OBELISK_RT_DB_SUSPEND = 34,
  OBELISK_RT_DB_TERMINATE = 35,
  OBELISK_RT_DB_INTRINSIC = 36,
  OBELISK_RT_DB_FAIL = 37,
  OBELISK_RT_DB_MAKE_LOCAL_HANDLE = 38,
  OBELISK_RT_DB_HANDLE_ID = 39,
  OBELISK_RT_DB_TASK_CALL = 40,
  OBELISK_RT_DB_VIRTUAL_CALL = 41,
  // Consume one precise managed-root word from a canonical continuation
  // frame after the value has been restored into traced bytecode registers.
  OBELISK_RT_DB_CLEAR_FRAME_ROOT = 42,
  OBELISK_RT_DB_OVERRIDE_STATE = 43,
  OBELISK_RT_DB_RELEASE_STATE = 44,
  OBELISK_RT_DB_FADD = 45,
  OBELISK_RT_DB_FSUB = 46,
  OBELISK_RT_DB_FMUL = 47,
  OBELISK_RT_DB_FDIV = 48,
  OBELISK_RT_DB_FNEG = 49,
  OBELISK_RT_DB_FCOMPARE = 50,
  OBELISK_RT_DB_FEXT = 51,
  OBELISK_RT_DB_FTRUNC = 52,
  OBELISK_RT_DB_FPOW = 53,
  OBELISK_RT_DB_VIRTUAL_TASK_CALL = 54,
  // Declarative precise-root record for a canonical process frame. Execution
  // is a no-op; the collector scans these records while activations suspend.
  OBELISK_RT_DB_FRAME_ROOT = 55,
  // Interface dispatch uses destination as an operand-table index whose pair
  // stores the interface ID and stable method ordinal.
  OBELISK_RT_DB_INTERFACE_CALL = 56,
  OBELISK_RT_DB_INTERFACE_TASK_CALL = 57
};

// StoreState writes its exact post-resolution transition predicate to the
// destination i1 register when this flag is set.
#define OBELISK_RT_DB_STORE_STATE_CHANGED UINT16_C(1)
// Preserve the latest continuous publication beneath force / procedural
// assign so releasing the override can immediately reveal its driver.
#define OBELISK_RT_DB_STORE_STATE_CONTINUOUS UINT16_C(2)

typedef uint16_t obelisk_rt_design_extract_kind;
enum {
  OBELISK_RT_DB_EXTRACT_ZERO_EXTEND = 0,
  OBELISK_RT_DB_EXTRACT_SIGN_EXTEND = 1,
  // Physical 64-bit class-handle lane of an aggregate value. This is
  // deliberately distinct from ordinary numeric extraction so the bytecode
  // validator can keep managed/numeric coercions confined to compiler-marked
  // aggregate element operations.
  OBELISK_RT_DB_AGGREGATE_MANAGED = 2
};

typedef uint16_t obelisk_rt_design_select_kind;
enum {
  OBELISK_RT_DB_SELECT_BINARY = 0,
  OBELISK_RT_DB_SELECT_FOUR_STATE = 1
};

typedef uint16_t obelisk_rt_design_reduction_kind;
enum {
  OBELISK_RT_DB_REDUCE_AND = 0,
  OBELISK_RT_DB_REDUCE_OR = 1,
  OBELISK_RT_DB_REDUCE_XOR = 2,
  OBELISK_RT_DB_REDUCE_NAND = 3,
  OBELISK_RT_DB_REDUCE_NOR = 4,
  OBELISK_RT_DB_REDUCE_XNOR = 5,
  OBELISK_RT_DB_REDUCE_IS_TRUE = 6,
  OBELISK_RT_DB_REDUCE_LOGICAL_NOT = 7,
  OBELISK_RT_DB_REDUCE_LOGICAL_VALUE = 8
};

typedef uint16_t obelisk_rt_design_float_compare_kind;
enum {
  OBELISK_RT_DB_FCMP_EQ = 0,
  OBELISK_RT_DB_FCMP_NE = 1,
  OBELISK_RT_DB_FCMP_LT = 2,
  OBELISK_RT_DB_FCMP_LE = 3,
  OBELISK_RT_DB_FCMP_GT = 4,
  OBELISK_RT_DB_FCMP_GE = 5
};

typedef uint16_t obelisk_rt_design_override_kind;
enum {
  OBELISK_RT_DB_OVERRIDE_FORCE = 0,
  OBELISK_RT_DB_OVERRIDE_ASSIGN = 1
};

// COMPARE flags. Case comparisons return a known two-state result. Wildcard
// equality masks unknown RHS bits but can return X for a relevant unknown LHS
// bit; ordinary comparisons return X when an operand has unknown bits.
typedef uint16_t obelisk_rt_design_compare_kind;
enum {
  OBELISK_RT_DB_CMP_EQ = 0,
  OBELISK_RT_DB_CMP_NE = 1,
  OBELISK_RT_DB_CMP_ULT = 2,
  OBELISK_RT_DB_CMP_ULE = 3,
  OBELISK_RT_DB_CMP_UGT = 4,
  OBELISK_RT_DB_CMP_UGE = 5,
  OBELISK_RT_DB_CMP_SLT = 6,
  OBELISK_RT_DB_CMP_SLE = 7,
  OBELISK_RT_DB_CMP_SGT = 8,
  OBELISK_RT_DB_CMP_SGE = 9,
  OBELISK_RT_DB_CMP_CASE_EQ = 10,
  OBELISK_RT_DB_CMP_CASE_NE = 11,
  OBELISK_RT_DB_CMP_WILD_EQ = 12,
  OBELISK_RT_DB_CMP_WILD_NE = 13,
  OBELISK_RT_DB_CMP_CASEZ_EQ = 14,
  OBELISK_RT_DB_CMP_CASEXZ_EQ = 15
};

typedef uint32_t obelisk_rt_intrinsic_id;
enum {
  OBELISK_RT_INTRINSIC_V1_FORMAT = UINT32_C(0x00010001),
  OBELISK_RT_INTRINSIC_V1_DISPLAY = UINT32_C(0x00010002),
  OBELISK_RT_INTRINSIC_V1_TIME_FORMAT = UINT32_C(0x00010003),
  OBELISK_RT_INTRINSIC_V1_FILE_BASE = UINT32_C(0x00010100),
  OBELISK_RT_INTRINSIC_V1_FILE_OPEN_MCD = UINT32_C(0x00010100),
  OBELISK_RT_INTRINSIC_V1_FILE_OPEN = UINT32_C(0x00010101),
  OBELISK_RT_INTRINSIC_V1_FILE_CLOSE = UINT32_C(0x00010102),
  OBELISK_RT_INTRINSIC_V1_FILE_FLUSH = UINT32_C(0x00010103),
  OBELISK_RT_INTRINSIC_V1_FILE_GETC = UINT32_C(0x00010104),
  OBELISK_RT_INTRINSIC_V1_FILE_UNGETC = UINT32_C(0x00010105),
  OBELISK_RT_INTRINSIC_V1_FILE_GETLINE = UINT32_C(0x00010106),
  OBELISK_RT_INTRINSIC_V1_FILE_READ_PACKED = UINT32_C(0x00010107),
  OBELISK_RT_INTRINSIC_V1_FILE_EOF = UINT32_C(0x00010108),
  OBELISK_RT_INTRINSIC_V1_FILE_SEEK = UINT32_C(0x00010109),
  OBELISK_RT_INTRINSIC_V1_FILE_TELL = UINT32_C(0x0001010a),
  OBELISK_RT_INTRINSIC_V1_FILE_REWIND = UINT32_C(0x0001010b),
  OBELISK_RT_INTRINSIC_V1_FILE_OPEN_STRING_MCD = UINT32_C(0x0001010c),
  OBELISK_RT_INTRINSIC_V1_FILE_OPEN_STRING = UINT32_C(0x0001010d),
  OBELISK_RT_INTRINSIC_V1_FILE_GETLINE_STRING = UINT32_C(0x0001010e),
  OBELISK_RT_INTRINSIC_V1_FILE_ERROR_STRING = UINT32_C(0x0001010f),
  OBELISK_RT_INTRINSIC_V1_PLUSARG_TEST = UINT32_C(0x00010110),
  OBELISK_RT_INTRINSIC_V1_PLUSARG_VALUE = UINT32_C(0x00010111),
  OBELISK_RT_INTRINSIC_V1_FILE_SCAN_FIELD = UINT32_C(0x00010112),
  OBELISK_RT_INTRINSIC_V1_SPAWN = UINT32_C(0x00010200),
  OBELISK_RT_INTRINSIC_V1_NBA = UINT32_C(0x00010201),
  // Statically planned NBA. The final i64 input is the NBASiteAttr identity;
  // the remaining inputs have the same ABI as OBELISK_RT_INTRINSIC_V1_NBA.
  // Runtimes without an installed static schedule execute this through the
  // canonical generic NBA path.
  OBELISK_RT_INTRINSIC_V1_STATIC_NBA = UINT32_C(0x00010219),
  OBELISK_RT_INTRINSIC_V1_EVENT_TRIGGER = UINT32_C(0x00010202),
  OBELISK_RT_INTRINSIC_V1_STATE_ALLOC = UINT32_C(0x00010203),
  OBELISK_RT_INTRINSIC_V1_EVENT_TRIGGERED = UINT32_C(0x00010204),
  OBELISK_RT_INTRINSIC_V1_DISABLE_CHILDREN = UINT32_C(0x00010205),
  OBELISK_RT_INTRINSIC_V1_CONTROL_ENTER = UINT32_C(0x00010206),
  OBELISK_RT_INTRINSIC_V1_CONTROL_LEAVE = UINT32_C(0x00010207),
  OBELISK_RT_INTRINSIC_V1_CONTROL_DISABLE = UINT32_C(0x00010208),
  OBELISK_RT_INTRINSIC_V1_STATIC_ONCE = UINT32_C(0x00010209),
  OBELISK_RT_INTRINSIC_V1_FINISH = UINT32_C(0x0001020a),
  OBELISK_RT_INTRINSIC_V1_FATAL = UINT32_C(0x0001020b),
  OBELISK_RT_INTRINSIC_V1_TERMINATION_REQUESTED = UINT32_C(0x0001020c),
  OBELISK_RT_INTRINSIC_V1_TIME_NOW = UINT32_C(0x0001020d),
  OBELISK_RT_INTRINSIC_V1_TIME_TO_REAL = UINT32_C(0x0001020e),
  OBELISK_RT_INTRINSIC_V1_TIME_FROM_REAL = UINT32_C(0x0001020f),
  OBELISK_RT_INTRINSIC_V1_REAL_FROM_INTEGER = UINT32_C(0x00010210),
  OBELISK_RT_INTRINSIC_V1_REAL_TO_INTEGER = UINT32_C(0x00010211),
  OBELISK_RT_INTRINSIC_V1_REAL_COMPARE = UINT32_C(0x00010212),
  OBELISK_RT_INTRINSIC_V1_COUNT_BITS = UINT32_C(0x00010213),
  OBELISK_RT_INTRINSIC_V1_CLOG2 = UINT32_C(0x00010214),
  OBELISK_RT_INTRINSIC_V1_MONITOR_REGISTER = UINT32_C(0x00010215),
  OBELISK_RT_INTRINSIC_V1_MONITOR_CONTROL = UINT32_C(0x00010216),
  OBELISK_RT_INTRINSIC_V1_MONITOR_CURRENT = UINT32_C(0x00010217),
  OBELISK_RT_INTRINSIC_V1_DEFERRED_ONCE = UINT32_C(0x00010218),
  OBELISK_RT_INTRINSIC_V1_STOP = UINT32_C(0x0001021a),
  OBELISK_RT_INTRINSIC_V1_DEFERRED_ENQUEUE = UINT32_C(0x0001021b),
  OBELISK_RT_INTRINSIC_V1_DEFERRED_MATURE = UINT32_C(0x0001021c),
  OBELISK_RT_INTRINSIC_V1_SAMPLED_READ = UINT32_C(0x0001021d),
  OBELISK_RT_INTRINSIC_V1_SAMPLED_HISTORY = UINT32_C(0x0001021e),
  OBELISK_RT_INTRINSIC_V1_ASSERTION_CONTROL = UINT32_C(0x0001021f),
  OBELISK_RT_INTRINSIC_V1_ASSERTION_ENABLED = UINT32_C(0x00010220),
  OBELISK_RT_INTRINSIC_V1_ASSERTION_ACTION_STATE = UINT32_C(0x00010221),
  OBELISK_RT_INTRINSIC_V1_CLOCKED_SAMPLE_UPDATE = UINT32_C(0x00010222),
  OBELISK_RT_INTRINSIC_V1_CLOCKED_SAMPLE_READ = UINT32_C(0x00010223),
  OBELISK_RT_INTRINSIC_V1_DUMP_OPEN = UINT32_C(0x00010224),
  OBELISK_RT_INTRINSIC_V1_DUMP_OPEN_STRING = UINT32_C(0x00010225),
  OBELISK_RT_INTRINSIC_V1_DUMP_TIMESCALE = UINT32_C(0x00010226),
  OBELISK_RT_INTRINSIC_V1_DUMP_VARS = UINT32_C(0x00010227),
  OBELISK_RT_INTRINSIC_V1_DUMP_ALL = UINT32_C(0x00010228),
  OBELISK_RT_INTRINSIC_V1_DUMP_CONTROL = UINT32_C(0x00010229),
  OBELISK_RT_INTRINSIC_V1_DUMP_LIMIT = UINT32_C(0x0001022a),
  OBELISK_RT_INTRINSIC_V1_DUMP_FLUSH = UINT32_C(0x0001022b),
  OBELISK_RT_INTRINSIC_V1_IMPORT = UINT32_C(0x00010300),
  OBELISK_RT_INTRINSIC_V1_DPI_IMPORT = UINT32_C(0x00010301),
  OBELISK_RT_INTRINSIC_V1_CLASS_ALLOC = UINT32_C(0x00010400),
  OBELISK_RT_INTRINSIC_V1_CLASS_COPY = UINT32_C(0x00010401),
  OBELISK_RT_INTRINSIC_V1_CLASS_IS_INSTANCE = UINT32_C(0x00010402),
  OBELISK_RT_INTRINSIC_V1_CLASS_CAST = UINT32_C(0x00010403),
  OBELISK_RT_INTRINSIC_V1_CLASS_FIELD_REF = UINT32_C(0x00010404),
  OBELISK_RT_INTRINSIC_V1_MANAGED_LOAD = UINT32_C(0x00010405),
  OBELISK_RT_INTRINSIC_V1_MANAGED_STORE = UINT32_C(0x00010406),
  OBELISK_RT_INTRINSIC_V1_WEAK_CREATE = UINT32_C(0x00010407),
  OBELISK_RT_INTRINSIC_V1_WEAK_GET = UINT32_C(0x00010408),
  OBELISK_RT_INTRINSIC_V1_WEAK_CLEAR = UINT32_C(0x00010409),
  OBELISK_RT_INTRINSIC_V1_GC_SAFEPOINT = UINT32_C(0x0001040a),
  OBELISK_RT_INTRINSIC_V1_CLASS_ID = UINT32_C(0x0001040b),
  OBELISK_RT_INTRINSIC_V1_MANAGED_NBA = UINT32_C(0x0001040c),
  OBELISK_RT_INTRINSIC_V1_ARGUMENT_REF_FROM_REF = UINT32_C(0x0001040d),
  OBELISK_RT_INTRINSIC_V1_ARGUMENT_REF_FROM_MANAGED = UINT32_C(0x0001040e),
  OBELISK_RT_INTRINSIC_V1_ARGUMENT_REF_LOAD = UINT32_C(0x0001040f),
  OBELISK_RT_INTRINSIC_V1_ARGUMENT_REF_STORE = UINT32_C(0x00010410),
  OBELISK_RT_INTRINSIC_V1_MANAGED_ROOT_EXTRACT = UINT32_C(0x00010411),
  OBELISK_RT_INTRINSIC_V1_REFERENCE_PATH_INDEX = UINT32_C(0x00010412),
  OBELISK_RT_INTRINSIC_V1_ARGUMENT_REF_FROM_PATH = UINT32_C(0x00010413),
  OBELISK_RT_INTRINSIC_V1_STRING_LITERAL = UINT32_C(0x00010420),
  OBELISK_RT_INTRINSIC_V1_STRING_FROM_PACKED = UINT32_C(0x00010421),
  OBELISK_RT_INTRINSIC_V1_STRING_TO_PACKED = UINT32_C(0x00010422),
  OBELISK_RT_INTRINSIC_V1_STRING_CONCAT = UINT32_C(0x00010423),
  OBELISK_RT_INTRINSIC_V1_STRING_REPEAT = UINT32_C(0x00010424),
  OBELISK_RT_INTRINSIC_V1_STRING_LENGTH = UINT32_C(0x00010425),
  OBELISK_RT_INTRINSIC_V1_STRING_GETC = UINT32_C(0x00010426),
  OBELISK_RT_INTRINSIC_V1_STRING_PUTC = UINT32_C(0x00010427),
  OBELISK_RT_INTRINSIC_V1_STRING_SUBSTR = UINT32_C(0x00010428),
  OBELISK_RT_INTRINSIC_V1_STRING_COMPARE = UINT32_C(0x00010429),
  OBELISK_RT_INTRINSIC_V1_STRING_CASE_CONVERT = UINT32_C(0x0001042a),
  OBELISK_RT_INTRINSIC_V1_STRING_PARSE_INTEGER = UINT32_C(0x0001042b),
  OBELISK_RT_INTRINSIC_V1_STRING_PARSE_REAL = UINT32_C(0x0001042c),
  OBELISK_RT_INTRINSIC_V1_STRING_FORMAT_INTEGER = UINT32_C(0x0001042d),
  OBELISK_RT_INTRINSIC_V1_STRING_FORMAT_REAL = UINT32_C(0x0001042e),
  OBELISK_RT_INTRINSIC_V1_STRING_SCAN_FIELD = UINT32_C(0x0001042f),
  OBELISK_RT_INTRINSIC_V1_CONTAINER_SIZE = UINT32_C(0x00010430),
  OBELISK_RT_INTRINSIC_V1_CONTAINER_CREATE_LIKE = UINT32_C(0x00010431),
  OBELISK_RT_INTRINSIC_V1_CONTAINER_READ = UINT32_C(0x00010432),
  OBELISK_RT_INTRINSIC_V1_CONTAINER_WRITE = UINT32_C(0x00010433),
  OBELISK_RT_INTRINSIC_V1_CONTAINER_CREATE = UINT32_C(0x00010434),
  OBELISK_RT_INTRINSIC_V1_CONTAINER_CLONE = UINT32_C(0x00010435),
  OBELISK_RT_INTRINSIC_V1_CONTAINER_DELETE = UINT32_C(0x00010436),
  OBELISK_RT_INTRINSIC_V1_RANDOM_BOUNDED = UINT32_C(0x00010437),
  OBELISK_RT_INTRINSIC_V1_ASSOC_CREATE = UINT32_C(0x00010438),
  OBELISK_RT_INTRINSIC_V1_ASSOC_READ = UINT32_C(0x00010439),
  OBELISK_RT_INTRINSIC_V1_ASSOC_WRITE = UINT32_C(0x0001043a),
  OBELISK_RT_INTRINSIC_V1_ASSOC_EXISTS = UINT32_C(0x0001043b),
  OBELISK_RT_INTRINSIC_V1_ASSOC_DELETE = UINT32_C(0x0001043c),
  OBELISK_RT_INTRINSIC_V1_ASSOC_DEFAULT = UINT32_C(0x0001043d),
  OBELISK_RT_INTRINSIC_V1_ASSOC_TRAVERSE = UINT32_C(0x0001043e),
  OBELISK_RT_INTRINSIC_V1_REFERENCE_PATH_ASSOC = UINT32_C(0x0001043f),
  OBELISK_RT_INTRINSIC_V1_RANDOM_NEXT = UINT32_C(0x00010440),
  OBELISK_RT_INTRINSIC_V1_RANDOM_SEED = UINT32_C(0x00010441),
  OBELISK_RT_INTRINSIC_V1_RANDOM_GET_STATE = UINT32_C(0x00010442),
  OBELISK_RT_INTRINSIC_V1_RANDOM_SET_STATE = UINT32_C(0x00010443),
  OBELISK_RT_INTRINSIC_V1_QUEUE_DELETE = UINT32_C(0x00010444),
  OBELISK_RT_INTRINSIC_V1_QUEUE_INSERT = UINT32_C(0x00010445),
  OBELISK_RT_INTRINSIC_V1_RANDOM_DISTRIBUTION = UINT32_C(0x00010446),
  OBELISK_RT_INTRINSIC_V1_RANDOM_SOLVE = UINT32_C(0x00010447),
  OBELISK_RT_INTRINSIC_V1_RANDOM_SOLVE_STATE = UINT32_C(0x00010448),
  OBELISK_RT_INTRINSIC_V1_RANDOM_CYCLE_NEXT = UINT32_C(0x00010449),
  OBELISK_RT_INTRINSIC_V1_COVERGROUP_CREATE = UINT32_C(0x00010450),
  OBELISK_RT_INTRINSIC_V1_COVERGROUP_SET_ENABLED = UINT32_C(0x00010451),
  OBELISK_RT_INTRINSIC_V1_COVERGROUP_SAMPLE_ENABLED = UINT32_C(0x00010452),
  OBELISK_RT_INTRINSIC_V1_COVERGROUP_BIN_HIT = UINT32_C(0x00010453),
  OBELISK_RT_INTRINSIC_V1_COVERGROUP_INSTANCE_QUERY = UINT32_C(0x00010454),
  OBELISK_RT_INTRINSIC_V1_COVERGROUP_TYPE_QUERY = UINT32_C(0x00010455),
  OBELISK_RT_INTRINSIC_V1_COVERGROUP_SAMPLE = UINT32_C(0x00010456),
  OBELISK_RT_INTRINSIC_V1_RANDOM_SOLVE_WIDE_STATE = UINT32_C(0x00010457),
  OBELISK_RT_INTRINSIC_V1_VPI_ROOT = UINT32_C(0x00011000),
  OBELISK_RT_INTRINSIC_V1_VPI_CHILD = UINT32_C(0x00011001),
  OBELISK_RT_INTRINSIC_V1_VPI_SIBLING = UINT32_C(0x00011002),
  OBELISK_RT_INTRINSIC_V1_VPI_LOOKUP = UINT32_C(0x00011003),
  OBELISK_RT_INTRINSIC_V1_VPI_INFO = UINT32_C(0x00011004),
  OBELISK_RT_INTRINSIC_V1_VPI_READ = UINT32_C(0x00011005),
  OBELISK_RT_INTRINSIC_V1_VPI_WRITE = UINT32_C(0x00011006),
  OBELISK_RT_INTRINSIC_V1_VPI_CHILD_AT = UINT32_C(0x00011007),
  OBELISK_RT_INTRINSIC_V1_VPI_NAME = UINT32_C(0x00011008),
  OBELISK_RT_INTRINSIC_V1_VPI_TYPE_INFO = UINT32_C(0x00011009),
  OBELISK_RT_INTRINSIC_V1_VPI_TYPE_CHILD = UINT32_C(0x0001100a)
};

// Imported zero-time calls are resolved through mutable context bindings. The
// bytecode image contains only a deterministic 32-bit symbol ID and typed
// register metadata; it never contains a host function or data pointer.
// Generation one imports accept arbitrary-width bits/logic, status values,
// and opaque four-word stable/reference handles. A callback may copy a handle
// but must not synthesize its representation. The callback may inspect inputs
// and fill the zero-initialized output planes.
typedef struct obelisk_rt_import_input_v1 {
  obelisk_rt_design_register_kind kind;
  uint8_t flags;
  uint16_t reserved;
  uint32_t bit_width;
  const uint64_t *value;
  const uint64_t *unknown;
  uint64_t limb_count;
} obelisk_rt_import_input_v1;

typedef struct obelisk_rt_import_output_v1 {
  obelisk_rt_design_register_kind kind;
  uint8_t flags;
  uint16_t reserved;
  uint32_t bit_width;
  uint64_t *value;
  uint64_t *unknown;
  uint64_t limb_count;
} obelisk_rt_import_output_v1;

typedef obelisk_rt_status (*obelisk_rt_import_callback_v1)(
    obelisk_rt_context *context, uint32_t import_id,
    const obelisk_rt_import_input_v1 *inputs, uint32_t input_count,
    obelisk_rt_import_output_v1 *outputs, uint32_t output_count,
    void *user_data);

// A call site contains only stable metadata. Generated native code may point
// source_file at an immutable string; bytecode materializes the same record
// from its pointer-free string and call-site sections before entering the
// runtime boundary.
#define OBELISK_RT_IMPORT_PURE (UINT32_C(1) << 0)
#define OBELISK_RT_IMPORT_CONTEXT (UINT32_C(1) << 1)
#define OBELISK_RT_IMPORT_TASK (UINT32_C(1) << 2)

typedef struct obelisk_rt_import_site_v1 {
  uint32_t version;
  uint32_t flags;
  uint32_t import_id;
  uint32_t reserved;
  uint64_t scope_id;
  const char *source_file;
  uint64_t source_file_size;
  uint32_t source_line;
  uint32_t source_column;
  uint64_t abi_signature;
} obelisk_rt_import_site_v1;

// VPI bytecode intrinsics use known two-state i64 section offsets as cursors
// and a trailing `status` output. Traversal failures (including EOF) are
// written to that output instead of aborting the interpreter. INFO outputs
// kind, capabilities, stable ID, type cursor, left, right, width, status.
// NAME outputs string-section offset, byte count, status. TYPE_INFO outputs
// the fields of obelisk_rt_design_type_info_v1 in declaration order followed
// by status. READ and WRITE infer the value width from their numeric register.

// The design database is a DWARF-like byte image. All cursors and links are
// section-relative offsets, never native pointers.
#define OBELISK_RT_DESIGN_PROFILE_READ (UINT32_C(1) << 0)
#define OBELISK_RT_DESIGN_PROFILE_WRITE (UINT32_C(1) << 1)

typedef uint32_t obelisk_rt_design_record_kind;
enum {
  OBELISK_RT_DESIGN_RECORD_INVALID = 0,
  OBELISK_RT_DESIGN_RECORD_SCOPE = 1,
  OBELISK_RT_DESIGN_RECORD_STORAGE = 2,
  OBELISK_RT_DESIGN_RECORD_NET = 3,
  OBELISK_RT_DESIGN_RECORD_DRIVER = 4,
  OBELISK_RT_DESIGN_RECORD_PROCESS = 5,
  OBELISK_RT_DESIGN_RECORD_TYPE = 6,
  OBELISK_RT_DESIGN_RECORD_FUNCTION = 7
};

typedef uint32_t obelisk_rt_design_capability;
enum {
  OBELISK_RT_DESIGN_CAP_READ = UINT32_C(1) << 0,
  OBELISK_RT_DESIGN_CAP_WRITE = UINT32_C(1) << 1,
  OBELISK_RT_DESIGN_CAP_ITERATE = UINT32_C(1) << 2
};

typedef struct obelisk_rt_design_cursor_v1 {
  uint64_t offset;
} obelisk_rt_design_cursor_v1;

typedef struct obelisk_rt_design_info_v1 {
  obelisk_rt_design_record_kind kind;
  obelisk_rt_design_capability capabilities;
  obelisk_rt_handle_v1 handle;
  uint64_t type_offset;
  int64_t range_left;
  int64_t range_right;
  uint64_t bit_width;
} obelisk_rt_design_info_v1;

typedef uint32_t obelisk_rt_design_type_kind;
enum {
  OBELISK_RT_DESIGN_TYPE_SCALAR = 1,
  OBELISK_RT_DESIGN_TYPE_ARRAY = 2,
  OBELISK_RT_DESIGN_TYPE_STRUCT = 3,
  OBELISK_RT_DESIGN_TYPE_UNION = 4,
  OBELISK_RT_DESIGN_TYPE_FIELD = 5
};

typedef uint32_t obelisk_rt_design_type_flags;
enum {
  OBELISK_RT_DESIGN_TYPE_FOUR_STATE = UINT32_C(1) << 0,
  OBELISK_RT_DESIGN_TYPE_SIGNED = UINT32_C(1) << 1,
  OBELISK_RT_DESIGN_TYPE_PACKED = UINT32_C(1) << 2,
  OBELISK_RT_DESIGN_TYPE_TAGGED = UINT32_C(1) << 3
};

typedef struct obelisk_rt_design_type_info_v1 {
  obelisk_rt_design_type_kind kind;
  obelisk_rt_design_type_flags flags;
  uint64_t bit_width;
  int64_t range_left;
  int64_t range_right;
  obelisk_rt_design_cursor_v1 element_type;
  obelisk_rt_design_cursor_v1 first_child;
  uint64_t child_count;
  uint64_t ordinal;
  uint64_t tag_bits;
  uint64_t packed_offset;
} obelisk_rt_design_type_info_v1;

// Every code form returns through this action ABI. A continuation identifies
// the next fixed fragment state. Suspend payloads are interpreted according to
// suspend_kind (for example, a deadline or stable event descriptor ID).
typedef uint32_t obelisk_rt_fragment_action_kind;
enum {
  OBELISK_RT_FRAGMENT_CONTINUE = 0,
  OBELISK_RT_FRAGMENT_SUSPEND = 1,
  OBELISK_RT_FRAGMENT_TERMINATE = 2,
  OBELISK_RT_FRAGMENT_TASK_CALL = 3
};

typedef uint32_t obelisk_rt_suspend_kind;
enum {
  OBELISK_RT_SUSPEND_NONE = 0,
  OBELISK_RT_SUSPEND_DELAY = 1,
  OBELISK_RT_SUSPEND_CHANGE = 2,
  OBELISK_RT_SUSPEND_EDGE = 3,
  OBELISK_RT_SUSPEND_EVENT = 4,
  OBELISK_RT_SUSPEND_AWAIT = 5,
  OBELISK_RT_SUSPEND_JOIN = 6,
  OBELISK_RT_SUSPEND_FOREVER = 7,
  OBELISK_RT_SUSPEND_FRONTIER = 8,
  OBELISK_RT_SUSPEND_CHILDREN = 9,
  OBELISK_RT_SUSPEND_OBSERVER = 10
};

typedef struct obelisk_rt_fragment_action_v1 {
  obelisk_rt_fragment_action_kind kind;
  obelisk_rt_suspend_kind suspend_kind;
  uint32_t continuation;
  uint32_t flags;
  uint64_t payload;
  uint64_t auxiliary;
} obelisk_rt_fragment_action_v1;

// Legacy fragments use no action flags. Process actions use FRAME_WAIT_RECORD
// to identify payload/auxiliary as a checked frame-relative offset and size.
#define OBELISK_RT_FRAGMENT_FLAGS_NONE UINT32_C(0)
#define OBELISK_RT_ACTION_FRAME_WAIT_RECORD (UINT32_C(1) << 0)
#define OBELISK_RT_ACTION_RESUME_REGION_VALID (UINT32_C(1) << 1)
#define OBELISK_RT_ACTION_RESUME_REGION_SHIFT 2u
#define OBELISK_RT_ACTION_RESUME_REGION_MASK                                   \
  (UINT32_C(7) << OBELISK_RT_ACTION_RESUME_REGION_SHIFT)
#define OBELISK_RT_ACTION_RESUME_REGION(region)                                \
  (OBELISK_RT_ACTION_RESUME_REGION_VALID |                                     \
   ((uint32_t)(region) << OBELISK_RT_ACTION_RESUME_REGION_SHIFT))

typedef obelisk_rt_status (*obelisk_rt_native_fragment_v1)(
    obelisk_rt_context *context, void *frame, uint64_t frame_size,
    uint32_t continuation, obelisk_rt_fragment_action_v1 *out_action);

// Bytecode instructions are exactly 16 bytes and little endian:
//   opcode:u8, type:u8, dst:u16, src0:u16, src1:u16, immediate:u64.
// Branch immediates are absolute instruction indices. Frame offsets are byte
// offsets and are checked against frame_size before every access. Stable
// scheduler continuation IDs are mapped to entry instructions by the immutable
// descriptor table below; they are never interpreted directly as bytecode PCs.
#define OBELISK_RT_BYTECODE_INSTRUCTION_SIZE 16u
// Typed registers live in the process frame rather than being allocated on
// each resume. register_offset marks the end of ordinary frame data and the
// start of register_count fixed-size scratch slots.
#define OBELISK_RT_BYTECODE_REGISTER_SIZE 16u

typedef uint8_t obelisk_rt_bytecode_type;
enum {
  OBELISK_RT_BC_TYPE_NONE = 0,
  OBELISK_RT_BC_TYPE_U64 = 1,
  OBELISK_RT_BC_TYPE_I64 = 2,
  OBELISK_RT_BC_TYPE_BOOL = 3,
  OBELISK_RT_BC_TYPE_STATUS = 4,
  // Resource values are interpreter-local IDs produced only by validated
  // service calls; they are never native addresses.
  OBELISK_RT_BC_TYPE_RESOURCE = 5
};

typedef uint8_t obelisk_rt_bytecode_opcode;
enum {
  OBELISK_RT_BC_NOP = 0,
  OBELISK_RT_BC_CONST = 1,
  OBELISK_RT_BC_MOVE = 2,
  OBELISK_RT_BC_ADD = 3,
  OBELISK_RT_BC_SUB = 4,
  OBELISK_RT_BC_MUL = 5,
  OBELISK_RT_BC_AND = 6,
  OBELISK_RT_BC_OR = 7,
  OBELISK_RT_BC_XOR = 8,
  OBELISK_RT_BC_NOT = 9,
  OBELISK_RT_BC_EQ = 10,
  OBELISK_RT_BC_ULT = 11,
  OBELISK_RT_BC_SLT = 12,
  OBELISK_RT_BC_LOAD_FRAME = 13,
  OBELISK_RT_BC_STORE_FRAME = 14,
  OBELISK_RT_BC_JUMP = 15,
  OBELISK_RT_BC_BRANCH_ZERO = 16,
  OBELISK_RT_BC_CONTINUE = 17,
  OBELISK_RT_BC_SUSPEND = 18,
  OBELISK_RT_BC_TERMINATE = 19,
  // CALL_SERVICE's immediate is a service-site index and its destination is a
  // STATUS register. FAIL returns the nonzero status held in source0 through
  // the fragment ABI.
  OBELISK_RT_BC_CALL_SERVICE = 20,
  OBELISK_RT_BC_FAIL = 21
};

typedef struct obelisk_rt_bytecode_v1 {
  const uint8_t *code;
  uint64_t code_size;
  const struct obelisk_rt_bytecode_entry_v1 *entries;
  uint32_t entry_count;
  uint32_t register_count;
  uint64_t register_offset;
  // Generated immutable descriptors may point at a dedicated writable
  // validation record. The runtime always validates caller-controlled tables;
  // this record only reports the most recent result. Null omits that report.
  struct obelisk_rt_bytecode_validation_v1 *validation;
  const uint8_t *constants;
  uint64_t constant_size;
  const struct obelisk_rt_bytecode_service_site_v1 *service_sites;
  uint32_t service_site_count;
  uint32_t reserved;
  const struct obelisk_rt_bytecode_operand_v1 *operands;
  uint64_t operand_count;
} obelisk_rt_bytecode_v1;

typedef struct obelisk_rt_bytecode_entry_v1 {
  uint32_t continuation;
  uint32_t instruction;
} obelisk_rt_bytecode_entry_v1;

typedef struct obelisk_rt_bytecode_validation_v1 {
  // Runtime-written observation. Incoming state is never trusted. Initialize
  // the whole record to zero; reserved must remain zero and the associated
  // immutable descriptor must not be mutated during dispatch.
  uint32_t state;
  uint32_t reserved;
} obelisk_rt_bytecode_validation_v1;

enum {
  OBELISK_RT_BC_VALIDATION_UNVALIDATED = 0,
  OBELISK_RT_BC_VALIDATION_VALID = 2,
  OBELISK_RT_BC_VALIDATION_INVALID = 3
};

// Service-aware bytecode adds a constant pool plus immutable, fully validated
// metadata to the v1 program. No service operand contains a native address.
// All offsets and ranges are checked before a service is invoked.
typedef uint8_t obelisk_rt_bytecode_operand_kind;
enum {
  OBELISK_RT_BC_OPERAND_IMMEDIATE = 0,
  OBELISK_RT_BC_OPERAND_REGISTER = 1,
  OBELISK_RT_BC_OPERAND_FRAME = 2,
  OBELISK_RT_BC_OPERAND_CONSTANT = 3,
  OBELISK_RT_BC_OPERAND_RESOURCE = 4
};

typedef uint8_t obelisk_rt_bytecode_operand_direction;
enum {
  OBELISK_RT_BC_OPERAND_INPUT = 0,
  OBELISK_RT_BC_OPERAND_OUTPUT = 1,
  OBELISK_RT_BC_OPERAND_INOUT = 2
};

typedef uint8_t obelisk_rt_bytecode_value_kind;
enum {
  OBELISK_RT_BC_VALUE_NONE = 0,
  OBELISK_RT_BC_VALUE_U8 = 1,
  OBELISK_RT_BC_VALUE_U32 = 2,
  OBELISK_RT_BC_VALUE_I32 = 3,
  OBELISK_RT_BC_VALUE_U64 = 4,
  OBELISK_RT_BC_VALUE_I64 = 5,
  OBELISK_RT_BC_VALUE_BYTES = 6,
  OBELISK_RT_BC_VALUE_MUTABLE_BYTES = 7,
  OBELISK_RT_BC_VALUE_ARGUMENT_ARRAY = 8,
  OBELISK_RT_BC_VALUE_FORMAT_ENVIRONMENT = 9,
  OBELISK_RT_BC_VALUE_BUFFER = 10,
  // The following kinds are valid only as children of ARGUMENT_ARRAY.
  OBELISK_RT_BC_VALUE_ARGUMENT_EMPTY = 11,
  OBELISK_RT_BC_VALUE_ARGUMENT_LOGIC = 12,
  OBELISK_RT_BC_VALUE_ARGUMENT_STRING = 13,
  OBELISK_RT_BC_VALUE_ARGUMENT_REAL = 14,
  OBELISK_RT_BC_VALUE_ARGUMENT_TIME = 15
};

typedef uint32_t obelisk_rt_bytecode_service;
enum {
  OBELISK_RT_BC_SERVICE_FORMAT = 1,
  OBELISK_RT_BC_SERVICE_DISPLAY = 2,
  OBELISK_RT_BC_SERVICE_BUFFER_RELEASE = 3,
  OBELISK_RT_BC_SERVICE_FILE_OPEN_MCD = 10,
  OBELISK_RT_BC_SERVICE_FILE_OPEN = 11,
  OBELISK_RT_BC_SERVICE_FILE_CLOSE = 12,
  OBELISK_RT_BC_SERVICE_FILE_FLUSH = 13,
  OBELISK_RT_BC_SERVICE_FILE_WRITE = 14,
  OBELISK_RT_BC_SERVICE_FILE_READ = 15,
  OBELISK_RT_BC_SERVICE_FILE_GETC = 16,
  OBELISK_RT_BC_SERVICE_FILE_UNGETC = 17,
  OBELISK_RT_BC_SERVICE_FILE_GETLINE = 18,
  OBELISK_RT_BC_SERVICE_FILE_EOF = 19,
  OBELISK_RT_BC_SERVICE_FILE_ERROR = 20,
  OBELISK_RT_BC_SERVICE_FILE_SEEK = 21,
  OBELISK_RT_BC_SERVICE_FILE_TELL = 22,
  OBELISK_RT_BC_SERVICE_FILE_REWIND = 23
};

// value is an immediate, register index, byte offset, or first child operand
// according to kind/value_kind. size is a byte/bit count or child count.
// A nonempty FORMAT_ENVIRONMENT has five children: scope bytes, library.cell
// bytes, U32 time width, suffix bytes, and a nonzero U64 time multiplier.
// auxiliary is used only by ARGUMENT_LOGIC: UINT64_MAX means a known value;
// otherwise it is the checked byte offset of the unknown plane in the same
// frame or constant pool as the value plane. flags use OBELISK_RT_ARG_* for
// argument children and must otherwise be zero. MUTABLE_BYTES is INOUT. A
// writable frame range in one service site must not overlap any other operand's
// frame range in that site.
typedef struct obelisk_rt_bytecode_operand_v1 {
  obelisk_rt_bytecode_operand_kind kind;
  obelisk_rt_bytecode_operand_direction direction;
  obelisk_rt_bytecode_value_kind value_kind;
  uint8_t flags;
  uint32_t reserved;
  uint64_t value;
  uint64_t size;
  uint64_t auxiliary;
} obelisk_rt_bytecode_operand_v1;

typedef struct obelisk_rt_bytecode_service_site_v1 {
  obelisk_rt_bytecode_service service;
  uint32_t first_operand;
  uint16_t operand_count;
  uint16_t flags;
  uint32_t reserved;
} obelisk_rt_bytecode_service_site_v1;

// Canonical process frames are shared by every executable tier. Layout fields
// are sorted by offset and describe all compiler-owned ranges, including both
// adjacent planes of a four-state value and scheduler wait records. Native
// addresses are forbidden in the canonical frame.
typedef uint32_t obelisk_rt_frame_field_kind;
enum {
  OBELISK_RT_FRAME_CAPTURE = 1,
  OBELISK_RT_FRAME_CONTINUATION = 2,
  OBELISK_RT_FRAME_LIVE = 3,
  OBELISK_RT_FRAME_WAIT = 4
};

typedef uint32_t obelisk_rt_frame_field_flags;
enum {
  OBELISK_RT_FRAME_FIELD_FLAGS_NONE = 0,
  OBELISK_RT_FRAME_FOUR_STATE_VALUE = 1u << 0,
  OBELISK_RT_FRAME_FOUR_STATE_UNKNOWN = 1u << 1,
  // The field is an aligned object-pointer slot traced for the lifetime of
  // its process activation, including while that activation is suspended.
  OBELISK_RT_FRAME_MANAGED_ROOT = 1u << 2
};

typedef struct obelisk_rt_frame_field_v1 {
  obelisk_rt_frame_field_kind kind;
  obelisk_rt_frame_field_flags flags;
  uint64_t offset;
  uint64_t size;
  uint32_t alignment;
  uint32_t reserved;
} obelisk_rt_frame_field_v1;

typedef struct obelisk_rt_frame_layout_v1 {
  uint32_t version;
  uint32_t flags;
  uint64_t frame_size;
  uint64_t frame_alignment;
  const obelisk_rt_frame_field_v1 *fields;
  uint32_t field_count;
  uint32_t continuation_count;
  const uint32_t *continuations;
  uint64_t checksum;
} obelisk_rt_frame_layout_v1;

// A wait record is stored entirely inside the canonical frame. The action
// payload is its frame-relative offset and action auxiliary is its byte size.
// One entry follows the header for every watched stable handle. Signal waits
// preserve a requested edge per entry; other wait families use EDGE_NONE.
typedef uint32_t obelisk_rt_wait_flags;
enum {
  OBELISK_RT_WAIT_FLAGS_NONE = 0,
  // A level wait has one CHANGE entry and latches an occurrence whenever the
  // complete watched value is true immediately after an overlapping store.
  OBELISK_RT_WAIT_LEVEL_TRUE = UINT32_C(1) << 0,
  // An iff wait has one primary edge entry followed by an EDGE_NONE condition
  // entry. The condition is sampled only when the primary event occurs.
  OBELISK_RT_WAIT_EDGE_IFF = UINT32_C(1) << 1,
  // A direct signal wait ignores publications from the currently executing
  // logical process. This models an always @* wait that is inactive while its
  // controlled statement evaluates.
  OBELISK_RT_WAIT_SUPPRESS_ACTIVE_SELF = UINT32_C(1) << 2
};
typedef uint32_t obelisk_rt_wait_edge_kind;
enum {
  OBELISK_RT_WAIT_EDGE_CHANGE = 0,
  OBELISK_RT_WAIT_EDGE_POSEDGE = 1,
  OBELISK_RT_WAIT_EDGE_NEGEDGE = 2,
  OBELISK_RT_WAIT_EDGE_BOTH = 3,
  OBELISK_RT_WAIT_EDGE_NONE = UINT32_MAX
};

typedef struct obelisk_rt_wait_record_v1 {
  uint32_t version;
  obelisk_rt_suspend_kind kind;
  uint32_t flags;
  uint32_t count;
  uint64_t payload;
  uint64_t auxiliary;
} obelisk_rt_wait_record_v1;

typedef struct obelisk_rt_wait_entry_v1 {
  uint64_t stable_id;
  obelisk_rt_wait_edge_kind edge;
  uint32_t reserved;
} obelisk_rt_wait_entry_v1;

#define OBELISK_RT_COMPUTED_WAIT_FLAGS_NONE 0u
#define OBELISK_RT_COMPUTED_WAIT_INTERLEAVED (UINT32_C(1) << 0)
#define OBELISK_RT_OBSERVER_CONDITION_NONE UINT32_MAX
#define OBELISK_RT_COMPUTED_CLAUSE_EVENT_PRIMARY (UINT32_C(1) << 0)
#define OBELISK_RT_COMPUTED_CLAUSE_LEVEL_TRUE (UINT32_C(1) << 1)

typedef struct obelisk_rt_computed_wait_record_v1 {
  uint32_t version;
  obelisk_rt_suspend_kind kind;
  uint32_t flags;
  uint32_t clause_count;
  uint32_t observer_count;
  uint32_t capture_count;
  uint32_t dependency_count;
  uint32_t previous_limb_count;
  uint64_t observers_offset;
  uint64_t captures_offset;
  uint64_t dependencies_offset;
  uint64_t clauses_offset;
  uint64_t previous_value_offset;
  uint64_t previous_unknown_offset;
  uint64_t total_size;
  uint64_t reserved;
} obelisk_rt_computed_wait_record_v1;

typedef struct obelisk_rt_computed_observer_v1 {
  uint64_t code_unit_id;
  uint32_t capture_begin;
  uint32_t capture_count;
  uint32_t dependency_begin;
  uint32_t dependency_count;
  uint32_t previous_offset;
  uint32_t reserved;
} obelisk_rt_computed_observer_v1;

typedef struct obelisk_rt_computed_capture_v1 {
  uint64_t stable_id;
  uint64_t payload0;
  uint64_t payload1;
  uint64_t payload2;
} obelisk_rt_computed_capture_v1;

typedef uint32_t obelisk_rt_observer_dependency_kind;
enum {
  OBELISK_RT_OBSERVER_DEPENDENCY_SIGNAL = 1,
  OBELISK_RT_OBSERVER_DEPENDENCY_EVENT = 2
};

typedef struct obelisk_rt_computed_dependency_v1 {
  uint64_t stable_id;
  obelisk_rt_observer_dependency_kind kind;
  uint32_t width;
} obelisk_rt_computed_dependency_v1;

typedef struct obelisk_rt_computed_clause_v1 {
  uint32_t primary_observer;
  uint32_t condition_observer;
  obelisk_rt_wait_edge_kind edge;
  uint32_t flags;
} obelisk_rt_computed_clause_v1;

typedef struct obelisk_rt_process_instance_v1 obelisk_rt_process_instance_v1;

typedef obelisk_rt_status (*obelisk_rt_native_requirements_v1)(
    uint64_t *out_size, uint64_t *out_alignment);
typedef obelisk_rt_status (*obelisk_rt_native_process_execute_v1)(
    obelisk_rt_process_instance_v1 *instance);
typedef void (*obelisk_rt_native_process_destroy_v1)(
    obelisk_rt_process_instance_v1 *instance);

typedef uint32_t obelisk_rt_execution_tier;
enum { OBELISK_RT_TIER_NATIVE = 1, OBELISK_RT_TIER_BYTECODE = 2 };
#define OBELISK_RT_TIER_MASK_NATIVE (UINT32_C(1) << 0)
#define OBELISK_RT_TIER_MASK_BYTECODE (UINT32_C(1) << 1)

typedef uint32_t obelisk_rt_process_lifecycle;
enum {
  OBELISK_RT_PROCESS_READY = 0,
  OBELISK_RT_PROCESS_EXECUTING = 1,
  OBELISK_RT_PROCESS_SUSPENDED = 2,
  OBELISK_RT_PROCESS_TERMINATED = 3
};

typedef struct obelisk_rt_process_descriptor_v1 {
  obelisk_rt_handle_v1 handle;
  uint32_t version;
  uint32_t flags;
  uint32_t available_tiers;
  uint32_t reserved;
  const obelisk_rt_frame_layout_v1 *frame_layout;
  obelisk_rt_native_requirements_v1 native_requirements;
  obelisk_rt_native_process_execute_v1 native_execute;
  obelisk_rt_native_process_destroy_v1 native_destroy;
  const obelisk_rt_bytecode_v1 *bytecode;
  const obelisk_rt_execution_descriptor_v1 *execution;
  const obelisk_rt_design_bytecode_entry_v1 *design_bytecode;
} obelisk_rt_process_descriptor_v1;

// The runtime owns this record and its allocation. Generated native hooks may
// read the fixed fields in order to refresh transient pointers before every
// resume; other clients must treat the contents as read-only.
struct obelisk_rt_process_instance_v1 {
  const obelisk_rt_process_descriptor_v1 *descriptor;
  void *allocation;
  void *frame;
  uint64_t frame_size;
  uint64_t scratch_offset;
  uint64_t scratch_size;
  void *native_handle;
  uint32_t continuation;
  obelisk_rt_execution_tier tier;
  obelisk_rt_process_lifecycle lifecycle;
  obelisk_rt_status status;
  obelisk_rt_context *context;
  obelisk_rt_fragment_action_v1 *action;
  // Persistent owner for automatic allocations; unlike context above this is
  // retained between fragments until termination or explicit destruction.
  obelisk_rt_context *ownership_context;
  // Observer callbacks pin a suspended activation while executing its
  // evaluator. Cancellation marks destruction pending and the final callback
  // release performs the actual frame/automatic-state cleanup.
  uint32_t observer_pin_count;
  uint32_t observer_destroy_pending;
};

// Create exactly one allocation containing [instance][runtime-private
// metadata][padding][canonical frame][padding][shared scratch tail]. The tail
// is the maximum of native coroutine storage and bytecode registers and is
// reused without copying when tiers change. Released allocations may be
// recycled by the runtime's bounded process-frame pool.
obelisk_rt_status obelisk_rt_v1_process_instance_create(
    const obelisk_rt_process_descriptor_v1 *descriptor,
    obelisk_rt_process_instance_v1 **out_instance);
obelisk_rt_status
obelisk_rt_v1_process_instance_frame(obelisk_rt_process_instance_v1 *instance,
                                     void **out_frame, uint64_t *out_size);
obelisk_rt_status obelisk_rt_v1_process_instance_execute(
    obelisk_rt_process_instance_v1 *instance, obelisk_rt_context *context,
    obelisk_rt_execution_tier requested_tier,
    obelisk_rt_fragment_action_v1 *out_action);
obelisk_rt_status obelisk_rt_v1_process_instance_destroy(
    obelisk_rt_process_instance_v1 *instance);

// Construct a context bound to a generated design. The legacy constructor is
// equivalent to passing a null execution descriptor and remains useful for
// formatting/file-only clients.
obelisk_rt_status obelisk_rt_v1_context_create_for_design(
    const obelisk_rt_execution_descriptor_v1 *execution,
    obelisk_rt_context **out_context);

// Register a generated worker lane with the managed heap. A lane is inactive
// after creation; generated code brackets executable fragments with enter and
// leave so a collector never waits for an idle host thread. Active lanes must
// reach allocation or explicit safepoint calls.
obelisk_rt_status
obelisk_rt_v1_gc_lane_create(obelisk_rt_context *context,
                             obelisk_rt_gc_lane_v1 **out_lane);
obelisk_rt_status obelisk_rt_v1_gc_lane_destroy(obelisk_rt_gc_lane_v1 *lane);
obelisk_rt_status obelisk_rt_v1_gc_lane_enter(obelisk_rt_gc_lane_v1 *lane);
obelisk_rt_status obelisk_rt_v1_gc_lane_leave(obelisk_rt_gc_lane_v1 *lane);
obelisk_rt_status obelisk_rt_v1_gc_safepoint(obelisk_rt_gc_lane_v1 *lane);
// Return the active managed lane for the calling generated worker. Execution
// entry points establish this binding once per fragment/process activation;
// class intrinsics do not allocate or register a lane per operation.
obelisk_rt_gc_lane_v1 *
obelisk_rt_v1_gc_current_lane(obelisk_rt_context *context);

// Root records are caller-owned and allocation-free. Static roots are
// registered infrequently and remain live until explicitly unregistered.
obelisk_rt_status obelisk_rt_v1_gc_root_push(obelisk_rt_gc_lane_v1 *lane,
                                             obelisk_rt_gc_root_v1 *root,
                                             obelisk_rt_object_v1 **slot);
obelisk_rt_status obelisk_rt_v1_gc_root_pop(obelisk_rt_gc_lane_v1 *lane,
                                            obelisk_rt_gc_root_v1 *root);
obelisk_rt_status
obelisk_rt_v1_gc_root_range_push(obelisk_rt_gc_lane_v1 *lane,
                                 obelisk_rt_gc_root_range_v1 *range,
                                 obelisk_rt_object_v1 **slots, uint64_t count);
obelisk_rt_status
obelisk_rt_v1_gc_root_range_pop(obelisk_rt_gc_lane_v1 *lane,
                                obelisk_rt_gc_root_range_v1 *range);
obelisk_rt_status
obelisk_rt_v1_gc_managed_root_push(obelisk_rt_gc_lane_v1 *lane,
                                   obelisk_rt_gc_managed_root_v1 *root,
                                   obelisk_rt_managed_word_v1 *slot);
obelisk_rt_status
obelisk_rt_v1_gc_managed_root_pop(obelisk_rt_gc_lane_v1 *lane,
                                  obelisk_rt_gc_managed_root_v1 *root);
obelisk_rt_status obelisk_rt_v1_gc_managed_root_range_push(
    obelisk_rt_gc_lane_v1 *lane, obelisk_rt_gc_managed_root_range_v1 *range,
    obelisk_rt_managed_word_v1 *slots, uint64_t count);
obelisk_rt_status obelisk_rt_v1_gc_managed_root_range_pop(
    obelisk_rt_gc_lane_v1 *lane, obelisk_rt_gc_managed_root_range_v1 *range);
obelisk_rt_status
obelisk_rt_v1_gc_static_root_register(obelisk_rt_context *context,
                                      obelisk_rt_object_v1 **slot);
obelisk_rt_status
obelisk_rt_v1_gc_static_root_unregister(obelisk_rt_context *context,
                                        obelisk_rt_object_v1 **slot);

// External integrations that cannot expose a root slot may pin an object.
// Pins are exact roots and must be balanced before context destruction.
obelisk_rt_status obelisk_rt_v1_gc_pin(obelisk_rt_context *context,
                                       obelisk_rt_object_v1 *object);
obelisk_rt_status obelisk_rt_v1_gc_unpin(obelisk_rt_context *context,
                                         obelisk_rt_object_v1 *object);

// Request a stop-the-world collection. The requesting lane participates in
// root publication; every other active lane parks at its next safepoint.
obelisk_rt_status obelisk_rt_v1_gc_collect(obelisk_rt_gc_lane_v1 *lane);
obelisk_rt_status obelisk_rt_v1_gc_set_threshold(obelisk_rt_context *context,
                                                 uint64_t allocation_bytes);
obelisk_rt_status
obelisk_rt_v1_gc_statistics(obelisk_rt_context *context,
                            obelisk_rt_gc_statistics_v1 *out_statistics);

// Validate immutable compiler-emitted class metadata without allocating.
obelisk_rt_status
obelisk_rt_v1_class_validate(const obelisk_rt_class_descriptor_v1 *descriptor);
// Register immutable generated metadata with one context. The registry is
// keyed by stable class ID so pointer-free bytecode can resolve descriptors.
obelisk_rt_status
obelisk_rt_v1_class_register(obelisk_rt_context *context,
                             const obelisk_rt_class_descriptor_v1 *descriptor);

// Validate and register immutable compiler-emitted element metadata. Repeated
// registration of an equivalent descriptor is accepted; conflicting use of a
// stable type ID is rejected.
obelisk_rt_status obelisk_rt_v1_element_type_validate(
    const obelisk_rt_element_type_v1 *descriptor);
obelisk_rt_status obelisk_rt_v1_element_type_register(
    obelisk_rt_context *context, const obelisk_rt_element_type_v1 *descriptor);

// Register one 64-bit-aligned class-handle slot in the pointer-free design
// state. This is used by the bytecode tier; the backing state allocation is
// fixed for the context lifetime.
obelisk_rt_status
obelisk_rt_v1_gc_design_root_register(obelisk_rt_context *context,
                                      uint64_t bit_offset);

// Allocate zero-initialized class storage. Abstract and interface descriptors
// cannot be allocated. The descriptor word is installed before the object is
// returned; constructors and property initializers are generated code.
obelisk_rt_status
obelisk_rt_v1_object_allocate(obelisk_rt_gc_lane_v1 *lane,
                              const obelisk_rt_class_descriptor_v1 *descriptor,
                              obelisk_rt_object_v1 **out_object);
// Validate the source against its static handle type, then allocate and copy
// its complete dynamic class, preserving virtual dispatch and derived fields.
obelisk_rt_status obelisk_rt_v1_object_shallow_copy(
    obelisk_rt_gc_lane_v1 *lane,
    const obelisk_rt_class_descriptor_v1 *static_descriptor,
    obelisk_rt_object_v1 *source, obelisk_rt_object_v1 **out_object);

// Locked field access supplies a race-free baseline for shared class objects.
// Offsets are relative to the descriptor word at object byte zero.
obelisk_rt_status obelisk_rt_v1_object_read(obelisk_rt_object_v1 *object,
                                            uint64_t offset, void *data,
                                            uint64_t size);
obelisk_rt_status obelisk_rt_v1_object_write(obelisk_rt_object_v1 *object,
                                             uint64_t offset, const void *data,
                                             uint64_t size);
// Atomically access adjacent value/unknown field planes under one object lock.
obelisk_rt_status obelisk_rt_v1_object_read_planes(obelisk_rt_object_v1 *object,
                                                   uint64_t offset, void *value,
                                                   void *unknown,
                                                   uint64_t plane_size);
obelisk_rt_status
obelisk_rt_v1_object_write_planes(obelisk_rt_object_v1 *object, uint64_t offset,
                                  const void *value, const void *unknown,
                                  uint64_t plane_size);

// Load/store through the erased representation of a language `ref` formal.
// `managed` zero selects a stable ordinary-state handle in payload, one
// selects a class owner and byte offset, and two selects a ReferencePath in
// owner (payload is unused). Generated code supplies its native state planes
// so the same ABI serves native and bytecode callees.
typedef uint32_t obelisk_rt_argument_value_kind_v1;
enum {
  OBELISK_RT_ARGUMENT_VALUE_BITS = 0,
  OBELISK_RT_ARGUMENT_VALUE_CLASS = 1,
  OBELISK_RT_ARGUMENT_VALUE_STRING = 2
};
obelisk_rt_status obelisk_rt_v1_argument_ref_load(
    obelisk_rt_context *context, const uint8_t *state_value,
    const uint8_t *state_unknown, uint64_t state_bit_count,
    obelisk_rt_object_v1 *owner, uint64_t payload, uint32_t managed,
    uint64_t bit_width, uint64_t plane_size, uint32_t four_state,
    uint32_t value_kind, void *out_value, void *out_unknown);
obelisk_rt_status obelisk_rt_v1_argument_ref_store(
    obelisk_rt_context *context, uint8_t *state_value, uint8_t *state_unknown,
    uint64_t state_bit_count, obelisk_rt_object_v1 *owner, uint64_t payload,
    uint32_t managed, uint64_t bit_width, uint64_t plane_size,
    uint32_t four_state, uint32_t value_kind, const void *value,
    const void *unknown);
obelisk_rt_status
obelisk_rt_v1_object_field_load(obelisk_rt_object_v1 *object, uint64_t offset,
                                obelisk_rt_object_v1 **out_value);
obelisk_rt_status obelisk_rt_v1_object_field_store(obelisk_rt_object_v1 *object,
                                                   uint64_t offset,
                                                   obelisk_rt_object_v1 *value);

uint32_t
obelisk_rt_v1_object_is_instance(const obelisk_rt_object_v1 *object,
                                 const obelisk_rt_class_descriptor_v1 *target);
obelisk_rt_status
obelisk_rt_v1_object_cast(obelisk_rt_object_v1 *object,
                          const obelisk_rt_class_descriptor_v1 *target,
                          obelisk_rt_object_v1 **out_object);
uint64_t obelisk_rt_v1_object_id(const obelisk_rt_object_v1 *object);

// Immutable, non-interned managed strings. Zero is the canonical empty string.
// The view API uses caller scratch for SSO values; heap views remain valid
// while the string word is rooted. The scratch buffer must have eight bytes.
typedef struct obelisk_rt_string_span_v1 {
  obelisk_rt_string_v1 string;
} obelisk_rt_string_span_v1;

obelisk_rt_status obelisk_rt_v1_string_create(obelisk_rt_gc_lane_v1 *lane,
                                              const char *bytes, uint64_t size,
                                              obelisk_rt_string_v1 *out_string);
obelisk_rt_status obelisk_rt_v1_string_concat(obelisk_rt_gc_lane_v1 *lane,
                                              obelisk_rt_string_v1 left,
                                              obelisk_rt_string_v1 right,
                                              obelisk_rt_string_v1 *out_string);
obelisk_rt_status obelisk_rt_v1_string_concat_many(
    obelisk_rt_gc_lane_v1 *lane, const obelisk_rt_string_span_v1 *spans,
    uint64_t span_count, obelisk_rt_string_v1 *out_string);
obelisk_rt_status obelisk_rt_v1_string_repeat(obelisk_rt_gc_lane_v1 *lane,
                                              obelisk_rt_string_v1 string,
                                              uint64_t count,
                                              obelisk_rt_string_v1 *out_string);
obelisk_rt_status
obelisk_rt_v1_string_from_packed(obelisk_rt_gc_lane_v1 *lane, const void *value,
                                 const void *unknown, uint64_t bit_width,
                                 obelisk_rt_string_v1 *out_string);
obelisk_rt_status obelisk_rt_v1_string_to_packed(obelisk_rt_string_v1 string,
                                                 void *value, void *unknown,
                                                 uint64_t bit_width);
obelisk_rt_status obelisk_rt_v1_string_view(obelisk_rt_string_v1 string,
                                            char scratch[8],
                                            const char **out_bytes,
                                            uint64_t *out_size);
uint64_t obelisk_rt_v1_string_length(obelisk_rt_string_v1 string);
uint64_t obelisk_rt_v1_string_hash(obelisk_rt_string_v1 string);
uint32_t obelisk_rt_v1_string_getc(obelisk_rt_string_v1 string, int64_t index);
obelisk_rt_status obelisk_rt_v1_string_putc(obelisk_rt_gc_lane_v1 *lane,
                                            obelisk_rt_string_v1 string,
                                            int64_t index, uint32_t character,
                                            obelisk_rt_string_v1 *out_string);
obelisk_rt_status obelisk_rt_v1_string_substr(obelisk_rt_gc_lane_v1 *lane,
                                              obelisk_rt_string_v1 string,
                                              int64_t left, int64_t right,
                                              obelisk_rt_string_v1 *out_string);
int32_t obelisk_rt_v1_string_compare(obelisk_rt_string_v1 left,
                                     obelisk_rt_string_v1 right);
int32_t obelisk_rt_v1_string_compare_insensitive(obelisk_rt_string_v1 left,
                                                 obelisk_rt_string_v1 right);
obelisk_rt_status obelisk_rt_v1_string_case_convert(
    obelisk_rt_gc_lane_v1 *lane, obelisk_rt_string_v1 string, uint32_t to_upper,
    obelisk_rt_string_v1 *out_string);
// One $sscanf/$fscanf conversion. `prefix` is the format text preceding the
// conversion: whitespace in it matches any run of input whitespace including
// none, and every other character must match exactly. `specifier` is the
// conversion letter, whose field is returned as text for the caller to parse.
// `out_ok` is zero when the prefix failed to match or the field was empty, in
// which case the cursor does not advance.
obelisk_rt_status obelisk_rt_v1_string_scan_field(
    obelisk_rt_gc_lane_v1 *lane, obelisk_rt_string_v1 input, uint32_t cursor,
    const char *prefix, uint64_t prefix_size, uint32_t specifier,
    obelisk_rt_string_v1 *out_field, uint32_t *out_cursor, uint32_t *out_ok);
obelisk_rt_status
obelisk_rt_v1_string_parse_integer(obelisk_rt_string_v1 string, uint32_t radix,
                                   uint64_t *out_value);
obelisk_rt_status obelisk_rt_v1_string_parse_real(obelisk_rt_string_v1 string,
                                                  double *out_value);
obelisk_rt_status
obelisk_rt_v1_string_format_integer(obelisk_rt_gc_lane_v1 *lane, uint64_t value,
                                    uint32_t radix, uint32_t is_signed,
                                    obelisk_rt_string_v1 *out_string);
obelisk_rt_status
obelisk_rt_v1_string_format_real(obelisk_rt_gc_lane_v1 *lane, double value,
                                 obelisk_rt_string_v1 *out_string);

typedef uint32_t obelisk_rt_container_kind_v1;
enum {
  OBELISK_RT_CONTAINER_DYNAMIC_ARRAY = 1,
  OBELISK_RT_CONTAINER_QUEUE = 2,
  OBELISK_RT_CONTAINER_ASSOCIATIVE_ARRAY = 3
};

typedef uint32_t obelisk_rt_assoc_key_kind_v1;
enum {
  OBELISK_RT_ASSOC_KEY_UNSIGNED = 1,
  OBELISK_RT_ASSOC_KEY_SIGNED = 2,
  OBELISK_RT_ASSOC_KEY_STRING = 3
};

// Canonical typed associative key. Integral keys use value and unknown with
// width in [1, 64]. String keys use string and require width/value/unknown to
// be zero. Any nonzero unknown mask makes that operation a no-op.
typedef struct obelisk_rt_assoc_key_v1 {
  obelisk_rt_assoc_key_kind_v1 kind;
  uint32_t reserved;
  uint64_t width;
  uint64_t value;
  uint64_t unknown;
  obelisk_rt_string_v1 string;
} obelisk_rt_assoc_key_v1;

// Erased dynamic-array, queue, and associative-array primitives. Value and
// unknown point to
// descriptor-sized planes. Reads outside the live range return zeroes. Queue
// writes at index size append a default element before applying the write.
obelisk_rt_status obelisk_rt_v1_dynamic_array_create(
    obelisk_rt_gc_lane_v1 *lane, const obelisk_rt_element_type_v1 *element_type,
    uint64_t size, obelisk_rt_object_v1 **out_array);
obelisk_rt_status
obelisk_rt_v1_dynamic_array_resize(obelisk_rt_gc_lane_v1 *lane,
                                   obelisk_rt_object_v1 *array,
                                   uint64_t new_size);
// bound is the maximum legal queue index. UINT64_MAX denotes an unbounded
// queue, so bound zero represents the one-element declaration [$:0].
obelisk_rt_status
obelisk_rt_v1_queue_create(obelisk_rt_gc_lane_v1 *lane,
                           const obelisk_rt_element_type_v1 *element_type,
                           uint64_t bound, obelisk_rt_object_v1 **out_queue);
// Create a dynamic array or queue with the same immutable element metadata as
// the first nonnull source. Queue bounds are preserved. If both sources are
// null, the canonical null/default container is returned.
obelisk_rt_status obelisk_rt_v1_container_create_like(
    obelisk_rt_gc_lane_v1 *lane, obelisk_rt_object_v1 *preferred,
    obelisk_rt_object_v1 *fallback, uint64_t size,
    obelisk_rt_object_v1 **out_container);
obelisk_rt_status obelisk_rt_v1_container_create_typed(
    obelisk_rt_gc_lane_v1 *lane, uint32_t container_kind, uint64_t type_id,
    uint32_t element_kind, uint32_t element_flags, uint64_t value_size,
    uint64_t alignment, uint64_t bit_width,
    const obelisk_rt_element_trace_slot_v1 *trace_slots,
    uint64_t trace_slot_count, uint64_t size, uint64_t bound,
    obelisk_rt_object_v1 **out_container);
uint64_t obelisk_rt_v1_container_size(obelisk_rt_object_v1 *container);
obelisk_rt_status obelisk_rt_v1_container_read(obelisk_rt_object_v1 *container,
                                               int64_t index, void *out_value,
                                               void *out_unknown);
// Bytecode-safe variants reject buffers that cannot hold the immutable element
// descriptor's planes before reading or writing any bytes.
obelisk_rt_status obelisk_rt_v1_container_read_checked(
    obelisk_rt_object_v1 *container, int64_t index, void *out_value,
    uint64_t value_size, void *out_unknown, uint64_t unknown_size);
obelisk_rt_status obelisk_rt_v1_container_write(obelisk_rt_gc_lane_v1 *lane,
                                                obelisk_rt_object_v1 *container,
                                                int64_t index,
                                                const void *value,
                                                const void *unknown);
obelisk_rt_status obelisk_rt_v1_container_write_checked(
    obelisk_rt_gc_lane_v1 *lane, obelisk_rt_object_v1 *container, int64_t index,
    const void *value, uint64_t value_size, const void *unknown,
    uint64_t unknown_size);
obelisk_rt_status
obelisk_rt_v1_container_clone(obelisk_rt_gc_lane_v1 *lane,
                              obelisk_rt_object_v1 *container,
                              obelisk_rt_object_v1 **out_container);
obelisk_rt_status
obelisk_rt_v1_container_delete(obelisk_rt_object_v1 *container);
obelisk_rt_status obelisk_rt_v1_queue_push(obelisk_rt_gc_lane_v1 *lane,
                                           obelisk_rt_object_v1 *queue,
                                           uint32_t front, const void *value,
                                           const void *unknown);
obelisk_rt_status obelisk_rt_v1_queue_pop(obelisk_rt_object_v1 *queue,
                                          uint32_t front, void *out_value,
                                          void *out_unknown,
                                          uint32_t *out_present);
obelisk_rt_status obelisk_rt_v1_queue_insert(obelisk_rt_gc_lane_v1 *lane,
                                             obelisk_rt_object_v1 *queue,
                                             int64_t index, const void *value,
                                             const void *unknown);
obelisk_rt_status obelisk_rt_v1_queue_delete_index(obelisk_rt_object_v1 *queue,
                                                   int64_t index);
obelisk_rt_status obelisk_rt_v1_assoc_create(
    obelisk_rt_gc_lane_v1 *lane, const obelisk_rt_element_type_v1 *element_type,
    obelisk_rt_assoc_key_kind_v1 key_kind, uint64_t key_width,
    obelisk_rt_object_v1 **out_array);
obelisk_rt_status obelisk_rt_v1_assoc_create_typed(
    obelisk_rt_gc_lane_v1 *lane, uint64_t type_id, uint32_t element_kind,
    uint32_t element_flags, uint64_t value_size, uint64_t alignment,
    uint64_t bit_width, const obelisk_rt_element_trace_slot_v1 *trace_slots,
    uint64_t trace_slot_count, obelisk_rt_assoc_key_kind_v1 key_kind,
    uint64_t key_width, obelisk_rt_object_v1 **out_array);
obelisk_rt_status obelisk_rt_v1_assoc_exists(obelisk_rt_object_v1 *array,
                                             const obelisk_rt_assoc_key_v1 *key,
                                             uint32_t *out_exists);
obelisk_rt_status
obelisk_rt_v1_assoc_key_info(obelisk_rt_object_v1 *array,
                             obelisk_rt_assoc_key_kind_v1 *out_kind,
                             uint64_t *out_width);
obelisk_rt_status obelisk_rt_v1_assoc_read(obelisk_rt_object_v1 *array,
                                           const obelisk_rt_assoc_key_v1 *key,
                                           void *out_value, void *out_unknown,
                                           uint32_t *out_present);
obelisk_rt_status obelisk_rt_v1_assoc_read_checked(
    obelisk_rt_object_v1 *array, const obelisk_rt_assoc_key_v1 *key,
    void *out_value, uint64_t value_size, void *out_unknown,
    uint64_t unknown_size, uint32_t *out_present);
obelisk_rt_status obelisk_rt_v1_assoc_write(obelisk_rt_gc_lane_v1 *lane,
                                            obelisk_rt_object_v1 *array,
                                            const obelisk_rt_assoc_key_v1 *key,
                                            const void *value,
                                            const void *unknown);
obelisk_rt_status obelisk_rt_v1_assoc_write_checked(
    obelisk_rt_gc_lane_v1 *lane, obelisk_rt_object_v1 *array,
    const obelisk_rt_assoc_key_v1 *key, const void *value, uint64_t value_size,
    const void *unknown, uint64_t unknown_size);
obelisk_rt_status obelisk_rt_v1_assoc_set_default(obelisk_rt_gc_lane_v1 *lane,
                                                  obelisk_rt_object_v1 *array,
                                                  const void *value,
                                                  const void *unknown);
obelisk_rt_status obelisk_rt_v1_assoc_set_default_checked(
    obelisk_rt_gc_lane_v1 *lane, obelisk_rt_object_v1 *array, const void *value,
    uint64_t value_size, const void *unknown, uint64_t unknown_size);
obelisk_rt_status
obelisk_rt_v1_assoc_delete(obelisk_rt_object_v1 *array,
                           const obelisk_rt_assoc_key_v1 *key);
// Ordered traversal follows SystemVerilog key ordering. first/last ignore the
// input key; next/prev replace it only on success. Rebuilding the ordered cache
// can collect and therefore requires the current managed lane.
obelisk_rt_status obelisk_rt_v1_assoc_first(obelisk_rt_gc_lane_v1 *lane,
                                            obelisk_rt_object_v1 *array,
                                            obelisk_rt_assoc_key_v1 *inout_key,
                                            uint32_t *out_success);
obelisk_rt_status obelisk_rt_v1_assoc_last(obelisk_rt_gc_lane_v1 *lane,
                                           obelisk_rt_object_v1 *array,
                                           obelisk_rt_assoc_key_v1 *inout_key,
                                           uint32_t *out_success);
obelisk_rt_status obelisk_rt_v1_assoc_next(obelisk_rt_gc_lane_v1 *lane,
                                           obelisk_rt_object_v1 *array,
                                           obelisk_rt_assoc_key_v1 *inout_key,
                                           uint32_t *out_success);
obelisk_rt_status obelisk_rt_v1_assoc_prev(obelisk_rt_gc_lane_v1 *lane,
                                           obelisk_rt_object_v1 *array,
                                           obelisk_rt_assoc_key_v1 *inout_key,
                                           uint32_t *out_success);

// Escaping dynamic element lvalues are represented by managed paths, never by
// interior pointers. A path captures its container owner and canonical
// selector and resolves the live element on every access.
obelisk_rt_status obelisk_rt_v1_reference_path_index_create(
    obelisk_rt_gc_lane_v1 *lane, obelisk_rt_object_v1 *container, int64_t index,
    obelisk_rt_object_v1 *watch_owner, uint64_t owner_payload,
    uint32_t owner_managed, obelisk_rt_object_v1 **out_path);
obelisk_rt_status obelisk_rt_v1_reference_path_assoc_create(
    obelisk_rt_gc_lane_v1 *lane, obelisk_rt_object_v1 *array,
    const obelisk_rt_assoc_key_v1 *key, obelisk_rt_object_v1 *watch_owner,
    uint64_t owner_payload, uint32_t owner_managed,
    obelisk_rt_object_v1 **out_path);
obelisk_rt_status obelisk_rt_v1_reference_path_load(obelisk_rt_object_v1 *path,
                                                    void *out_value,
                                                    void *out_unknown,
                                                    uint32_t *out_present);
obelisk_rt_status
obelisk_rt_v1_reference_path_store(obelisk_rt_gc_lane_v1 *lane,
                                   obelisk_rt_object_v1 *path,
                                   const void *value, const void *unknown);

obelisk_rt_status obelisk_rt_v1_method_resolve(
    obelisk_rt_object_v1 *receiver, uint64_t slot, uint64_t signature_id,
    const obelisk_rt_method_descriptor_v1 **out_method);
obelisk_rt_status obelisk_rt_v1_interface_method_resolve(
    obelisk_rt_object_v1 *receiver, uint64_t interface_id,
    uint64_t interface_ordinal, uint64_t signature_id,
    const obelisk_rt_method_descriptor_v1 **out_method);
obelisk_rt_status obelisk_rt_v1_method_invoke(
    obelisk_rt_gc_lane_v1 *lane, obelisk_rt_object_v1 *receiver, uint64_t slot,
    uint64_t signature_id, const obelisk_rt_method_argument_v1 *arguments,
    uint32_t argument_count, void *result, uint64_t result_size);
obelisk_rt_status obelisk_rt_v1_interface_method_invoke(
    obelisk_rt_gc_lane_v1 *lane, obelisk_rt_object_v1 *receiver,
    uint64_t interface_id, uint64_t interface_ordinal, uint64_t signature_id,
    const obelisk_rt_method_argument_v1 *arguments, uint32_t argument_count,
    void *result, uint64_t result_size);
obelisk_rt_status obelisk_rt_v1_method_task_activate(
    obelisk_rt_gc_lane_v1 *lane, obelisk_rt_object_v1 *receiver, uint64_t slot,
    uint64_t signature_id, const obelisk_rt_method_argument_v1 *arguments,
    uint32_t argument_count, uint64_t *out_activation);
obelisk_rt_status obelisk_rt_v1_interface_method_task_activate(
    obelisk_rt_gc_lane_v1 *lane, obelisk_rt_object_v1 *receiver,
    uint64_t interface_id, uint64_t interface_ordinal, uint64_t signature_id,
    const obelisk_rt_method_argument_v1 *arguments, uint32_t argument_count,
    uint64_t *out_activation);

// IEEE 1800-2023 weak_reference support. The wrapper is itself a managed
// object. Its referent is cleared during the collection that first determines
// that no strong path reaches the referent.
obelisk_rt_status
obelisk_rt_v1_weak_create(obelisk_rt_gc_lane_v1 *lane,
                          const obelisk_rt_class_descriptor_v1 *descriptor,
                          obelisk_rt_object_v1 *referent,
                          obelisk_rt_object_v1 **out_weak);
obelisk_rt_status obelisk_rt_v1_weak_get(obelisk_rt_object_v1 *weak,
                                         obelisk_rt_object_v1 **out_referent);
obelisk_rt_status obelisk_rt_v1_weak_clear(obelisk_rt_object_v1 *weak);

// Validate and traverse the optional reflection image with checked cursors.
// Returned names are immutable spans into the database and remain valid for
// the lifetime of the execution descriptor.
obelisk_rt_status obelisk_rt_v1_design_validate(
    const obelisk_rt_execution_descriptor_v1 *execution);
obelisk_rt_status
obelisk_rt_v1_design_root(const obelisk_rt_execution_descriptor_v1 *execution,
                          obelisk_rt_design_cursor_v1 *out_cursor);
obelisk_rt_status
obelisk_rt_v1_design_child(const obelisk_rt_execution_descriptor_v1 *execution,
                           obelisk_rt_design_cursor_v1 cursor,
                           obelisk_rt_design_cursor_v1 *out_cursor);
obelisk_rt_status obelisk_rt_v1_design_child_at(
    const obelisk_rt_execution_descriptor_v1 *execution,
    obelisk_rt_design_cursor_v1 cursor, uint64_t index,
    obelisk_rt_design_cursor_v1 *out_cursor);
obelisk_rt_status obelisk_rt_v1_design_sibling(
    const obelisk_rt_execution_descriptor_v1 *execution,
    obelisk_rt_design_cursor_v1 cursor,
    obelisk_rt_design_cursor_v1 *out_cursor);
obelisk_rt_status
obelisk_rt_v1_design_lookup(const obelisk_rt_execution_descriptor_v1 *execution,
                            const uint8_t *name, uint64_t name_size,
                            obelisk_rt_design_cursor_v1 *out_cursor);
obelisk_rt_status
obelisk_rt_v1_design_info(const obelisk_rt_execution_descriptor_v1 *execution,
                          obelisk_rt_design_cursor_v1 cursor,
                          obelisk_rt_design_info_v1 *out_info);
obelisk_rt_status obelisk_rt_v1_design_type_info(
    const obelisk_rt_execution_descriptor_v1 *execution,
    obelisk_rt_design_cursor_v1 cursor,
    obelisk_rt_design_type_info_v1 *out_info);
obelisk_rt_status obelisk_rt_v1_design_type_child(
    const obelisk_rt_execution_descriptor_v1 *execution,
    obelisk_rt_design_cursor_v1 cursor, uint64_t index,
    obelisk_rt_design_cursor_v1 *out_cursor);
obelisk_rt_status
obelisk_rt_v1_design_name(const obelisk_rt_execution_descriptor_v1 *execution,
                          obelisk_rt_design_cursor_v1 cursor,
                          const uint8_t **out_data, uint64_t *out_size);
obelisk_rt_status obelisk_rt_v1_design_read(obelisk_rt_context *context,
                                            obelisk_rt_design_cursor_v1 cursor,
                                            uint64_t *value, uint64_t *unknown,
                                            uint64_t bit_width);
obelisk_rt_status obelisk_rt_v1_design_write(obelisk_rt_context *context,
                                             obelisk_rt_design_cursor_v1 cursor,
                                             const uint64_t *value,
                                             const uint64_t *unknown,
                                             uint64_t bit_width);
obelisk_rt_status obelisk_rt_v1_design_force(obelisk_rt_context *context,
                                             obelisk_rt_design_cursor_v1 cursor,
                                             const uint64_t *value,
                                             const uint64_t *unknown,
                                             uint64_t bit_width);
obelisk_rt_status
obelisk_rt_v1_design_release(obelisk_rt_context *context,
                             obelisk_rt_design_cursor_v1 cursor);

// Activate the single-context VPI shim and invoke startup tables belonging to
// already loaded DT_NEEDED modules. Module names are runtime loader identities
// (DT_SONAME, or the no-SONAME basename) and remain caller-owned for the call.
obelisk_rt_status obelisk_rt_v1_vpi_startup(obelisk_rt_context *context,
                                            const char *const *modules,
                                            uint64_t module_count);
void obelisk_rt_v1_vpi_shutdown(obelisk_rt_context *context);

// Generated single-threaded schedule ABI.  The plan is immutable; mutable
// generated storage is reached through `mutable_state` and may be installed in
// at most one live context at a time.  A successful add transfers process
// ownership to the runtime, just like the generic scheduler add APIs.
#define OBELISK_RT_NATIVE_SCHEDULE_FULLY_STATIC UINT32_C(1)
// Actor slot zero is the native-only root bootstrap. It must run natively so
// static child ownership is established before bytecode/VPI transition
// fragments execute.
#define OBELISK_RT_NATIVE_SCHEDULE_ROOT_SLOT_ZERO UINT32_C(2)
// The generated plan may advance constant deadlines and static NBA barriers
// without entering the generic scheduler. An external write or unsupported
// control operation requests a generic handover at a region boundary.
#define OBELISK_RT_NATIVE_SCHEDULE_STATIC_CONTROL UINT32_C(4)
// Direct packed transitions may use compiler-proven static sensitivity fanout
// without allocating generic signal subscriptions.
#define OBELISK_RT_NATIVE_SCHEDULE_STATIC_FANOUT UINT32_C(8)
// Compiler-emitted state planes are canonical for eligible narrow roots.
#define OBELISK_RT_NATIVE_SCHEDULE_DIRECT_STATE UINT32_C(16)
// The plan carries fixed narrow-NBA root accumulators and site claims.
#define OBELISK_RT_NATIVE_SCHEDULE_STATIC_NBA UINT32_C(32)
// Native fragment actions are emitted by the same revision-coupled compiler as
// the plan. The runtime may omit redundant generic frame/action validation
// after the generated executor returns successfully.
#define OBELISK_RT_NATIVE_SCHEDULE_GENERATED_ACTIONS UINT32_C(64)
// Exact fanout metadata is present but must be activated only after the
// runtime proves that VPI startup did not write or dirty native state.
#define OBELISK_RT_NATIVE_SCHEDULE_GUARDED_FANOUT UINT32_C(128)
// The generated plan exposes a runtime-owned clean-state flag used to bypass
// per-root specialization guards while no writable VPI state is dirty.
#define OBELISK_RT_NATIVE_SCHEDULE_GUARDED_SPECIALIZATION UINT32_C(256)
// The compiler proved fixed actor multiplicity, generated actions, static
// control, and exact fanout. While the run-level VPI guard is clean, the
// runtime may omit per-actor handover checks; an unexpected action requests a
// transactional transfer to the generic scheduler.
#define OBELISK_RT_NATIVE_SCHEDULE_CLEAN_SUPERSTEP UINT32_C(512)
// Experimental closed-world eval loop: exact static transitions feed the
// generated trigger masks instead of re-entering the actor worklist.
#define OBELISK_RT_NATIVE_SCHEDULE_EVAL UINT32_C(1024)

typedef struct obelisk_rt_aot_deopt_actor {
  uint32_t slot;
  uint32_t flags;
  uint32_t schedule_rank;
  uint32_t queued_region;
  uint64_t insertion_sequence;
  uint64_t wake_time;
  uint64_t wait_offset;
  uint64_t wait_size;
  obelisk_rt_fragment_action_v1 action;
  uint32_t started;
  uint32_t ready;
} obelisk_rt_aot_deopt_actor;

typedef struct obelisk_rt_aot_deopt_nba {
  uint32_t slot;
  uint32_t exec_region;
  uint64_t sequence;
  uint64_t due_time;
} obelisk_rt_aot_deopt_nba;

typedef struct obelisk_rt_aot_deopt_snapshot {
  uint32_t size;
  uint64_t current_time;
  const obelisk_rt_aot_deopt_actor *actors;
  uint32_t actor_count;
  uint32_t ready_count;
  const obelisk_rt_aot_deopt_nba *nbas;
  uint32_t nba_count;
  uint32_t reserved;
  uint64_t next_sequence;
} obelisk_rt_aot_deopt_snapshot;

typedef struct obelisk_rt_native_schedule_node {
  uint32_t actor_slot;
  uint32_t continuation;
  uint32_t fusion_group;
} obelisk_rt_native_schedule_node;

// Revision-coupled description of a structurally proven free-running clock.
// Generated run_until code uses the returned control pointers directly while
// it owns the scheduler transaction; no runtime call is needed per edge.
typedef struct obelisk_rt_native_periodic_clock_v1 {
  uint32_t actor_slot;
  uint32_t continuation;
  uint32_t static_state;
  uint32_t reserved;
  uint64_t bit_offset;
  uint64_t half_period;
} obelisk_rt_native_periodic_clock_v1;

// Proven one-bit projection of a periodic source through a single-driver
// port/net alias.  Bootstrap and generated run_until use the same physical
// mapping so finite reset processes observe the identical clock history.
typedef struct obelisk_rt_native_periodic_alias_v1 {
  uint32_t source_static_state;
  uint32_t forwarding_actor_slot;
  uint32_t forwarding_continuation;
  uint32_t target_static_state;
  uint64_t source_bit_offset;
  uint64_t target_bit_offset;
  uint64_t driver_bit_offset;
} obelisk_rt_native_periodic_alias_v1;

typedef struct obelisk_rt_native_periodic_control_v1 {
  uint64_t *scheduler_time;
  uint32_t *termination_requested;
  uint64_t next_runtime_deadline;
} obelisk_rt_native_periodic_control_v1;

typedef uint32_t obelisk_rt_static_nba_storage;
enum {
  OBELISK_RT_STATIC_NBA_FIXED_SLOT = 0,
  OBELISK_RT_STATIC_NBA_ROOT_ACCUMULATOR = 1
};

// Shared scalar and generated-accumulator width policies. Generated staging
// writes the record below directly; the runtime consumes it at the NBA barrier
// and owns validation, force masking, edge computation, fanout, observer
// notification, snapshot, and deoptimization. Each write-mask run is one
// aligned 32-bit lane; repeated writes to a lane coalesce in place.
#define OBELISK_RT_SCALAR_NBA_MAX_BITS UINT64_C(64)
#define OBELISK_RT_GENERATED_NBA_MAX_BITS UINT64_C(256)
typedef struct obelisk_rt_generated_nba_accumulator_256 {
  uint64_t value[4];
  uint64_t unknown[4];
  uint64_t write_mask[4];
  uint32_t valid;
  uint32_t exec_region;
} obelisk_rt_generated_nba_accumulator_256;

typedef struct obelisk_rt_static_nba_root {
  uint32_t commit_node;
  uint32_t static_state;
  uint64_t bit_width;
  obelisk_rt_generated_nba_accumulator_256 *generated_accumulator;
} obelisk_rt_static_nba_root;

typedef struct obelisk_rt_static_nba_site {
  uint64_t site;
  uint32_t root;
  obelisk_rt_static_nba_storage storage;
} obelisk_rt_static_nba_site;

// Internal routing encoded in static_fanout_entry::reserved for eval plans.
// Generic plans require RUNTIME. The names keep the revision-coupled compiler
// and runtime ABI from assigning independent meanings to numeric literals.
#define OBELISK_RT_FANOUT_RUNTIME UINT32_C(0)
#define OBELISK_RT_FANOUT_DIRECT UINT32_C(1)
#define OBELISK_RT_FANOUT_PERIODIC_ALIAS UINT32_C(2)

typedef struct obelisk_rt_static_fanout_entry {
  uint32_t static_state;
  uint32_t actor_slot;
  uint32_t continuation;
  obelisk_rt_wait_edge_kind edge;
  uint32_t compute_node;
  uint32_t reserved;
  uint64_t low_bit;
  uint64_t bit_width;
  // Generated clock-kernel target. `compute_node` remains the exact generic
  // fallback identity; native ingress uses this owner/bit pair directly.
  uint32_t kernel;
  uint32_t merged_bit;
} obelisk_rt_static_fanout_entry;

typedef struct obelisk_rt_native_clock_kernel {
  uint32_t static_state;
  obelisk_rt_wait_edge_kind edge;
  uint64_t low_bit;
  uint64_t bit_width;
  uint64_t *ingress_mask;
  uint32_t ingress_word_count;
  uint32_t reserved;
  // Compiler-owned permanent-method eligibility. The runtime seeds a bit
  // once the fallback actor reaches the corresponding typed wait; generated
  // eval/NBA code consumes it without inspecting coroutine state.
  uint64_t *active_mask;
} obelisk_rt_native_clock_kernel;

#define OBELISK_RT_MERGED_FRAGMENT_SHARED UINT32_C(1)
#define OBELISK_RT_MERGED_FRAGMENT_FALLBACK UINT32_C(2)
typedef struct obelisk_rt_native_merged_fragment {
  uint32_t actor_slot;
  uint32_t continuation;
  uint32_t kernel;
  uint32_t bit;
  uint32_t compute_node;
  uint32_t flags;
  // Private generated body for clean native execution. A null pointer keeps
  // this stable ingress as an explicit fine-scheduler fallback boundary.
  obelisk_rt_status (*execute)(obelisk_rt_context *context);
} obelisk_rt_native_merged_fragment;

#define OBELISK_RT_STATIC_ROOT_READ UINT32_C(1)
#define OBELISK_RT_STATIC_ROOT_WRITE UINT32_C(2)

typedef struct obelisk_rt_static_actor_root {
  uint32_t actor_slot;
  uint32_t static_state;
  uint32_t flags;
  uint32_t reserved;
} obelisk_rt_static_actor_root;

typedef obelisk_rt_status (*obelisk_rt_native_schedule_bind)(
    void *mutable_state, obelisk_rt_context *context, uint32_t actor_slot,
    obelisk_rt_process_instance_v1 *instance);
// The runtime may replace an occupied slot when entering or returning from a
// task call, and passes a null instance when releasing a slot. Implementations
// must store the supplied instance without inspecting the former actor.
typedef obelisk_rt_status (*obelisk_rt_native_schedule_run)(
    void *mutable_state, obelisk_rt_context *context);
typedef obelisk_rt_status (*obelisk_rt_native_schedule_snapshot)(
    void *mutable_state, obelisk_rt_context *context,
    obelisk_rt_aot_deopt_snapshot *out_snapshot);
typedef obelisk_rt_status (*obelisk_rt_native_schedule_nba_commit)(
    void *mutable_state, obelisk_rt_context *context, uint32_t barrier_region,
    uint32_t *out_changed);
typedef obelisk_rt_status (*obelisk_rt_native_timeslot_coordinator)(
    void *mutable_state, obelisk_rt_context *context);
typedef void (*obelisk_rt_native_promotion_invalidate)(void);
typedef uint32_t (*obelisk_rt_native_promotion_ready)(void);
typedef obelisk_rt_status (*obelisk_rt_native_checkpoint_callback)(
    obelisk_rt_context *context);

typedef struct obelisk_rt_native_schedule_plan {
  uint32_t size;
  uint64_t graph_layout_checksum;
  void *mutable_state;
  uint64_t mutable_state_size;
  uint32_t actor_capacity;
  uint32_t flags;
  uint8_t *state_value;
  uint8_t *state_unknown;
  uint64_t state_bit_count;
  obelisk_rt_native_schedule_bind bind;
  obelisk_rt_native_schedule_run run;
  obelisk_rt_native_schedule_snapshot fallback_snapshot;
  // The prototype ABI accepts exactly sizeof(this structure).
  const obelisk_rt_static_nba_root *nba_roots;
  uint32_t nba_root_count;
  uint32_t nba_reserved;
  const obelisk_rt_static_nba_site *nba_sites;
  uint64_t nba_site_count;
  const obelisk_rt_static_fanout_entry *fanout_entries;
  uint64_t fanout_entry_count;
  const obelisk_rt_static_actor_root *actor_roots;
  uint64_t actor_root_count;
  obelisk_rt_native_schedule_nba_commit nba_commit;
  // Compiler-emitted hot-path guard. The runtime sets this to one only while
  // the generated state planes are canonical for every root, and clears it
  // before handing any VPI write to the guarded per-root path.
  uint32_t *specialization_fast;
  // Compiler-owned dirty-root bitmap. Generated fixed-site NBA staging sets
  // one bit per root; the runtime clears committed bits at the barrier.
  uint64_t *nba_dirty_roots;
  uint32_t nba_dirty_word_count;
  uint32_t nba_dirty_reserved;
  uint64_t *nba_dirty_summary;
  uint32_t nba_dirty_summary_word_count;
  uint32_t nba_dirty_summary_reserved;
  const obelisk_rt_native_clock_kernel *clock_kernels;
  uint32_t clock_kernel_count;
  uint32_t clock_kernel_reserved;
  const obelisk_rt_native_merged_fragment *merged_fragments;
  uint64_t merged_fragment_count;
  obelisk_rt_native_timeslot_coordinator timeslot_coordinator;
  // Cold-path invalidation for the generated two-state closure. External
  // X/Z writes and transactional fine-scheduler handoffs clear the selected
  // variant here; the generated coordinator rescans only after quiescence.
  obelisk_rt_native_promotion_invalidate promotion_invalidate;
  // Cold query used only by a transient Tier-2 transaction. It returns one
  // after the candidate closure's canonical unknown plane is clear, allowing
  // the runtime to hand control directly to the two-state Tier-1 route.
  obelisk_rt_native_promotion_ready promotion_ready;
} obelisk_rt_native_schedule_plan;

// Serial generated-simulator scheduler. The scheduler owns an instance after
// a successful add. Bit zero selects final-phase work and bits 1-3 encode the
// executable home region. A zero flags value retains the historical ordinary
// Active-process behavior.
obelisk_rt_status
obelisk_rt_v1_scheduler_add(obelisk_rt_context *context,
                            obelisk_rt_process_instance_v1 *instance,
                            uint32_t flags);
// Ranked form used by generated simulators. Lower ranks execute first within
// an event region; equal ranks retain deterministic insertion order.
obelisk_rt_status
obelisk_rt_v1_scheduler_add_ranked(obelisk_rt_context *context,
                                   obelisk_rt_process_instance_v1 *instance,
                                   uint32_t flags, uint32_t schedule_rank);
obelisk_rt_status obelisk_rt_v1_scheduler_add_planned(
    obelisk_rt_context *context, obelisk_rt_process_instance_v1 *instance,
    uint32_t flags, uint32_t initial_rank, const uint32_t *continuations,
    const uint32_t *ranks, uint32_t continuation_count);
obelisk_rt_status obelisk_rt_v1_scheduler_install_aot(
    obelisk_rt_context *context, const obelisk_rt_native_schedule_plan *plan);
// OR one stable merged-fragment bit into a generated kernel's ingress.  The
// operation is idempotent until the generated coordinator drains the mask.
obelisk_rt_status obelisk_rt_v1_scheduler_activate_clock_kernel(
    obelisk_rt_context *context, uint32_t kernel, uint32_t merged_bit);
// Execute all clock masks accumulated in the current time slot.  A generated
// coordinator owns the shared-kernel drain and the single NBA/fanout epilogue.
obelisk_rt_status obelisk_rt_v1_scheduler_run_clock_coordinator(
    obelisk_rt_context *context);
// Enter/leave one compiler-proven clean fragment without resuming its
// coroutine. The actor remains suspended at the same stable continuation so a
// transactional fallback can use the original frame immediately.
obelisk_rt_status obelisk_rt_v1_scheduler_direct_fragment_enter(
    obelisk_rt_context *context, uint32_t actor_slot, uint32_t continuation,
    obelisk_rt_process_instance_v1 **out_instance);
obelisk_rt_status obelisk_rt_v1_scheduler_direct_fragment_leave(
    obelisk_rt_context *context, uint32_t actor_slot);
// Experimental eval-mode entry: execute one statically bound actor directly
// inside the scheduler-owned clean transaction.
obelisk_rt_status obelisk_rt_v1_scheduler_execute_aot_actor(
    obelisk_rt_context *context, uint32_t actor_slot);
// Returns nonzero when a priority signal-resumed process must run before the
// generated coordinator executes another owner in the current event region.
uint32_t
obelisk_rt_v1_scheduler_priority_signal_pending(obelisk_rt_context *context);
// Publish one exact cold continuation after the generated coordinator
// returns. The callback is invoked outside the hot Tier-1/Tier-2 call graph;
// its generated thunk resumes the same slot's ready-mask/NBA transaction.
obelisk_rt_status obelisk_rt_v1_scheduler_queue_aot_checkpoint(
    obelisk_rt_context *context, uint32_t actor_slot, uint32_t continuation,
    obelisk_rt_native_checkpoint_callback callback);
obelisk_rt_status obelisk_rt_v1_scheduler_add_aot(
    obelisk_rt_context *context, obelisk_rt_process_instance_v1 *instance,
    uint32_t flags, uint32_t actor_slot, uint32_t initial_rank,
    const uint32_t *continuations, const uint32_t *ranks,
    uint32_t continuation_count, const uint32_t *bytecode_continuations,
    uint32_t bytecode_continuation_count);
obelisk_rt_status obelisk_rt_v1_scheduler_run_aot_nodes(
    obelisk_rt_context *context, const obelisk_rt_native_schedule_node *nodes,
    uint32_t node_count);
// Drain time zero to quiescence, validate the proven periodic actors against
// their actual coroutine state, and detach only those deadlines for generated
// run_until ownership. `next_edges` has one element per clock.
obelisk_rt_status obelisk_rt_v1_scheduler_prepare_periodic_aot(
    obelisk_rt_context *context, const obelisk_rt_native_schedule_node *nodes,
    uint32_t node_count,
    const obelisk_rt_native_periodic_clock_v1 *clocks, uint32_t clock_count,
    const obelisk_rt_native_periodic_alias_v1 *aliases, uint32_t alias_count,
    uint64_t *next_edges, obelisk_rt_native_periodic_control_v1 *out_control);
// Restore detached periodic actors before the generated coordinator returns a
// Tier-2/3 boundary to the runtime.
obelisk_rt_status obelisk_rt_v1_scheduler_handoff_periodic_aot(
    obelisk_rt_context *context,
    const obelisk_rt_native_periodic_clock_v1 *clocks, uint32_t clock_count,
    const uint64_t *next_edges);
// Materialize the runtime-owned hybrid scheduler records at an AOT region
// boundary. Returned pointers remain context-owned until scheduler mutation.
obelisk_rt_status obelisk_rt_v1_scheduler_snapshot_aot(
    obelisk_rt_context *context, obelisk_rt_aot_deopt_snapshot *out_snapshot);
// Return nonzero when an actor/root access may use compiler-emitted planes.
// Deposits are transient until slot quiescence; force/assign ownership remains
// dirty until its corresponding release.
uint32_t obelisk_rt_v1_static_specialization_guard(obelisk_rt_context *context,
                                                   uint32_t actor_slot,
                                                   uint32_t static_state,
                                                   uint32_t flags);
uint32_t
obelisk_rt_v1_static_nba_specialization_guard(obelisk_rt_context *context,
                                              uint32_t root_index);
// Return the scheduler-owned stable identity used by await/join records. The
// token is never a host address and is not reused within a context.
uint64_t
obelisk_rt_v1_scheduler_process_token(obelisk_rt_context *context,
                                      obelisk_rt_process_instance_v1 *instance);
// Recursively terminate every live descendant of the currently executing
// logical process. Task activations retain their caller's logical identity.
obelisk_rt_status
obelisk_rt_v1_scheduler_disable_children(obelisk_rt_context *context);
// Install a new context-wide $monitor process. `design_process` selects the
// design-bytecode token namespace; native process tokens use zero.
obelisk_rt_status obelisk_rt_v1_monitor_register(obelisk_rt_context *context,
                                                 uint64_t process_token,
                                                 uint32_t design_process);
obelisk_rt_status obelisk_rt_v1_monitor_control(obelisk_rt_context *context,
                                                uint32_t enabled);
uint32_t obelisk_rt_v1_monitor_current(obelisk_rt_context *context);
// Dynamic named-block activations are inherited by spawned logical
// processes. A nonzero activation selects one lexical activation. With a zero
// activation, all_activations selects either the innermost inherited lexical
// activation or every live activation with the exact target ID.
obelisk_rt_status obelisk_rt_v1_control_enter(obelisk_rt_context *context,
                                              uint64_t target_id,
                                              uint64_t *out_activation);
obelisk_rt_status obelisk_rt_v1_control_leave(obelisk_rt_context *context,
                                              uint64_t activation);
obelisk_rt_status obelisk_rt_v1_control_disable(obelisk_rt_context *context,
                                                uint64_t target_id,
                                                uint64_t activation,
                                                uint32_t all_activations);
// Return one exactly once for each nonzero site ID in a context and zero on
// later claims. This guards descriptor-backed static local initialization.
uint32_t obelisk_rt_v1_static_once(obelisk_rt_context *context,
                                   uint64_t site_id);
// Return one for the first execution of a deferred-immediate assertion site
// by the current logical process in a time slot, and zero for repeats.
uint32_t obelisk_rt_v1_deferred_once(obelisk_rt_context *context,
                                     uint64_t site_id);
// Enqueue one deferred-immediate assertion report for the current logical
// process. Re-enqueuing the same site in the same slot supersedes the previous
// ticket. The returned nonzero ticket is opaque and context-local.
uint64_t obelisk_rt_v1_deferred_enqueue(obelisk_rt_context *context,
                                        uint64_t site_id);
// Site enqueue carrying the stable identity of a specifically labeled
// assertion. A later disable of that assertion cancels every pending ticket
// with the same assertion identity without flushing unrelated reports.
uint64_t obelisk_rt_v1_deferred_enqueue_for_assertion(
    obelisk_rt_context *context, uint64_t site_id, uint64_t assertion_id);
// Apply one IEEE 1800 assertion-control action (1 through 11) to one
// compiler-resolved assertion identity. Kill also cancels its queued deferred
// reports; Off leaves already queued reports intact. A locked identity ignores
// every control action except Unlock (2).
obelisk_rt_status obelisk_rt_v1_assertion_control(obelisk_rt_context *context,
                                                 uint32_t action,
                                                 uint64_t assertion_id);
// Return one when a new attempt may start for the assertion identity.
uint32_t obelisk_rt_v1_assertion_enabled(obelisk_rt_context *context,
                                        uint64_t assertion_id);
// Snapshot pass/fail action enablement for a new attempt. Bit 0 is
// nonvacuous pass, bit 1 is vacuous pass, and bit 2 is fail.
uint32_t obelisk_rt_v1_assertion_action_state(obelisk_rt_context *context,
                                             uint64_t assertion_id);
// Consume a deferred report ticket and return one iff it is still the latest
// report for its originating process and assertion site.
uint32_t obelisk_rt_v1_deferred_mature(obelisk_rt_context *context,
                                       uint64_t ticket);
// Copy one nonblocking assignment into the current time slot. The scheduler
// applies queued updates in call order after active work reaches quiescence.
// A UINT64_MAX bit offset is an out-of-range dynamic selection and is ignored.
obelisk_rt_status obelisk_rt_v1_scheduler_nba(
    obelisk_rt_context *context, uint8_t *value_plane, uint8_t *unknown_plane,
    uint64_t plane_bit_count, uint64_t bit_offset, uint64_t bit_width,
    uint64_t delay, const uint8_t *value, const uint8_t *unknown);
// Site-aware form shared by native and design-bytecode fragments. The site
// identity is validated against an installed static schedule when one is
// present; otherwise this has exactly the generic scheduler semantics.
obelisk_rt_status obelisk_rt_v1_scheduler_static_nba(
    obelisk_rt_context *context, uint64_t site, uint8_t *value_plane,
    uint8_t *unknown_plane, uint64_t plane_bit_count, uint64_t bit_offset,
    uint64_t bit_width, const uint8_t *value, const uint8_t *unknown);
// AOT-only packed staging entry. The compiler has already resolved the site
// to an installed root and proved the destination to be wholly in range.
obelisk_rt_status obelisk_rt_v1_scheduler_static_nba_packed(
    obelisk_rt_context *context, uint32_t root, uint8_t *value_plane,
    uint8_t *unknown_plane, uint64_t plane_bit_count, uint64_t root_offset,
    uint64_t bit_width, uint64_t value, uint64_t unknown);
// Revision-coupled generated staging for a compiler-proven fixed wide root in
// a VPI-off static-control schedule. Lifecycle, root bounds, and plane layout
// are established by the installed plan, so this avoids redundant hot checks.
void obelisk_rt_v1_static_nba_stage_wide(obelisk_rt_context *context,
                                         uint32_t root, uint64_t root_offset,
                                         uint64_t bit_width, uint64_t value,
                                         uint64_t unknown,
                                         uint32_t has_unknown);
// Claim one statically resolved narrow root update for this event slot. If a
// generic update has already claimed the root, the runtime stages this and
// subsequent claims through the ordered generic queue until slot quiescence.
obelisk_rt_status obelisk_rt_v1_static_nba_claim(
    obelisk_rt_context *context, uint32_t root, uint8_t *value_plane,
    uint8_t *unknown_plane, uint64_t plane_bit_count, uint64_t root_offset,
    uint64_t bit_width, uint64_t value, uint64_t unknown);
// Commit one valid fixed accumulator at an NBA barrier. Generated callbacks
// invoke roots in compute-graph order and OR their transition result into
// out_changed.
obelisk_rt_status
obelisk_rt_v1_static_nba_commit_root(obelisk_rt_context *context, uint32_t root,
                                     uint32_t barrier_region,
                                     uint32_t *out_changed);
// Commit the leading compute-graph-ordered accumulator roots in one runtime
// call. Generated callbacks use this bulk form to avoid one ABI crossing per
// root; the single-root entry remains available for mixed/deoptimized paths.
obelisk_rt_status obelisk_rt_v1_static_nba_commit_roots(
    obelisk_rt_context *context, uint32_t root_count, uint32_t barrier_region,
    uint32_t *out_changed);
// Generated clean-superstep NBA commit code checks this once per barrier.
// A false result retains the validating generic commit path for VPI,
// force/release, bytecode mutation, and transactional handoff.
uint32_t
obelisk_rt_v1_static_nba_direct_commit_guard(obelisk_rt_context *context);
// Account for scalar accumulators committed directly by one generated
// callback. Keeping this batched preserves diagnostics without a call per
// root.
void obelisk_rt_v1_static_nba_account_generated_commits(
    obelisk_rt_context *context, uint32_t count);
// Schedule one whole managed string word. The queued word remains a precise
// tagged root through commit, and equal byte contents do not publish a signal
// transition even when the immutable handles differ.
obelisk_rt_status obelisk_rt_v1_scheduler_string_nba(
    obelisk_rt_context *context, uint8_t *value_plane, uint64_t plane_bit_count,
    uint64_t bit_offset, uint64_t delay, obelisk_rt_string_v1 value);
// Schedule one class-property update. The destination and a managed value, if
// present, remain precise GC roots through commit. `unknown` is either null or
// a second `plane_size` byte plane immediately following the value plane in
// the property layout.
obelisk_rt_status obelisk_rt_v1_scheduler_managed_nba(
    obelisk_rt_context *context, obelisk_rt_object_v1 *destination,
    uint64_t offset, const void *value, const void *unknown,
    uint64_t plane_size, uint64_t delay);
// Record a changed native-state range. `edges` is a bitmask of the edge kinds
// observed on the range's least-significant bit. Signal-width wait records use
// the final wait-entry word for their watched width.
#define OBELISK_RT_SIGNAL_CHANGE (UINT32_C(1) << 0)
#define OBELISK_RT_SIGNAL_POSEDGE (UINT32_C(1) << 1)
#define OBELISK_RT_SIGNAL_NEGEDGE (UINT32_C(1) << 2)
void obelisk_rt_v1_scheduler_signal(obelisk_rt_context *context,
                                    uint64_t bit_offset, uint64_t bit_width,
                                    uint32_t edges);
void obelisk_rt_v1_scheduler_signal_transition(
    obelisk_rt_context *context, uint64_t bit_offset, uint64_t bit_width,
    const uint8_t *old_value, const uint8_t *old_unknown,
    const uint8_t *new_value, const uint8_t *new_unknown);
// Revision-coupled clean-AOT leaf for compiler-proven fixed packed roots.
// Scalar planes and a validated root-relative range allow exact static fanout
// without generic handle decoding, transition buffers, or observer scans.
void obelisk_rt_v1_scheduler_static_transition(
    obelisk_rt_context *context, uint32_t static_state, uint64_t low_bit,
    uint64_t bit_width, uint64_t old_value, uint64_t old_unknown,
    uint64_t new_value, uint64_t new_unknown);
// Activate a compiler-grouped set of exact static-fanout compute nodes. This
// is an internal clean-transaction leaf; dynamic/VPI handoff continues to use
// ordinary scalar transition publication.
void obelisk_rt_v1_scheduler_activate_static_nodes(
    obelisk_rt_context *context, const uint64_t *node_words,
    uint32_t word_count);
void obelisk_rt_v1_scheduler_real_transition(obelisk_rt_context *context,
                                             uint64_t bit_offset,
                                             uint32_t bit_width,
                                             const void *old_value,
                                             const void *new_value);
void obelisk_rt_v1_scheduler_event(obelisk_rt_context *context,
                                   uint64_t stable_id, uint32_t nonblocking);
// Trigger immediately, or enqueue a nonblocking named-event occurrence after
// `delay` design-precision ticks. A nonzero delay with a blocking trigger is
// invalid and records a scheduler failure.
void obelisk_rt_v1_scheduler_event_after(obelisk_rt_context *context,
                                         uint64_t stable_id,
                                         uint32_t nonblocking, uint64_t delay);
uint32_t obelisk_rt_v1_scheduler_event_triggered(obelisk_rt_context *context,
                                                 uint64_t stable_id);
void obelisk_rt_v1_scheduler_fail(obelisk_rt_context *context,
                                  obelisk_rt_status status);
// Register one compiler-assigned static-state object. Static handles retain
// these root bounds when views apply signed offsets, so partial out-of-range
// accesses cannot spill into an adjacent object in the shared bit plane.
obelisk_rt_status
obelisk_rt_v1_native_state_register_static(obelisk_rt_context *context,
                                           uint32_t id, uint64_t bit_offset,
                                           uint64_t bit_width);
// Seed the canonical design image from compiler-emitted native planes after
// static-state registration and before VPI startup/root-process creation. The
// planes remain bound for sparse Preponed capture and runtime-originated
// state publication while the context is alive.
obelisk_rt_status obelisk_rt_v1_native_state_sync(obelisk_rt_context *context,
                                                  uint8_t *value,
                                                  uint8_t *unknown,
                                                  uint64_t bit_count);
// Whole static packed language force/assign services. `assign` selects
// procedural assign ownership; release with `assign` selects deassign.
obelisk_rt_status obelisk_rt_v1_native_override(
    obelisk_rt_context *context, uint8_t *global_value, uint8_t *global_unknown,
    uint64_t global_bit_count, uint64_t handle, uint64_t bit_width,
    uint32_t descriptor_kind, uint32_t assign, const uint8_t *value,
    const uint8_t *unknown);
obelisk_rt_status obelisk_rt_v1_native_release_override(
    obelisk_rt_context *context, uint8_t *global_value, uint8_t *global_unknown,
    uint64_t global_bit_count, uint64_t handle, uint64_t bit_width,
    uint32_t descriptor_kind, uint32_t assign);
uint64_t obelisk_rt_v1_native_state_static_handle(uint32_t id);
uint64_t obelisk_rt_v1_native_handle_offset(uint64_t handle, int64_t offset);
obelisk_rt_status obelisk_rt_v1_native_state_alloc(obelisk_rt_context *context,
                                                   uint64_t bit_width,
                                                   const uint8_t *value,
                                                   const uint8_t *unknown,
                                                   uint64_t *out_handle);
// Allocate automatic state and publish all embedded managed roots as one
// ownership transaction. On failure no automatic state remains registered and
// out_handle is reset to UINT64_MAX.
obelisk_rt_status obelisk_rt_v1_native_state_alloc_with_roots(
    obelisk_rt_context *context, uint64_t bit_width, const uint8_t *value,
    const uint8_t *unknown, const uint64_t *bit_offsets, uint64_t count,
    uint64_t *out_handle);
obelisk_rt_status obelisk_rt_v1_native_state_retain(obelisk_rt_context *context,
                                                    uint64_t handle);
// Attach precise managed-word offsets to one automatic state allocation.
// Offsets are in bits from the value plane and must be distinct 64-bit words;
// tagged inline strings remain allocation-free roots.
obelisk_rt_status obelisk_rt_v1_native_state_register_managed_roots(
    obelisk_rt_context *context, uint64_t handle, const uint64_t *bit_offsets,
    uint64_t count);
obelisk_rt_status
obelisk_rt_v1_native_state_release(obelisk_rt_context *context, uint64_t handle,
                                   uint32_t owner_reference);
obelisk_rt_status obelisk_rt_v1_native_state_load_plane(
    obelisk_rt_context *context, const uint8_t *global_plane,
    uint64_t global_bit_count, uint64_t handle, uint64_t bit_width,
    uint32_t unknown_plane, uint32_t fallback, uint8_t *out_value);
obelisk_rt_status obelisk_rt_v1_native_state_store_plane(
    obelisk_rt_context *context, uint8_t *global_plane,
    uint64_t global_bit_count, uint64_t handle, uint64_t bit_width,
    uint32_t unknown_plane, const uint8_t *value, uint8_t *out_changed);
obelisk_rt_status obelisk_rt_v1_native_state_store_continuous_plane(
    obelisk_rt_context *context, uint8_t *global_plane,
    uint64_t global_bit_count, uint64_t handle, uint64_t bit_width,
    uint32_t unknown_plane, const uint8_t *value, uint8_t *out_changed);
void obelisk_rt_v1_scheduler_notify(obelisk_rt_context *context);
// Request orderly design-wide termination. The scheduler stops selecting
// ordinary processes and pending updates, then runs every final process.
// `verbosity` is retained for SystemVerilog compatibility; diagnostic text is
// implementation-defined and this runtime currently emits none.
obelisk_rt_status obelisk_rt_v1_scheduler_finish(obelisk_rt_context *context,
                                                 uint32_t verbosity);
// Batch-mode implementation of SystemVerilog's interactive suspension task.
// With no debugger to resume the design, this requests the same orderly,
// successful final-process phase as finish while retaining the distinct ABI
// entry point and bytecode intrinsic.
obelisk_rt_status obelisk_rt_v1_scheduler_stop(obelisk_rt_context *context,
                                               uint32_t verbosity);
obelisk_rt_status obelisk_rt_v1_scheduler_fatal(obelisk_rt_context *context,
                                                uint32_t verbosity);
uint32_t
obelisk_rt_v1_scheduler_termination_requested(obelisk_rt_context *context);
uint64_t obelisk_rt_v1_scheduler_time(obelisk_rt_context *context);

// Read one packed value from the once-per-time-slot Preponed snapshot. The
// stable handle and width are compiler-resolved; automatic storage is rejected
// because its lifetime is not part of the canonical design plane.
obelisk_rt_status obelisk_rt_v1_sampled_read(
    obelisk_rt_context *context, uint64_t stable_id, uint64_t bit_width,
    uint8_t *out_value, uint8_t *out_unknown);

// Return `depth` enabled invocations before `current`, then advance the
// compiler-assigned ring when gate is true. Missing history has the IEEE
// default sampled value (X for four-state, zero for two-state).
obelisk_rt_status obelisk_rt_v1_sampled_history(
    obelisk_rt_context *context, uint64_t site_id, uint64_t bit_width,
    uint64_t depth, uint32_t four_state, uint32_t gate,
    const uint8_t *current_value, const uint8_t *current_unknown,
    uint8_t *out_value, uint8_t *out_unknown);

// Advance and read compiler-planned history shared by all call sites using an
// explicit alternate clock. `depth` is the oldest retained age, so the ring
// stores depth + 1 samples and age zero is the most recent enabled clock tick.
obelisk_rt_status obelisk_rt_v1_clocked_sample_update(
    obelisk_rt_context *context, uint64_t site_id, uint64_t bit_width,
    uint64_t depth, uint32_t four_state, uint32_t gate,
    const uint8_t *current_value, const uint8_t *current_unknown);
obelisk_rt_status obelisk_rt_v1_clocked_sample_read(
    obelisk_rt_context *context, uint64_t site_id, uint64_t bit_width,
    uint64_t depth, uint64_t age, uint32_t four_state, uint8_t *out_value,
    uint8_t *out_unknown);
obelisk_rt_status obelisk_rt_v1_scheduler_run(obelisk_rt_context *context);
obelisk_rt_status obelisk_rt_v1_scheduler_run_aot(obelisk_rt_context *context);

// Waveform dumping. Value collection is a once-per-time-slot difference over
// the canonical state planes rather than a per-transition callback, so it is
// invisible to native, bytecode, and generated writers alike. The traced set,
// its hierarchical names, and its canonical bit ranges all come from the
// design database, so a design database must be present in the execution
// descriptor. The compiler embeds waveform metadata automatically when a
// design contains these system tasks; this does not enable VPI.
//
// Selecting a nonexistent path reports INVALID_HANDLE; `$dumpvars` with no
// prior `$dumpfile` opens the IEEE default `dump.vcd`.
obelisk_rt_status obelisk_rt_v1_dump_open(obelisk_rt_context *context,
                                          const uint8_t *path,
                                          uint64_t path_size);
obelisk_rt_status
obelisk_rt_v1_dump_open_string(obelisk_rt_context *context,
                               obelisk_rt_string_v1 path);
// Declare the `$timescale` written into the header, as a decimal exponent in
// seconds (-15..0). The compiler knows the elaborated design precision and
// emits this before the first dump call. Without it the dump falls back to the
// DPI time precision, and then to the compiler's own default of 1ns.
obelisk_rt_status obelisk_rt_v1_dump_timescale(obelisk_rt_context *context,
                                               int32_t exponent);
// `levels` is the IEEE `$dumpvars` depth: zero selects every level below the
// named scope. A null or empty scope selects the design root. Repeated calls
// accumulate; the plan is materialized once, at the end of the time slot in
// which the first selection was made.
obelisk_rt_status obelisk_rt_v1_dump_vars(obelisk_rt_context *context,
                                          uint64_t levels, const uint8_t *scope,
                                          uint64_t scope_size);
// Emit every selected variable at the current time regardless of change.
obelisk_rt_status obelisk_rt_v1_dump_all(obelisk_rt_context *context);
// `$dumpoff` / `$dumpon`. Suspension emits one all-X checkpoint; resumption
// re-emits every current value.
obelisk_rt_status obelisk_rt_v1_dump_control(obelisk_rt_context *context,
                                             uint32_t enabled);
// `$dumplimit`. Zero removes the limit. Once the written size reaches the
// limit the file is closed and no further records are produced.
obelisk_rt_status obelisk_rt_v1_dump_limit(obelisk_rt_context *context,
                                           uint64_t bytes);
obelisk_rt_status obelisk_rt_v1_dump_flush(obelisk_rt_context *context);
obelisk_rt_status obelisk_rt_v1_dump_close(obelisk_rt_context *context);

typedef uint32_t obelisk_rt_fragment_code_kind;
enum { OBELISK_RT_FRAGMENT_NATIVE = 0, OBELISK_RT_FRAGMENT_BYTECODE = 1 };

typedef struct obelisk_rt_fragment_descriptor_v1 {
  obelisk_rt_handle_v1 handle;
  obelisk_rt_fragment_code_kind code_kind;
  uint32_t flags;
  union {
    obelisk_rt_native_fragment_v1 native_entry;
    obelisk_rt_bytecode_v1 bytecode;
  } code;
} obelisk_rt_fragment_descriptor_v1;

// Dispatch one immutable fragment descriptor. Native and bytecode fragments
// have identical context/frame/continuation inputs and action results.
obelisk_rt_status obelisk_rt_v1_fragment_execute(
    const obelisk_rt_fragment_descriptor_v1 *descriptor,
    obelisk_rt_context *context, void *frame, uint64_t frame_size,
    uint32_t continuation, obelisk_rt_fragment_action_v1 *out_action);

// Test and differential-execution entry point for bytecode fragments. A
// nonzero instruction_limit bounds this invocation and reports
// OBELISK_RT_STEP_LIMIT when exhausted.
obelisk_rt_status obelisk_rt_v1_bytecode_execute_bounded(
    const obelisk_rt_fragment_descriptor_v1 *descriptor,
    obelisk_rt_context *context, void *frame, uint64_t frame_size,
    uint32_t continuation, uint64_t instruction_limit,
    obelisk_rt_fragment_action_v1 *out_action);

typedef uint32_t obelisk_rt_arg_kind;
enum {
  OBELISK_RT_ARG_EMPTY = 0,
  OBELISK_RT_ARG_LOGIC = 1,
  OBELISK_RT_ARG_STRING = 2,
  OBELISK_RT_ARG_REAL = 3,
  OBELISK_RT_ARG_TIME = 4,
  // data points to one obelisk_rt_string_v1 word; size must be zero.
  OBELISK_RT_ARG_MANAGED_STRING = 5,
  // data points to one obelisk_rt_object_v1 pointer word naming a sequential
  // container; size must be zero.
  OBELISK_RT_ARG_MANAGED_CONTAINER = 6
};

typedef uint32_t obelisk_rt_arg_flags;
enum {
  OBELISK_RT_ARG_SIGNED = 1u << 0,
  OBELISK_RT_ARG_FORMAT_STRING = 1u << 1
};

// LOGIC: size is the bit width, data points to little-endian uint64_t value
// words, and unknown points to matching unknown words (or is null for known
// data). STRING: size is the byte count and data points to those bytes. REAL
// and TIME point to one double or uint64_t respectively and ignore unknown.
typedef struct obelisk_rt_arg_v1 {
  obelisk_rt_arg_kind kind;
  obelisk_rt_arg_flags flags;
  uint64_t size;
  const void *data;
  const uint64_t *unknown;
} obelisk_rt_arg_v1;

typedef uint32_t obelisk_rt_radix;
enum {
  OBELISK_RT_RADIX_BINARY = 2,
  OBELISK_RT_RADIX_OCTAL = 8,
  OBELISK_RT_RADIX_DECIMAL = 10,
  OBELISK_RT_RADIX_HEX = 16
};

typedef struct obelisk_rt_format_env_v1 {
  const char *scope;
  uint64_t scope_size;
  const char *library_cell;
  uint64_t library_cell_size;
  uint32_t time_width;
  // Decimal exponent, in seconds, of one design-precision tick. %t needs it to
  // rescale when $timeformat has overridden the display units.
  int32_t time_precision;
  const char *time_suffix;
  uint64_t time_suffix_size;
  uint64_t time_multiplier;
} obelisk_rt_format_env_v1;

// $dist_* distribution selector. UNIFORM takes (start, end), NORMAL takes
// (mean, standard deviation), ERLANG takes (k, mean), and the rest take a
// single parameter: a mean for EXPONENTIAL and POISSON, degrees of freedom
// for CHI_SQUARE and T.
typedef uint32_t obelisk_rt_distribution;
enum {
  OBELISK_RT_DISTRIBUTION_UNIFORM = 0,
  OBELISK_RT_DISTRIBUTION_NORMAL = 1,
  OBELISK_RT_DISTRIBUTION_EXPONENTIAL = 2,
  OBELISK_RT_DISTRIBUTION_POISSON = 3,
  OBELISK_RT_DISTRIBUTION_CHI_SQUARE = 4,
  OBELISK_RT_DISTRIBUTION_T = 5,
  OBELISK_RT_DISTRIBUTION_ERLANG = 6
};

typedef uint32_t obelisk_rt_seek_origin;
enum {
  OBELISK_RT_SEEK_SET = 0,
  OBELISK_RT_SEEK_CUR = 1,
  OBELISK_RT_SEEK_END = 2
};

// PCG-XSH-RR stream state. The increment is always odd for runtime-created
// streams. Both words are explicit so generated code can snapshot and step an
// object-local stream without a runtime call.
typedef struct obelisk_rt_random_state_v1 {
  uint64_t state;
  uint64_t increment;
} obelisk_rt_random_state_v1;

// Versioned stack program executed by the constrained-random fallback. The
// byte representation is explicitly little-endian and does not use these C
// types as an in-memory wire format. Version 1 has a 24-byte header followed by
// instruction_count fixed-width 16-byte instructions. Version 2 has a 32-byte
// header, also uses 16-byte instructions, and follows them with a little-endian
// u64 literal-word pool. Its header adds literal_word_count and a zero reserved
// u32. Its instructions contain opcode:u8, flags:u8, reserved:u16, width:u32,
// operand:u32, and auxiliary:u32. PUSH_LITERAL auxiliary is a word-pool index;
// END_SOFT auxiliary is its priority; every other auxiliary is zero. This
// allows arbitrary-width bit-vector expressions without changing the compact
// version-1 runtime fast path. END instructions use
// operand as the disabled constraint-block bit (or the unmasked sentinel).
// END_SOFT uses the v1 immediate or v2 auxiliary as a contiguous, zero-based
// priority where larger values have higher priority; END_HARD requires that
// field to be zero. When
// HAS_SOLVE_BEFORE is set, the instructions are followed by a little-endian
// u32 edge count and fixed-width 24-byte edge records. Each record contains a
// u64 before-property mask, a u64 after-property mask, a u32 constraint-block
// bit (or the unmasked sentinel), and a zero u32 reserved field. When HAS_DIST
// is set, that optional edge section is followed by a u32 group count, a u32
// record count, and fixed-width 48-byte weighted-range records. A record holds
// group/block/target metadata, a biased lower bound and cardinality, an exact
// integer normalization coefficient, and the capture containing its weight.
// When HAS_DOMAINS is set, the preceding optional sections are followed by a
// u32 group count, a u32 record count, and fixed-width 32-byte domain-pattern
// records. Each group describes one complete, non-overlapping packed subfield.
// Its disjoint records contain group/target metadata and a field-local
// mask/value pair; values matching any pattern belong to the subfield's
// semantic domain.
#define OBELISK_RT_RANDOM_PROGRAM_MAGIC UINT32_C(0x3152444f)
#define OBELISK_RT_RANDOM_PROGRAM_VERSION_V1 UINT16_C(1)
#define OBELISK_RT_RANDOM_PROGRAM_VERSION_V2 UINT16_C(2)
#define OBELISK_RT_RANDOM_PROGRAM_VERSION OBELISK_RT_RANDOM_PROGRAM_VERSION_V1
#define OBELISK_RT_RANDOM_PROGRAM_HEADER_SIZE UINT16_C(24)
#define OBELISK_RT_RANDOM_INSTRUCTION_SIZE UINT16_C(16)
#define OBELISK_RT_RANDOM_PROGRAM_HEADER_SIZE_V2 UINT16_C(32)
#define OBELISK_RT_RANDOM_INSTRUCTION_SIZE_V2 UINT16_C(16)
#define OBELISK_RT_RANDOM_PROGRAM_HAS_SOFT UINT32_C(1)
#define OBELISK_RT_RANDOM_PROGRAM_HAS_SOLVE_BEFORE UINT32_C(2)
#define OBELISK_RT_RANDOM_PROGRAM_HAS_DIST UINT32_C(4)
#define OBELISK_RT_RANDOM_PROGRAM_HAS_DOMAINS UINT32_C(8)
#define OBELISK_RT_RANDOM_SOLVE_EDGE_HEADER_SIZE UINT16_C(4)
#define OBELISK_RT_RANDOM_SOLVE_EDGE_SIZE UINT16_C(24)
#define OBELISK_RT_RANDOM_DIST_HEADER_SIZE UINT16_C(8)
#define OBELISK_RT_RANDOM_DIST_RECORD_SIZE UINT16_C(48)
#define OBELISK_RT_RANDOM_DIST_WEIGHT_SIGNED UINT32_C(1)
#define OBELISK_RT_RANDOM_DIST_TARGET_SIGNED UINT32_C(2)
#define OBELISK_RT_RANDOM_DOMAIN_HEADER_SIZE UINT16_C(8)
#define OBELISK_RT_RANDOM_DOMAIN_RECORD_SIZE UINT16_C(32)
#define OBELISK_RT_RANDOM_INSTRUCTION_SIGNED UINT8_C(1)
#define OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1 UINT32_MAX

typedef enum obelisk_rt_random_opcode_v1 {
  OBELISK_RT_RANDOM_PUSH_VARIABLE_V1 = 1,
  OBELISK_RT_RANDOM_PUSH_CAPTURE_V1 = 2,
  OBELISK_RT_RANDOM_CAST_V1 = 3,
  OBELISK_RT_RANDOM_POS_V1 = 4,
  OBELISK_RT_RANDOM_NEG_V1 = 5,
  OBELISK_RT_RANDOM_BIT_NOT_V1 = 6,
  OBELISK_RT_RANDOM_REDUCE_AND_V1 = 7,
  OBELISK_RT_RANDOM_REDUCE_OR_V1 = 8,
  OBELISK_RT_RANDOM_REDUCE_XOR_V1 = 9,
  OBELISK_RT_RANDOM_REDUCE_NAND_V1 = 10,
  OBELISK_RT_RANDOM_REDUCE_NOR_V1 = 11,
  OBELISK_RT_RANDOM_REDUCE_XNOR_V1 = 12,
  OBELISK_RT_RANDOM_LOGICAL_NOT_V1 = 13,
  OBELISK_RT_RANDOM_ADD_V1 = 14,
  OBELISK_RT_RANDOM_SUB_V1 = 15,
  OBELISK_RT_RANDOM_MUL_V1 = 16,
  OBELISK_RT_RANDOM_BIT_AND_V1 = 17,
  OBELISK_RT_RANDOM_BIT_OR_V1 = 18,
  OBELISK_RT_RANDOM_BIT_XOR_V1 = 19,
  OBELISK_RT_RANDOM_BIT_XNOR_V1 = 20,
  OBELISK_RT_RANDOM_EQ_V1 = 21,
  OBELISK_RT_RANDOM_NE_V1 = 22,
  OBELISK_RT_RANDOM_GE_V1 = 23,
  OBELISK_RT_RANDOM_GT_V1 = 24,
  OBELISK_RT_RANDOM_LE_V1 = 25,
  OBELISK_RT_RANDOM_LT_V1 = 26,
  OBELISK_RT_RANDOM_LOGICAL_AND_V1 = 27,
  OBELISK_RT_RANDOM_LOGICAL_OR_V1 = 28,
  OBELISK_RT_RANDOM_LOGICAL_IMPLIES_V1 = 29,
  OBELISK_RT_RANDOM_LOGICAL_EQUIV_V1 = 30,
  OBELISK_RT_RANDOM_SELECT_V1 = 31,
  OBELISK_RT_RANDOM_END_HARD_V1 = 32,
  OBELISK_RT_RANDOM_END_SOFT_V1 = 33,
  OBELISK_RT_RANDOM_PUSH_LITERAL_V1 = 34,
  OBELISK_RT_RANDOM_DIV_V1 = 35,
  OBELISK_RT_RANDOM_MOD_V1 = 36,
  OBELISK_RT_RANDOM_SHIFT_LEFT_V1 = 37,
  OBELISK_RT_RANDOM_SHIFT_RIGHT_V1 = 38,
  OBELISK_RT_RANDOM_SHIFT_RIGHT_ARITH_V1 = 39,
  OBELISK_RT_RANDOM_POWER_V1 = 40
} obelisk_rt_random_opcode_v1;

// Context and error handling. Operations on one live context may be called
// concurrently; last_error is isolated per calling thread. The caller must
// keep the context alive until all operations finish and must not race destroy
// with another runtime call.
obelisk_rt_status
obelisk_rt_v1_context_create(obelisk_rt_context **out_context);
obelisk_rt_status
obelisk_rt_v1_context_configure_argv(obelisk_rt_context *context, int argc,
                                     const char *const *argv);
obelisk_rt_status obelisk_rt_v1_context_seed(obelisk_rt_context *context,
                                             uint64_t seed);
obelisk_rt_status
obelisk_rt_v1_covergroup_create(obelisk_rt_context *context, uint64_t type_id,
                                const uint64_t *coverpoint_bins,
                                uint64_t coverpoint_count,
                                obelisk_rt_covergroup_v1 *out_handle);
obelisk_rt_status
obelisk_rt_v1_covergroup_set_enabled(obelisk_rt_context *context,
                                     obelisk_rt_covergroup_v1 handle,
                                     uint32_t enabled);
obelisk_rt_status
obelisk_rt_v1_covergroup_sample_enabled(obelisk_rt_context *context,
                                        obelisk_rt_covergroup_v1 handle,
                                        uint32_t *out_enabled);
obelisk_rt_status
obelisk_rt_v1_covergroup_bin_hit(obelisk_rt_context *context,
                                 obelisk_rt_covergroup_v1 handle,
                                 uint32_t coverpoint, uint32_t bin);
obelisk_rt_status
obelisk_rt_v1_covergroup_sample(obelisk_rt_context *context,
                                obelisk_rt_covergroup_v1 handle,
                                const uint8_t *hits, uint64_t hit_count);
obelisk_rt_status obelisk_rt_v1_covergroup_instance_query(
    obelisk_rt_context *context, obelisk_rt_covergroup_v1 handle,
    double *out_percentage, int32_t *out_covered, int32_t *out_total);
obelisk_rt_status obelisk_rt_v1_covergroup_type_query(
    obelisk_rt_context *context, uint64_t type_id,
    const uint64_t *coverpoint_bins, uint64_t coverpoint_count,
    double *out_percentage, int32_t *out_covered, int32_t *out_total);
obelisk_rt_status obelisk_rt_v1_random_next(obelisk_rt_context *context,
                                            uint64_t *out_value);
obelisk_rt_status obelisk_rt_v1_random_seed(obelisk_rt_context *context,
                                            uint64_t seed);
obelisk_rt_status obelisk_rt_v1_random_bounded(obelisk_rt_context *context,
                                               uint64_t bound,
                                               uint64_t *out_value);
// $dist_* draws (IEEE 1800 20.15 and normative Annex N). The explicit seed is
// the source inout state and `out_next_seed` returns its value after the draw.
// `first` and `second` carry shape parameters in source order; distributions
// taking one parameter ignore `second`.
obelisk_rt_status
obelisk_rt_v1_random_distribution(obelisk_rt_context *context,
                                  obelisk_rt_distribution distribution,
                                  int32_t seed, int32_t first, int32_t second,
                                  int32_t *out_value, int32_t *out_next_seed);
// Advance a keyed randc permutation over exactly 2^width values. Widths 1..32
// are supported. The caller owns key/position storage and explicitly rekeys
// whenever the returned position wraps to zero.
obelisk_rt_status obelisk_rt_v1_random_cycle_next(
    uint64_t key, uint64_t position, uint32_t width,
    uint64_t *out_next_position, uint64_t *out_value);
// Execute a compiler-produced residual constraint program. `start` selects
// the first aggregate assignment and `max_attempts` is a deterministic work
// bound. A successful solve writes both outputs. Exhaustion is reported by
// out_success == 0 and is not a runtime API error.
obelisk_rt_status obelisk_rt_v1_random_solve(
    obelisk_rt_context *context, const uint8_t *program, uint64_t program_size,
    uint64_t start, uint64_t max_attempts, const uint64_t *captures,
    uint64_t capture_count, uint64_t *out_assignment, uint32_t *out_success);
// As above, but enumerate only bits selected by `mutable_mask`; every other
// aggregate bit retains its value from `start` in every candidate.
obelisk_rt_status obelisk_rt_v1_random_solve_masked(
    obelisk_rt_context *context, const uint8_t *program, uint64_t program_size,
    uint64_t start, uint64_t mutable_mask, uint64_t max_attempts,
    const uint64_t *captures, uint64_t capture_count, uint64_t *out_assignment,
    uint32_t *out_success);
// As above, with a disabled-bit mask for compiler-assigned constraint blocks.
// Program constraints marked as unmasked remain active.
obelisk_rt_status obelisk_rt_v1_random_solve_modes(
    obelisk_rt_context *context, const uint8_t *program, uint64_t program_size,
    uint64_t start, uint64_t mutable_mask, uint64_t constraint_mask,
    uint64_t max_attempts, const uint64_t *captures, uint64_t capture_count,
    uint64_t *out_assignment, uint32_t *out_success);
// Stateful form used by object randomization. Ordered fallback sampling draws
// unbiased bounded indices from the supplied PCG stream and returns its
// advanced state. Non-ordered programs leave the state unchanged. The
// increment must be odd. Stateless solve entry points reject programs with an
// active solve-before edge because they cannot preserve its distribution.
obelisk_rt_status obelisk_rt_v1_random_solve_modes_state(
    obelisk_rt_context *context, const uint8_t *program, uint64_t program_size,
    uint64_t start, uint64_t mutable_mask, uint64_t constraint_mask,
    uint64_t max_attempts, uint64_t rng_state, uint64_t rng_increment,
    const uint64_t *captures, uint64_t capture_count, uint64_t *out_assignment,
    uint32_t *out_success, uint64_t *out_rng_state);
// Arbitrary-width stateful residual solve. Assignment and capture values use
// canonical little-endian 64-bit words. `assignment_word_count` must equal
// ceil(program.aggregate_width / 64). Capture words are concatenated in
// capture-index order. `capture_widths` identifies their source-register
// boundaries and must match the widths carried by the version-2 program.
// This pointer/count-only ABI is directly representable in WebAssembly linear
// memory and deliberately exposes no host big-integer representation.
obelisk_rt_status obelisk_rt_v1_random_solve_wide_modes_state(
    obelisk_rt_context *context, const uint8_t *program, uint64_t program_size,
    const uint64_t *start_words, const uint64_t *mutable_mask_words,
    uint64_t assignment_word_count, uint64_t constraint_mask,
    uint64_t max_attempts, uint64_t rng_state, uint64_t rng_increment,
    const uint64_t *capture_words, uint64_t capture_word_count,
    const uint32_t *capture_widths, uint64_t capture_count,
    uint64_t *out_assignment_words, uint32_t *out_success,
    uint64_t *out_rng_state);
obelisk_rt_status
obelisk_rt_v1_random_get_state(obelisk_rt_context *context,
                               obelisk_rt_random_state_v1 *out_state);
obelisk_rt_status
obelisk_rt_v1_random_set_state(obelisk_rt_context *context,
                               const obelisk_rt_random_state_v1 *state);
void obelisk_rt_v1_random_state_seed(obelisk_rt_random_state_v1 *state,
                                     uint64_t seed, uint64_t sequence);
uint32_t obelisk_rt_v1_random_state_next32(obelisk_rt_random_state_v1 *state);
uint64_t obelisk_rt_v1_random_state_next64(obelisk_rt_random_state_v1 *state);
obelisk_rt_status
obelisk_rt_v1_random_state_bounded(obelisk_rt_random_state_v1 *state,
                                   uint64_t bound, uint64_t *out_value);
uint32_t obelisk_rt_v1_import_id(const uint8_t *symbol, uint64_t symbol_size);
obelisk_rt_status obelisk_rt_v1_context_register_import(
    obelisk_rt_context *context, uint32_t import_id,
    obelisk_rt_import_callback_v1 callback, void *user_data);
// Generated simulators use the signature form so malformed bytecode cannot
// dispatch a register layout to an incompatible native thunk. A zero
// signature is reserved for the generic host-registration API above.
obelisk_rt_status obelisk_rt_v1_context_register_import_signature(
    obelisk_rt_context *context, uint32_t import_id, uint64_t abi_signature,
    obelisk_rt_import_callback_v1 callback, void *user_data);
obelisk_rt_status obelisk_rt_v1_import_call(
    obelisk_rt_context *context, const obelisk_rt_import_site_v1 *site,
    const obelisk_rt_import_input_v1 *inputs, uint32_t input_count,
    obelisk_rt_import_output_v1 *outputs, uint32_t output_count);
void obelisk_rt_v1_context_destroy(obelisk_rt_context *context);
const char *obelisk_rt_v1_status_string(obelisk_rt_status status);
void obelisk_rt_v1_buffer_release(obelisk_rt_buffer_v1 *buffer);
obelisk_rt_status obelisk_rt_v1_last_error(obelisk_rt_context *context,
                                           obelisk_rt_buffer_v1 *out_message);

// Formatting and display. Input arrays remain caller-owned and must not be
// mutated until the call returns. format() consumes every supplied argument.
// display() treats STRING arguments carrying FORMAT_STRING as format strings
// that consume following items; other items use the requested default radix.
obelisk_rt_status
obelisk_rt_v1_format(obelisk_rt_context *context, const char *format,
                     uint64_t format_size, const obelisk_rt_arg_v1 *arguments,
                     uint64_t argument_count,
                     const obelisk_rt_format_env_v1 *environment,
                     obelisk_rt_buffer_v1 *out_buffer);
// $timeformat (IEEE 1800 20.4.2). `units` is the decimal exponent in seconds
// of the unit %t reports in; `fraction_digits` and `width` are its precision
// and minimum field width, the latter covering the suffix too. The override
// applies to every later %t in the design.
obelisk_rt_status obelisk_rt_v1_time_format(obelisk_rt_context *context,
                                            int32_t units,
                                            uint32_t fraction_digits,
                                            const char *suffix,
                                            uint64_t suffix_size,
                                            uint32_t width);
obelisk_rt_status
obelisk_rt_v1_display(obelisk_rt_context *context, uint32_t descriptor,
                      uint32_t append_newline, obelisk_rt_radix default_radix,
                      const obelisk_rt_arg_v1 *items, uint64_t item_count,
                      const obelisk_rt_format_env_v1 *environment);

// File channels. Calls using one context are serialized around each libc stream
// operation. Descriptor close/reuse must be coordinated by the caller so no
// thread begins an operation after another thread closes that descriptor. The
// one-argument fopen form produces an MCD; the mode-taking
// form produces a descriptor with bit 31 set. A descriptor of zero passed to
// flush means all runtime-owned streams plus stdout.
obelisk_rt_status obelisk_rt_v1_file_open_mcd(obelisk_rt_context *context,
                                              const char *path,
                                              uint64_t path_size,
                                              uint32_t *out_descriptor);
obelisk_rt_status obelisk_rt_v1_file_open(obelisk_rt_context *context,
                                          const char *path, uint64_t path_size,
                                          const char *mode, uint64_t mode_size,
                                          uint32_t *out_descriptor);
obelisk_rt_status
obelisk_rt_v1_file_open_string_mcd(obelisk_rt_context *context,
                                   obelisk_rt_string_v1 path,
                                   uint32_t *out_descriptor);
obelisk_rt_status obelisk_rt_v1_file_open_string(obelisk_rt_context *context,
                                                 obelisk_rt_string_v1 path,
                                                 obelisk_rt_string_v1 mode,
                                                 uint32_t *out_descriptor);
obelisk_rt_status obelisk_rt_v1_file_close(obelisk_rt_context *context,
                                           uint32_t descriptor);
obelisk_rt_status obelisk_rt_v1_file_flush(obelisk_rt_context *context,
                                           uint32_t descriptor);
obelisk_rt_status obelisk_rt_v1_file_write(obelisk_rt_context *context,
                                           uint32_t descriptor,
                                           const void *data, uint64_t size,
                                           uint64_t *out_written);
obelisk_rt_status obelisk_rt_v1_file_read(obelisk_rt_context *context,
                                          uint32_t descriptor, void *data,
                                          uint64_t size, uint64_t *out_read);
obelisk_rt_status obelisk_rt_v1_file_getc(obelisk_rt_context *context,
                                          uint32_t descriptor,
                                          uint8_t *out_byte);
obelisk_rt_status obelisk_rt_v1_file_ungetc(obelisk_rt_context *context,
                                            uint32_t descriptor, uint8_t byte);
obelisk_rt_status obelisk_rt_v1_file_getline(obelisk_rt_context *context,
                                             uint32_t descriptor,
                                             uint64_t max_bytes,
                                             obelisk_rt_buffer_v1 *out_line);
obelisk_rt_status obelisk_rt_v1_file_getline_string(
    obelisk_rt_context *context, obelisk_rt_gc_lane_v1 *lane,
    uint32_t descriptor, obelisk_rt_string_v1 *out_string, uint32_t *out_count);
// Consume one formatted field at the descriptor's current position. Disabled
// calls leave the stream untouched so lowering can stop after a mismatch.
obelisk_rt_status obelisk_rt_v1_file_scan_field(
    obelisk_rt_context *context, obelisk_rt_gc_lane_v1 *lane,
    uint32_t descriptor, uint32_t enabled, const char *prefix,
    uint64_t prefix_size, uint32_t specifier,
    obelisk_rt_string_v1 *out_field, uint32_t *out_ok, uint32_t *out_eof);
obelisk_rt_status obelisk_rt_v1_file_eof(obelisk_rt_context *context,
                                         uint32_t descriptor,
                                         uint32_t *out_is_eof);
obelisk_rt_status obelisk_rt_v1_file_error(obelisk_rt_context *context,
                                           uint32_t descriptor,
                                           int32_t *out_error_code,
                                           obelisk_rt_buffer_v1 *out_message);
// $ferror form: the message lands in a managed string instead of a buffer the
// caller has to release. A descriptor with no pending error yields code zero
// and an empty string.
obelisk_rt_status obelisk_rt_v1_file_error_string(
    obelisk_rt_context *context, obelisk_rt_gc_lane_v1 *lane,
    uint32_t descriptor, obelisk_rt_string_v1 *out_message,
    int32_t *out_error_code);
// Command-line plusargs. Arguments introduced by '+' are recorded by
// configure_argv with that prefix stripped; both queries match the caller's
// text as a prefix of a recorded argument, taking the first that matches.
// value() additionally returns the matched argument's remaining text, which
// the caller converts according to the format specifier it stripped off.
obelisk_rt_status obelisk_rt_v1_plusarg_test(obelisk_rt_context *context,
                                             obelisk_rt_string_v1 name,
                                             uint32_t *out_found);
obelisk_rt_status obelisk_rt_v1_plusarg_value(obelisk_rt_context *context,
                                              obelisk_rt_gc_lane_v1 *lane,
                                              obelisk_rt_string_v1 prefix,
                                              obelisk_rt_string_v1 *out_tail,
                                              uint32_t *out_found);

obelisk_rt_status obelisk_rt_v1_file_seek(obelisk_rt_context *context,
                                          uint32_t descriptor, int64_t offset,
                                          obelisk_rt_seek_origin origin);
obelisk_rt_status obelisk_rt_v1_file_tell(obelisk_rt_context *context,
                                          uint32_t descriptor,
                                          int64_t *out_offset);
obelisk_rt_status obelisk_rt_v1_file_rewind(obelisk_rt_context *context,
                                            uint32_t descriptor);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // OBELISK_RUNTIME_RUNTIME_H
