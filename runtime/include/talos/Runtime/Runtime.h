//===- Runtime.h - Talos native runtime C ABI -----------------*- C -*-===//

#ifndef TALOS_RUNTIME_RUNTIME_H
#define TALOS_RUNTIME_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// All in-tree runtime records and generated artifacts use this single
// prototype schema version. Names ending in _v1 identify the same schema.
// This is not a backward-compatibility promise: generated code, bytecode, and
// libtalos_rt must come from the same Talos source revision.
#define TALOS_RT_VERSION 1u

typedef struct talos_rt_context talos_rt_context;
typedef struct talos_rt_gc_lane_v1 talos_rt_gc_lane_v1;
typedef struct talos_rt_object_v1 talos_rt_object_v1;
typedef struct talos_rt_random_state_v1 talos_rt_random_state_v1;
typedef struct talos_rt_random_graph_v1 talos_rt_random_graph_v1;

// Functional-coverage handles are context-local monotonically allocated IDs.
// Zero is the language null value and never names a live instance.
typedef uint64_t talos_rt_covergroup_v1;

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
typedef uint64_t talos_rt_managed_word_v1;
typedef talos_rt_managed_word_v1 talos_rt_string_v1;

typedef int32_t talos_rt_status;
#define TALOS_RT_OK INT32_C(0)
#define TALOS_RT_EOF INT32_C(1)
#define TALOS_RT_INVALID_ARGUMENT INT32_C(2)
#define TALOS_RT_INVALID_HANDLE INT32_C(3)
#define TALOS_RT_IO_ERROR INT32_C(4)
#define TALOS_RT_OUT_OF_MEMORY INT32_C(5)
#define TALOS_RT_OUT_OF_RESOURCES INT32_C(6)
#define TALOS_RT_FORMAT_ERROR INT32_C(7)
#define TALOS_RT_ARGUMENT_MISMATCH INT32_C(8)
#define TALOS_RT_INVALID_BYTECODE INT32_C(9)
#define TALOS_RT_STEP_LIMIT INT32_C(10)
#define TALOS_RT_LAYOUT_MISMATCH INT32_C(11)
#define TALOS_RT_INVALID_CONTINUATION INT32_C(12)
#define TALOS_RT_TIER_UNAVAILABLE INT32_C(13)
#define TALOS_RT_INVALID_LIFECYCLE INT32_C(14)
#define TALOS_RT_INVALID_FRAME INT32_C(15)
#define TALOS_RT_INVALID_DESIGN INT32_C(16)
#define TALOS_RT_PERMISSION_DENIED INT32_C(17)
#define TALOS_RT_DPI_DISABLE_UNSUPPORTED INT32_C(18)
#define TALOS_RT_FATAL INT32_C(19)
// Generated run_until reached an enabled runtime synchronization boundary.
// Unlike TIER_UNAVAILABLE, this is a resumable handoff and must not deopt the
// native schedule permanently.
#define TALOS_RT_AOT_CHECKPOINT INT32_C(20)
// Generated run_until stopped before a proven future runtime deadline. The
// runtime advances to that deadline, drains the slot, and resumes the saved
// periodic phase. Same-slot branch checkpoints continue to use 20.
#define TALOS_RT_AOT_TIMED_CHECKPOINT INT32_C(21)
// Internal generated-branch handoff. The generated run loop consumes this
// status into an exact callback transaction before returning ordinary status
// 20 to the runtime scheduler.
#define TALOS_RT_AOT_GENERATED_CHECKPOINT INT32_C(22)

// Combine two IEEE 1800-2017 Figure 28-2 strength ranges. Bit positions 0..14
// represent supply0..highz..supply1. Generated native code and bytecode use
// this shared primitive so their ambiguous-strength resolution is identical.
uint16_t talos_rt_v1_strength_resolve(uint16_t lhs, uint16_t rhs);
uint16_t talos_rt_v1_strength_resolve_kind(uint16_t lhs, uint16_t rhs,
                                             uint32_t resolution);

// Tokens in an IEEE 1800 $readmemb/$readmemh input stream. Address tokens are
// always hexadecimal; data tokens use the radix selected by the system task.
typedef uint32_t talos_rt_readmem_token_kind_v1;
enum {
  TALOS_RT_READMEM_EOF = 0,
  TALOS_RT_READMEM_DATA = 1,
  TALOS_RT_READMEM_ADDRESS = 2
};

// Managed SystemVerilog value ABI. Object handles are nullable pointers into
// the context-owned, non-moving heap. Collector and synchronization metadata
// live outside the language-visible object. Class objects retain an immutable
// class descriptor in their first word; runtime-owned variable-sized values
// use an opaque runtime descriptor word and never pass through class
// validation.
typedef uint32_t talos_rt_managed_kind_v1;
enum {
  TALOS_RT_MANAGED_INVALID = 0,
  TALOS_RT_MANAGED_CLASS = 1,
  TALOS_RT_MANAGED_STRING = 2,
  TALOS_RT_MANAGED_CONTAINER = 3,
  TALOS_RT_MANAGED_BUFFER = 4,
  TALOS_RT_MANAGED_KEY_BLOB = 5,
  TALOS_RT_MANAGED_REFERENCE_PATH = 6
};

typedef uint32_t talos_rt_trace_kind;
enum {
  TALOS_RT_TRACE_STRONG = 1,
  TALOS_RT_TRACE_WEAK = 2,
  TALOS_RT_TRACE_EMBEDDED = 3
};

typedef uint32_t talos_rt_managed_slot_kind_v1;
enum {
  TALOS_RT_MANAGED_SLOT_INVALID = 0,
  TALOS_RT_MANAGED_SLOT_CLASS = 1,
  TALOS_RT_MANAGED_SLOT_STRING = 2,
  TALOS_RT_MANAGED_SLOT_CONTAINER = 3,
  TALOS_RT_MANAGED_SLOT_REFERENCE_PATH = 4,
  TALOS_RT_MANAGED_SLOT_MAILBOX = 5,
  TALOS_RT_MANAGED_SLOT_SEMAPHORE = 6,
  // The low bits of a candidate slot kind are an
  // TALOS_RT_MANAGED_ROOT_KIND_* mask. Candidate slots belong to an
  // overlapping source union and may contain ordinary bits; they are traced
  // only when the current word names a live object of an allowed kind.
  TALOS_RT_MANAGED_SLOT_CANDIDATE = UINT32_C(0x80000000)
};

typedef uint32_t talos_rt_managed_root_kind_mask_v1;
enum {
  TALOS_RT_MANAGED_ROOT_KIND_CLASS = 1u << 0,
  TALOS_RT_MANAGED_ROOT_KIND_STRING = 1u << 1,
  TALOS_RT_MANAGED_ROOT_KIND_CONTAINER = 1u << 2,
  TALOS_RT_MANAGED_ROOT_KIND_REFERENCE_PATH = 1u << 3,
  TALOS_RT_MANAGED_ROOT_KIND_ALL = TALOS_RT_MANAGED_ROOT_KIND_CLASS |
                                     TALOS_RT_MANAGED_ROOT_KIND_STRING |
                                     TALOS_RT_MANAGED_ROOT_KIND_CONTAINER |
                                     TALOS_RT_MANAGED_ROOT_KIND_REFERENCE_PATH
};

struct talos_rt_trace_layout_v1;

// One entry describes either repeated object-handle slots or repeated embedded
// values. For scalar fields count is one and stride may be zero. Embedded
// entries recurse through child_layout; handle entries require it to be null.
typedef struct talos_rt_trace_entry_v1 {
  uint64_t offset;
  uint64_t stride;
  uint64_t count;
  talos_rt_trace_kind kind;
  talos_rt_managed_slot_kind_v1 slot_kind;
  const struct talos_rt_trace_layout_v1 *child_layout;
} talos_rt_trace_entry_v1;

typedef struct talos_rt_trace_layout_v1 {
  uint32_t version;
  uint32_t reserved;
  uint64_t size;
  uint64_t alignment;
  const talos_rt_trace_entry_v1 *entries;
  uint64_t entry_count;
} talos_rt_trace_layout_v1;

typedef struct talos_rt_method_argument_v1 {
  const void *data;
  uint64_t size;
} talos_rt_method_argument_v1;

typedef talos_rt_status (*talos_rt_native_method_v1)(
    talos_rt_context *context, talos_rt_gc_lane_v1 *lane,
    talos_rt_object_v1 *receiver,
    const talos_rt_method_argument_v1 *arguments, uint32_t argument_count,
    void *result, uint64_t result_size);

#define TALOS_RT_METHOD_TASK (UINT32_C(1) << 0)
#define TALOS_RT_METHOD_PURE (UINT32_C(1) << 1)
#define TALOS_RT_METHOD_NO_BYTECODE UINT32_MAX

typedef struct talos_rt_method_descriptor_v1 {
  uint64_t signature_id;
  uint32_t flags;
  uint32_t bytecode_function;
  talos_rt_native_method_v1 native_entry;
  const void *environment;
  // Stable dispatch slot. Class arrays contain only direct declarations,
  // ordered by slot; inherited entries remain in the base descriptor.
  uint64_t slot;
} talos_rt_method_descriptor_v1;

// An interface introduced by this class. Signatures are indexed by the
// declaring interface's stable method ordinal. Inherited bindings remain in
// the base descriptor and resolve against the dynamic class's method index.
typedef struct talos_rt_interface_descriptor_v1 {
  uint64_t interface_id;
  const uint64_t *method_signatures;
  uint64_t method_count;
  // Shared interface inheritance graph; bases contain direct parents only.
  const struct talos_rt_interface_descriptor_v1 *const *bases;
  uint64_t base_count;
} talos_rt_interface_descriptor_v1;

// One direct rand class-handle edge declared by a class. The mode word and
// handle are read from the same object; a set mode bit disables traversal.
// Inherited edges remain in the base descriptor and are visited through the
// descriptor chain rather than duplicated in every derived descriptor.
typedef struct talos_rt_random_edge_v1 {
  uint64_t handle_offset;
  uint64_t mode_offset;
  uint64_t mode_mask;
} talos_rt_random_edge_v1;

// One direct packed rand/randc property declared by a class. Four-state
// storage has an equally sized unknown plane immediately after the value
// plane. Randc offsets are UINT64_MAX for ordinary rand variables.
#define TALOS_RT_RANDOM_VARIABLE_FOUR_STATE (UINT32_C(1) << 0)
#define TALOS_RT_RANDOM_VARIABLE_SIGNED (UINT32_C(1) << 1)
#define TALOS_RT_RANDOM_VARIABLE_RANDC (UINT32_C(1) << 2)
typedef struct talos_rt_random_variable_v1 {
  uint64_t value_offset;
  uint64_t mode_offset;
  uint64_t mode_mask;
  uint64_t randc_key_offset;
  uint64_t randc_position_offset;
  uint32_t bit_width;
  uint32_t flags;
} talos_rt_random_variable_v1;

// One object-relative reference used by a relocatable random-constraint
// template. Starting from a graph object, handle_offsets follows strong class
// fields to the object that owns the referenced random variable. An empty path
// denotes the starting object. The remaining fields identify that variable
// exactly in its declaring class layout.
typedef struct talos_rt_random_variable_reference_v1 {
  const uint64_t *handle_offsets;
  uint64_t handle_count;
  uint64_t value_offset;
  uint32_t bit_width;
  uint32_t flags;
} talos_rt_random_variable_reference_v1;

typedef struct talos_rt_random_layout_v1 {
  uint32_t version;
  uint32_t reserved;
  const talos_rt_random_edge_v1 *edges;
  uint64_t edge_count;
  const talos_rt_random_variable_v1 *variables;
  uint64_t variable_count;
} talos_rt_random_layout_v1;

// A byte range of a class instance, relative to the descriptor word.
typedef struct talos_rt_byte_range_v1 {
  uint64_t offset;
  uint64_t size;
} talos_rt_byte_range_v1;

#define TALOS_RT_CLASS_ABSTRACT (UINT32_C(1) << 0)
#define TALOS_RT_CLASS_INTERFACE (UINT32_C(1) << 1)
#define TALOS_RT_CLASS_FINAL (UINT32_C(1) << 2)
#define TALOS_RT_CLASS_WEAK_WRAPPER (UINT32_C(1) << 3)

typedef uint32_t talos_rt_builtin_class_kind_v1;
enum {
  TALOS_RT_BUILTIN_CLASS_NONE = 0,
  TALOS_RT_BUILTIN_CLASS_MAILBOX = 1,
  TALOS_RT_BUILTIN_CLASS_SEMAPHORE = 2
};

typedef struct talos_rt_class_descriptor_v1 {
  uint32_t version;
  uint32_t flags;
  uint64_t class_id;
  uint64_t instance_size;
  uint64_t instance_alignment;
  const struct talos_rt_class_descriptor_v1 *base;
  const talos_rt_interface_descriptor_v1 *interfaces;
  uint64_t interface_count;
  // Contains direct trace entries and embeds a nonempty base layout exactly
  // once at offset zero; other entries start after the base instance. An
  // unchanged, equal-size layout may share its pointer.
  const talos_rt_trace_layout_v1 *layout;
  const talos_rt_method_descriptor_v1 *methods;
  uint64_t method_count;
  const char *debug_name;
  uint64_t debug_name_size;
  const talos_rt_random_layout_v1 *random_layout;
  // This class's own instance ranges that a shallow copy (IEEE 1800-2023 8.12)
  // clears instead of copying, such as embedded covergroup handles. Base
  // descriptors list their own ranges.
  const talos_rt_byte_range_v1 *copy_resets;
  uint64_t copy_reset_count;
  // A direct builtin superclass is implemented by an own strong payload
  // field. User subclasses inherit this metadata through base. Upcasts keep
  // the outer object identity; builtin operations resolve this private field.
  talos_rt_builtin_class_kind_v1 builtin_base_kind;
  uint32_t builtin_base_reserved;
  uint64_t builtin_base_offset;
} talos_rt_class_descriptor_v1;

// Compiler-emitted erased element metadata. Four-state storage places an
// equally sized unknown plane immediately after the value plane. type_id is a
// stable, nonzero design identifier so pointer-free bytecode can resolve the
// native descriptor registered during context startup.
typedef uint32_t talos_rt_element_kind_v1;
enum {
  TALOS_RT_ELEMENT_BITS = 1,
  TALOS_RT_ELEMENT_LOGIC = 2,
  TALOS_RT_ELEMENT_REAL = 3,
  TALOS_RT_ELEMENT_CLASS_HANDLE = 4,
  TALOS_RT_ELEMENT_STRING = 5,
  TALOS_RT_ELEMENT_CONTAINER_HANDLE = 6,
  TALOS_RT_ELEMENT_AGGREGATE = 7,
  TALOS_RT_ELEMENT_EVENT = 8
};

#define TALOS_RT_ELEMENT_FOUR_STATE (UINT32_C(1) << 0)
#define TALOS_RT_ELEMENT_SIGNED (UINT32_C(1) << 1)

typedef struct talos_rt_element_trace_slot_v1 {
  uint64_t offset;
  talos_rt_managed_slot_kind_v1 kind;
  uint32_t reserved;
} talos_rt_element_trace_slot_v1;

#define TALOS_RT_MANAGED_ROOT_SLOT_CANDIDATE UINT32_C(1)
typedef struct talos_rt_managed_root_slot_v1 {
  uint64_t bit_offset;
  talos_rt_managed_root_kind_mask_v1 kind_mask;
  uint32_t flags;
} talos_rt_managed_root_slot_v1;

typedef struct talos_rt_element_type_v1 {
  uint32_t version;
  talos_rt_element_kind_v1 kind;
  uint64_t type_id;
  uint32_t flags;
  uint32_t reserved;
  uint64_t value_size;
  uint64_t alignment;
  uint64_t bit_width;
  const talos_rt_trace_layout_v1 *trace;
  // Optional default element value: value_size bytes of value plane followed,
  // for a four-state element, by value_size bytes of unknown plane. Null
  // means the type default of IEEE 1800-2023 Table 6-7, zero with X in every
  // four-state bit.
  const uint8_t *default_image;
} talos_rt_element_type_v1;

#define TALOS_RT_CONTAINER_PATTERN_PLAN_VERSION UINT64_C(1)
#define TALOS_RT_CONTAINER_PATTERN_PLAN_HEADER_WORDS UINT64_C(6)

// Registered descriptors and every trace-layout object reachable from them
// are immutable and must remain alive until their context is destroyed.

// Stack-owned precise root record. Push/pop must be properly nested on one
// registered lane. Keeping the record caller-owned avoids an allocation at
// every native call boundary.
typedef struct talos_rt_gc_root_v1 {
  talos_rt_object_v1 **slot;
  struct talos_rt_gc_root_v1 *previous;
  uint64_t cookie;
} talos_rt_gc_root_v1;

// Stack-owned contiguous root range. Generated activations use one range
// record and one pointer slot per live managed SSA value, independent of the
// number of roots.
typedef struct talos_rt_gc_root_range_v1 {
  talos_rt_object_v1 **slots;
  uint64_t count;
  struct talos_rt_gc_root_range_v1 *previous;
  uint64_t cookie;
} talos_rt_gc_root_range_v1;

// Tagged managed roots are separate from legacy object-pointer roots so
// native class/container ABI users remain source compatible. Immediate words
// are validated but have no outgoing heap edge.
typedef struct talos_rt_gc_managed_root_v1 {
  talos_rt_managed_word_v1 *slot;
  struct talos_rt_gc_managed_root_v1 *previous;
  uint64_t cookie;
} talos_rt_gc_managed_root_v1;

typedef struct talos_rt_gc_managed_root_range_v1 {
  talos_rt_managed_word_v1 *slots;
  uint64_t count;
  struct talos_rt_gc_managed_root_range_v1 *previous;
  uint64_t cookie;
} talos_rt_gc_managed_root_range_v1;

typedef struct talos_rt_gc_statistics_v1 {
  uint64_t live_objects;
  uint64_t live_bytes;
  uint64_t allocated_objects;
  uint64_t reclaimed_objects;
  uint64_t collection_count;
  uint64_t chunk_allocation_count;
  uint64_t large_allocation_count;
  uint64_t cached_empty_chunks;
  uint64_t next_collection_bytes;
} talos_rt_gc_statistics_v1;

typedef struct talos_rt_buffer_v1 {
  uint8_t *data;
  uint64_t size;
} talos_rt_buffer_v1;

// Stable simulator handles never contain native addresses. Generation is
// incremented when a dynamic descriptor slot is reused; static descriptors use
// generation zero.
typedef uint32_t talos_rt_descriptor_kind;
enum {
  TALOS_RT_DESCRIPTOR_INVALID = 0,
  TALOS_RT_DESCRIPTOR_SCOPE = 1,
  TALOS_RT_DESCRIPTOR_STORAGE = 2,
  TALOS_RT_DESCRIPTOR_NET = 3,
  TALOS_RT_DESCRIPTOR_DRIVER = 4,
  TALOS_RT_DESCRIPTOR_EVENT = 5,
  TALOS_RT_DESCRIPTOR_PROCESS = 6,
  TALOS_RT_DESCRIPTOR_FRAGMENT = 7,
  TALOS_RT_DESCRIPTOR_FUNCTION = 8,
  TALOS_RT_DESCRIPTOR_OBSERVER = 9,
  TALOS_RT_DESCRIPTOR_PORT = 10
};

typedef struct talos_rt_handle_v1 {
  talos_rt_descriptor_kind kind;
  uint32_t generation;
  uint64_t id;
} talos_rt_handle_v1;

// Design-wide executable metadata is deliberately split from the optional
// reflection image used by VPI and waveform dumping. Both payloads are
// immutable, pointer-free byte images; the only native pointers are this
// linker-resolved pointer/size pair. A descriptor with no reflection consumers
// has a null image.
#define TALOS_RT_EXECUTION_HAS_BYTECODE (UINT32_C(1) << 0)
#define TALOS_RT_EXECUTION_HAS_DESIGN_DATABASE (UINT32_C(1) << 1)
#define TALOS_RT_EXECUTION_VPI_READ (UINT32_C(1) << 2)
#define TALOS_RT_EXECUTION_VPI_WRITE (UINT32_C(1) << 3)
#define TALOS_RT_EXECUTION_REQUIRE_BYTECODE (UINT32_C(1) << 4)
#define TALOS_RT_EXECUTION_PREPONED_SNAPSHOT (UINT32_C(1) << 5)
// The design database is also the immutable hierarchy/type image consumed by
// waveform dumping. This grants no VPI access; the VPI permission bits remain
// the sole authority for VPI handles and reads.
#define TALOS_RT_EXECUTION_WAVEFORM_METADATA (UINT32_C(1) << 6)
// The optional v1 execution extension contains scope-specific zero-time DPI
// export descriptors.
#define TALOS_RT_EXECUTION_DPI_EXPORTS (UINT32_C(1) << 7)
// The v1 execution extension carries the canonical, pointer-free class
// bit-stream schema image. The image is validated and bound to the context's
// registered class descriptors before the first cast executes.
#define TALOS_RT_EXECUTION_CLASS_BITSTREAM (UINT32_C(1) << 8)
// The v1 execution extension carries a valid schema-only .obcov image. Runtime
// counters and run metadata are context-owned and never mutate this
// compiler-emitted blob.
#define TALOS_RT_EXECUTION_COVERAGE_SCHEMA (UINT32_C(1) << 9)
// Compiled code exposes its managed values to the collector only at GC polls
// (talos_rt_v1_gc_safepoint) and at calls that may reach one. Allocation
// records pressure but never collects or parks; the heap reclaims at polls,
// between activations, and at designated collection points.
#define TALOS_RT_EXECUTION_POLLED_COLLECTION (UINT32_C(1) << 10)
/* Value-only simulation state. Canonical unknown-plane pointers are null;
 * DPI C representations retain their declared layout and receive zero bval. */
#define TALOS_RT_EXECUTION_TWO_STATE (UINT32_C(1) << 11)

// Executable event-region ordinals. The eight PLI callback regions remain
// compiler-only until the callback ABI can populate them. Preponed is serviced
// by a slot-boundary hook rather than a process queue.
typedef uint32_t talos_rt_event_region_v1;
enum {
  TALOS_RT_REGION_ACTIVE = 0,
  TALOS_RT_REGION_INACTIVE = 1,
  TALOS_RT_REGION_NBA = 2,
  TALOS_RT_REGION_OBSERVED = 3,
  TALOS_RT_REGION_REACTIVE = 4,
  TALOS_RT_REGION_RE_INACTIVE = 5,
  TALOS_RT_REGION_RE_NBA = 6,
  TALOS_RT_REGION_POSTPONED = 7
};

// Scheduler registration flags. FINAL deliberately retains the old phase-one
// encoding. The home region is encoded directly so Observed and Postponed do
// not require future ABI changes.
#define TALOS_RT_SCHEDULE_FINAL (UINT32_C(1) << 0)
#define TALOS_RT_SCHEDULE_HOME_SHIFT 1u
#define TALOS_RT_SCHEDULE_HOME_MASK                                          \
  (UINT32_C(7) << TALOS_RT_SCHEDULE_HOME_SHIFT)
#define TALOS_RT_SCHEDULE_HOME(region)                                       \
  ((uint32_t)(region) << TALOS_RT_SCHEDULE_HOME_SHIFT)
#define TALOS_RT_SCHEDULE_INITIAL (UINT32_C(1) << 4)
#define TALOS_RT_SCHEDULE_STARTUP (UINT32_C(1) << 5)
#define TALOS_RT_SCHEDULE_DETACHED_CONTROLS (UINT32_C(1) << 6)
// A signal-resumed process with this flag runs before ordinary work already
// queued in the same event region. Unlike STARTUP, this does not change the
// process's event region or make its initial activation urgent.
#define TALOS_RT_SCHEDULE_PRIORITY_SIGNAL (UINT32_C(1) << 7)
#define TALOS_RT_SCHEDULE_ROOT (UINT32_C(1) << 8)

// The bytecode SPAWN intrinsic uses its four high flag bits for scheduler
// classifications and the remaining bits for the callee function index.
#define TALOS_RT_INTRINSIC_SPAWN_STARTUP (UINT32_C(1) << 31)
#define TALOS_RT_INTRINSIC_SPAWN_DETACHED_CONTROLS (UINT32_C(1) << 30)
#define TALOS_RT_INTRINSIC_SPAWN_PRIORITY_SIGNAL (UINT32_C(1) << 29)
#define TALOS_RT_INTRINSIC_SPAWN_PRIME (UINT32_C(1) << 28)
#define TALOS_RT_INTRINSIC_SPAWN_PROGRAM (UINT32_C(1) << 27)
#define TALOS_RT_INTRINSIC_SPAWN_FUNCTION_MASK UINT32_C(0x07ffffff)

// Serialized design-bytecode function flags. Process functions encode their
// canonical frame size shifted left by one. Bits 60-62 encode the executable
// home region and the high bit records final-phase scheduling.
#define TALOS_RT_DESIGN_FUNCTION_PROCESS UINT64_C(0x0000000000000001)
#define TALOS_RT_DESIGN_FUNCTION_FRAME_SIZE_MASK UINT64_C(0x0ffffffffffffffe)
#define TALOS_RT_DESIGN_FUNCTION_FRAME_SIZE_LIMIT UINT64_C(0x0800000000000000)
#define TALOS_RT_DESIGN_FUNCTION_HOME_SHIFT 60u
#define TALOS_RT_DESIGN_FUNCTION_HOME_MASK UINT64_C(0x7000000000000000)
#define TALOS_RT_DESIGN_FUNCTION_HOME(region)                                \
  ((uint64_t)(region) << TALOS_RT_DESIGN_FUNCTION_HOME_SHIFT)
#define TALOS_RT_DESIGN_FUNCTION_FINAL UINT64_C(0x8000000000000000)

// DPI scope records are immutable execution metadata and remain available
// independently of the optional VPI design database. IDs are dense from zero;
// UINT64_MAX denotes the root's absent parent. Time unit and precision are
// decimal exponents in seconds retained as simulator scope metadata.
typedef struct talos_rt_dpi_scope_v1 {
  uint64_t id;
  uint64_t parent_id;
  const char *name;
  uint64_t name_size;
  int32_t time_unit;
  int32_t time_precision;
  uint32_t reserved;
} talos_rt_dpi_scope_v1;

// One unpacked dimension of the private descriptor carried by an
// svOpenArrayHandle.  Generated call thunks point at immutable records with
// source-declared bounds and a byte stride in the canonical C buffer.  Keeping
// strides in the descriptor makes every svGet* operation O(dimensions) and
// avoids type reflection in the runtime hot path.
typedef struct talos_rt_dpi_dimension_v1 {
  int32_t left;
  int32_t right;
  uint64_t byte_stride;
  uint32_t flags;
  uint32_t reserved;
} talos_rt_dpi_dimension_v1;

#define TALOS_RT_DPI_DIMENSION_EMPTY (UINT32_C(1) << 0)
#define TALOS_RT_DPI_DIMENSION_RUNTIME (UINT32_C(1) << 1)

#define TALOS_RT_DPI_OPEN_ARRAY_MAGIC UINT32_C(0x4f424f41)
#define TALOS_RT_DPI_OPEN_ARRAY_WRITABLE (UINT32_C(1) << 0)
#define TALOS_RT_DPI_OPEN_ARRAY_C_LAYOUT (UINT32_C(1) << 1)
#define TALOS_RT_DPI_OPEN_ARRAY_PACKED (UINT32_C(1) << 2)
#define TALOS_RT_DPI_OPEN_ARRAY_FOUR_STATE (UINT32_C(1) << 3)
#define TALOS_RT_DPI_OPEN_ARRAY_EMPTY (UINT32_C(1) << 4)

// Stack-owned svOpenArrayHandle payload. `data` contains one canonical C
// representation per unpacked element: an ordinary scalar/aggregate C value,
// svBitVecVal words, or svLogicVecVal words. The handle and its range table are
// valid only for the duration of the enclosing DPI call.
typedef struct talos_rt_dpi_open_array_v1 {
  uint32_t magic;
  uint32_t flags;
  uint32_t dimensions;
  uint32_t element_bit_width;
  int32_t packed_left;
  int32_t packed_right;
  uint64_t element_size;
  void *data;
  uint64_t data_size;
  const talos_rt_dpi_dimension_v1 *ranges;
  uint64_t reserved;
} talos_rt_dpi_open_array_v1;

typedef struct talos_rt_dpi_open_array_storage_v1 {
  talos_rt_dpi_open_array_v1 descriptor;
  void *allocation;
} talos_rt_dpi_open_array_storage_v1;

// Canonicalize a compact Simulation-IR value/unknown bit stream to the exact
// per-element representation required by Annex H, and invert that mapping.
// These leaf helpers allocate nothing; native lowering owns stack storage and
// bytecode lowering owns interpreter scratch storage.
talos_rt_status talos_rt_v1_dpi_open_array_pack(
    const void *value, const void *unknown, uint64_t plane_size,
    uint64_t total_bit_width, uint32_t four_state, uint32_t element_category,
    uint32_t element_bit_width, uint64_t element_count,
    uint32_t reverse_elements, void *out_data, uint64_t out_size);
talos_rt_status talos_rt_v1_dpi_open_array_unpack(
    const void *data, uint64_t data_size, uint32_t element_category,
    uint32_t element_bit_width, uint64_t element_count, void *out_value,
    void *out_unknown, uint64_t plane_size, uint64_t total_bit_width,
    uint32_t four_state, uint32_t reverse_elements);

// Compose the open-array descriptor with the exact recursive C layout of one
// element. The element plan is wrapped in one runtime repeat record, keeping
// emitted metadata and native code independent of the actual array extent.
talos_rt_status talos_rt_v1_dpi_open_array_aggregate_pack(
    const void *value, const void *unknown, uint64_t plane_size,
    uint64_t total_bit_width, uint32_t four_state, uint32_t element_bit_width,
    uint64_t element_count, uint32_t reverse_elements,
    const int64_t *unpacked_ranges, uint32_t unpacked_dimensions,
    void *out_data, uint64_t data_size, uint64_t out_capacity,
    uint64_t element_c_size, uint64_t element_string_count,
    const int64_t *element_plan, uint64_t element_plan_words);
talos_rt_status talos_rt_v1_dpi_open_array_aggregate_unpack(
    talos_rt_context *context, const void *data, uint64_t data_size,
    uint64_t element_c_size, const int64_t *element_plan,
    uint64_t element_plan_words, uint64_t element_count,
    uint32_t reverse_elements, const int64_t *unpacked_ranges,
    uint32_t unpacked_dimensions, void *out_value, void *out_unknown,
    uint64_t plane_size, uint64_t total_bit_width, uint32_t four_state,
    uint32_t element_bit_width);
// Keep every heap string in a repeated open-array element plan rooted while
// the caller installs unpacked values into managed containers.
talos_rt_status talos_rt_v1_dpi_open_array_aggregate_roots_push(
    talos_rt_context *context, void *value, uint64_t plane_size,
    uint64_t total_bit_width, uint32_t element_bit_width,
    uint64_t element_count, uint32_t reverse_elements,
    const int64_t *unpacked_ranges, uint32_t unpacked_dimensions,
    uint64_t element_c_size, const int64_t *element_plan,
    uint64_t element_plan_words, void **out_handle);

talos_rt_status talos_rt_v1_dpi_open_array_prepare_recursive(
    const void *value, const void *unknown, uint64_t plane_size,
    uint64_t transport_width, uint32_t transport_four_state, uint32_t writable,
    uint32_t element_category, uint32_t element_bit_width,
    uint32_t element_four_state, int32_t packed_left, int32_t packed_right,
    uint64_t element_c_size, uint32_t element_c_alignment,
    uint64_t element_string_count, const int64_t *element_plan,
    uint64_t element_plan_words, const int64_t *shape_plan, uint32_t dimensions,
    talos_rt_dpi_open_array_storage_v1 *out_storage);
talos_rt_status talos_rt_v1_dpi_open_array_finish_recursive(
    talos_rt_status call_status, talos_rt_context *context,
    const talos_rt_dpi_open_array_storage_v1 *storage, void *value,
    void *unknown, uint64_t plane_size, uint64_t transport_width,
    uint32_t transport_four_state, uint32_t element_bit_width,
    uint32_t element_four_state, uint64_t element_c_size,
    const int64_t *element_plan, uint64_t element_plan_words,
    const int64_t *shape_plan, uint32_t dimensions);
void talos_rt_v1_dpi_open_array_release_recursive(
    talos_rt_dpi_open_array_storage_v1 *storage);

// Convert one sized unpacked aggregate between the simulator's compact
// value/unknown planes and its exact recursive C layout. The immutable plan
// uses eight-word leaf/repeat records. `out_capacity` may exceed `c_size` and
// supplies eight persistent scratch bytes for every string leaf; this keeps
// inline C string pointers valid through the enclosing DPI call.
talos_rt_status
talos_rt_v1_dpi_aggregate_pack(const void *value, const void *unknown,
                                 uint64_t plane_size, uint64_t total_bit_width,
                                 uint32_t four_state, void *out_data,
                                 uint64_t c_size, uint64_t out_capacity,
                                 const int64_t *plan, uint64_t plan_words);
talos_rt_status talos_rt_v1_dpi_aggregate_unpack(
    talos_rt_context *context, const void *data, uint64_t c_size,
    const int64_t *plan, uint64_t plan_words, void *out_value,
    void *out_unknown, uint64_t plane_size, uint64_t total_bit_width,
    uint32_t four_state);
// Keep every heap string word described by an aggregate plan rooted across an
// exported wrapper's complete unpack / dispatch / pack sequence.
talos_rt_status talos_rt_v1_dpi_aggregate_roots_push(
    talos_rt_context *context, void *value, uint64_t plane_size,
    uint64_t total_bit_width, const int64_t *plan, uint64_t plan_words,
    void **out_handle);
talos_rt_status
talos_rt_v1_dpi_aggregate_roots_pop(talos_rt_context *context,
                                      void *handle);
// Exported aggregate string pointers are retained per output slot until that
// slot is produced by a later exported call on the same thread.
talos_rt_status talos_rt_v1_dpi_aggregate_export_pack(
    const void *value, const void *unknown, uint64_t plane_size,
    uint64_t total_bit_width, uint32_t four_state, void *out_data,
    uint64_t c_size, const int64_t *plan, uint64_t plan_words,
    uint32_t output_slot);
// Allocate automatic task state while deriving precise string roots from the
// same compact aggregate plan used by the C marshaller. This keeps compiler
// work proportional to type structure even for very large fixed arrays.
talos_rt_status talos_rt_v1_dpi_aggregate_state_alloc(
    talos_rt_context *context, uint64_t total_bit_width, const uint8_t *value,
    const uint8_t *unknown, const int64_t *plan, uint64_t plan_words,
    uint64_t *out_handle);

struct talos_rt_process_descriptor_v1;

#define TALOS_RT_ACTIVATION_HAS_NATIVE (UINT32_C(1) << 0)
#define TALOS_RT_ACTIVATION_HAS_BYTECODE (UINT32_C(1) << 1)
#define TALOS_RT_ACTIVATION_NO_BYTECODE UINT32_MAX

// Direct activation inventory. Records are sorted by stable code-unit ID and
// identify both executable representations of a process, fork branch, or
// task. This remains independent of optional VPI reflection metadata.
typedef struct talos_rt_activation_descriptor_v1 {
  uint64_t code_unit_id;
  const struct talos_rt_process_descriptor_v1 *native_entry;
  uint32_t bytecode_function;
  uint32_t flags;
} talos_rt_activation_descriptor_v1;

typedef uint32_t talos_rt_observer_capture_kind;
enum {
  TALOS_RT_OBSERVER_CAPTURE_STORAGE = 1,
  TALOS_RT_OBSERVER_CAPTURE_NET = 2,
  TALOS_RT_OBSERVER_CAPTURE_EVENT = 3,
  TALOS_RT_OBSERVER_CAPTURE_DRIVER = 4,
  TALOS_RT_OBSERVER_CAPTURE_MANAGED = 5,
  // An opaque functional-coverage instance handle. Unlike state captures,
  // this word is not decoded as a stable packed-state handle.
  TALOS_RT_OBSERVER_CAPTURE_COVERGROUP = 6,
  // A language ref alias represented by three words: owner, payload, and
  // backing kind. The fourth computed-capture word is reserved and zero.
  TALOS_RT_OBSERVER_CAPTURE_ARGUMENT_REF = 7
};

typedef struct talos_rt_observer_capture_abi_v1 {
  talos_rt_observer_capture_kind kind;
  uint32_t width;
} talos_rt_observer_capture_abi_v1;

#define TALOS_RT_OBSERVER_FOUR_STATE (UINT32_C(1) << 0)
#define TALOS_RT_OBSERVER_REAL32 (UINT32_C(1) << 1)
#define TALOS_RT_OBSERVER_REAL64 (UINT32_C(1) << 2)
#define TALOS_RT_OBSERVER_NO_BYTECODE UINT32_MAX

// `captures` addresses `capture_count` consecutive four-word computed-capture
// records. The first word is the stable handle for ordinary captures. An
// ARGUMENT_REF uses the first three words for owner, payload, and backing kind.
typedef talos_rt_status (*talos_rt_native_observer_v1)(
    talos_rt_context *context, const uint64_t *captures,
    uint32_t capture_count, uint64_t *value, uint64_t *unknown,
    uint32_t limb_count);

typedef struct talos_rt_observer_descriptor_v1 {
  uint64_t code_unit_id;
  const talos_rt_observer_capture_abi_v1 *capture_abi;
  uint32_t capture_count;
  uint32_t result_width;
  uint32_t flags;
  uint32_t bytecode_function;
  talos_rt_native_observer_v1 native_evaluator;
  uint64_t reserved;
} talos_rt_observer_descriptor_v1;

// One compiler-coalesced canonical state range retained in the dense
// Preponed snapshot. Each range begins at a byte boundary in the snapshot;
// source offsets and widths remain bit-granular.
typedef struct talos_rt_sampled_range_v1 {
  uint64_t source_bit_offset;
  uint64_t snapshot_byte_offset;
  uint64_t bit_width;
} talos_rt_sampled_range_v1;

struct talos_rt_import_input_v1;
struct talos_rt_import_output_v1;

// Exported zero-time functions use the validated DPI value transport in the
// opposite direction from imports. One immutable record exists for every
// elaborated `(C identifier, scope)` binding.
typedef talos_rt_status (*talos_rt_native_export_v1)(
    talos_rt_context *context,
    const struct talos_rt_import_input_v1 *inputs, uint32_t input_count,
    struct talos_rt_import_output_v1 *outputs, uint32_t output_count);

#define TALOS_RT_EXPORT_HAS_NATIVE (UINT32_C(1) << 0)
#define TALOS_RT_EXPORT_HAS_BYTECODE (UINT32_C(1) << 1)
#define TALOS_RT_EXPORT_TASK (UINT32_C(1) << 2)
#define TALOS_RT_EXPORT_NO_BYTECODE UINT32_MAX
typedef struct talos_rt_export_descriptor_v1 {
  uint32_t export_id;
  uint32_t flags;
  uint64_t scope_id;
  uint64_t code_unit_id;
  uint64_t abi_signature;
  uint32_t input_count;
  uint32_t output_count;
  uint32_t bytecode_function;
  uint32_t reserved;
  talos_rt_native_export_v1 native_entry;
  uint64_t reserved_tail;
} talos_rt_export_descriptor_v1;

// Optional data carried at the byte offset in execution_descriptor_v1::reserved
// whenever an execution feature needs immutable compiler metadata. The offset
// is relative to the execution descriptor, so generated static data needs no
// pointer-to-integer relocation and has the same representation on wasm32 and
// native targets. Talos is still a prototype: this layout evolves in place
// and deliberately retains the single v1 identity.
#define TALOS_RT_EXECUTION_EXTENSION_VERSION UINT32_C(1)
typedef struct talos_rt_execution_extension_v1 {
  uint32_t version;
  uint32_t size;
  const talos_rt_sampled_range_v1 *sampled_ranges;
  uint64_t sampled_range_count;
  const talos_rt_export_descriptor_v1 *exports;
  uint64_t export_count;
  const uint8_t *class_bitstream;
  uint64_t class_bitstream_size;
  const uint8_t *coverage_schema;
  uint64_t coverage_schema_size;
} talos_rt_execution_extension_v1;

typedef struct talos_rt_execution_descriptor_v1 {
  uint32_t version;
  uint32_t flags;
  uint64_t reserved;
  const uint8_t *bytecode;
  uint64_t bytecode_size;
  const uint8_t *design_database;
  uint64_t design_database_size;
  uint64_t state_bit_count;
  uint64_t checksum;
  const talos_rt_dpi_scope_v1 *dpi_scopes;
  uint64_t dpi_scope_count;
  int32_t dpi_time_precision;
  uint32_t dpi_reserved;
  const talos_rt_activation_descriptor_v1 *activations;
  uint64_t activation_count;
  const talos_rt_observer_descriptor_v1 *observers;
  uint64_t observer_count;
} talos_rt_execution_descriptor_v1;

// One of these immutable records is attached to the canonical process
// descriptor for every process encoded in the design image.
typedef struct talos_rt_design_bytecode_entry_v1 {
  const talos_rt_execution_descriptor_v1 *execution;
  uint32_t function;
  uint32_t reserved;
} talos_rt_design_bytecode_entry_v1;

// Pointer-free serialized image headers shared by the encoder and runtime
// validator. Fields are read and written explicitly as little-endian values;
// the structs provide one typed source of truth for field offsets.
typedef struct talos_rt_design_bytecode_header_v1 {
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
} talos_rt_design_bytecode_header_v1;

typedef struct talos_rt_design_database_header_v1 {
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
  uint64_t statement_offset;
  uint64_t statement_count;
  uint64_t statement_site_offset;
  uint64_t statement_site_count;
  uint64_t relation_offset;
  uint64_t relation_count;
} talos_rt_design_database_header_v1;

#define TALOS_RT_DESIGN_BYTECODE_HEADER_SIZE 208u
#define TALOS_RT_DESIGN_DATABASE_HEADER_SIZE 176u
#define TALOS_RT_DESIGN_BYTECODE_INSTRUCTION_SIZE 32u
#define TALOS_RT_DESIGN_BYTECODE_MAGIC "OBBCDS1"
#define TALOS_RT_DESIGN_DATABASE_MAGIC "OBDSGN1"

// Static driver-state descriptor flag. Delayed gate and continuous-assignment
// drivers start at X until their time-zero evaluation propagates to the output.
#define TALOS_RT_DB_DRIVER_INITIAL_X (UINT32_C(1) << 14)

typedef uint8_t talos_rt_design_register_kind;
enum {
  TALOS_RT_DBREG_INVALID = 0,
  TALOS_RT_DBREG_BITS = 1,
  TALOS_RT_DBREG_LOGIC = 2,
  TALOS_RT_DBREG_HANDLE = 3,
  TALOS_RT_DBREG_STATUS = 4,
  TALOS_RT_DBREG_RESOURCE = 5,
  TALOS_RT_DBREG_BYTES = 6,
  // Native object pointers are confined to these precisely traced register
  // kinds. MANAGED_REF is {object, byte offset}; ARGUMENT_REF uses tag 0 for
  // ordinary state, tag 1 for {class owner, field offset}, and tag 2 for a
  // ReferencePath in its rooted owner word. Their first words are roots.
  TALOS_RT_DBREG_MANAGED = 7,
  TALOS_RT_DBREG_MANAGED_REF = 8,
  TALOS_RT_DBREG_ARGUMENT_REF = 9,
  // Managed strings are tagged words, not native object pointers. Floating
  // registers retain their semantic width so the validator cannot admit
  // integer opcodes over IEEE payloads.
  TALOS_RT_DBREG_STRING = 10,
  TALOS_RT_DBREG_REAL32 = 11,
  TALOS_RT_DBREG_REAL64 = 12,
  // Import descriptors use this kind for a value pointer that is itself an
  // talos_rt_dpi_open_array_v1. It is never a bytecode register layout.
  TALOS_RT_DBREG_OPEN_ARRAY = 13,
  // Import descriptors use this kind for a pointer to an exact C-layout
  // sized unpacked aggregate. It is never a bytecode register layout.
  TALOS_RT_DBREG_AGGREGATE = 14
};

#define TALOS_RT_DBREG_SIGNED (UINT8_C(1) << 0)
// Distinguishes nullable class handles from managed container/path values.
#define TALOS_RT_DBREG_CLASS_HANDLE (UINT8_C(1) << 1)
/* An opaque C pointer in a canonical unsigned 64-bit register. */
#define TALOS_RT_DBREG_CHANDLE (UINT8_C(1) << 2)
// Distinguishes scheduler events from other canonical bytecode handles.
#define TALOS_RT_DBREG_EVENT (UINT8_C(1) << 3)
// A context-local covergroup identity, stored as a canonical 64-bit word.
#define TALOS_RT_DBREG_COVERGROUP (UINT8_C(1) << 4)
// Bits64 scheduler process identity; zero is null.
#define TALOS_RT_DBREG_PROCESS (UINT8_C(1) << 5)
// Bits64 elaborated interface scope identity; zero is null.
#define TALOS_RT_DBREG_VIRTUAL_INTERFACE (UINT8_C(1) << 6)
// Managed64 semaphore identity, distinct from classes and other containers.
#define TALOS_RT_DBREG_SEMAPHORE (UINT8_C(1) << 7)
// Managed64 typed mailbox identity. Register flags are interpreted by kind;
// this bit denotes an event only in Handle256 layouts.
#define TALOS_RT_DBREG_MAILBOX (UINT8_C(1) << 3)

typedef uint16_t talos_rt_design_bytecode_opcode;
enum {
  TALOS_RT_DB_NOP = 0,
  TALOS_RT_DB_CONSTANT = 1,
  TALOS_RT_DB_MOVE = 2,
  TALOS_RT_DB_NOT = 3,
  TALOS_RT_DB_AND = 4,
  TALOS_RT_DB_OR = 5,
  TALOS_RT_DB_XOR = 6,
  TALOS_RT_DB_ADD = 7,
  TALOS_RT_DB_SUB = 8,
  TALOS_RT_DB_MUL = 9,
  TALOS_RT_DB_UDIV = 10,
  TALOS_RT_DB_SDIV = 11,
  TALOS_RT_DB_UREM = 12,
  TALOS_RT_DB_SREM = 13,
  TALOS_RT_DB_SHL = 14,
  TALOS_RT_DB_LSHR = 15,
  TALOS_RT_DB_ASHR = 16,
  TALOS_RT_DB_COMPARE = 17,
  TALOS_RT_DB_SELECT = 18,
  TALOS_RT_DB_REDUCE = 19,
  TALOS_RT_DB_CONCAT = 20,
  TALOS_RT_DB_EXTRACT = 21,
  TALOS_RT_DB_INSERT = 22,
  TALOS_RT_DB_LOAD_FRAME = 23,
  TALOS_RT_DB_STORE_FRAME = 24,
  TALOS_RT_DB_MAKE_HANDLE = 25,
  TALOS_RT_DB_HANDLE_OFFSET = 26,
  TALOS_RT_DB_LOAD_STATE = 27,
  TALOS_RT_DB_STORE_STATE = 28,
  TALOS_RT_DB_JUMP = 29,
  TALOS_RT_DB_BRANCH = 30,
  TALOS_RT_DB_CALL = 31,
  TALOS_RT_DB_RETURN = 32,
  TALOS_RT_DB_CONTINUE = 33,
  TALOS_RT_DB_SUSPEND = 34,
  TALOS_RT_DB_TERMINATE = 35,
  TALOS_RT_DB_INTRINSIC = 36,
  TALOS_RT_DB_FAIL = 37,
  TALOS_RT_DB_MAKE_LOCAL_HANDLE = 38,
  TALOS_RT_DB_HANDLE_ID = 39,
  TALOS_RT_DB_TASK_CALL = 40,
  TALOS_RT_DB_VIRTUAL_CALL = 41,
  // Consume one precise managed-root word from a canonical continuation
  // frame after the value has been restored into traced bytecode registers.
  TALOS_RT_DB_CLEAR_FRAME_ROOT = 42,
  TALOS_RT_DB_OVERRIDE_STATE = 43,
  TALOS_RT_DB_RELEASE_STATE = 44,
  TALOS_RT_DB_FADD = 45,
  TALOS_RT_DB_FSUB = 46,
  TALOS_RT_DB_FMUL = 47,
  TALOS_RT_DB_FDIV = 48,
  TALOS_RT_DB_FNEG = 49,
  TALOS_RT_DB_FCOMPARE = 50,
  TALOS_RT_DB_FEXT = 51,
  TALOS_RT_DB_FTRUNC = 52,
  TALOS_RT_DB_FPOW = 53,
  TALOS_RT_DB_VIRTUAL_TASK_CALL = 54,
  // Declarative precise-root record for a canonical process frame. Execution
  // is a no-op; the collector scans these records while activations suspend.
  TALOS_RT_DB_FRAME_ROOT = 55,
  // Interface dispatch uses destination as an operand-table index whose pair
  // stores the interface ID and stable method ordinal.
  TALOS_RT_DB_INTERFACE_CALL = 56,
  TALOS_RT_DB_INTERFACE_TASK_CALL = 57,
  // Terminator for IEEE process-object kill/suspend/resume. The target is a
  // shared tagged process identity and immediate is the resume continuation.
  TALOS_RT_DB_PROCESS_CONTROL = 58,
  // Reinterpret the payload of an IEEE real as an equal-width two-state
  // integer, or vice versa. Integer register padding is not part of the
  // payload. Also converts a class handle to its mailbox or semaphore base
  // view, preserving its managed word after checking builtin ancestry.
  TALOS_RT_DB_BITCAST = 59,
  // Integral modular exponentiation. The destination and base widths match;
  // the exponent retains its independent SystemVerilog packed width.
  TALOS_RT_DB_POWER = 60,
  // Packed replication. The immediate is the positive replication count;
  // destination width equals source width times that count.
  TALOS_RT_DB_REPLICATE = 61,
  // Two-state population count (math.ctpop). Destination and source are
  // equal-width bit registers.
  TALOS_RT_DB_POPCOUNT = 62
};

// StoreState writes its exact post-resolution transition predicate to the
// destination i1 register when this flag is set.
#define TALOS_RT_DB_STORE_STATE_CHANGED UINT16_C(1)
// Preserve the latest continuous publication beneath force / procedural
// assign so releasing the override can immediately reveal its driver.
#define TALOS_RT_DB_STORE_STATE_CONTINUOUS UINT16_C(2)
// Store a physical driver bank without resolving its destination net. A
// following driver store completes the logical update and publishes once. If
// CHANGED is also set, the result is false because no logical net transition
// has been published yet.
#define TALOS_RT_DB_STORE_STATE_DEFER_NET_RESOLUTION UINT16_C(4)

typedef uint16_t talos_rt_design_extract_kind;
enum {
  TALOS_RT_DB_EXTRACT_ZERO_EXTEND = 0,
  TALOS_RT_DB_EXTRACT_SIGN_EXTEND = 1,
  // Physical 64-bit class-handle lane of an aggregate value. This is
  // deliberately distinct from ordinary numeric extraction so the bytecode
  // validator can keep managed/numeric coercions confined to compiler-marked
  // aggregate element operations.
  TALOS_RT_DB_AGGREGATE_MANAGED = 2
};

// INSERT takes its signed two's-complement low-bit index from source2 when
// this flag is set. Unknown, nonoverlapping, and partially overlapping writes
// follow IEEE 1800 packed-selection assignment semantics.
#define TALOS_RT_DB_INSERT_DYNAMIC UINT16_C(1)

typedef uint16_t talos_rt_design_select_kind;
enum { TALOS_RT_DB_SELECT_BINARY = 0, TALOS_RT_DB_SELECT_FOUR_STATE = 1 };

typedef uint16_t talos_rt_design_reduction_kind;
enum {
  TALOS_RT_DB_REDUCE_AND = 0,
  TALOS_RT_DB_REDUCE_OR = 1,
  TALOS_RT_DB_REDUCE_XOR = 2,
  TALOS_RT_DB_REDUCE_NAND = 3,
  TALOS_RT_DB_REDUCE_NOR = 4,
  TALOS_RT_DB_REDUCE_XNOR = 5,
  TALOS_RT_DB_REDUCE_IS_TRUE = 6,
  TALOS_RT_DB_REDUCE_LOGICAL_NOT = 7,
  TALOS_RT_DB_REDUCE_LOGICAL_VALUE = 8
};

typedef uint16_t talos_rt_design_float_compare_kind;
enum {
  TALOS_RT_DB_FCMP_EQ = 0,
  TALOS_RT_DB_FCMP_NE = 1,
  TALOS_RT_DB_FCMP_LT = 2,
  TALOS_RT_DB_FCMP_LE = 3,
  TALOS_RT_DB_FCMP_GT = 4,
  TALOS_RT_DB_FCMP_GE = 5
};

typedef uint16_t talos_rt_design_override_kind;
enum {
  TALOS_RT_DB_OVERRIDE_FORCE = 0,
  TALOS_RT_DB_OVERRIDE_ASSIGN = 1,
  TALOS_RT_DB_OVERRIDE_DYNAMIC = 2,
  TALOS_RT_DB_OVERRIDE_CLAIM = 4,
  TALOS_RT_DB_OVERRIDE_KIND_MASK = 1,
  TALOS_RT_DB_OVERRIDE_FLAG_MASK = 7
};

// COMPARE flags. Case comparisons return a known two-state result. Wildcard
// equality masks unknown RHS bits but can return X for a relevant unknown LHS
// bit; ordinary comparisons return X when an operand has unknown bits.
typedef uint16_t talos_rt_design_compare_kind;
enum {
  TALOS_RT_DB_CMP_EQ = 0,
  TALOS_RT_DB_CMP_NE = 1,
  TALOS_RT_DB_CMP_ULT = 2,
  TALOS_RT_DB_CMP_ULE = 3,
  TALOS_RT_DB_CMP_UGT = 4,
  TALOS_RT_DB_CMP_UGE = 5,
  TALOS_RT_DB_CMP_SLT = 6,
  TALOS_RT_DB_CMP_SLE = 7,
  TALOS_RT_DB_CMP_SGT = 8,
  TALOS_RT_DB_CMP_SGE = 9,
  TALOS_RT_DB_CMP_CASE_EQ = 10,
  TALOS_RT_DB_CMP_CASE_NE = 11,
  TALOS_RT_DB_CMP_WILD_EQ = 12,
  TALOS_RT_DB_CMP_WILD_NE = 13,
  TALOS_RT_DB_CMP_CASEZ_EQ = 14,
  TALOS_RT_DB_CMP_CASEXZ_EQ = 15
};

// IEEE 1800-2017 20.16 stochastic queue manager. The first seven status
// values are the standard Table 20-11 values. The final two make otherwise
// unspecified invalid-statistic and not-yet-available-statistic results
// deterministic while preserving the standard values verbatim.
typedef uint32_t talos_rt_stochastic_queue_action_v1;
enum {
  TALOS_RT_STOCHASTIC_QUEUE_INITIALIZE = 0,
  TALOS_RT_STOCHASTIC_QUEUE_ADD = 1,
  TALOS_RT_STOCHASTIC_QUEUE_REMOVE = 2,
  TALOS_RT_STOCHASTIC_QUEUE_FULL = 3,
  TALOS_RT_STOCHASTIC_QUEUE_EXAM = 4
};

typedef uint32_t talos_rt_stochastic_queue_status_v1;
enum {
  TALOS_RT_STOCHASTIC_QUEUE_OK = 0,
  TALOS_RT_STOCHASTIC_QUEUE_FULL_STATUS = 1,
  TALOS_RT_STOCHASTIC_QUEUE_UNDEFINED_ID = 2,
  TALOS_RT_STOCHASTIC_QUEUE_EMPTY = 3,
  TALOS_RT_STOCHASTIC_QUEUE_UNSUPPORTED_TYPE = 4,
  TALOS_RT_STOCHASTIC_QUEUE_INVALID_LENGTH = 5,
  TALOS_RT_STOCHASTIC_QUEUE_DUPLICATE_ID = 6,
  TALOS_RT_STOCHASTIC_QUEUE_OUT_OF_MEMORY = 7,
  TALOS_RT_STOCHASTIC_QUEUE_UNDEFINED_STATISTIC = 8,
  TALOS_RT_STOCHASTIC_QUEUE_NO_STATISTICS = 10
};

typedef uint32_t talos_rt_intrinsic_id;
enum {
  TALOS_RT_INTRINSIC_V1_FORMAT = UINT32_C(0x00010001),
  TALOS_RT_INTRINSIC_V1_DISPLAY = UINT32_C(0x00010002),
  TALOS_RT_INTRINSIC_V1_TIME_FORMAT = UINT32_C(0x00010003),
  TALOS_RT_INTRINSIC_V1_FILE_BASE = UINT32_C(0x00010100),
  TALOS_RT_INTRINSIC_V1_FILE_OPEN_MCD = UINT32_C(0x00010100),
  TALOS_RT_INTRINSIC_V1_FILE_OPEN = UINT32_C(0x00010101),
  TALOS_RT_INTRINSIC_V1_FILE_CLOSE = UINT32_C(0x00010102),
  TALOS_RT_INTRINSIC_V1_FILE_FLUSH = UINT32_C(0x00010103),
  TALOS_RT_INTRINSIC_V1_FILE_GETC = UINT32_C(0x00010104),
  TALOS_RT_INTRINSIC_V1_FILE_UNGETC = UINT32_C(0x00010105),
  TALOS_RT_INTRINSIC_V1_FILE_GETLINE = UINT32_C(0x00010106),
  TALOS_RT_INTRINSIC_V1_FILE_READ_PACKED = UINT32_C(0x00010107),
  TALOS_RT_INTRINSIC_V1_FILE_EOF = UINT32_C(0x00010108),
  TALOS_RT_INTRINSIC_V1_FILE_SEEK = UINT32_C(0x00010109),
  TALOS_RT_INTRINSIC_V1_FILE_TELL = UINT32_C(0x0001010a),
  TALOS_RT_INTRINSIC_V1_FILE_REWIND = UINT32_C(0x0001010b),
  TALOS_RT_INTRINSIC_V1_FILE_OPEN_STRING_MCD = UINT32_C(0x0001010c),
  TALOS_RT_INTRINSIC_V1_FILE_OPEN_STRING = UINT32_C(0x0001010d),
  TALOS_RT_INTRINSIC_V1_FILE_GETLINE_STRING = UINT32_C(0x0001010e),
  TALOS_RT_INTRINSIC_V1_FILE_ERROR_STRING = UINT32_C(0x0001010f),
  TALOS_RT_INTRINSIC_V1_PLUSARG_TEST = UINT32_C(0x00010110),
  TALOS_RT_INTRINSIC_V1_PLUSARG_VALUE = UINT32_C(0x00010111),
  TALOS_RT_INTRINSIC_V1_FILE_SCAN_FIELD = UINT32_C(0x00010112),
  TALOS_RT_INTRINSIC_V1_FILE_READMEM_TOKEN = UINT32_C(0x00010113),
  TALOS_RT_INTRINSIC_V1_PLUSARG_SCAN = UINT32_C(0x00010114),
  TALOS_RT_INTRINSIC_V1_SYSTEM = UINT32_C(0x00010115),
  TALOS_RT_INTRINSIC_V1_PLUSARG_PARSE_LOGIC = UINT32_C(0x00010116),
  TALOS_RT_INTRINSIC_V1_PLUSARG_PARSE_REAL = UINT32_C(0x00010117),
  TALOS_RT_INTRINSIC_V1_SPAWN = UINT32_C(0x00010200),
  TALOS_RT_INTRINSIC_V1_NBA = UINT32_C(0x00010201),
  // Statically planned NBA. The final i64 input is the NBASiteAttr identity;
  // the remaining inputs have the same ABI as TALOS_RT_INTRINSIC_V1_NBA.
  // Runtimes without an installed static schedule execute this through the
  // canonical generic NBA path.
  TALOS_RT_INTRINSIC_V1_STATIC_NBA = UINT32_C(0x00010219),
  TALOS_RT_INTRINSIC_V1_EVENT_TRIGGER = UINT32_C(0x00010202),
  TALOS_RT_INTRINSIC_V1_STATE_ALLOC = UINT32_C(0x00010203),
  TALOS_RT_INTRINSIC_V1_EVENT_TRIGGERED = UINT32_C(0x00010204),
  TALOS_RT_INTRINSIC_V1_DISABLE_CHILDREN = UINT32_C(0x00010205),
  TALOS_RT_INTRINSIC_V1_CONTROL_ENTER = UINT32_C(0x00010206),
  TALOS_RT_INTRINSIC_V1_CONTROL_LEAVE = UINT32_C(0x00010207),
  TALOS_RT_INTRINSIC_V1_CONTROL_DISABLE = UINT32_C(0x00010208),
  TALOS_RT_INTRINSIC_V1_STATIC_ONCE = UINT32_C(0x00010209),
  TALOS_RT_INTRINSIC_V1_FINISH = UINT32_C(0x0001020a),
  TALOS_RT_INTRINSIC_V1_FATAL = UINT32_C(0x0001020b),
  TALOS_RT_INTRINSIC_V1_TERMINATION_REQUESTED = UINT32_C(0x0001020c),
  TALOS_RT_INTRINSIC_V1_TIME_NOW = UINT32_C(0x0001020d),
  TALOS_RT_INTRINSIC_V1_TIME_TO_REAL = UINT32_C(0x0001020e),
  TALOS_RT_INTRINSIC_V1_TIME_FROM_REAL = UINT32_C(0x0001020f),
  TALOS_RT_INTRINSIC_V1_REAL_FROM_INTEGER = UINT32_C(0x00010210),
  TALOS_RT_INTRINSIC_V1_REAL_TO_INTEGER = UINT32_C(0x00010211),
  TALOS_RT_INTRINSIC_V1_REAL_COMPARE = UINT32_C(0x00010212),
  TALOS_RT_INTRINSIC_V1_COUNT_BITS = UINT32_C(0x00010213),
  TALOS_RT_INTRINSIC_V1_CLOG2 = UINT32_C(0x00010214),
  TALOS_RT_INTRINSIC_V1_MONITOR_REGISTER = UINT32_C(0x00010215),
  TALOS_RT_INTRINSIC_V1_MONITOR_CONTROL = UINT32_C(0x00010216),
  TALOS_RT_INTRINSIC_V1_MONITOR_CURRENT = UINT32_C(0x00010217),
  TALOS_RT_INTRINSIC_V1_DEFERRED_ONCE = UINT32_C(0x00010218),
  TALOS_RT_INTRINSIC_V1_STOP = UINT32_C(0x0001021a),
  TALOS_RT_INTRINSIC_V1_DEFERRED_ENQUEUE = UINT32_C(0x0001021b),
  TALOS_RT_INTRINSIC_V1_DEFERRED_MATURE = UINT32_C(0x0001021c),
  TALOS_RT_INTRINSIC_V1_SAMPLED_READ = UINT32_C(0x0001021d),
  TALOS_RT_INTRINSIC_V1_SAMPLED_HISTORY = UINT32_C(0x0001021e),
  TALOS_RT_INTRINSIC_V1_ASSERTION_CONTROL = UINT32_C(0x0001021f),
  TALOS_RT_INTRINSIC_V1_ASSERTION_ENABLED = UINT32_C(0x00010220),
  TALOS_RT_INTRINSIC_V1_ASSERTION_ACTION_STATE = UINT32_C(0x00010221),
  TALOS_RT_INTRINSIC_V1_CLOCKED_SAMPLE_UPDATE = UINT32_C(0x00010222),
  TALOS_RT_INTRINSIC_V1_CLOCKED_SAMPLE_READ = UINT32_C(0x00010223),
  TALOS_RT_INTRINSIC_V1_DUMP_OPEN = UINT32_C(0x00010224),
  TALOS_RT_INTRINSIC_V1_DUMP_OPEN_STRING = UINT32_C(0x00010225),
  TALOS_RT_INTRINSIC_V1_DUMP_TIMESCALE = UINT32_C(0x00010226),
  TALOS_RT_INTRINSIC_V1_DUMP_VARS = UINT32_C(0x00010227),
  TALOS_RT_INTRINSIC_V1_DUMP_ALL = UINT32_C(0x00010228),
  TALOS_RT_INTRINSIC_V1_DUMP_CONTROL = UINT32_C(0x00010229),
  TALOS_RT_INTRINSIC_V1_DUMP_LIMIT = UINT32_C(0x0001022a),
  TALOS_RT_INTRINSIC_V1_DUMP_FLUSH = UINT32_C(0x0001022b),
  TALOS_RT_INTRINSIC_V1_PROCESS_CURRENT = UINT32_C(0x0001022c),
  TALOS_RT_INTRINSIC_V1_PROCESS_STATUS = UINT32_C(0x0001022d),
  TALOS_RT_INTRINSIC_V1_PROCESS_RANDOM_GET = UINT32_C(0x0001022e),
  TALOS_RT_INTRINSIC_V1_PROCESS_RANDOM_SET = UINT32_C(0x0001022f),
  // Automatic-state allocation with typed root triples following the initial
  // value: bit offset, allowed managed-kind mask, and root-slot flags.
  TALOS_RT_INTRINSIC_V1_STATE_ALLOC_TYPED = UINT32_C(0x00010230),
  TALOS_RT_INTRINSIC_V1_ERROR = UINT32_C(0x00010231),
  TALOS_RT_INTRINSIC_V1_DUMP_PORTS = UINT32_C(0x00010232),
  TALOS_RT_INTRINSIC_V1_DUMP_PORTS_CONTROL = UINT32_C(0x00010233),
  TALOS_RT_INTRINSIC_V1_ASSERTION_KILL_EPOCH = UINT32_C(0x00010234),
  TALOS_RT_INTRINSIC_V1_CLOCKING_NBA = UINT32_C(0x00010235),
  TALOS_RT_INTRINSIC_V1_INERTIAL_DRIVER = UINT32_C(0x00010236),
  TALOS_RT_INTRINSIC_V1_INERTIAL_DRIVER_STRENGTH_PAIR = UINT32_C(0x00010237),
  TALOS_RT_INTRINSIC_V1_WAIT_ORDER_FAILED = UINT32_C(0x00010238),
  TALOS_RT_INTRINSIC_V1_CONTROL_BOUNDARY = UINT32_C(0x00010239),
  TALOS_RT_INTRINSIC_V1_CONTROL_ESCAPE_PENDING = UINT32_C(0x0001023a),
  TALOS_RT_INTRINSIC_V1_NET_COUNT_DRIVERS = UINT32_C(0x0001023b),
  TALOS_RT_INTRINSIC_V1_PROGRAM_EXIT = UINT32_C(0x0001023c),
  TALOS_RT_INTRINSIC_V1_PASS_SWITCH_CONTROL = UINT32_C(0x0001023d),
  TALOS_RT_INTRINSIC_V1_PASS_SWITCH_CONTROL_DELAYED = UINT32_C(0x0001023e),
  TALOS_RT_INTRINSIC_V1_MOS_DRIVE_DELAYED = UINT32_C(0x0001023f),
  TALOS_RT_INTRINSIC_V1_LOGIC_CASE_DIFFERENCE_MASK = UINT32_C(0x00010240),
  TALOS_RT_INTRINSIC_V1_INERTIAL_PATH_DRIVER = UINT32_C(0x00010241),
  TALOS_RT_INTRINSIC_V1_TIME_SCAN_SCALE = UINT32_C(0x00010242),
  TALOS_RT_INTRINSIC_V1_STRING_SCAN_RAW = UINT32_C(0x00010243),
  TALOS_RT_INTRINSIC_V1_FILE_SCAN_RAW = UINT32_C(0x00010244),
  TALOS_RT_INTRINSIC_V1_INERTIAL_PATH_STORAGE = UINT32_C(0x00010245),
  TALOS_RT_INTRINSIC_V1_STRING_SCAN_DYNAMIC = UINT32_C(0x00010246),
  TALOS_RT_INTRINSIC_V1_FILE_SCAN_DYNAMIC = UINT32_C(0x00010247),
  TALOS_RT_INTRINSIC_V1_SCAN_DYNAMIC_VALIDATE = UINT32_C(0x00010248),
  TALOS_RT_INTRINSIC_V1_CLOCK_OCCURRENCE_CONSUME = UINT32_C(0x00010249),
  TALOS_RT_INTRINSIC_V1_INERTIAL_PATH_STRENGTH_PAIR = UINT32_C(0x0001024a),
  // Compiler-private replace/cancel service for one static Clause 31 timer.
  // Keeping this separate preserves the ordinary EVENT_TRIGGER bytecode hot
  // case and its flags ABI exactly.
  TALOS_RT_INTRINSIC_V1_EVENT_REPLACE_AFTER = UINT32_C(0x0001024b),
  TALOS_RT_INTRINSIC_V1_NOCHANGE_UPDATE = UINT32_C(0x0001024c),
  TALOS_RT_INTRINSIC_V1_CLOCKING_OUTPUT_TRACK = UINT32_C(0x0001024d),
  TALOS_RT_INTRINSIC_V1_CLOCKING_OUTPUT_CURRENT = UINT32_C(0x0001024e),
  TALOS_RT_INTRINSIC_V1_PROGRAM_OWNED = UINT32_C(0x0001024f),
  TALOS_RT_INTRINSIC_V1_CONTROL_DISABLE_SELECTED = UINT32_C(0x00010250),
  TALOS_RT_INTRINSIC_V1_IMPORT = UINT32_C(0x00010300),
  TALOS_RT_INTRINSIC_V1_DPI_IMPORT = UINT32_C(0x00010301),
  TALOS_RT_INTRINSIC_V1_CLASS_ALLOC = UINT32_C(0x00010400),
  TALOS_RT_INTRINSIC_V1_CLASS_COPY = UINT32_C(0x00010401),
  TALOS_RT_INTRINSIC_V1_CLASS_IS_INSTANCE = UINT32_C(0x00010402),
  TALOS_RT_INTRINSIC_V1_CLASS_CAST = UINT32_C(0x00010403),
  TALOS_RT_INTRINSIC_V1_CLASS_FIELD_REF = UINT32_C(0x00010404),
  TALOS_RT_INTRINSIC_V1_MANAGED_LOAD = UINT32_C(0x00010405),
  TALOS_RT_INTRINSIC_V1_MANAGED_STORE = UINT32_C(0x00010406),
  TALOS_RT_INTRINSIC_V1_WEAK_CREATE = UINT32_C(0x00010407),
  TALOS_RT_INTRINSIC_V1_WEAK_GET = UINT32_C(0x00010408),
  TALOS_RT_INTRINSIC_V1_WEAK_CLEAR = UINT32_C(0x00010409),
  TALOS_RT_INTRINSIC_V1_GC_SAFEPOINT = UINT32_C(0x0001040a),
  TALOS_RT_INTRINSIC_V1_CLASS_ID = UINT32_C(0x0001040b),
  TALOS_RT_INTRINSIC_V1_MANAGED_NBA = UINT32_C(0x0001040c),
  TALOS_RT_INTRINSIC_V1_ARGUMENT_REF_FROM_REF = UINT32_C(0x0001040d),
  TALOS_RT_INTRINSIC_V1_ARGUMENT_REF_FROM_MANAGED = UINT32_C(0x0001040e),
  TALOS_RT_INTRINSIC_V1_ARGUMENT_REF_LOAD = UINT32_C(0x0001040f),
  TALOS_RT_INTRINSIC_V1_ARGUMENT_REF_STORE = UINT32_C(0x00010410),
  TALOS_RT_INTRINSIC_V1_MANAGED_ROOT_EXTRACT = UINT32_C(0x00010411),
  TALOS_RT_INTRINSIC_V1_REFERENCE_PATH_INDEX = UINT32_C(0x00010412),
  TALOS_RT_INTRINSIC_V1_ARGUMENT_REF_FROM_PATH = UINT32_C(0x00010413),
  TALOS_RT_INTRINSIC_V1_MANAGED_CANDIDATE_ROOT = UINT32_C(0x00010414),
  TALOS_RT_INTRINSIC_V1_MANAGED_WATCH = UINT32_C(0x00010415),
  TALOS_RT_INTRINSIC_V1_MANAGED_OVERRIDE = UINT32_C(0x00010461),
  TALOS_RT_INTRINSIC_V1_MANAGED_RELEASE_OVERRIDE = UINT32_C(0x00010462),
  TALOS_RT_INTRINSIC_V1_REFERENCE_PATH_STRING_CHARACTER =
      UINT32_C(0x00010416),
  TALOS_RT_INTRINSIC_V1_REFERENCE_PATH_AGGREGATE_ELEMENT =
      UINT32_C(0x0001045e),
  TALOS_RT_INTRINSIC_V1_CONTAINER_IMPORT_FIXED = UINT32_C(0x0001045f),
  TALOS_RT_INTRINSIC_V1_CONTAINER_EXPORT_FIXED = UINT32_C(0x00010460),
  TALOS_RT_INTRINSIC_V1_CONTAINER_EXPORT_BITSTREAM = UINT32_C(0x00010464),
  TALOS_RT_INTRINSIC_V1_AGGREGATE_EXPORT_BITSTREAM = UINT32_C(0x00010465),
  TALOS_RT_INTRINSIC_V1_AGGREGATE_IMPORT_BITSTREAM = UINT32_C(0x00010466),
  TALOS_RT_INTRINSIC_V1_VPI_FRAME_BIND = UINT32_C(0x00011016),
  TALOS_RT_INTRINSIC_V1_BOX_BIND_TYPE = UINT32_C(0x00010418),
  TALOS_RT_INTRINSIC_V1_BOX_IS_TYPE = UINT32_C(0x00010417),
  TALOS_RT_INTRINSIC_V1_STRING_LITERAL = UINT32_C(0x00010420),
  TALOS_RT_INTRINSIC_V1_STRING_FROM_PACKED = UINT32_C(0x00010421),
  TALOS_RT_INTRINSIC_V1_STRING_TO_PACKED = UINT32_C(0x00010422),
  TALOS_RT_INTRINSIC_V1_STRING_CONCAT = UINT32_C(0x00010423),
  TALOS_RT_INTRINSIC_V1_STRING_REPEAT = UINT32_C(0x00010424),
  TALOS_RT_INTRINSIC_V1_STRING_LENGTH = UINT32_C(0x00010425),
  TALOS_RT_INTRINSIC_V1_STRING_GETC = UINT32_C(0x00010426),
  TALOS_RT_INTRINSIC_V1_STRING_PUTC = UINT32_C(0x00010427),
  TALOS_RT_INTRINSIC_V1_STRING_SUBSTR = UINT32_C(0x00010428),
  TALOS_RT_INTRINSIC_V1_STRING_COMPARE = UINT32_C(0x00010429),
  TALOS_RT_INTRINSIC_V1_STRING_CASE_CONVERT = UINT32_C(0x0001042a),
  TALOS_RT_INTRINSIC_V1_STRING_PARSE_INTEGER = UINT32_C(0x0001042b),
  TALOS_RT_INTRINSIC_V1_STRING_PARSE_REAL = UINT32_C(0x0001042c),
  TALOS_RT_INTRINSIC_V1_STRING_FORMAT_INTEGER = UINT32_C(0x0001042d),
  TALOS_RT_INTRINSIC_V1_STRING_FORMAT_REAL = UINT32_C(0x0001042e),
  TALOS_RT_INTRINSIC_V1_STRING_SCAN_FIELD = UINT32_C(0x0001042f),
  TALOS_RT_INTRINSIC_V1_CONTAINER_SIZE = UINT32_C(0x00010430),
  TALOS_RT_INTRINSIC_V1_CONTAINER_CREATE_LIKE = UINT32_C(0x00010431),
  TALOS_RT_INTRINSIC_V1_CONTAINER_READ = UINT32_C(0x00010432),
  TALOS_RT_INTRINSIC_V1_CONTAINER_WRITE = UINT32_C(0x00010433),
  TALOS_RT_INTRINSIC_V1_CONTAINER_CREATE = UINT32_C(0x00010434),
  TALOS_RT_INTRINSIC_V1_CONTAINER_CLONE = UINT32_C(0x00010435),
  TALOS_RT_INTRINSIC_V1_CONTAINER_DELETE = UINT32_C(0x00010436),
  TALOS_RT_INTRINSIC_V1_RANDOM_BOUNDED = UINT32_C(0x00010437),
  TALOS_RT_INTRINSIC_V1_ASSOC_CREATE = UINT32_C(0x00010438),
  TALOS_RT_INTRINSIC_V1_ASSOC_READ = UINT32_C(0x00010439),
  TALOS_RT_INTRINSIC_V1_ASSOC_WRITE = UINT32_C(0x0001043a),
  TALOS_RT_INTRINSIC_V1_ASSOC_EXISTS = UINT32_C(0x0001043b),
  TALOS_RT_INTRINSIC_V1_ASSOC_DELETE = UINT32_C(0x0001043c),
  TALOS_RT_INTRINSIC_V1_ASSOC_DEFAULT = UINT32_C(0x0001043d),
  TALOS_RT_INTRINSIC_V1_ASSOC_TRAVERSE = UINT32_C(0x0001043e),
  TALOS_RT_INTRINSIC_V1_REFERENCE_PATH_ASSOC = UINT32_C(0x0001043f),
  TALOS_RT_INTRINSIC_V1_RANDOM_NEXT = UINT32_C(0x00010440),
  TALOS_RT_INTRINSIC_V1_RANDOM_SEED = UINT32_C(0x00010441),
  TALOS_RT_INTRINSIC_V1_RANDOM_GET_STATE = UINT32_C(0x00010442),
  TALOS_RT_INTRINSIC_V1_RANDOM_SET_STATE = UINT32_C(0x00010443),
  TALOS_RT_INTRINSIC_V1_QUEUE_DELETE = UINT32_C(0x00010444),
  TALOS_RT_INTRINSIC_V1_QUEUE_INSERT = UINT32_C(0x00010445),
  TALOS_RT_INTRINSIC_V1_RANDOM_DISTRIBUTION = UINT32_C(0x00010446),
  TALOS_RT_INTRINSIC_V1_RANDOM_SOLVE = UINT32_C(0x00010447),
  TALOS_RT_INTRINSIC_V1_RANDOM_SOLVE_STATE = UINT32_C(0x00010448),
  TALOS_RT_INTRINSIC_V1_RANDOM_CYCLE_NEXT = UINT32_C(0x00010449),
  TALOS_RT_INTRINSIC_V1_MAILBOX_CREATE = UINT32_C(0x0001044a),
  TALOS_RT_INTRINSIC_V1_MAILBOX_NUM = UINT32_C(0x0001044b),
  TALOS_RT_INTRINSIC_V1_MAILBOX_TRY_PUT = UINT32_C(0x0001044c),
  TALOS_RT_INTRINSIC_V1_MAILBOX_TRY_PEEK = UINT32_C(0x0001044d),
  TALOS_RT_INTRINSIC_V1_MAILBOX_TRY_GET = UINT32_C(0x0001044e),
  TALOS_RT_INTRINSIC_V1_COVERGROUP_CREATE = UINT32_C(0x00010450),
  TALOS_RT_INTRINSIC_V1_COVERGROUP_SET_ENABLED = UINT32_C(0x00010451),
  TALOS_RT_INTRINSIC_V1_COVERGROUP_SAMPLE_ENABLED = UINT32_C(0x00010452),
  TALOS_RT_INTRINSIC_V1_COVERGROUP_FORMAL_READ = UINT32_C(0x00010453),
  TALOS_RT_INTRINSIC_V1_COVERGROUP_INSTANCE_QUERY = UINT32_C(0x00010454),
  TALOS_RT_INTRINSIC_V1_COVERGROUP_TYPE_QUERY = UINT32_C(0x00010455),
  TALOS_RT_INTRINSIC_V1_COVERGROUP_SAMPLE = UINT32_C(0x00010456),
  TALOS_RT_INTRINSIC_V1_RANDOM_SOLVE_WIDE_STATE = UINT32_C(0x00010457),
  TALOS_RT_INTRINSIC_V1_SEMAPHORE_CREATE = UINT32_C(0x00010458),
  TALOS_RT_INTRINSIC_V1_SEMAPHORE_PUT = UINT32_C(0x00010459),
  TALOS_RT_INTRINSIC_V1_SEMAPHORE_TRY_GET = UINT32_C(0x0001045a),
  TALOS_RT_INTRINSIC_V1_EVENT_CREATE = UINT32_C(0x0001045b),
  TALOS_RT_INTRINSIC_V1_STRING_PARSE_LOGIC = UINT32_C(0x0001045c),
  TALOS_RT_INTRINSIC_V1_CONTAINER_SWAP = UINT32_C(0x0001045d),
  TALOS_RT_INTRINSIC_V1_STOCHASTIC_QUEUE = UINT32_C(0x00010463),
  TALOS_RT_INTRINSIC_V1_RANDOM_LEGACY = UINT32_C(0x00010467),
  TALOS_RT_INTRINSIC_V1_COVERAGE_POINT_HIT = UINT32_C(0x00010468),
  TALOS_RT_INTRINSIC_V1_FUNCTIONAL_COVERAGE_GET = UINT32_C(0x00010469),
  TALOS_RT_INTRINSIC_V1_FUNCTIONAL_COVERAGE_SET_DB_NAME =
      UINT32_C(0x0001046a),
  TALOS_RT_INTRINSIC_V1_FUNCTIONAL_COVERAGE_LOAD_DB = UINT32_C(0x0001046b),
  TALOS_RT_INTRINSIC_V1_COVERAGE_CONTROL_DEFINITION = UINT32_C(0x0001046c),
  TALOS_RT_INTRINSIC_V1_COVERAGE_CONTROL_INSTANCE = UINT32_C(0x0001046d),
  TALOS_RT_INTRINSIC_V1_COVERAGE_QUERY_DEFINITION = UINT32_C(0x0001046e),
  TALOS_RT_INTRINSIC_V1_COVERAGE_QUERY_INSTANCE = UINT32_C(0x0001046f),
  TALOS_RT_INTRINSIC_V1_COVERAGE_DATABASE_SAVE = UINT32_C(0x00010470),
  TALOS_RT_INTRINSIC_V1_COVERAGE_DATABASE_MERGE = UINT32_C(0x00010471),
  TALOS_RT_INTRINSIC_V1_COVERGROUP_SET_NAME = UINT32_C(0x00010472),
  TALOS_RT_INTRINSIC_V1_COVERGROUP_CLOCK_EVENT_REGISTER =
      UINT32_C(0x00010473),
  TALOS_RT_INTRINSIC_V1_COVERGROUP_BLOCK_EVENT_REGISTER =
      UINT32_C(0x00010474),
  TALOS_RT_INTRINSIC_V1_COVERGROUP_BLOCK_EVENT_FIRE = UINT32_C(0x00010475),
  TALOS_RT_INTRINSIC_V1_COVERGROUP_SET_INTEGER_OPTION = UINT32_C(0x00010476),
  TALOS_RT_INTRINSIC_V1_COVERGROUP_SET_STRING_OPTION = UINT32_C(0x00010477),
  TALOS_RT_INTRINSIC_V1_COVERGROUP_SET_TYPE_INTEGER_OPTION =
      UINT32_C(0x00010478),
  TALOS_RT_INTRINSIC_V1_COVERGROUP_SET_TYPE_STRING_OPTION =
      UINT32_C(0x00010479),
  TALOS_RT_INTRINSIC_V1_COVERGROUP_GET_INTEGER_OPTION = UINT32_C(0x0001047a),
  TALOS_RT_INTRINSIC_V1_COVERGROUP_GET_STRING_OPTION = UINT32_C(0x0001047b),
  TALOS_RT_INTRINSIC_V1_COVERGROUP_OPTION_REFERENCE = UINT32_C(0x0001047c),
  TALOS_RT_INTRINSIC_V1_COVERGROUP_TYPE_OPTION_REFERENCE =
      UINT32_C(0x0001047d),
  TALOS_RT_INTRINSIC_V1_COVERGROUP_PREPARE = UINT32_C(0x0001047e),
  TALOS_RT_INTRINSIC_V1_COVERGROUP_CANDIDATES = UINT32_C(0x0001047f),
  TALOS_RT_INTRINSIC_V1_COVERGROUP_RELEASE_PREPARATION = UINT32_C(0x00010480),
  TALOS_RT_INTRINSIC_V1_COVERGROUP_BIND_WITH = UINT32_C(0x00010481),
  TALOS_RT_INTRINSIC_V1_RANDOM_INITIALIZATION_ENTER = UINT32_C(0x00010482),
  TALOS_RT_INTRINSIC_V1_RANDOM_INITIALIZATION_LEAVE = UINT32_C(0x00010483),
  TALOS_RT_INTRINSIC_V1_RANDOM_REAL_TO_RANK = UINT32_C(0x00010484),
  TALOS_RT_INTRINSIC_V1_RANDOM_REAL_FROM_RANK = UINT32_C(0x00010485),
  TALOS_RT_INTRINSIC_V1_RANDOM_CYCLE_SAMPLE = UINT32_C(0x00010486),
  TALOS_RT_INTRINSIC_V1_STRING_PARSE_REAL_CONSTANT = UINT32_C(0x00010487),
  TALOS_RT_INTRINSIC_V1_MANAGED_NBA_MASKED = UINT32_C(0x00010488),
  TALOS_RT_INTRINSIC_V1_VPI_ROOT = UINT32_C(0x00011000),
  TALOS_RT_INTRINSIC_V1_VPI_CHILD = UINT32_C(0x00011001),
  TALOS_RT_INTRINSIC_V1_VPI_SIBLING = UINT32_C(0x00011002),
  TALOS_RT_INTRINSIC_V1_VPI_LOOKUP = UINT32_C(0x00011003),
  TALOS_RT_INTRINSIC_V1_VPI_INFO = UINT32_C(0x00011004),
  TALOS_RT_INTRINSIC_V1_VPI_READ = UINT32_C(0x00011005),
  TALOS_RT_INTRINSIC_V1_VPI_WRITE = UINT32_C(0x00011006),
  TALOS_RT_INTRINSIC_V1_VPI_CHILD_AT = UINT32_C(0x00011007),
  TALOS_RT_INTRINSIC_V1_VPI_NAME = UINT32_C(0x00011008),
  TALOS_RT_INTRINSIC_V1_VPI_TYPE_INFO = UINT32_C(0x00011009),
  TALOS_RT_INTRINSIC_V1_VPI_TYPE_CHILD = UINT32_C(0x0001100a),
  TALOS_RT_INTRINSIC_V1_VPI_CALL_PREPARE = UINT32_C(0x00011010),
  TALOS_RT_INTRINSIC_V1_VPI_FRAME_ENTER_PREPARED = UINT32_C(0x00011011),
  TALOS_RT_INTRINSIC_V1_VPI_FRAME_ENTER = UINT32_C(0x00011012),
  TALOS_RT_INTRINSIC_V1_VPI_FRAME_CHECKPOINT = UINT32_C(0x00011013),
  TALOS_RT_INTRINSIC_V1_VPI_FRAME_LEAVE = UINT32_C(0x00011014),
  TALOS_RT_INTRINSIC_V1_VPI_CALL_FINISH = UINT32_C(0x00011015),
  TALOS_RT_INTRINSIC_V1_VPI_RESOLUTION_PREPARE = UINT32_C(0x00011017)
};

// Versioned, target-independent plan consumed by fixed aggregate bit-stream
// exports. All words are little-endian uint64 values. The four-word header is
// followed by fixed six-word records in preorder. A COPY record contributes
// one source bit window. A REPEAT record owns the following body_records and
// repeats that compact child program without making compiler IR proportional
// to the array extent.
enum {
  TALOS_RT_AGGREGATE_BITSTREAM_PLAN_MAGIC = UINT32_C(0x42535450),
  TALOS_RT_AGGREGATE_BITSTREAM_PLAN_VERSION = UINT32_C(1),
  TALOS_RT_AGGREGATE_BITSTREAM_PLAN_HEADER_WORDS = UINT32_C(4),
  TALOS_RT_AGGREGATE_BITSTREAM_PLAN_RECORD_WORDS = UINT32_C(6),
  TALOS_RT_AGGREGATE_BITSTREAM_PLAN_MAX_DEPTH = UINT32_C(1024),
  TALOS_RT_AGGREGATE_BITSTREAM_PLAN_MAX_RECORDS = UINT32_C(1048576),
  TALOS_RT_AGGREGATE_BITSTREAM_COPY = UINT32_C(1),
  TALOS_RT_AGGREGATE_BITSTREAM_REPEAT = UINT32_C(2),
  TALOS_RT_AGGREGATE_BITSTREAM_COPY_LOGIC = UINT32_C(3)
};

// Versioned preorder plan for recursively dynamically sized bit-stream
// sources. The four-word header is followed by six-word records. COPY reads a
// fixed packed window, REPEAT walks one fixed unpacked-array dimension,
// CONTAINER walks one dynamic-array, queue, or associative-array value, and
// STRING appends one managed string as bytes. OBJECT is reserved for the
// class-source extension and is rejected by this version's interpreter.
enum {
  TALOS_RT_RECURSIVE_BITSTREAM_PLAN_MAGIC = UINT32_C(0x42535250),
  TALOS_RT_RECURSIVE_BITSTREAM_PLAN_VERSION = UINT32_C(1),
  TALOS_RT_RECURSIVE_BITSTREAM_PLAN_OBJECT_VERSION = UINT32_C(2),
  TALOS_RT_RECURSIVE_BITSTREAM_PLAN_HEADER_WORDS = UINT32_C(4),
  TALOS_RT_RECURSIVE_BITSTREAM_PLAN_RECORD_WORDS = UINT32_C(6),
  TALOS_RT_RECURSIVE_BITSTREAM_PLAN_MAX_RECORDS = UINT32_C(1048576),
  TALOS_RT_RECURSIVE_BITSTREAM_COPY = UINT32_C(1),
  TALOS_RT_RECURSIVE_BITSTREAM_REPEAT = UINT32_C(2),
  TALOS_RT_RECURSIVE_BITSTREAM_CONTAINER = UINT32_C(3),
  TALOS_RT_RECURSIVE_BITSTREAM_STRING = UINT32_C(4),
  TALOS_RT_RECURSIVE_BITSTREAM_OBJECT = UINT32_C(5)
};

// Versioned equality plan of an TALOS_RT_ASSOC_KEY_ELEMENT index type (IEEE
// 1800-2023 7.8.5). It lists in preorder every part of an index value that the
// type's equality operator compares, so the runtime can hash, compare and order
// indices without knowing their source type. All words are little-endian
// uint64 values: a two-word header {MAGIC | VERSION << 32, record count} and
// four-word records {opcode | body_records << 32, bit offset, operand0,
// operand1}. A record owns the body_records that follow it.
//  - BITS compares operand0 bits. When operand1 is one they are four-state,
//    and an X or Z bit makes the index invalid (7.8.6).
//  - STRING compares a managed string word by value.
//  - OBJECT compares a managed object word by identity. Null is a value.
//  - SEQUENCE compares a dynamic-array or queue word by size and then element
//    by element. Its body describes one element of operand0 bytes, from that
//    element's own planes. Null is the empty array.
//  - ASSOCIATIVE compares an associative-array word by its entries. Its body
//    describes one value of operand0 bytes. Null is the empty array.
//  - REPEAT applies its body operand0 times, operand1 bits apart.
//  - TAGGED compares a tagged union by its operand1-bit tag, operand0 bits past
//    the union, and then by the active member. Its body is ALTERNATIVE records,
//    each naming one member's tag value in operand0 and describing that member
//    in its own body.
// Except for SEQUENCE and ASSOCIATIVE, a body's offsets are relative to the
// position its record's offset names. Two indices are equal when every
// comparison holds; their relative order is fixed for a run but unspecified.
enum {
  TALOS_RT_ASSOC_KEY_PLAN_MAGIC = UINT32_C(0x4b504b41),
  TALOS_RT_ASSOC_KEY_PLAN_VERSION = UINT32_C(1),
  TALOS_RT_ASSOC_KEY_PLAN_HEADER_WORDS = UINT32_C(2),
  TALOS_RT_ASSOC_KEY_PLAN_RECORD_WORDS = UINT32_C(4),
  TALOS_RT_ASSOC_KEY_PLAN_MAX_DEPTH = UINT32_C(64),
  TALOS_RT_ASSOC_KEY_PLAN_MAX_RECORDS = UINT32_C(1048576),
  TALOS_RT_ASSOC_KEY_PLAN_BITS = UINT32_C(1),
  TALOS_RT_ASSOC_KEY_PLAN_STRING = UINT32_C(2),
  TALOS_RT_ASSOC_KEY_PLAN_OBJECT = UINT32_C(3),
  TALOS_RT_ASSOC_KEY_PLAN_SEQUENCE = UINT32_C(4),
  TALOS_RT_ASSOC_KEY_PLAN_ASSOCIATIVE = UINT32_C(5),
  TALOS_RT_ASSOC_KEY_PLAN_REPEAT = UINT32_C(6),
  TALOS_RT_ASSOC_KEY_PLAN_TAGGED = UINT32_C(7),
  TALOS_RT_ASSOC_KEY_PLAN_ALTERNATIVE = UINT32_C(8)
};

// Pointer-free class bit-stream image. Every record is naturally aligned and
// has the same byte layout on native64 and wasm32. Offsets are image-relative;
// counts and ranges are validated before any record is trusted.
// This header is also C. MSVC-compatible compilers type every unfixed enum as
// int, which cannot hold UINT32_MAX, so the sentinel is a macro.
#define TALOS_RT_CLASS_BITSTREAM_NO_BYTECODE UINT32_MAX
enum {
  TALOS_RT_CLASS_BITSTREAM_BLOB_MAGIC = UINT32_C(0x43425342),
  TALOS_RT_CLASS_BITSTREAM_BLOB_VERSION = UINT32_C(1),
  TALOS_RT_CLASS_BITSTREAM_FIELD_FOUR_STATE = UINT32_C(1) << 0
};

typedef struct talos_rt_class_bitstream_header_v1 {
  uint64_t identity;
  uint64_t size;
  uint64_t site_offset;
  uint64_t site_count;
  uint64_t group_offset;
  uint64_t group_count;
  uint64_t member_offset;
  uint64_t member_count;
  uint64_t schema_offset;
  uint64_t schema_count;
  uint64_t field_offset;
  uint64_t field_count;
  uint64_t plan_offset;
  uint64_t plan_size;
  uint64_t reserved0;
  uint64_t reserved1;
} talos_rt_class_bitstream_header_v1;

typedef struct talos_rt_class_bitstream_site_v1 {
  uint64_t site_id;
  uint32_t bytecode_function;
  uint32_t bytecode_site;
  uint64_t plan_offset;
  uint64_t plan_size;
  uint64_t reserved;
} talos_rt_class_bitstream_site_v1;

typedef struct talos_rt_class_bitstream_group_v1 {
  uint64_t group_id;
  uint64_t static_class_id;
  uint64_t first_member;
  uint64_t member_count;
  uint32_t allow_hidden_root;
  uint32_t reserved;
} talos_rt_class_bitstream_group_v1;

typedef struct talos_rt_class_bitstream_schema_v1 {
  uint64_t class_id;
  uint64_t instance_size;
  uint64_t first_field;
  uint64_t field_count;
  uint32_t instance_alignment;
  uint32_t reserved;
  uint64_t reserved_tail;
} talos_rt_class_bitstream_schema_v1;

typedef struct talos_rt_class_bitstream_field_v1 {
  uint64_t offset;
  uint64_t plane_size;
  uint64_t root_span;
  uint64_t plan_offset;
  uint64_t plan_size;
  uint32_t flags;
  uint32_t value_alignment;
} talos_rt_class_bitstream_field_v1;

// Imported zero-time calls are resolved through mutable context bindings. The
// bytecode image contains only a deterministic 32-bit symbol ID and typed
// register metadata; it never contains a host function or data pointer.
// Generation one imports accept arbitrary-width bits/logic, status values,
// opaque four-word stable/reference handles, managed string words, and IEEE
// binary32/binary64 values. A callback may copy a handle but must not
// synthesize its representation. Floating values occupy exactly four or eight
// bytes at the eight-byte-aligned `value`; `limb_count` remains one so
// descriptor counts stay uniform. The callback may inspect inputs and fill the
// zero-initialized output storage.
typedef struct talos_rt_import_input_v1 {
  talos_rt_design_register_kind kind;
  uint8_t flags;
  uint16_t reserved;
  uint32_t bit_width;
  const uint64_t *value;
  const uint64_t *unknown;
  uint64_t limb_count;
} talos_rt_import_input_v1;

typedef struct talos_rt_import_output_v1 {
  talos_rt_design_register_kind kind;
  uint8_t flags;
  uint16_t reserved;
  uint32_t bit_width;
  uint64_t *value;
  uint64_t *unknown;
  uint64_t limb_count;
} talos_rt_import_output_v1;

typedef talos_rt_status (*talos_rt_import_callback_v1)(
    talos_rt_context *context, uint32_t import_id,
    const talos_rt_import_input_v1 *inputs, uint32_t input_count,
    talos_rt_import_output_v1 *outputs, uint32_t output_count,
    void *user_data);

// A call site contains only stable metadata. Generated native code may point
// source_file at an immutable string; bytecode materializes the same record
// from its pointer-free string and call-site sections before entering the
// runtime boundary.
#define TALOS_RT_IMPORT_PURE (UINT32_C(1) << 0)
#define TALOS_RT_IMPORT_CONTEXT (UINT32_C(1) << 1)
#define TALOS_RT_IMPORT_TASK (UINT32_C(1) << 2)

typedef struct talos_rt_import_site_v1 {
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
} talos_rt_import_site_v1;

// VPI bytecode intrinsics use known two-state i64 section offsets as cursors
// and a trailing `status` output. Traversal failures (including EOF) are
// written to that output instead of aborting the interpreter. INFO outputs
// kind, capabilities, stable ID, type cursor, left, right, width, status.
// NAME outputs string-section offset, byte count, status. TYPE_INFO outputs
// the fields of talos_rt_design_type_info_v1 in declaration order followed
// by status. READ and WRITE infer the value width from their numeric register.

// The design database is a DWARF-like byte image. All cursors and links are
// section-relative offsets, never native pointers.
#define TALOS_RT_DESIGN_PROFILE_READ (UINT32_C(1) << 0)
#define TALOS_RT_DESIGN_PROFILE_WRITE (UINT32_C(1) << 1)

typedef uint32_t talos_rt_design_record_kind;
enum {
  TALOS_RT_DESIGN_RECORD_INVALID = 0,
  TALOS_RT_DESIGN_RECORD_SCOPE = 1,
  TALOS_RT_DESIGN_RECORD_STORAGE = 2,
  TALOS_RT_DESIGN_RECORD_NET = 3,
  TALOS_RT_DESIGN_RECORD_DRIVER = 4,
  TALOS_RT_DESIGN_RECORD_PROCESS = 5,
  TALOS_RT_DESIGN_RECORD_TYPE = 6,
  TALOS_RT_DESIGN_RECORD_FUNCTION = 7,
  TALOS_RT_DESIGN_RECORD_PORT = 8,
  // Immutable source object with no executable descriptor or live state.
  TALOS_RT_DESIGN_RECORD_STATIC_OBJECT = 9
};

typedef uint32_t talos_rt_design_capability;
enum {
  TALOS_RT_DESIGN_CAP_READ = UINT32_C(1) << 0,
  TALOS_RT_DESIGN_CAP_WRITE = UINT32_C(1) << 1,
  TALOS_RT_DESIGN_CAP_ITERATE = UINT32_C(1) << 2,
  TALOS_RT_DESIGN_CAP_PORT_INPUT = UINT32_C(1) << 3,
  TALOS_RT_DESIGN_CAP_PORT_OUTPUT = UINT32_C(1) << 4,
  // Marks a compiler-generated record that is deliberately absent from VPI.
  TALOS_RT_DESIGN_CAP_INTERNAL = UINT32_C(1) << 5,
  // Marks a VPI-profile port whose lower connection is an unselected whole
  // storage or net object. Its vpiLowConn relation is mandatory.
  TALOS_RT_DESIGN_CAP_PORT_WHOLE_SOURCE = UINT32_C(1) << 6,
  // Distinguishes a user-defined typedef typespec, whose vpiName is defined,
  // from unnamed built-in and anonymous static typespec records.
  TALOS_RT_DESIGN_CAP_NAMED_TYPESPEC = UINT32_C(1) << 7,
  // The same record-kind-specific bit identifies a physical port whose IEEE
  // direction is ref. Its execution-facing input/output bits remain available
  // to the scheduler and waveform backends.
  TALOS_RT_DESIGN_CAP_PORT_REF = UINT32_C(1) << 7,
  // The same record-kind-specific bit marks a process, function, generic
  // interconnect net, class-static backing storage, or event-array storage
  // whose lexical VPI owner is supplied by generated relations or an exact
  // backing binding instead of
  // its physical executable scope. These kinds cannot be typespec records.
  TALOS_RT_DESIGN_CAP_LEXICAL_ANCHOR = UINT32_C(1) << 7,
  // The same record-kind-specific bit marks a value parameter that directly
  // declares a packed range. Built-in and typedef-provided ranges do not set
  // it, as required by IEEE 1800-2023 37.28 detail 5.
  TALOS_RT_DESIGN_CAP_PARAMETER_EXPLICIT_RANGE = UINT32_C(1) << 7,
  // Static-only lexical type parameters have no effective semantic payload.
  // Their exact ClassDefn owner and source declaration remain authoritative.
  TALOS_RT_DESIGN_CAP_UNRESOLVED_TYPE_PARAMETER = UINT32_C(1) << 8,
  // A present original parameter default has no supported expression graph.
  // Distinct from an absent default; a vpiExpr relation cannot coexist.
  TALOS_RT_DESIGN_CAP_UNAVAILABLE_PARAMETER_DEFAULT = UINT32_C(1) << 9,
  // Lexical generic value declaration: no specialization has supplied a value.
  TALOS_RT_DESIGN_CAP_UNRESOLVED_VALUE_PARAMETER = 1u << 10,
  // Concrete lexical class property with exact source type but no executable
  // field/storage binding. Values are unavailable until specialization.
  TALOS_RT_DESIGN_CAP_SOURCE_CLASS_PROPERTY = UINT32_C(1) << 11,
  // Named typedef of an unresolved lexical class type parameter. This object
  // acts as a typespec, not a parameter declaration with an effective value.
  TALOS_RT_DESIGN_CAP_UNRESOLVED_TYPE_ALIAS = UINT32_C(1) << 12,
  // Immutable automatic declaration; its value belongs to a live VPI frame.
  TALOS_RT_DESIGN_CAP_FRAME_LOCAL = UINT32_C(1) << 13,
  // Definition-backed vpiIODecl views use these record-kind-specific bits
  // for the exact IEEE direction, including vpiRef and undefined.
  TALOS_RT_DESIGN_CAP_LEXICAL_CLASS_TYPESPEC = UINT32_C(1) << 14,
  // Present source inheritance forms without a supported relation graph.
  TALOS_RT_DESIGN_CAP_UNAVAILABLE_CLASS_BASE = UINT32_C(1) << 15,
  TALOS_RT_DESIGN_CAP_UNAVAILABLE_CONSTRUCTOR_ARGUMENTS = UINT32_C(1) << 16,
  TALOS_RT_DESIGN_CAP_UNAVAILABLE_CLASS_PARAMETERS = UINT32_C(1) << 17,
  // ParamAssign whose Rhs is the exact Lhs declaration's original Expr.
  TALOS_RT_DESIGN_CAP_DEFAULT_CLASS_PARAMETER_ASSIGNMENT = UINT32_C(1) << 18,
  // ParamAssign with an explicit source actual, never an effective value.
  TALOS_RT_DESIGN_CAP_EXPLICIT_CLASS_PARAMETER_ASSIGNMENT = UINT32_C(1) << 19,
  // Explicit actual is present but its original RHS form is unsupported.
  TALOS_RT_DESIGN_CAP_UNAVAILABLE_CLASS_PARAMETER_RHS = UINT32_C(1) << 20,
  // Definition-backed vpiIODecl views use these record-kind-specific bits
  // for the exact IEEE direction, including vpiRef and undefined.
  TALOS_RT_DESIGN_CAP_IO_DIRECTION_SHIFT = 8,
  TALOS_RT_DESIGN_CAP_IO_DIRECTION_MASK = UINT32_C(7) << 8,
  TALOS_RT_DESIGN_CAP_PORT_ORDINAL_SHIFT = 8,
  TALOS_RT_DESIGN_CAP_PORT_ORDINAL_MASK = UINT32_C(0xffffff) << 8
};

typedef struct talos_rt_design_cursor_v1 {
  uint64_t offset;
} talos_rt_design_cursor_v1;

typedef struct talos_rt_design_info_v1 {
  talos_rt_design_record_kind kind;
  talos_rt_design_capability capabilities;
  talos_rt_handle_v1 handle;
  uint64_t type_offset;
  int64_t range_left;
  int64_t range_right;
  uint64_t bit_width;
} talos_rt_design_info_v1;

typedef uint32_t talos_rt_design_type_kind;
enum {
  TALOS_RT_DESIGN_TYPE_SCALAR = 1,
  TALOS_RT_DESIGN_TYPE_ARRAY = 2,
  TALOS_RT_DESIGN_TYPE_STRUCT = 3,
  TALOS_RT_DESIGN_TYPE_UNION = 4,
  TALOS_RT_DESIGN_TYPE_FIELD = 5
};

typedef uint32_t talos_rt_design_type_flags;
enum {
  TALOS_RT_DESIGN_TYPE_FOUR_STATE = UINT32_C(1) << 0,
  TALOS_RT_DESIGN_TYPE_SIGNED = UINT32_C(1) << 1,
  TALOS_RT_DESIGN_TYPE_PACKED = UINT32_C(1) << 2,
  TALOS_RT_DESIGN_TYPE_TAGGED = UINT32_C(1) << 3
};

typedef struct talos_rt_design_type_info_v1 {
  talos_rt_design_type_kind kind;
  talos_rt_design_type_flags flags;
  uint64_t bit_width;
  int64_t range_left;
  int64_t range_right;
  talos_rt_design_cursor_v1 element_type;
  talos_rt_design_cursor_v1 first_child;
  uint64_t child_count;
  uint64_t ordinal;
  uint64_t tag_bits;
  uint64_t packed_offset;
} talos_rt_design_type_info_v1;

typedef uint32_t talos_rt_design_semantic_type_flags;
typedef uint32_t talos_rt_design_semantic_type_kind;
enum {
  TALOS_RT_DESIGN_SEMANTIC_UNKNOWN = 0,
  TALOS_RT_DESIGN_SEMANTIC_GENERIC_INTEGRAL = 1,
  TALOS_RT_DESIGN_SEMANTIC_BIT = 2,
  TALOS_RT_DESIGN_SEMANTIC_LOGIC = 3,
  TALOS_RT_DESIGN_SEMANTIC_REG = 4,
  TALOS_RT_DESIGN_SEMANTIC_BYTE = 5,
  TALOS_RT_DESIGN_SEMANTIC_SHORT_INT = 6,
  TALOS_RT_DESIGN_SEMANTIC_INT = 7,
  TALOS_RT_DESIGN_SEMANTIC_LONG_INT = 8,
  TALOS_RT_DESIGN_SEMANTIC_INTEGER = 9,
  TALOS_RT_DESIGN_SEMANTIC_ENUM = 10,
  TALOS_RT_DESIGN_SEMANTIC_TIME = 11,
  TALOS_RT_DESIGN_SEMANTIC_SHORT_REAL = 12,
  TALOS_RT_DESIGN_SEMANTIC_REAL = 13,
  TALOS_RT_DESIGN_SEMANTIC_REALTIME = 14,
  TALOS_RT_DESIGN_SEMANTIC_STRING = 15,
  TALOS_RT_DESIGN_SEMANTIC_CHANDLE = 16,
  TALOS_RT_DESIGN_SEMANTIC_PACKED_ARRAY = 17,
  TALOS_RT_DESIGN_SEMANTIC_UNPACKED_ARRAY = 18,
  TALOS_RT_DESIGN_SEMANTIC_PACKED_STRUCT = 19,
  TALOS_RT_DESIGN_SEMANTIC_UNPACKED_STRUCT = 20,
  TALOS_RT_DESIGN_SEMANTIC_PACKED_UNION = 21,
  TALOS_RT_DESIGN_SEMANTIC_UNPACKED_UNION = 22,
  TALOS_RT_DESIGN_SEMANTIC_DYNAMIC_ARRAY = 23,
  TALOS_RT_DESIGN_SEMANTIC_QUEUE = 24,
  TALOS_RT_DESIGN_SEMANTIC_ASSOC_ARRAY = 25,
  TALOS_RT_DESIGN_SEMANTIC_CLASS = 26,
  TALOS_RT_DESIGN_SEMANTIC_VIRTUAL_INTERFACE = 27,
  TALOS_RT_DESIGN_SEMANTIC_EVENT = 28,
  TALOS_RT_DESIGN_SEMANTIC_PACKED_OPEN_ARRAY = 29,
  TALOS_RT_DESIGN_SEMANTIC_UNPACKED_OPEN_ARRAY = 30,
  TALOS_RT_DESIGN_SEMANTIC_PROCESS = 31,
  TALOS_RT_DESIGN_SEMANTIC_COVERGROUP = 32,
  TALOS_RT_DESIGN_SEMANTIC_MAILBOX = 33,
  TALOS_RT_DESIGN_SEMANTIC_SEMAPHORE = 34,
  TALOS_RT_DESIGN_SEMANTIC_VOID = 35,
  TALOS_RT_DESIGN_SEMANTIC_UNTYPED = 36,
  TALOS_RT_DESIGN_SEMANTIC_SEQUENCE = 37,
  TALOS_RT_DESIGN_SEMANTIC_PROPERTY = 38
};
enum {
  TALOS_RT_DESIGN_SEMANTIC_SIGNED = UINT32_C(1) << 8,
  TALOS_RT_DESIGN_SEMANTIC_FOUR_STATE = UINT32_C(1) << 9,
  TALOS_RT_DESIGN_SEMANTIC_HAS_RANGE = UINT32_C(1) << 10,
  TALOS_RT_DESIGN_SEMANTIC_TAGGED = UINT32_C(1) << 11,
  TALOS_RT_DESIGN_SEMANTIC_SOFT = UINT32_C(1) << 12,
  TALOS_RT_DESIGN_SEMANTIC_WILDCARD_INDEX = UINT32_C(1) << 13,
  // Source four-state identity with value-plane-only executable storage.
  TALOS_RT_DESIGN_SEMANTIC_PROJECTED = UINT32_C(1) << 14
};
enum {
  TALOS_RT_DESIGN_SEMANTIC_PUBLIC_VPI_KIND_SHIFT = 16,
  TALOS_RT_DESIGN_SEMANTIC_PUBLIC_VPI_KIND_MASK = UINT32_C(0xffff) << 16
};

typedef uint32_t talos_rt_design_semantic_edge_role;
enum {
  TALOS_RT_DESIGN_SEMANTIC_EDGE_ENUM_BASE = 1,
  TALOS_RT_DESIGN_SEMANTIC_EDGE_ELEMENT = 2,
  TALOS_RT_DESIGN_SEMANTIC_EDGE_ASSOC_INDEX = 3,
  TALOS_RT_DESIGN_SEMANTIC_EDGE_MEMBER = 4
};

typedef uint32_t talos_rt_design_semantic_edge_flags;
enum {
  TALOS_RT_DESIGN_SEMANTIC_EDGE_NOT_RANDOM = 1,
  TALOS_RT_DESIGN_SEMANTIC_EDGE_RANDOM = 2,
  TALOS_RT_DESIGN_SEMANTIC_EDGE_RANDOM_CYCLIC = 3
};

typedef struct talos_rt_design_semantic_type_info_v1 {
  uint32_t kind;
  talos_rt_design_semantic_type_flags flags;
  uint32_t public_vpi_kind;
  uint32_t first_edge;
  uint32_t edge_count;
  talos_rt_design_cursor_v1 alias_object;
  talos_rt_design_cursor_v1 identity_target;
  const uint8_t *name;
  uint64_t name_size;
  const uint8_t *modport;
  uint64_t modport_size;
  uint32_t queue_bound;
  int64_t range_left;
  int64_t range_right;
  uint64_t bit_width;
  uint64_t tag_bits;
} talos_rt_design_semantic_type_info_v1;

typedef struct talos_rt_design_semantic_type_edge_v1 {
  talos_rt_design_cursor_v1 child;
  talos_rt_design_semantic_edge_role role;
  uint32_t flags;
  uint32_t ordinal;
  const uint8_t *name;
  uint64_t name_size;
  uint64_t packed_offset;
} talos_rt_design_semantic_type_edge_v1;

#define TALOS_RT_DESIGN_STATEMENT_PROTECTED (UINT32_C(1) << 0)
#define TALOS_RT_DESIGN_STATEMENT_SCOPE (UINT32_C(1) << 1)

// Every code form returns through this action ABI. A continuation identifies
// the next fixed fragment state. Suspend payloads are interpreted according to
// suspend_kind (for example, a deadline or stable event descriptor ID).
typedef uint32_t talos_rt_fragment_action_kind;
enum {
  TALOS_RT_FRAGMENT_CONTINUE = 0,
  TALOS_RT_FRAGMENT_SUSPEND = 1,
  TALOS_RT_FRAGMENT_TERMINATE = 2,
  TALOS_RT_FRAGMENT_TASK_CALL = 3,
  // The current activation remains live at continuation but is explicitly
  // suspended until process::resume clears its scheduler state.
  TALOS_RT_FRAGMENT_PROCESS_SUSPEND = 4
};

typedef uint32_t talos_rt_suspend_kind;
enum {
  TALOS_RT_SUSPEND_NONE = 0,
  TALOS_RT_SUSPEND_DELAY = 1,
  TALOS_RT_SUSPEND_CHANGE = 2,
  TALOS_RT_SUSPEND_EDGE = 3,
  TALOS_RT_SUSPEND_EVENT = 4,
  TALOS_RT_SUSPEND_AWAIT = 5,
  TALOS_RT_SUSPEND_JOIN = 6,
  TALOS_RT_SUSPEND_FOREVER = 7,
  TALOS_RT_SUSPEND_FRONTIER = 8,
  TALOS_RT_SUSPEND_CHILDREN = 9,
  TALOS_RT_SUSPEND_OBSERVER = 10,
  // Wait for a managed mailbox predicate. The sole wait entry contains the
  // mailbox object address; flags select NOT_EMPTY or NOT_FULL below.
  TALOS_RT_SUSPEND_MAILBOX = 11,
  // Wait for a managed semaphore acquisition. The sole wait entry contains
  // the semaphore object address and payload is the nonnegative key count.
  TALOS_RT_SUSPEND_SEMAPHORE = 12,
  // Wait for every event entry in order. The scheduler records success or an
  // out-of-order failure at the event publication that determines it.
  TALOS_RT_SUSPEND_EVENT_ORDER = 13
};

#define TALOS_RT_WAIT_MAILBOX_NOT_EMPTY UINT32_C(0)
#define TALOS_RT_WAIT_MAILBOX_NOT_FULL UINT32_C(1)

typedef struct talos_rt_fragment_action_v1 {
  talos_rt_fragment_action_kind kind;
  talos_rt_suspend_kind suspend_kind;
  uint32_t continuation;
  uint32_t flags;
  uint64_t payload;
  uint64_t auxiliary;
} talos_rt_fragment_action_v1;

// Legacy fragments use no action flags. Process actions use FRAME_WAIT_RECORD
// to identify payload/auxiliary as a checked frame-relative offset and size.
#define TALOS_RT_FRAGMENT_FLAGS_NONE UINT32_C(0)
#define TALOS_RT_ACTION_FRAME_WAIT_RECORD (UINT32_C(1) << 0)
// TASK_CALL only: auxiliary carries its exact prepared source CallID.
#define TALOS_RT_ACTION_VPI_CALL_SITE (UINT32_C(1) << 6)
#define TALOS_RT_ACTION_RESUME_REGION_VALID (UINT32_C(1) << 1)
#define TALOS_RT_ACTION_RESUME_REGION_SHIFT 2u
#define TALOS_RT_ACTION_RESUME_REGION_MASK                                   \
  (UINT32_C(7) << TALOS_RT_ACTION_RESUME_REGION_SHIFT)
#define TALOS_RT_ACTION_RESUME_REGION(region)                                \
  (TALOS_RT_ACTION_RESUME_REGION_VALID |                                     \
   ((uint32_t)(region) << TALOS_RT_ACTION_RESUME_REGION_SHIFT))
// IEEE 1800-2023 9.4.2.4, 14.10: resume in the first iteration of the
// process's home region set that follows the Observed region -- an Active-home
// process after all Observed work and before Reactive, a Reactive-home process
// in Reactive. The region is relative to the running process, so a task
// resumes in its caller's region. Exclusive with RESUME_REGION_VALID.
#define TALOS_RT_ACTION_RESUME_POST_OBSERVED (UINT32_C(1) << 5)

typedef talos_rt_status (*talos_rt_native_fragment_v1)(
    talos_rt_context *context, void *frame, uint64_t frame_size,
    uint32_t continuation, talos_rt_fragment_action_v1 *out_action);

// Bytecode instructions are exactly 16 bytes and little endian:
//   opcode:u8, type:u8, dst:u16, src0:u16, src1:u16, immediate:u64.
// Branch immediates are absolute instruction indices. Frame offsets are byte
// offsets and are checked against frame_size before every access. Stable
// scheduler continuation IDs are mapped to entry instructions by the immutable
// descriptor table below; they are never interpreted directly as bytecode PCs.
#define TALOS_RT_BYTECODE_INSTRUCTION_SIZE 16u
// Typed registers live in the process frame rather than being allocated on
// each resume. register_offset marks the end of ordinary frame data and the
// start of register_count fixed-size scratch slots.
#define TALOS_RT_BYTECODE_REGISTER_SIZE 16u

typedef uint8_t talos_rt_bytecode_type;
enum {
  TALOS_RT_BC_TYPE_NONE = 0,
  TALOS_RT_BC_TYPE_U64 = 1,
  TALOS_RT_BC_TYPE_I64 = 2,
  TALOS_RT_BC_TYPE_BOOL = 3,
  TALOS_RT_BC_TYPE_STATUS = 4,
  // Resource values are interpreter-local IDs produced only by validated
  // service calls; they are never native addresses.
  TALOS_RT_BC_TYPE_RESOURCE = 5
};

typedef uint8_t talos_rt_bytecode_opcode;
enum {
  TALOS_RT_BC_NOP = 0,
  TALOS_RT_BC_CONST = 1,
  TALOS_RT_BC_MOVE = 2,
  TALOS_RT_BC_ADD = 3,
  TALOS_RT_BC_SUB = 4,
  TALOS_RT_BC_MUL = 5,
  TALOS_RT_BC_AND = 6,
  TALOS_RT_BC_OR = 7,
  TALOS_RT_BC_XOR = 8,
  TALOS_RT_BC_NOT = 9,
  TALOS_RT_BC_EQ = 10,
  TALOS_RT_BC_ULT = 11,
  TALOS_RT_BC_SLT = 12,
  TALOS_RT_BC_LOAD_FRAME = 13,
  TALOS_RT_BC_STORE_FRAME = 14,
  TALOS_RT_BC_JUMP = 15,
  TALOS_RT_BC_BRANCH_ZERO = 16,
  TALOS_RT_BC_CONTINUE = 17,
  TALOS_RT_BC_SUSPEND = 18,
  TALOS_RT_BC_TERMINATE = 19,
  // CALL_SERVICE's immediate is a service-site index and its destination is a
  // STATUS register. FAIL returns the nonzero status held in source0 through
  // the fragment ABI.
  TALOS_RT_BC_CALL_SERVICE = 20,
  TALOS_RT_BC_FAIL = 21
};

typedef struct talos_rt_bytecode_v1 {
  const uint8_t *code;
  uint64_t code_size;
  const struct talos_rt_bytecode_entry_v1 *entries;
  uint32_t entry_count;
  uint32_t register_count;
  uint64_t register_offset;
  // Generated immutable descriptors may point at a dedicated writable
  // validation record. The runtime always validates caller-controlled tables;
  // this record only reports the most recent result. Null omits that report.
  struct talos_rt_bytecode_validation_v1 *validation;
  const uint8_t *constants;
  uint64_t constant_size;
  const struct talos_rt_bytecode_service_site_v1 *service_sites;
  uint32_t service_site_count;
  uint32_t reserved;
  const struct talos_rt_bytecode_operand_v1 *operands;
  uint64_t operand_count;
} talos_rt_bytecode_v1;

typedef struct talos_rt_bytecode_entry_v1 {
  uint32_t continuation;
  uint32_t instruction;
} talos_rt_bytecode_entry_v1;

typedef struct talos_rt_bytecode_validation_v1 {
  // Runtime-written observation. Incoming state is never trusted. Initialize
  // the whole record to zero; reserved must remain zero and the associated
  // immutable descriptor must not be mutated during dispatch.
  uint32_t state;
  uint32_t reserved;
} talos_rt_bytecode_validation_v1;

enum {
  TALOS_RT_BC_VALIDATION_UNVALIDATED = 0,
  TALOS_RT_BC_VALIDATION_VALID = 2,
  TALOS_RT_BC_VALIDATION_INVALID = 3
};

// Service-aware bytecode adds a constant pool plus immutable, fully validated
// metadata to the v1 program. No service operand contains a native address.
// All offsets and ranges are checked before a service is invoked.
typedef uint8_t talos_rt_bytecode_operand_kind;
enum {
  TALOS_RT_BC_OPERAND_IMMEDIATE = 0,
  TALOS_RT_BC_OPERAND_REGISTER = 1,
  TALOS_RT_BC_OPERAND_FRAME = 2,
  TALOS_RT_BC_OPERAND_CONSTANT = 3,
  TALOS_RT_BC_OPERAND_RESOURCE = 4
};

typedef uint8_t talos_rt_bytecode_operand_direction;
enum {
  TALOS_RT_BC_OPERAND_INPUT = 0,
  TALOS_RT_BC_OPERAND_OUTPUT = 1,
  TALOS_RT_BC_OPERAND_INOUT = 2
};

typedef uint8_t talos_rt_bytecode_value_kind;
enum {
  TALOS_RT_BC_VALUE_NONE = 0,
  TALOS_RT_BC_VALUE_U8 = 1,
  TALOS_RT_BC_VALUE_U32 = 2,
  TALOS_RT_BC_VALUE_I32 = 3,
  TALOS_RT_BC_VALUE_U64 = 4,
  TALOS_RT_BC_VALUE_I64 = 5,
  TALOS_RT_BC_VALUE_BYTES = 6,
  TALOS_RT_BC_VALUE_MUTABLE_BYTES = 7,
  TALOS_RT_BC_VALUE_ARGUMENT_ARRAY = 8,
  TALOS_RT_BC_VALUE_FORMAT_ENVIRONMENT = 9,
  TALOS_RT_BC_VALUE_BUFFER = 10,
  // The following kinds are valid only as children of ARGUMENT_ARRAY.
  TALOS_RT_BC_VALUE_ARGUMENT_EMPTY = 11,
  TALOS_RT_BC_VALUE_ARGUMENT_LOGIC = 12,
  TALOS_RT_BC_VALUE_ARGUMENT_STRING = 13,
  TALOS_RT_BC_VALUE_ARGUMENT_REAL = 14,
  TALOS_RT_BC_VALUE_ARGUMENT_TIME = 15
};

typedef uint32_t talos_rt_bytecode_service;
enum {
  TALOS_RT_BC_SERVICE_FORMAT = 1,
  TALOS_RT_BC_SERVICE_DISPLAY = 2,
  TALOS_RT_BC_SERVICE_BUFFER_RELEASE = 3,
  TALOS_RT_BC_SERVICE_FILE_OPEN_MCD = 10,
  TALOS_RT_BC_SERVICE_FILE_OPEN = 11,
  TALOS_RT_BC_SERVICE_FILE_CLOSE = 12,
  TALOS_RT_BC_SERVICE_FILE_FLUSH = 13,
  TALOS_RT_BC_SERVICE_FILE_WRITE = 14,
  TALOS_RT_BC_SERVICE_FILE_READ = 15,
  TALOS_RT_BC_SERVICE_FILE_GETC = 16,
  TALOS_RT_BC_SERVICE_FILE_UNGETC = 17,
  TALOS_RT_BC_SERVICE_FILE_GETLINE = 18,
  TALOS_RT_BC_SERVICE_FILE_EOF = 19,
  TALOS_RT_BC_SERVICE_FILE_ERROR = 20,
  TALOS_RT_BC_SERVICE_FILE_SEEK = 21,
  TALOS_RT_BC_SERVICE_FILE_TELL = 22,
  TALOS_RT_BC_SERVICE_FILE_REWIND = 23
};

// value is an immediate, register index, byte offset, or first child operand
// according to kind/value_kind. size is a byte/bit count or child count.
// A nonempty FORMAT_ENVIRONMENT has five children: scope bytes, library.cell
// bytes, U32 time width, suffix bytes, and a nonzero U64 time multiplier.
// auxiliary is used only by ARGUMENT_LOGIC: UINT64_MAX means a known value;
// otherwise it is the checked byte offset of the unknown plane in the same
// frame or constant pool as the value plane. flags use TALOS_RT_ARG_* for
// argument children and must otherwise be zero. MUTABLE_BYTES is INOUT. A
// writable frame range in one service site must not overlap any other operand's
// frame range in that site.
typedef struct talos_rt_bytecode_operand_v1 {
  talos_rt_bytecode_operand_kind kind;
  talos_rt_bytecode_operand_direction direction;
  talos_rt_bytecode_value_kind value_kind;
  uint8_t flags;
  uint32_t reserved;
  uint64_t value;
  uint64_t size;
  uint64_t auxiliary;
} talos_rt_bytecode_operand_v1;

typedef struct talos_rt_bytecode_service_site_v1 {
  talos_rt_bytecode_service service;
  uint32_t first_operand;
  uint16_t operand_count;
  uint16_t flags;
  uint32_t reserved;
} talos_rt_bytecode_service_site_v1;

// Canonical process frames are shared by every executable tier. Layout fields
// are sorted by offset and describe all compiler-owned ranges, including the
// live planes of four-state values and scheduler wait records. Value/unknown
// planes are paired when both remain live; precise managed-root partitioning
// can leave an independently described unknown plane. ABI-required padding
// may appear between planes. Native addresses are forbidden in the canonical
// frame.
typedef uint32_t talos_rt_frame_field_kind;
enum {
  TALOS_RT_FRAME_CAPTURE = 1,
  TALOS_RT_FRAME_CONTINUATION = 2,
  TALOS_RT_FRAME_LIVE = 3,
  TALOS_RT_FRAME_WAIT = 4
};

typedef uint32_t talos_rt_frame_field_flags;
enum {
  TALOS_RT_FRAME_FIELD_FLAGS_NONE = 0,
  TALOS_RT_FRAME_FOUR_STATE_VALUE = 1u << 0,
  TALOS_RT_FRAME_FOUR_STATE_UNKNOWN = 1u << 1,
  // The field is an aligned 64-bit managed-word slot traced for the lifetime
  // of its process activation, including while that activation is suspended.
  TALOS_RT_FRAME_MANAGED_ROOT = 1u << 2,
  // The field overlaps ordinary union bits. `reserved` carries an
  // TALOS_RT_MANAGED_ROOT_KIND_* mask and the word is traced only when it
  // currently names an allowed live managed object.
  TALOS_RT_FRAME_CANDIDATE_ROOT = 1u << 3
};

typedef struct talos_rt_frame_field_v1 {
  talos_rt_frame_field_kind kind;
  talos_rt_frame_field_flags flags;
  uint64_t offset;
  uint64_t size;
  uint32_t alignment;
  uint32_t reserved;
} talos_rt_frame_field_v1;

typedef struct talos_rt_frame_layout_v1 {
  uint32_t version;
  uint32_t flags;
  uint64_t frame_size;
  uint64_t frame_alignment;
  const talos_rt_frame_field_v1 *fields;
  uint32_t field_count;
  uint32_t continuation_count;
  const uint32_t *continuations;
  uint64_t checksum;
} talos_rt_frame_layout_v1;

// A wait record is stored entirely inside the canonical frame. The action
// payload is its frame-relative offset and action auxiliary is its byte size.
// One entry follows the header for every watched stable handle. Signal waits
// preserve a requested edge per entry; other wait families use EDGE_NONE.
typedef uint32_t talos_rt_wait_flags;
enum {
  TALOS_RT_WAIT_FLAGS_NONE = 0,
  // A level wait has one CHANGE entry and latches an occurrence whenever the
  // complete watched value is true immediately after an overlapping store.
  TALOS_RT_WAIT_LEVEL_TRUE = UINT32_C(1) << 0,
  // An iff wait has one primary edge entry followed by an EDGE_NONE condition
  // entry. The condition is sampled only when the primary event occurs.
  TALOS_RT_WAIT_EDGE_IFF = UINT32_C(1) << 1,
  // A direct signal wait ignores publications from the currently executing
  // logical process when a write must not satisfy the future wait, such as a
  // nested control that has not been reached yet.
  TALOS_RT_WAIT_SUPPRESS_ACTIVE_SELF = UINT32_C(1) << 2,
  // A compiler-generated assertion coordinator records every occurrence of
  // up to 64 frozen direct clocks, grouped by exact scheduler publication
  // wave. The payload is its nonzero compiler site and the auxiliary word is
  // the per-primary iff-condition presence mask.
  TALOS_RT_WAIT_CLOCK_OCCURRENCE = UINT32_C(1) << 3,
  // A Clause 31 clock-occurrence coordinator whose same-time decision must
  // run only after every ordinary producer region and barrier at the current
  // numeric simulation time is quiescent, but before Postponed or time
  // advance. This modifier is legal only with WAIT_CLOCK_OCCURRENCE and uses
  // the existing wait-record flags word and occurrence queue.
  TALOS_RT_WAIT_CLOCK_OCCURRENCE_SLOT_FINAL = UINT32_C(1) << 4,
  // At least one clock condition is a compiled observer. Its ordinary
  // condition entry carries the evaluator ID and capture count; existing
  // computed-capture records follow all wait entries. Direct-only waits never
  // set this bit and retain their byte-for-byte frame representation.
  TALOS_RT_WAIT_CLOCK_OCCURRENCE_OBSERVERS = UINT32_C(1) << 5
};
typedef uint32_t talos_rt_wait_edge_kind;
// A macro for the same reason as TALOS_RT_CLASS_BITSTREAM_NO_BYTECODE.
#define TALOS_RT_WAIT_EDGE_NONE UINT32_MAX
enum {
  TALOS_RT_WAIT_EDGE_CHANGE = 0,
  TALOS_RT_WAIT_EDGE_POSEDGE = 1,
  TALOS_RT_WAIT_EDGE_NEGEDGE = 2,
  TALOS_RT_WAIT_EDGE_BOTH = 3,
  // Clock-occurrence waits alone may encode an IEEE 1800-2017 31.5
  // transition-descriptor mask in the otherwise ordinary edge word. Bits
  // 0--5 are 01, 0x, 10, 1x, x0, and x1 respectively. The marker keeps every
  // existing wait record and ordinary direct-wait edge value unchanged.
  TALOS_RT_WAIT_EDGE_TRANSITION_MASK = UINT32_C(0x100),
  TALOS_RT_WAIT_EDGE_TRANSITION_CLASSES = UINT32_C(0x3f)
};

enum {
  // A clock condition entry uses its otherwise-unused edge word to freeze the
  // small Clause 31.7 predicate. NONE remains the legacy known-one spelling.
  TALOS_RT_WAIT_CONDITION_PREDICATE = UINT32_C(0x200),
  TALOS_RT_WAIT_CONDITION_KNOWN_ONE = UINT32_C(0x200),
  TALOS_RT_WAIT_CONDITION_KNOWN_ZERO = UINT32_C(0x201),
  TALOS_RT_WAIT_CONDITION_LOGICAL_EQ_ZERO = UINT32_C(0x202),
  TALOS_RT_WAIT_CONDITION_LOGICAL_EQ_ONE = UINT32_C(0x203),
  TALOS_RT_WAIT_CONDITION_LOGICAL_NE_ZERO = UINT32_C(0x204),
  TALOS_RT_WAIT_CONDITION_LOGICAL_NE_ONE = UINT32_C(0x205),
  TALOS_RT_WAIT_CONDITION_CASE_EQ_ZERO = UINT32_C(0x206),
  TALOS_RT_WAIT_CONDITION_CASE_EQ_ONE = UINT32_C(0x207),
  TALOS_RT_WAIT_CONDITION_CASE_NE_ZERO = UINT32_C(0x208),
  TALOS_RT_WAIT_CONDITION_CASE_NE_ONE = UINT32_C(0x209),
  TALOS_RT_WAIT_CONDITION_OBSERVER = UINT32_C(0x400),
  TALOS_RT_WAIT_CONDITION_OBSERVER_LAST = UINT32_C(0x409)
};

typedef struct talos_rt_wait_record_v1 {
  uint32_t version;
  talos_rt_suspend_kind kind;
  uint32_t flags;
  uint32_t count;
  uint64_t payload;
  uint64_t auxiliary;
} talos_rt_wait_record_v1;

typedef struct talos_rt_wait_entry_v1 {
  uint64_t stable_id;
  talos_rt_wait_edge_kind edge;
  uint32_t reserved;
} talos_rt_wait_entry_v1;

// `reserved` uses this sentinel when `stable_id` is a managed-watch token
// rather than a packed signal range. Managed watches support change events.
#define TALOS_RT_WAIT_WIDTH_MANAGED 0u

#define TALOS_RT_COMPUTED_WAIT_FLAGS_NONE 0u
#define TALOS_RT_COMPUTED_WAIT_INTERLEAVED (UINT32_C(1) << 0)
#define TALOS_RT_OBSERVER_CONDITION_NONE UINT32_MAX
#define TALOS_RT_COMPUTED_CLAUSE_EVENT_PRIMARY (UINT32_C(1) << 0)
#define TALOS_RT_COMPUTED_CLAUSE_LEVEL_TRUE (UINT32_C(1) << 1)

typedef struct talos_rt_computed_wait_record_v1 {
  uint32_t version;
  talos_rt_suspend_kind kind;
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
} talos_rt_computed_wait_record_v1;

typedef struct talos_rt_computed_observer_v1 {
  uint64_t code_unit_id;
  uint32_t capture_begin;
  uint32_t capture_count;
  uint32_t dependency_begin;
  uint32_t dependency_count;
  uint32_t previous_offset;
  uint32_t reserved;
} talos_rt_computed_observer_v1;

typedef struct talos_rt_computed_capture_v1 {
  uint64_t stable_id;
  uint64_t payload0;
  uint64_t payload1;
  uint64_t payload2;
} talos_rt_computed_capture_v1;

typedef uint32_t talos_rt_observer_dependency_kind;
enum {
  TALOS_RT_OBSERVER_DEPENDENCY_SIGNAL = 1,
  TALOS_RT_OBSERVER_DEPENDENCY_EVENT = 2,
  TALOS_RT_OBSERVER_DEPENDENCY_MANAGED = 3,
  // stable_id is the absolute capture-table index of an ArgumentRef capture.
  TALOS_RT_OBSERVER_DEPENDENCY_ARGUMENT_REF = 4
};

typedef struct talos_rt_computed_dependency_v1 {
  uint64_t stable_id;
  talos_rt_observer_dependency_kind kind;
  uint32_t width;
} talos_rt_computed_dependency_v1;

typedef struct talos_rt_computed_clause_v1 {
  uint32_t primary_observer;
  uint32_t condition_observer;
  talos_rt_wait_edge_kind edge;
  uint32_t flags;
} talos_rt_computed_clause_v1;

typedef struct talos_rt_process_instance_v1 talos_rt_process_instance_v1;

typedef talos_rt_status (*talos_rt_native_requirements_v1)(
    uint64_t *out_size, uint64_t *out_alignment);
typedef talos_rt_status (*talos_rt_native_process_execute_v1)(
    talos_rt_process_instance_v1 *instance);
typedef void (*talos_rt_native_process_destroy_v1)(
    talos_rt_process_instance_v1 *instance);

typedef uint32_t talos_rt_execution_tier;
enum { TALOS_RT_TIER_NATIVE = 1, TALOS_RT_TIER_BYTECODE = 2 };
#define TALOS_RT_TIER_MASK_NATIVE (UINT32_C(1) << 0)
#define TALOS_RT_TIER_MASK_BYTECODE (UINT32_C(1) << 1)

typedef uint32_t talos_rt_process_lifecycle;
enum {
  TALOS_RT_PROCESS_READY = 0,
  TALOS_RT_PROCESS_EXECUTING = 1,
  TALOS_RT_PROCESS_SUSPENDED = 2,
  TALOS_RT_PROCESS_TERMINATED = 3
};

// Opaque SystemVerilog process identities use one shared 64-bit namespace.
// Native scheduler tokens carry the high-bit tag; design-bytecode tokens keep
// it clear. Zero is the null process handle. Tokens are never reused within a
// context and remain valid as tombstones after termination.
#define TALOS_RT_LOGICAL_PROCESS_NATIVE_TAG (UINT64_C(1) << 63)

// IEEE 1800 process::state ordinals. Keep these values aligned with Slang's
// built-in std::process declaration and the Simulation IR contract.
typedef uint32_t talos_rt_process_state;
enum {
  TALOS_RT_PROCESS_FINISHED = 0,
  TALOS_RT_PROCESS_RUNNING = 1,
  TALOS_RT_PROCESS_WAITING = 2,
  TALOS_RT_PROCESS_EXPLICITLY_SUSPENDED = 3,
  TALOS_RT_PROCESS_KILLED = 4
};

typedef uint32_t talos_rt_process_control_kind;
enum {
  TALOS_RT_PROCESS_CONTROL_KILL = 0,
  TALOS_RT_PROCESS_CONTROL_SUSPEND = 1,
  TALOS_RT_PROCESS_CONTROL_RESUME = 2
};

typedef uint32_t talos_rt_process_control_disposition;
enum {
  TALOS_RT_PROCESS_CONTROL_CONTINUE = 0,
  TALOS_RT_PROCESS_CONTROL_SUSPEND_CURRENT = 1,
  TALOS_RT_PROCESS_CONTROL_KILL_CURRENT = 2
};

// Compiler certificate: this native entry and its callees do not access the
// managed heap. Unmarked entries retain the ordinary managed execution scope.
#define TALOS_RT_PROCESS_UNMANAGED_NATIVE UINT32_C(1)
// The descriptor has an talos_rt_table_process_descriptor_v1 trailing plan.
// Unflagged v1 descriptors retain their original size and layout.
#define TALOS_RT_PROCESS_TABLE_NATIVE UINT32_C(2)
// Frozen bytecode closure certificate: no canonical state mutation, unknown
// footprint, foreign call or reentrant callback in any reachable callee.
#define TALOS_RT_PROCESS_BYTECODE_READ_ONLY UINT32_C(4)

typedef struct talos_rt_process_descriptor_v1 {
  talos_rt_handle_v1 handle;
  uint32_t version;
  uint32_t flags;
  uint32_t available_tiers;
  uint32_t reserved;
  const talos_rt_frame_layout_v1 *frame_layout;
  talos_rt_native_requirements_v1 native_requirements;
  talos_rt_native_process_execute_v1 native_execute;
  talos_rt_native_process_destroy_v1 native_destroy;
  const talos_rt_bytecode_v1 *bytecode;
  const talos_rt_execution_descriptor_v1 *execution;
  const talos_rt_design_bytecode_entry_v1 *design_bytecode;
} talos_rt_process_descriptor_v1;

// A dense entry index selects the corresponding canonical continuation in
// frame_layout->continuations. Return a wait-table index, or TERMINATE. The
// instance status carries runtime failures independently of that result.
#define TALOS_RT_TABLE_TERMINATE UINT32_MAX
typedef uint32_t (*talos_rt_table_process_entry_v1)(
    talos_rt_process_instance_v1 *instance, uint32_t entry);
typedef struct talos_rt_table_watch_v1 {
  uint64_t capture_offset;
  uint32_t edge;
  uint32_t width;
} talos_rt_table_watch_v1;
typedef struct talos_rt_table_wait_v1 {
  uint32_t continuation;
  uint32_t action_flags;
  uint64_t frame_offset;
  uint64_t frame_size;
  talos_rt_wait_record_v1 record;
  const talos_rt_table_watch_v1 *watches;
} talos_rt_table_wait_v1;
typedef struct talos_rt_table_process_plan_v1 {
  uint32_t version;
  uint32_t wait_count;
  talos_rt_table_process_entry_v1 entry;
  const talos_rt_table_wait_v1 *waits;
} talos_rt_table_process_plan_v1;
typedef struct talos_rt_table_process_descriptor_v1 {
  talos_rt_process_descriptor_v1 base;
  const talos_rt_table_process_plan_v1 *plan;
} talos_rt_table_process_descriptor_v1;

talos_rt_status
talos_rt_v1_table_process_execute(talos_rt_process_instance_v1 *instance);

// Compiler-emitted spawn data. Captures occupy the initial frame prefix;
// continuation storage and wait records are initialized by the runtime.
#define TALOS_RT_SPAWN_PRIME UINT32_C(1)
#define TALOS_RT_SPAWN_PROGRAM_OWNER UINT32_C(2)
typedef struct talos_rt_process_spawn_plan_v1 {
  const talos_rt_process_descriptor_v1 *descriptor;
  uint64_t capture_size;
  const uint32_t *continuations;
  const uint32_t *ranks;
  const uint32_t *bytecode_continuations;
  uint64_t program_owner;
  uint32_t schedule_flags;
  uint32_t actor_slot; // UINT32_MAX selects the generic scheduler.
  uint32_t initial_rank;
  uint32_t continuation_count;
  uint32_t bytecode_continuation_count;
  uint32_t options;
} talos_rt_process_spawn_plan_v1;

// One row of a spawn table. Captures live in the table's shared capture blob
// at capture_offset bytes. A nonzero initialization_scope binds that
// instance's initialization RNG around the spawn, exactly as an
// initialization_enter/leave pair around a single spawn would: IEEE 1800-2023
// 18.14.1 seeds a static process from "the next value from the initialization
// RNG of the module instance ... containing the thread declaration".
typedef struct talos_rt_process_spawn_entry_v1 {
  const talos_rt_process_spawn_plan_v1 *plan;
  uint64_t capture_offset;
  uint64_t initialization_scope;
} talos_rt_process_spawn_entry_v1;

// Captures have already been retained by the caller, just as for an ordinary
// generated spawn. Insertion transfers instance ownership to the scheduler.
uint64_t
talos_rt_v1_process_spawn(talos_rt_context *context,
                            const talos_rt_process_spawn_plan_v1 *plan,
                            const void *captures);
// Spawn failures are reported through scheduler_fail and the remaining rows
// still spawn, as separate spawn calls would. A failed initialization binding
// stops the table and is returned to the caller.
talos_rt_status talos_rt_v1_process_spawn_batch(
    talos_rt_context *context,
    const talos_rt_process_spawn_entry_v1 *entries, const void *captures,
    uint32_t count);

// The runtime owns this record and its allocation. Generated native hooks may
// read the fixed fields in order to refresh transient pointers before every
// resume; other clients must treat the contents as read-only.
struct talos_rt_process_instance_v1 {
  const talos_rt_process_descriptor_v1 *descriptor;
  void *allocation;
  void *frame;
  uint64_t frame_size;
  uint64_t scratch_offset;
  uint64_t scratch_size;
  void *native_handle;
  uint32_t continuation;
  talos_rt_execution_tier tier;
  talos_rt_process_lifecycle lifecycle;
  talos_rt_status status;
  talos_rt_context *context;
  talos_rt_fragment_action_v1 *action;
  // Persistent owner for automatic allocations; unlike context above this is
  // retained between fragments until termination or explicit destruction.
  talos_rt_context *ownership_context;
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
talos_rt_status talos_rt_v1_process_instance_create(
    const talos_rt_process_descriptor_v1 *descriptor,
    talos_rt_process_instance_v1 **out_instance);
talos_rt_status
talos_rt_v1_process_instance_frame(talos_rt_process_instance_v1 *instance,
                                     void **out_frame, uint64_t *out_size);
talos_rt_status talos_rt_v1_process_instance_execute(
    talos_rt_process_instance_v1 *instance, talos_rt_context *context,
    talos_rt_execution_tier requested_tier,
    talos_rt_fragment_action_v1 *out_action);
talos_rt_status talos_rt_v1_process_instance_destroy(
    talos_rt_process_instance_v1 *instance);

// Cold DPI re-entry service. Takes ownership of a ready exported-task
// activation, runs the nested scheduler until that logical task finishes, and
// returns TALOS_RT_DPI_DISABLE_UNSUPPORTED when it was disabled. The legacy
// status spelling is retained for ABI stability; it now denotes the standard
// Clause 35.9 disabled return rather than an unsupported feature.
talos_rt_status
talos_rt_v1_dpi_export_task_run(talos_rt_context *context,
                                  talos_rt_process_instance_v1 *instance);

// Compiler-emitted bytecode exported-task marshalling entry. `directions`
// contains one 0=input, 1=output, or 2=inout byte per logical formal. Compact
// aggregate plans are null/zero for non-aggregate formals.
talos_rt_status talos_rt_v1_dpi_export_task_bytecode_run(
    talos_rt_context *context, uint32_t export_id,
    const talos_rt_import_input_v1 *inputs, uint32_t input_count,
    talos_rt_import_output_v1 *outputs, uint32_t output_count,
    const uint8_t *directions, const int64_t *const *aggregate_plans,
    const uint64_t *aggregate_plan_words);

// Construct a context bound to a generated design. The legacy constructor is
// equivalent to passing a null execution descriptor and remains useful for
// formatting/file-only clients.
talos_rt_status talos_rt_v1_context_create_for_design(
    const talos_rt_execution_descriptor_v1 *execution,
    talos_rt_context **out_context);

// Register a generated worker lane with the managed heap. A lane is inactive
// after creation; generated code brackets executable fragments with enter and
// leave so a collector never waits for an idle host thread. Active lanes must
// reach allocation or explicit safepoint calls.
talos_rt_status
talos_rt_v1_gc_lane_create(talos_rt_context *context,
                             talos_rt_gc_lane_v1 **out_lane);
talos_rt_status talos_rt_v1_gc_lane_destroy(talos_rt_gc_lane_v1 *lane);
talos_rt_status talos_rt_v1_gc_lane_enter(talos_rt_gc_lane_v1 *lane);
talos_rt_status talos_rt_v1_gc_lane_leave(talos_rt_gc_lane_v1 *lane);
talos_rt_status talos_rt_v1_gc_safepoint(talos_rt_gc_lane_v1 *lane);
// Return the active managed lane for the calling generated worker. Execution
// entry points establish this binding once per fragment/process activation;
// class intrinsics do not allocate or register a lane per operation.
talos_rt_gc_lane_v1 *
talos_rt_v1_gc_current_lane(talos_rt_context *context);

// Root records are caller-owned and allocation-free. Static roots are
// registered infrequently and remain live until explicitly unregistered.
talos_rt_status talos_rt_v1_gc_root_push(talos_rt_gc_lane_v1 *lane,
                                             talos_rt_gc_root_v1 *root,
                                             talos_rt_object_v1 **slot);
talos_rt_status talos_rt_v1_gc_root_pop(talos_rt_gc_lane_v1 *lane,
                                            talos_rt_gc_root_v1 *root);
talos_rt_status
talos_rt_v1_gc_root_range_push(talos_rt_gc_lane_v1 *lane,
                                 talos_rt_gc_root_range_v1 *range,
                                 talos_rt_object_v1 **slots, uint64_t count);
talos_rt_status
talos_rt_v1_gc_root_range_pop(talos_rt_gc_lane_v1 *lane,
                                talos_rt_gc_root_range_v1 *range);
talos_rt_status
talos_rt_v1_gc_managed_root_push(talos_rt_gc_lane_v1 *lane,
                                   talos_rt_gc_managed_root_v1 *root,
                                   talos_rt_managed_word_v1 *slot);
talos_rt_status
talos_rt_v1_gc_managed_root_pop(talos_rt_gc_lane_v1 *lane,
                                  talos_rt_gc_managed_root_v1 *root);
talos_rt_status talos_rt_v1_gc_managed_root_range_push(
    talos_rt_gc_lane_v1 *lane, talos_rt_gc_managed_root_range_v1 *range,
    talos_rt_managed_word_v1 *slots, uint64_t count);
talos_rt_status talos_rt_v1_gc_managed_root_range_pop(
    talos_rt_gc_lane_v1 *lane, talos_rt_gc_managed_root_range_v1 *range);
// Convert an overlapping union word into a valid temporary root. The result
// is the original word only when it is an immediate string or a live object
// in `context` whose kind is present in `allowed_kinds`; ordinary or stale
// bits produce zero. This function never allocates or collects.
talos_rt_managed_word_v1 talos_rt_v1_gc_candidate_root(
    talos_rt_context *context, talos_rt_managed_word_v1 word,
    talos_rt_managed_root_kind_mask_v1 allowed_kinds);
talos_rt_status
talos_rt_v1_gc_static_root_register(talos_rt_context *context,
                                      talos_rt_object_v1 **slot);
talos_rt_status talos_rt_v1_gc_candidate_static_root_register(
    talos_rt_context *context, talos_rt_managed_word_v1 *slot,
    talos_rt_managed_root_kind_mask_v1 allowed_kinds);
talos_rt_status
talos_rt_v1_gc_static_root_unregister(talos_rt_context *context,
                                        talos_rt_object_v1 **slot);

// External integrations that cannot expose a root slot may pin an object.
// Pins are exact roots and must be balanced before context destruction.
talos_rt_status talos_rt_v1_gc_pin(talos_rt_context *context,
                                       talos_rt_object_v1 *object);
talos_rt_status talos_rt_v1_gc_unpin(talos_rt_context *context,
                                         talos_rt_object_v1 *object);

// Request a stop-the-world collection. The requesting lane participates in
// root publication; every other active lane parks at its next safepoint.
talos_rt_status talos_rt_v1_gc_collect(talos_rt_gc_lane_v1 *lane);
// Designated collection. After designate, allocation records collection
// pressure instead of collecting (unless pressure runs far ahead), and the
// heap reclaims at collect_pending calls that the schedule places where no
// managed activation is running, such as once per periodic clock cycle.
// collect_pending is a no-op without pending pressure and defers when called
// from inside a managed activation.
talos_rt_status talos_rt_v1_gc_collect_pending(talos_rt_context *context);
talos_rt_status talos_rt_v1_gc_designate(talos_rt_context *context);
talos_rt_status talos_rt_v1_gc_set_threshold(talos_rt_context *context,
                                                 uint64_t allocation_bytes);
talos_rt_status
talos_rt_v1_gc_statistics(talos_rt_context *context,
                            talos_rt_gc_statistics_v1 *out_statistics);

// Validate immutable compiler-emitted class metadata without allocating.
talos_rt_status
talos_rt_v1_class_validate(const talos_rt_class_descriptor_v1 *descriptor);
// Register immutable generated metadata with one context. The registry is
// keyed by stable class ID so pointer-free bytecode can resolve descriptors.
talos_rt_status
talos_rt_v1_class_register(talos_rt_context *context,
                             const talos_rt_class_descriptor_v1 *descriptor);

// Discover the active recursive rand-object set required by IEEE 1800-2017
// 18.5.9. Each object identity occurs once in deterministic breadth-first
// order. Null and rand_mode-disabled edges are omitted. The graph pins its
// objects until destroy and therefore must not outlive its context.
talos_rt_status
talos_rt_v1_random_graph_discover(talos_rt_gc_lane_v1 *lane,
                                    talos_rt_object_v1 *root,
                                    talos_rt_random_graph_v1 **out_graph);
void talos_rt_v1_random_graph_destroy(talos_rt_random_graph_v1 *graph);
uint64_t
talos_rt_v1_random_graph_size(const talos_rt_random_graph_v1 *graph);
talos_rt_object_v1 *
talos_rt_v1_random_graph_object(const talos_rt_random_graph_v1 *graph,
                                  uint64_t index);
const talos_rt_class_descriptor_v1 *
talos_rt_v1_random_graph_object_descriptor(
    const talos_rt_random_graph_v1 *graph, uint64_t index);
// Active direct packed variables are flattened in graph-object order and,
// within each object, base-to-derived declaration order. Returned descriptor
// pointers remain valid for the registered class lifetime; objects remain
// pinned until graph destruction.
uint64_t talos_rt_v1_random_graph_variable_count(
    const talos_rt_random_graph_v1 *graph);
talos_rt_status talos_rt_v1_random_graph_variable(
    const talos_rt_random_graph_v1 *graph, uint64_t index,
    talos_rt_object_v1 **out_object,
    const talos_rt_random_variable_v1 **out_variable);
// Resolve a template reference from one graph object. `out_graph_index` is the
// flattened active-variable index, or UINT64_MAX when the referenced variable
// is currently a state variable because its rand_mode is disabled. Every path
// target must belong to the active graph; null or inactive handles are invalid
// here and are represented by separate state-capture template operands.
talos_rt_status talos_rt_v1_random_graph_resolve_variable(
    const talos_rt_random_graph_v1 *graph, uint64_t origin_index,
    const talos_rt_random_variable_reference_v1 *reference,
    talos_rt_object_v1 **out_object,
    const talos_rt_random_variable_v1 **out_variable,
    uint64_t *out_graph_index);

// Validate and register immutable compiler-emitted element metadata. Repeated
// registration of an equivalent descriptor is accepted; conflicting use of a
// stable type ID is rejected.
talos_rt_status talos_rt_v1_element_type_validate(
    const talos_rt_element_type_v1 *descriptor);
talos_rt_status talos_rt_v1_element_type_register(
    talos_rt_context *context, const talos_rt_element_type_v1 *descriptor);
// Declare the default value of every element of stable type `type_id`, in the
// default_image layout above, for an element type whose default is not
// Table 6-7's, such as an unpacked struct with member initializers
// (IEEE 1800-2023 7.2.2). Containers of that type created afterwards use it
// for every element they create and every nonexistent entry they read.
talos_rt_status talos_rt_v1_element_type_define_default(
    talos_rt_context *context, uint64_t type_id, const uint8_t *image,
    uint64_t image_size);

// Register one 64-bit-aligned class managed-word slot in the pointer-free
// design state. This is used by the bytecode tier; the backing state allocation
// is fixed for the context lifetime.
talos_rt_status
talos_rt_v1_gc_design_root_register(talos_rt_context *context,
                                      uint64_t bit_offset);
talos_rt_status talos_rt_v1_gc_design_candidate_root_register(
    talos_rt_context *context, uint64_t bit_offset,
    talos_rt_managed_root_kind_mask_v1 allowed_kinds);

// Allocate zero-initialized class storage. Abstract and interface descriptors
// cannot be allocated. The descriptor word is installed before the object is
// returned; constructors and property initializers are generated code.
talos_rt_status
talos_rt_v1_object_allocate(talos_rt_gc_lane_v1 *lane,
                              const talos_rt_class_descriptor_v1 *descriptor,
                              talos_rt_object_v1 **out_object);
// Validate the source against its static handle type, then allocate and copy
// its complete dynamic class, preserving virtual dispatch and derived fields.
talos_rt_status talos_rt_v1_object_shallow_copy(
    talos_rt_gc_lane_v1 *lane,
    const talos_rt_class_descriptor_v1 *static_descriptor,
    talos_rt_object_v1 *source, talos_rt_object_v1 **out_object);

// Locked field access supplies a race-free baseline for shared class objects.
// Offsets are relative to the descriptor word at object byte zero.
talos_rt_status talos_rt_v1_object_read(talos_rt_object_v1 *object,
                                            uint64_t offset, void *data,
                                            uint64_t size);
talos_rt_status talos_rt_v1_object_write(talos_rt_object_v1 *object,
                                             uint64_t offset, const void *data,
                                             uint64_t size);
// Atomically replace up to 64 bits within a two-state packed field. `valid`
// zero preserves the field, which lets generated code normalize arbitrarily
// wide signed indices without materializing the complete packed value.
talos_rt_status
talos_rt_v1_object_bits_insert(talos_rt_object_v1 *object, uint64_t offset,
                                 uint64_t field_bit_width, int64_t low_bit,
                                 uint32_t valid, uint64_t replacement,
                                 uint32_t replacement_width);
// Atomically access adjacent value/unknown field planes under one object lock.
talos_rt_status talos_rt_v1_object_read_planes(talos_rt_object_v1 *object,
                                                   uint64_t offset, void *value,
                                                   void *unknown,
                                                   uint64_t plane_size);
talos_rt_status
talos_rt_v1_object_write_planes(talos_rt_object_v1 *object, uint64_t offset,
                                  const void *value, const void *unknown,
                                  uint64_t plane_size);

// Load/store through the erased representation of a language `ref` formal.
// `managed` zero selects a stable ordinary-state handle in payload, one
// selects a class owner and byte offset, and two selects a ReferencePath in
// owner (payload is unused). Generated code supplies its native state planes
// so the same ABI serves native and bytecode callees.
typedef uint32_t talos_rt_argument_value_kind_v1;
enum {
  TALOS_RT_ARGUMENT_VALUE_BITS = 0,
  TALOS_RT_ARGUMENT_VALUE_CLASS = 1,
  TALOS_RT_ARGUMENT_VALUE_STRING = 2
};
talos_rt_status talos_rt_v1_argument_ref_load(
    talos_rt_context *context, const uint8_t *state_value,
    const uint8_t *state_unknown, uint64_t state_bit_count,
    talos_rt_object_v1 *owner, uint64_t payload, uint32_t managed,
    uint64_t bit_width, uint64_t plane_size, uint32_t four_state,
    uint32_t value_kind, void *out_value, void *out_unknown);
talos_rt_status talos_rt_v1_argument_ref_store(
    talos_rt_context *context, uint8_t *state_value, uint8_t *state_unknown,
    uint64_t state_bit_count, talos_rt_object_v1 *owner, uint64_t payload,
    uint32_t managed, uint64_t bit_width, uint64_t plane_size,
    uint32_t four_state, uint32_t value_kind, const void *value,
    const void *unknown);
talos_rt_status
talos_rt_v1_object_field_load(talos_rt_object_v1 *object, uint64_t offset,
                                talos_rt_object_v1 **out_value);
talos_rt_status talos_rt_v1_object_field_store(talos_rt_object_v1 *object,
                                                   uint64_t offset,
                                                   talos_rt_object_v1 *value);
// IEEE 1800-2017 10.6 whole class-property force / procedural assign. The
// object identity and field offset form the persistent target; ordinary field
// stores are masked until the corresponding release operation.
talos_rt_status
talos_rt_v1_object_override(talos_rt_object_v1 *object, uint64_t offset,
                              uint64_t plane_size, uint32_t four_state,
                              uint32_t assign, uint32_t dynamic,
                              uint64_t owner_process, uint32_t claim,
                              const void *value, const void *unknown);
talos_rt_status
talos_rt_v1_object_release_override(talos_rt_object_v1 *object,
                                      uint64_t offset, uint64_t plane_size,
                                      uint32_t four_state, uint32_t assign);

uint32_t
talos_rt_v1_object_is_instance(const talos_rt_object_v1 *object,
                                 const talos_rt_class_descriptor_v1 *target);
talos_rt_status
talos_rt_v1_object_cast(talos_rt_object_v1 *object,
                          const talos_rt_class_descriptor_v1 *target,
                          talos_rt_object_v1 **out_object);
uint64_t talos_rt_v1_object_id(const talos_rt_object_v1 *object);

typedef uint32_t talos_rt_managed_watch_kind;
enum {
  TALOS_RT_MANAGED_WATCH_FIELD = 1,
  TALOS_RT_MANAGED_WATCH_CONTAINER_SIZE = 2
};

// Return a scheduler-stable dependency token for one exact managed value.
// `selector` is the byte offset for a field watch and is ignored for a
// container-size watch. Null inputs produce token zero.
uint64_t talos_rt_v1_managed_watch(talos_rt_object_v1 *object,
                                     talos_rt_managed_watch_kind kind,
                                     uint64_t selector);

// Immutable, non-interned managed strings. Zero is the canonical empty string.
// The view API uses caller scratch for SSO values; heap views remain valid
// while the string word is rooted. The scratch buffer must have eight bytes.
typedef struct talos_rt_string_span_v1 {
  talos_rt_string_v1 string;
} talos_rt_string_span_v1;

talos_rt_status talos_rt_v1_string_create(talos_rt_gc_lane_v1 *lane,
                                              const char *bytes, uint64_t size,
                                              talos_rt_string_v1 *out_string);
talos_rt_status talos_rt_v1_string_concat(talos_rt_gc_lane_v1 *lane,
                                              talos_rt_string_v1 left,
                                              talos_rt_string_v1 right,
                                              talos_rt_string_v1 *out_string);
talos_rt_status talos_rt_v1_string_concat_many(
    talos_rt_gc_lane_v1 *lane, const talos_rt_string_span_v1 *spans,
    uint64_t span_count, talos_rt_string_v1 *out_string);
talos_rt_status talos_rt_v1_string_repeat(talos_rt_gc_lane_v1 *lane,
                                              talos_rt_string_v1 string,
                                              uint64_t count,
                                              talos_rt_string_v1 *out_string);
talos_rt_status
talos_rt_v1_string_from_packed(talos_rt_gc_lane_v1 *lane, const void *value,
                                 const void *unknown, uint64_t bit_width,
                                 talos_rt_string_v1 *out_string);
talos_rt_status talos_rt_v1_string_to_packed(talos_rt_string_v1 string,
                                                 void *value, void *unknown,
                                                 uint64_t bit_width);
talos_rt_status talos_rt_v1_string_view(talos_rt_string_v1 string,
                                            char scratch[8],
                                            const char **out_bytes,
                                            uint64_t *out_size);
uint64_t talos_rt_v1_string_length(talos_rt_string_v1 string);
uint64_t talos_rt_v1_string_hash(talos_rt_string_v1 string);
uint32_t talos_rt_v1_string_getc(talos_rt_string_v1 string, int64_t index);
talos_rt_status talos_rt_v1_string_putc(talos_rt_gc_lane_v1 *lane,
                                            talos_rt_string_v1 string,
                                            int64_t index, uint32_t character,
                                            talos_rt_string_v1 *out_string);
talos_rt_status talos_rt_v1_string_substr(talos_rt_gc_lane_v1 *lane,
                                              talos_rt_string_v1 string,
                                              int64_t left, int64_t right,
                                              talos_rt_string_v1 *out_string);
int32_t talos_rt_v1_string_compare(talos_rt_string_v1 left,
                                     talos_rt_string_v1 right);
int32_t talos_rt_v1_string_compare_insensitive(talos_rt_string_v1 left,
                                                 talos_rt_string_v1 right);
talos_rt_status talos_rt_v1_string_case_convert(
    talos_rt_gc_lane_v1 *lane, talos_rt_string_v1 string, uint32_t to_upper,
    talos_rt_string_v1 *out_string);
// One $sscanf/$fscanf conversion. `prefix` is the format text preceding the
// conversion: whitespace in it matches any run of input whitespace including
// none, and every other character must match exactly. `specifier` is the
// conversion letter, whose field is returned as text for the caller to parse.
// A zero `width` is unbounded. `out_ok` is zero when the prefix failed to match
// or an ordinary field was empty, in which case the cursor does not advance.
// The `%m` control conversion succeeds after its prefix and returns no field;
// lowering supplies the lexical hierarchy without consuming input bytes.
talos_rt_status talos_rt_v1_string_scan_field(
    talos_rt_gc_lane_v1 *lane, talos_rt_string_v1 input, uint32_t cursor,
    const char *prefix, uint64_t prefix_size, uint32_t specifier,
    uint64_t width, talos_rt_string_v1 *out_field, uint32_t *out_cursor,
    uint32_t *out_ok);
// Interpret one cached dynamic scanf plan through its next assigned
// conversion. Bits 0..25 of allowed_specifiers correspond to lower-case a..z.
// `finalize` consumes only trailing suppressed conversions / literal text and
// diagnoses a remaining assigned conversion. Plans and their prefixes remain
// runtime-owned; unchanged formats reuse a resident entry in the bounded
// cache, while an entry reparses after eviction. The two raw byte counts are
// the statically typed destination's exact `%u` and `%z` transfer sizes; they
// are zero when raw conversions are not legal for that destination.
enum {
  TALOS_RT_SCAN_DYNAMIC_TEXT = 1,
  TALOS_RT_SCAN_DYNAMIC_HIERARCHY = 2,
  TALOS_RT_SCAN_DYNAMIC_REAL = 3,
  TALOS_RT_SCAN_DYNAMIC_TIME = 4,
  TALOS_RT_SCAN_DYNAMIC_LOGIC2 = 5,
  TALOS_RT_SCAN_DYNAMIC_LOGIC8 = 6,
  TALOS_RT_SCAN_DYNAMIC_LOGIC10 = 7,
  TALOS_RT_SCAN_DYNAMIC_LOGIC16 = 8,
  TALOS_RT_SCAN_DYNAMIC_RAW2 = 9,
  TALOS_RT_SCAN_DYNAMIC_RAW4 = 10
};
// Validate one destination against the next assigned conversion without
// consuming input. `file` selects the task name used by diagnostics. Lowering
// threads `out_plan_cursor` through every destination and one final call, so
// the complete format is checked in O(conversions + destinations) before any
// scanner touches its source.
talos_rt_status talos_rt_v1_scan_dynamic_validate(
    talos_rt_context *context, talos_rt_string_v1 format,
    uint32_t plan_cursor, uint32_t file, uint32_t finalize,
    uint64_t allowed_specifiers, uint32_t *out_plan_cursor);
talos_rt_status talos_rt_v1_string_scan_dynamic(
    talos_rt_context *context, talos_rt_gc_lane_v1 *lane,
    talos_rt_string_v1 input, uint32_t cursor, talos_rt_string_v1 format,
    uint32_t plan_cursor, uint32_t enabled, uint32_t finalize,
    uint64_t allowed_specifiers, uint64_t raw_two_state_bytes,
    uint64_t raw_four_state_bytes, talos_rt_string_v1 *out_field,
    uint32_t *out_cursor, uint32_t *out_plan_cursor,
    uint32_t *out_conversion_kind, uint32_t *out_ok);
talos_rt_status talos_rt_v1_string_scan_raw(
    talos_rt_string_v1 input, uint32_t cursor, const char *prefix,
    uint64_t prefix_size, uint64_t raw_size, uint64_t bit_width,
    uint32_t four_state, uint64_t max_width, void *value, uint64_t value_size,
    void *unknown, uint64_t unknown_size, uint32_t *out_cursor,
    uint32_t *out_ok);
talos_rt_status
talos_rt_v1_string_parse_integer(talos_rt_string_v1 string, uint32_t radix,
                                   uint64_t *out_value);
talos_rt_status talos_rt_v1_string_parse_logic(talos_rt_string_v1 string,
                                                   uint32_t radix,
                                                   uint64_t *out_value,
                                                   uint64_t *out_unknown);
talos_rt_status talos_rt_v1_string_parse_real(talos_rt_string_v1 string,
                                                  double *out_value);
talos_rt_status
talos_rt_v1_string_parse_real_constant(talos_rt_string_v1 string,
                                         double *out_value);
talos_rt_status
talos_rt_v1_string_format_integer(talos_rt_gc_lane_v1 *lane, uint64_t value,
                                    uint32_t radix, uint32_t is_signed,
                                    talos_rt_string_v1 *out_string);
talos_rt_status
talos_rt_v1_string_format_real(talos_rt_gc_lane_v1 *lane, double value,
                                 talos_rt_string_v1 *out_string);

typedef uint32_t talos_rt_container_kind_v1;
enum {
  TALOS_RT_CONTAINER_DYNAMIC_ARRAY = 1,
  TALOS_RT_CONTAINER_QUEUE = 2,
  TALOS_RT_CONTAINER_ASSOCIATIVE_ARRAY = 3
};

typedef uint32_t talos_rt_assoc_key_kind_v1;
enum {
  TALOS_RT_ASSOC_KEY_UNSIGNED = 1,
  TALOS_RT_ASSOC_KEY_SIGNED = 2,
  TALOS_RT_ASSOC_KEY_STRING = 3,
  TALOS_RT_ASSOC_KEY_CLASS = 4,
  TALOS_RT_ASSOC_KEY_PROCESS = 5,
  TALOS_RT_ASSOC_KEY_WILDCARD = 6,
  // Any other index type with an equality operator, such as an unpacked
  // structure, array or union, a dynamically sized array, or a mailbox,
  // semaphore, event, chandle or virtual interface (IEEE 1800-2023 7.8.5). The
  // array compares these indices through an equality plan; see
  // talos_rt_v1_assoc_create_element_keyed.
  TALOS_RT_ASSOC_KEY_ELEMENT = 7
};

// Canonical typed associative key. Integral keys up to 64 bits use the inline
// value and unknown fields. Wider integral keys use little-endian value_data
// and optional unknown_data spans of ceil(width / 8) bytes. String keys use
// string and require width/value/unknown to be zero. Class keys use object and
// require width/unknown/string to be zero; null is a valid class key. Process
// keys use value and require width/unknown/string to be zero; their
// stable scheduler token provides identity and zero is a valid null key. Any
// nonzero unknown bit within width makes an integral-key operation a no-op.
// Element keys use value_data and, exactly when the key element type is
// four-state, unknown_data spans of the key element's value_size bytes, with
// width set to eight times that size; an X or Z bit that the key's equality
// plan compares as four-state makes the operation a no-op. The array keeps its
// own deep copy of every inserted element key. Wide spans only need to remain
// valid for the duration of the call; traversal writes through value_data and
// clears unknown_data when it is non-null, or for an element key copies the
// stored key's planes, whose dynamic members remain the stored key's own.
typedef struct talos_rt_assoc_key_v1 {
  talos_rt_assoc_key_kind_v1 kind;
  uint32_t reserved;
  uint64_t width;
  union {
    uint64_t value;
    void *value_data;
    talos_rt_object_v1 *object;
  };
  union {
    uint64_t unknown;
    void *unknown_data;
  };
  talos_rt_string_v1 string;
} talos_rt_assoc_key_v1;

// Erased dynamic-array, queue, and associative-array primitives. Value and
// unknown point to
// descriptor-sized planes. Reads outside the live range return zeroes. Queue
// writes at index size append a default element before applying the write.
talos_rt_status talos_rt_v1_dynamic_array_create(
    talos_rt_gc_lane_v1 *lane, const talos_rt_element_type_v1 *element_type,
    uint64_t size, talos_rt_object_v1 **out_array);
talos_rt_status
talos_rt_v1_dynamic_array_resize(talos_rt_gc_lane_v1 *lane,
                                   talos_rt_object_v1 *array,
                                   uint64_t new_size);
// bound is the maximum legal queue index. UINT64_MAX denotes an unbounded
// queue, so bound zero represents the one-element declaration [$:0].
talos_rt_status
talos_rt_v1_queue_create(talos_rt_gc_lane_v1 *lane,
                           const talos_rt_element_type_v1 *element_type,
                           uint64_t bound, talos_rt_object_v1 **out_queue);
// Create a dynamic array or queue with the same immutable element metadata as
// the first nonnull source. Queue bounds are preserved. If both sources are
// null, the canonical null/default container is returned.
talos_rt_status talos_rt_v1_container_create_like(
    talos_rt_gc_lane_v1 *lane, talos_rt_object_v1 *preferred,
    talos_rt_object_v1 *fallback, uint64_t size,
    talos_rt_object_v1 **out_container);
talos_rt_status talos_rt_v1_container_create_typed(
    talos_rt_gc_lane_v1 *lane, uint32_t container_kind, uint64_t type_id,
    uint32_t element_kind, uint32_t element_flags, uint64_t value_size,
    uint64_t alignment, uint64_t bit_width,
    const talos_rt_element_trace_slot_v1 *trace_slots,
    uint64_t trace_slot_count, uint64_t size, uint64_t bound,
    talos_rt_object_v1 **out_container);
// Extended typed creation used when a fixed unpacked aggregate element needs
// explicit assignment-pattern shape. pattern_bytes is a versioned sequence of
// little-endian 64-bit words; the runtime copies it while interning the type.
talos_rt_status talos_rt_v1_container_create_typed_pattern(
    talos_rt_gc_lane_v1 *lane, uint32_t container_kind, uint64_t type_id,
    uint32_t element_kind, uint32_t element_flags, uint64_t value_size,
    uint64_t alignment, uint64_t bit_width,
    const talos_rt_element_trace_slot_v1 *trace_slots,
    uint64_t trace_slot_count, const uint8_t *pattern_bytes,
    uint64_t pattern_byte_count, uint64_t size, uint64_t bound,
    talos_rt_object_v1 **out_container);
talos_rt_status talos_rt_v1_mailbox_create_typed(
    talos_rt_gc_lane_v1 *lane, uint64_t type_id, uint32_t element_kind,
    uint32_t element_flags, uint64_t value_size, uint64_t alignment,
    uint64_t bit_width, const talos_rt_element_trace_slot_v1 *trace_slots,
    uint64_t trace_slot_count, int64_t bound,
    talos_rt_object_v1 **out_mailbox);
talos_rt_status
talos_rt_v1_semaphore_create(talos_rt_gc_lane_v1 *lane, int32_t keys,
                               talos_rt_object_v1 **out_semaphore);
talos_rt_status talos_rt_v1_semaphore_put(talos_rt_object_v1 *semaphore,
                                              int32_t keys);
talos_rt_status
talos_rt_v1_semaphore_try_get(talos_rt_object_v1 *semaphore, int32_t keys,
                                uint32_t *out_success);
uint64_t talos_rt_v1_container_size(talos_rt_object_v1 *container);
uint32_t talos_rt_v1_box_is_type(talos_rt_object_v1 *box, uint64_t type_id);
// Bind the exact reflection descriptor of a one-element boxed array. The ID
// belongs to this context's validated design image, independently of executable
// element type IDs. A conflicting existing binding is rejected.
talos_rt_status talos_rt_v1_box_bind_type(talos_rt_context *context,
                                              talos_rt_object_v1 *box,
                                              uint64_t descriptor_id);

talos_rt_status talos_rt_v1_container_read(talos_rt_object_v1 *container,
                                               int64_t index, void *out_value,
                                               void *out_unknown);
// Bytecode-safe variants reject buffers that cannot hold the immutable element
// descriptor's planes before reading or writing any bytes.
talos_rt_status talos_rt_v1_container_read_checked(
    talos_rt_object_v1 *container, int64_t index, void *out_value,
    uint64_t value_size, void *out_unknown, uint64_t unknown_size);
talos_rt_status talos_rt_v1_container_write(talos_rt_gc_lane_v1 *lane,
                                                talos_rt_object_v1 *container,
                                                int64_t index,
                                                const void *value,
                                                const void *unknown);
talos_rt_status talos_rt_v1_container_write_checked(
    talos_rt_gc_lane_v1 *lane, talos_rt_object_v1 *container, int64_t index,
    const void *value, uint64_t value_size, const void *unknown,
    uint64_t unknown_size);
talos_rt_status
talos_rt_v1_container_clone(talos_rt_gc_lane_v1 *lane,
                              talos_rt_object_v1 *container,
                              talos_rt_object_v1 **out_container);
talos_rt_status talos_rt_v1_container_swap(talos_rt_object_v1 *container,
                                               int64_t left, int64_t right);
talos_rt_status talos_rt_v1_container_import_fixed(
    talos_rt_gc_lane_v1 *lane, talos_rt_object_v1 *container,
    const void *value, const void *unknown, uint64_t plane_size,
    uint64_t bit_width, uint32_t four_state, uint64_t element_span,
    uint64_t count);
talos_rt_status talos_rt_v1_container_export_fixed(
    talos_rt_object_v1 *container, void *out_value, void *out_unknown,
    uint64_t plane_size, uint64_t bit_width, uint32_t four_state,
    uint64_t element_span, uint64_t count);
talos_rt_status talos_rt_v1_container_export_bitstream(
    talos_rt_object_v1 *container, void *out_value, void *out_unknown,
    uint64_t plane_size, uint64_t bit_width, uint32_t four_state,
    uint64_t element_width, uint64_t count, uint64_t element_plane_size,
    uint32_t element_four_state);
talos_rt_status talos_rt_v1_aggregate_export_bitstream(
    const void *input_value, const void *input_unknown,
    uint64_t input_plane_size, uint64_t input_bit_width,
    uint32_t input_four_state, void *out_value, void *out_unknown,
    uint64_t output_plane_size, uint64_t output_bit_width,
    uint32_t output_four_state, const void *plan, uint64_t plan_size);
talos_rt_status talos_rt_v1_aggregate_import_bitstream(
    const void *input_value, const void *input_unknown,
    uint64_t input_plane_size, uint64_t input_bit_width,
    uint32_t input_four_state, void *out_value, void *out_unknown,
    uint64_t output_plane_size, uint64_t output_bit_width,
    uint32_t output_four_state, const void *plan, uint64_t plan_size);
talos_rt_status talos_rt_v1_recursive_export_bitstream(
    talos_rt_context *context, const void *input_value,
    const void *input_unknown, uint64_t input_plane_size,
    uint64_t input_bit_width, uint32_t input_four_state, void *out_value,
    void *out_unknown, uint64_t output_plane_size, uint64_t output_bit_width,
    uint32_t output_four_state, const void *plan, uint64_t plan_size,
    uint32_t observe, uint32_t *out_matched, uint64_t *out_watch);
talos_rt_status
talos_rt_v1_class_bitstream_finalize(talos_rt_context *context);
talos_rt_status talos_rt_v2_recursive_export_bitstream(
    talos_rt_context *context, uint64_t site_id, const void *input_value,
    const void *input_unknown, uint64_t input_plane_size,
    uint64_t input_bit_width, uint32_t input_four_state, void *out_value,
    void *out_unknown, uint64_t output_plane_size, uint64_t output_bit_width,
    uint32_t output_four_state, uint32_t observe, uint32_t *out_matched,
    uint64_t *out_watch);
// Remove every element. An associative array keeps its user-specified
// default (IEEE 1800-2023 7.9.11).
talos_rt_status
talos_rt_v1_container_delete(talos_rt_object_v1 *container);
talos_rt_status talos_rt_v1_queue_push(talos_rt_gc_lane_v1 *lane,
                                           talos_rt_object_v1 *queue,
                                           uint32_t front, const void *value,
                                           const void *unknown);
talos_rt_status talos_rt_v1_queue_pop(talos_rt_object_v1 *queue,
                                          uint32_t front, void *out_value,
                                          void *out_unknown,
                                          uint32_t *out_present);
// Atomic nonblocking mailbox operations. Mailboxes use the managed queue
// representation internally, but these entry points preserve the distinct
// success contract required by IEEE 1800 mailbox methods.
talos_rt_status talos_rt_v1_mailbox_try_put(talos_rt_gc_lane_v1 *lane,
                                                talos_rt_object_v1 *mailbox,
                                                const void *value,
                                                const void *unknown,
                                                uint32_t *out_success);
talos_rt_status talos_rt_v1_mailbox_try_put_checked(
    talos_rt_gc_lane_v1 *lane, talos_rt_object_v1 *mailbox,
    const void *value, uint64_t value_size, const void *unknown,
    uint64_t unknown_size, uint32_t *out_success);
talos_rt_status talos_rt_v1_mailbox_num(talos_rt_object_v1 *mailbox,
                                            uint32_t *out_count);
talos_rt_status talos_rt_v1_mailbox_try_peek(talos_rt_object_v1 *mailbox,
                                                 void *out_value,
                                                 void *out_unknown,
                                                 uint32_t *out_present);
talos_rt_status talos_rt_v1_mailbox_try_peek_checked(
    talos_rt_object_v1 *mailbox, void *out_value, uint64_t value_size,
    void *out_unknown, uint64_t unknown_size, uint32_t *out_present);
talos_rt_status talos_rt_v1_mailbox_try_get(talos_rt_object_v1 *mailbox,
                                                void *out_value,
                                                void *out_unknown,
                                                uint32_t *out_present);
talos_rt_status talos_rt_v1_mailbox_try_get_checked(
    talos_rt_object_v1 *mailbox, void *out_value, uint64_t value_size,
    void *out_unknown, uint64_t unknown_size, uint32_t *out_present);
talos_rt_status talos_rt_v1_queue_insert(talos_rt_gc_lane_v1 *lane,
                                             talos_rt_object_v1 *queue,
                                             int64_t index, const void *value,
                                             const void *unknown);
talos_rt_status talos_rt_v1_queue_delete_index(talos_rt_object_v1 *queue,
                                                   int64_t index);
talos_rt_status talos_rt_v1_assoc_create(
    talos_rt_gc_lane_v1 *lane, const talos_rt_element_type_v1 *element_type,
    talos_rt_assoc_key_kind_v1 key_kind, uint64_t key_width,
    talos_rt_object_v1 **out_array);
talos_rt_status talos_rt_v1_assoc_create_typed(
    talos_rt_gc_lane_v1 *lane, uint64_t type_id, uint32_t element_kind,
    uint32_t element_flags, uint64_t value_size, uint64_t alignment,
    uint64_t bit_width, const talos_rt_element_trace_slot_v1 *trace_slots,
    uint64_t trace_slot_count, talos_rt_assoc_key_kind_v1 key_kind,
    uint64_t key_width, talos_rt_object_v1 **out_array);
// Create an associative array whose index is an TALOS_RT_ASSOC_KEY_ELEMENT
// value of the key element type that follows the value element type. key_plan
// holds key_plan_size bytes of the key type's equality plan (see
// TALOS_RT_ASSOC_KEY_PLAN_MAGIC).
talos_rt_status talos_rt_v1_assoc_create_element_keyed(
    talos_rt_gc_lane_v1 *lane, uint64_t type_id, uint32_t element_kind,
    uint32_t element_flags, uint64_t value_size, uint64_t alignment,
    uint64_t bit_width, const talos_rt_element_trace_slot_v1 *trace_slots,
    uint64_t trace_slot_count, uint64_t key_type_id, uint32_t key_element_kind,
    uint32_t key_element_flags, uint64_t key_value_size, uint64_t key_alignment,
    uint64_t key_bit_width,
    const talos_rt_element_trace_slot_v1 *key_trace_slots,
    uint64_t key_trace_slot_count, const uint8_t *key_plan,
    uint64_t key_plan_size, talos_rt_object_v1 **out_array);
talos_rt_status talos_rt_v1_assoc_exists(talos_rt_object_v1 *array,
                                             const talos_rt_assoc_key_v1 *key,
                                             uint32_t *out_exists);
talos_rt_status
talos_rt_v1_assoc_key_info(talos_rt_object_v1 *array,
                             talos_rt_assoc_key_kind_v1 *out_kind,
                             uint64_t *out_width);
talos_rt_status talos_rt_v1_assoc_read(talos_rt_object_v1 *array,
                                           const talos_rt_assoc_key_v1 *key,
                                           void *out_value, void *out_unknown,
                                           uint32_t *out_present);
talos_rt_status talos_rt_v1_assoc_read_checked(
    talos_rt_object_v1 *array, const talos_rt_assoc_key_v1 *key,
    void *out_value, uint64_t value_size, void *out_unknown,
    uint64_t unknown_size, uint32_t *out_present);
talos_rt_status talos_rt_v1_assoc_write(talos_rt_gc_lane_v1 *lane,
                                            talos_rt_object_v1 *array,
                                            const talos_rt_assoc_key_v1 *key,
                                            const void *value,
                                            const void *unknown);
talos_rt_status talos_rt_v1_assoc_write_checked(
    talos_rt_gc_lane_v1 *lane, talos_rt_object_v1 *array,
    const talos_rt_assoc_key_v1 *key, const void *value, uint64_t value_size,
    const void *unknown, uint64_t unknown_size);
talos_rt_status talos_rt_v1_assoc_set_default(talos_rt_gc_lane_v1 *lane,
                                                  talos_rt_object_v1 *array,
                                                  const void *value,
                                                  const void *unknown);
talos_rt_status talos_rt_v1_assoc_set_default_checked(
    talos_rt_gc_lane_v1 *lane, talos_rt_object_v1 *array, const void *value,
    uint64_t value_size, const void *unknown, uint64_t unknown_size);
talos_rt_status
talos_rt_v1_assoc_delete(talos_rt_object_v1 *array,
                           const talos_rt_assoc_key_v1 *key);
// Ordered traversal follows SystemVerilog key ordering. first/last ignore the
// input key; next/prev replace it only on success. Rebuilding the ordered cache
// can collect and therefore requires the current managed lane.
talos_rt_status talos_rt_v1_assoc_first(talos_rt_gc_lane_v1 *lane,
                                            talos_rt_object_v1 *array,
                                            talos_rt_assoc_key_v1 *inout_key,
                                            uint32_t *out_success);
talos_rt_status talos_rt_v1_assoc_last(talos_rt_gc_lane_v1 *lane,
                                           talos_rt_object_v1 *array,
                                           talos_rt_assoc_key_v1 *inout_key,
                                           uint32_t *out_success);
talos_rt_status talos_rt_v1_assoc_next(talos_rt_gc_lane_v1 *lane,
                                           talos_rt_object_v1 *array,
                                           talos_rt_assoc_key_v1 *inout_key,
                                           uint32_t *out_success);
talos_rt_status talos_rt_v1_assoc_prev(talos_rt_gc_lane_v1 *lane,
                                           talos_rt_object_v1 *array,
                                           talos_rt_assoc_key_v1 *inout_key,
                                           uint32_t *out_success);

// Escaping dynamic element lvalues are represented by managed paths, never by
// interior pointers. A path captures its container owner and canonical
// selector and resolves the live element on every access.
talos_rt_status talos_rt_v1_reference_path_index_create(
    talos_rt_gc_lane_v1 *lane, talos_rt_object_v1 *container, int64_t index,
    talos_rt_object_v1 *watch_owner, uint64_t owner_payload,
    uint32_t owner_managed, uint8_t *state_value, uint8_t *state_unknown,
    uint64_t state_bit_count, talos_rt_object_v1 **out_path);
talos_rt_status talos_rt_v1_reference_path_assoc_create(
    talos_rt_gc_lane_v1 *lane, talos_rt_object_v1 *array,
    const talos_rt_assoc_key_v1 *key, talos_rt_object_v1 *watch_owner,
    uint64_t owner_payload, uint32_t owner_managed, uint8_t *state_value,
    uint8_t *state_unknown, uint64_t state_bit_count,
    talos_rt_object_v1 **out_path);
// Capture a mutable coverage option for the ordinary managed NBA queue.
// Exactly one of handle/type_id is nonzero. A type option also captures its
// ordinary static storage using the argument-reference binding. Integer patch
// values are i64 bit patterns with a positioned mask; character values are
// bytes at a captured index. Whole strings remain tagged managed words.
talos_rt_status talos_rt_v1_reference_path_covergroup_option_create(
    talos_rt_gc_lane_v1 *lane, uint64_t handle, uint64_t type_id,
    uint64_t item_id, uint32_t option, uint64_t mask, int64_t index,
    uint32_t character, talos_rt_object_v1 *watch_owner,
    uint64_t owner_payload, uint32_t owner_managed, uint8_t *state_value,
    uint8_t *state_unknown, uint64_t state_bit_count,
    talos_rt_object_v1 **out_path);

talos_rt_status talos_rt_v1_reference_path_string_character_create(
    talos_rt_gc_lane_v1 *lane, talos_rt_string_v1 string, int64_t index,
    talos_rt_object_v1 *watch_owner, uint64_t owner_payload,
    uint32_t owner_managed, uint8_t *state_value, uint8_t *state_unknown,
    uint64_t state_bit_count, talos_rt_object_v1 **out_path);
talos_rt_status talos_rt_v1_reference_path_aggregate_element_create(
    talos_rt_gc_lane_v1 *lane, talos_rt_object_v1 *watch_owner,
    uint64_t owner_payload, uint32_t owner_managed, uint8_t *state_value,
    uint8_t *state_unknown, uint64_t state_bit_count, int64_t index,
    int64_t left, int64_t right, uint64_t element_span,
    uint64_t owner_bit_width, uint64_t owner_plane_size,
    uint32_t owner_four_state, uint64_t type_id, uint32_t element_kind,
    uint32_t element_flags, uint64_t value_size, uint64_t alignment,
    uint64_t bit_width, const talos_rt_element_trace_slot_v1 *trace_slots,
    uint64_t trace_slot_count, talos_rt_object_v1 **out_path);
talos_rt_status talos_rt_v1_reference_path_load(talos_rt_object_v1 *path,
                                                    void *out_value,
                                                    void *out_unknown,
                                                    uint32_t *out_present);
talos_rt_status
talos_rt_v1_reference_path_store(talos_rt_gc_lane_v1 *lane,
                                   talos_rt_object_v1 *path,
                                   const void *value, const void *unknown);

talos_rt_status talos_rt_v1_method_resolve(
    talos_rt_object_v1 *receiver, uint64_t slot, uint64_t signature_id,
    const talos_rt_method_descriptor_v1 **out_method);
talos_rt_status talos_rt_v1_interface_method_resolve(
    talos_rt_object_v1 *receiver, uint64_t interface_id,
    uint64_t interface_ordinal, uint64_t signature_id,
    const talos_rt_method_descriptor_v1 **out_method);
talos_rt_status talos_rt_v1_method_invoke(
    talos_rt_gc_lane_v1 *lane, talos_rt_object_v1 *receiver, uint64_t slot,
    uint64_t signature_id, const talos_rt_method_argument_v1 *arguments,
    uint32_t argument_count, void *result, uint64_t result_size);
talos_rt_status talos_rt_v1_interface_method_invoke(
    talos_rt_gc_lane_v1 *lane, talos_rt_object_v1 *receiver,
    uint64_t interface_id, uint64_t interface_ordinal, uint64_t signature_id,
    const talos_rt_method_argument_v1 *arguments, uint32_t argument_count,
    void *result, uint64_t result_size);
talos_rt_status talos_rt_v1_method_task_activate(
    talos_rt_gc_lane_v1 *lane, talos_rt_object_v1 *receiver, uint64_t slot,
    uint64_t signature_id, const talos_rt_method_argument_v1 *arguments,
    uint32_t argument_count, uint64_t *out_activation);
talos_rt_status talos_rt_v1_interface_method_task_activate(
    talos_rt_gc_lane_v1 *lane, talos_rt_object_v1 *receiver,
    uint64_t interface_id, uint64_t interface_ordinal, uint64_t signature_id,
    const talos_rt_method_argument_v1 *arguments, uint32_t argument_count,
    uint64_t *out_activation);

// IEEE 1800-2023 weak_reference support. The wrapper is itself a managed
// object. Its referent is cleared during the collection that first determines
// that no strong path reaches the referent.
talos_rt_status
talos_rt_v1_weak_create(talos_rt_gc_lane_v1 *lane,
                          const talos_rt_class_descriptor_v1 *descriptor,
                          talos_rt_object_v1 *referent,
                          talos_rt_object_v1 **out_weak);
talos_rt_status talos_rt_v1_weak_get(talos_rt_object_v1 *weak,
                                         talos_rt_object_v1 **out_referent);
talos_rt_status talos_rt_v1_weak_clear(talos_rt_object_v1 *weak);

// Validate and traverse the optional reflection image with checked cursors.
// Returned names are immutable spans into the database and remain valid for
// the lifetime of the execution descriptor.
talos_rt_status talos_rt_v1_design_validate(
    const talos_rt_execution_descriptor_v1 *execution);
talos_rt_status
talos_rt_v1_design_root(const talos_rt_execution_descriptor_v1 *execution,
                          talos_rt_design_cursor_v1 *out_cursor);
talos_rt_status
talos_rt_v1_design_child(const talos_rt_execution_descriptor_v1 *execution,
                           talos_rt_design_cursor_v1 cursor,
                           talos_rt_design_cursor_v1 *out_cursor);
talos_rt_status talos_rt_v1_design_child_at(
    const talos_rt_execution_descriptor_v1 *execution,
    talos_rt_design_cursor_v1 cursor, uint64_t index,
    talos_rt_design_cursor_v1 *out_cursor);
talos_rt_status talos_rt_v1_design_sibling(
    const talos_rt_execution_descriptor_v1 *execution,
    talos_rt_design_cursor_v1 cursor,
    talos_rt_design_cursor_v1 *out_cursor);
talos_rt_status
talos_rt_v1_design_lookup(const talos_rt_execution_descriptor_v1 *execution,
                            const uint8_t *name, uint64_t name_size,
                            talos_rt_design_cursor_v1 *out_cursor);
talos_rt_status
talos_rt_v1_design_info(const talos_rt_execution_descriptor_v1 *execution,
                          talos_rt_design_cursor_v1 cursor,
                          talos_rt_design_info_v1 *out_info);
talos_rt_status talos_rt_v1_design_type_info(
    const talos_rt_execution_descriptor_v1 *execution,
    talos_rt_design_cursor_v1 cursor,
    talos_rt_design_type_info_v1 *out_info);
talos_rt_status talos_rt_v1_design_type_child(
    const talos_rt_execution_descriptor_v1 *execution,
    talos_rt_design_cursor_v1 cursor, uint64_t index,
    talos_rt_design_cursor_v1 *out_cursor);
talos_rt_status
talos_rt_v1_design_name(const talos_rt_execution_descriptor_v1 *execution,
                          talos_rt_design_cursor_v1 cursor,
                          const uint8_t **out_data, uint64_t *out_size);
talos_rt_status talos_rt_v1_design_read(talos_rt_context *context,
                                            talos_rt_design_cursor_v1 cursor,
                                            uint64_t *value, uint64_t *unknown,
                                            uint64_t bit_width);
talos_rt_status talos_rt_v1_design_write(talos_rt_context *context,
                                             talos_rt_design_cursor_v1 cursor,
                                             const uint64_t *value,
                                             const uint64_t *unknown,
                                             uint64_t bit_width);
talos_rt_status talos_rt_v1_design_force(talos_rt_context *context,
                                             talos_rt_design_cursor_v1 cursor,
                                             const uint64_t *value,
                                             const uint64_t *unknown,
                                             uint64_t bit_width);
talos_rt_status
talos_rt_v1_design_release(talos_rt_context *context,
                             talos_rt_design_cursor_v1 cursor);

// Immutable generated VPI object-model schema embedded in the runtime image.
// This descriptor is passive: obtaining it does not initialize VPI or alter
// scheduler observability. The fingerprint is the checksum of the complete
// canonical image with its checksum field (bytes 16..23) treated as zero.
typedef struct talos_rt_vpi_object_model_v1 {
  const uint8_t *data;
  uint64_t size;
  uint64_t fingerprint;
} talos_rt_vpi_object_model_v1;
const talos_rt_vpi_object_model_v1 *talos_rt_v1_vpi_object_model(void);

// Activate the single-context VPI shim and invoke startup tables belonging to
// already loaded DT_NEEDED modules. Module names are runtime loader identities
// (DT_SONAME, or the no-SONAME basename) and remain caller-owned for the call.
talos_rt_status talos_rt_v1_vpi_startup(talos_rt_context *context,
                                            const char *const *modules,
                                            uint64_t module_count);
// Complete elaboration, enter time zero, and finish simulation respectively.
// These are cold lifecycle boundaries; they never add scheduler polling.
talos_rt_status talos_rt_v1_vpi_end_compile(talos_rt_context *context);
talos_rt_status
talos_rt_v1_vpi_start_simulation(talos_rt_context *context);
talos_rt_status talos_rt_v1_vpi_end_simulation(talos_rt_context *context);
void talos_rt_v1_vpi_shutdown(talos_rt_context *context);

// IEEE 1800-2023 37.43: exact procedural activations. Generated code uses
// stable source IDs, resolved through the validated design image. Suspension
// preserves the activation stack; returning or disabling a scope removes it.
enum {
  TALOS_RT_VPI_FRAME_CODE_UNIT_SCOPE = 0,
  TALOS_RT_VPI_FRAME_CALL = 1,
  TALOS_RT_VPI_FRAME_STATEMENT_SCOPE = 2,
  TALOS_RT_VPI_FRAME_NET_RESOLUTION = 3,
  TALOS_RT_VPI_FRAME_DPI_EXPORT = 4
};
talos_rt_status talos_rt_v1_vpi_frame_enter(talos_rt_context *context,
                                                uint64_t code_unit_id,
                                                uint32_t origin_kind,
                                                uint64_t origin_id,
                                                uint64_t *out_activation);
// Bracket one exact source call. Preparation does not create an activation;
// the callee consumes it only after its root control boundary has been entered.
talos_rt_status talos_rt_v1_vpi_call_prepare(talos_rt_context *context,
                                                 uint64_t call_id,
                                                 uint64_t *out_ticket);
// A user-defined resolution function is activated by an exact net, not an SV
// call occurrence. It consumes this ticket through frame_enter_prepared with
// is_task zero and completes through call_finish like any synchronous call.
talos_rt_status
talos_rt_v1_vpi_resolution_prepare(talos_rt_context *context,
                                     uint64_t net_id, uint64_t *out_ticket);
// A generated DPI export bridge prepares its exact public callable scope.
// The runtime accepts it only during the matching validated export dispatch.
talos_rt_status talos_rt_v1_vpi_export_prepare(talos_rt_context *context,
                                                   uint64_t code_unit_id,
                                                   uint64_t *out_ticket);
talos_rt_status
talos_rt_v1_vpi_frame_enter_prepared(talos_rt_context *context,
                                       uint64_t code_unit_id, uint32_t is_task,
                                       uint64_t *out_activation);
talos_rt_status talos_rt_v1_vpi_call_finish(talos_rt_context *context,
                                                uint64_t ticket);
// Bind an existing automatic state reference to one immutable local declaration
// in the current exact activation. This neither allocates nor initializes it.
talos_rt_status talos_rt_v1_vpi_frame_bind(talos_rt_context *context,
                                               uint64_t local_id,
                                               uint64_t automatic_reference);
// Observe a ref/const-ref formal's actual ArgumentRef location. The binding
// retains no state allocation or managed root. managed has the same 0/1/2
// meanings as argument_ref_load; a zero global-state payload is valid.
talos_rt_status talos_rt_v1_vpi_frame_bind_alias(
    talos_rt_context *context, uint64_t local_id, talos_rt_object_v1 *owner,
    uint64_t payload, uint32_t managed);
// A positive source statement ID must belong to the current lexical owner.
// Zero explicitly marks the current statement unavailable; a later positive
// checkpoint restores exact identity. This differs from pre-statement absence.
talos_rt_status
talos_rt_v1_vpi_frame_checkpoint(talos_rt_context *context,
                                   uint64_t statement_id);
talos_rt_status talos_rt_v1_vpi_frame_leave(talos_rt_context *context,
                                                uint64_t activation);
talos_rt_status talos_rt_v1_vpi_frame_depth(talos_rt_context *context,
                                                uint64_t *out_depth);
talos_rt_status talos_rt_v1_vpi_frame_unwind(talos_rt_context *context,
                                                 uint64_t depth);

// Generated single-threaded schedule ABI.  The plan is immutable; mutable
// generated storage is reached through `mutable_state` and may be installed in
// at most one live context at a time.  A successful add transfers process
// ownership to the runtime, just like the generic scheduler add APIs.
#define TALOS_RT_NATIVE_SCHEDULE_FULLY_STATIC UINT32_C(1)
// Actor slot zero is the native-only root bootstrap. It must run natively so
// static child ownership is established before bytecode/VPI transition
// fragments execute.
#define TALOS_RT_NATIVE_SCHEDULE_ROOT_SLOT_ZERO UINT32_C(2)
// The generated plan may advance constant deadlines and static NBA barriers
// without entering the generic scheduler. An external write or unsupported
// control operation requests a generic handover at a region boundary.
#define TALOS_RT_NATIVE_SCHEDULE_STATIC_CONTROL UINT32_C(4)
// Direct packed transitions may use compiler-proven static sensitivity fanout
// without allocating generic signal subscriptions.
#define TALOS_RT_NATIVE_SCHEDULE_STATIC_FANOUT UINT32_C(8)
// Compiler-emitted state planes are canonical for eligible narrow roots.
#define TALOS_RT_NATIVE_SCHEDULE_DIRECT_STATE UINT32_C(16)
// The plan carries fixed narrow-NBA root accumulators and site claims.
#define TALOS_RT_NATIVE_SCHEDULE_STATIC_NBA UINT32_C(32)
// Native fragment actions are emitted by the same revision-coupled compiler as
// the plan. The runtime may omit redundant generic frame/action validation
// after the generated executor returns successfully.
#define TALOS_RT_NATIVE_SCHEDULE_GENERATED_ACTIONS UINT32_C(64)
// Exact fanout metadata is present but must be activated only after the
// runtime proves that VPI startup did not write or dirty native state.
#define TALOS_RT_NATIVE_SCHEDULE_GUARDED_FANOUT UINT32_C(128)
// The generated plan exposes a runtime-owned clean-state flag used to bypass
// per-root specialization guards while no writable VPI state is dirty.
#define TALOS_RT_NATIVE_SCHEDULE_GUARDED_SPECIALIZATION UINT32_C(256)
// The compiler proved fixed actor multiplicity, generated actions, static
// control, and exact fanout. While the run-level VPI guard is clean, the
// runtime may omit per-actor handover checks; an unexpected action requests a
// transactional transfer to the generic scheduler.
#define TALOS_RT_NATIVE_SCHEDULE_CLEAN_SUPERSTEP UINT32_C(512)
// Experimental closed-world eval loop: exact static transitions feed the
// generated trigger masks instead of re-entering the actor worklist.
#define TALOS_RT_NATIVE_SCHEDULE_EVAL UINT32_C(1024)
// The generated eval closure is a certified static island, but runtime-owned
// actors remain outside it. Unlike FULLY_STATIC, this never suppresses their
// subscriptions or enables whole-design inline NBA assumptions; it only lets
// exact generated fanout and control execute between runtime handoff points.
#define TALOS_RT_NATIVE_SCHEDULE_STATIC_EVAL_ISLAND UINT32_C(2048)
// The compiler retained every runtime-observed writer at a checkpoint and
// excluded Inactive delays, language observers, DPI exports, and coverage.
// Live VPI observation or intervention revokes the runtime proof. Exact
// generated fanout may run while the shared scheduler owns the calendar,
// including periodic bootstrap. Its compiler-owned node table is immutable
// and remains alive until plan release.
// Runtime consumers keep their subscriptions and ordinary event arbitration.
#define TALOS_RT_NATIVE_SCHEDULE_RUNTIME_CALENDAR_EVAL UINT32_C(4096)
// The periodic coordinator drains the shared event regions after each edge
// and at runtime deadlines, retaining its clock bindings across those drains.
// Requires RUNTIME_CALENDAR_EVAL and its complete publication certificate.
#define TALOS_RT_NATIVE_SCHEDULE_SHARED_PERIODIC UINT32_C(8192)

typedef struct talos_rt_aot_deopt_actor {
  uint32_t slot;
  uint32_t flags;
  uint32_t schedule_rank;
  uint32_t queued_region;
  uint64_t insertion_sequence;
  uint64_t wake_time;
  uint64_t wait_offset;
  uint64_t wait_size;
  talos_rt_fragment_action_v1 action;
  uint32_t started;
  uint32_t ready;
} talos_rt_aot_deopt_actor;

typedef struct talos_rt_aot_deopt_nba {
  uint32_t slot;
  uint32_t exec_region;
  uint64_t sequence;
  uint64_t due_time;
} talos_rt_aot_deopt_nba;

typedef struct talos_rt_aot_deopt_snapshot {
  uint32_t size;
  uint64_t current_time;
  const talos_rt_aot_deopt_actor *actors;
  uint32_t actor_count;
  uint32_t ready_count;
  const talos_rt_aot_deopt_nba *nbas;
  uint32_t nba_count;
  uint32_t reserved;
  uint64_t next_sequence;
} talos_rt_aot_deopt_snapshot;

typedef struct talos_rt_native_schedule_node {
  uint32_t actor_slot;
  uint32_t continuation;
  uint32_t fusion_group;
} talos_rt_native_schedule_node;

// Revision-coupled description of a structurally proven free-running clock.
// Generated run_until code uses the returned control pointers directly while
// it owns the scheduler transaction; no runtime call is needed per edge.
typedef struct talos_rt_native_periodic_clock_v1 {
  uint32_t actor_slot;
  uint32_t continuation;
  uint32_t static_state;
  uint32_t reserved;
  uint64_t bit_offset;
  uint64_t half_period;
  // Line hits normally emitted by the clock's toggle/rearm blocks. The cold
  // bootstrap advances proven clocks directly, so it must retain these hits.
  const uint64_t *coverage_points;
  uint64_t coverage_point_count;
} talos_rt_native_periodic_clock_v1;

// Proven one-bit projection of a periodic source through a single-driver
// port/net alias.  Bootstrap and generated run_until use the same physical
// mapping so finite reset processes observe the identical clock history.
typedef struct talos_rt_native_periodic_alias_v1 {
  uint32_t source_static_state;
  uint32_t forwarding_actor_slot;
  uint32_t forwarding_continuation;
  uint32_t target_static_state;
  uint64_t source_bit_offset;
  uint64_t target_bit_offset;
  uint64_t driver_bit_offset;
} talos_rt_native_periodic_alias_v1;

typedef struct talos_rt_native_periodic_control_v1 {
  uint64_t *scheduler_time;
  uint32_t *termination_requested;
  uint64_t next_runtime_deadline;
} talos_rt_native_periodic_control_v1;

typedef uint32_t talos_rt_static_nba_storage;
enum {
  TALOS_RT_STATIC_NBA_FIXED_SLOT = 0,
  TALOS_RT_STATIC_NBA_ROOT_ACCUMULATOR = 1
};

// Shared scalar and generated-accumulator width policies. Generated staging
// writes the record below directly; the runtime consumes it at the NBA barrier
// and owns validation, force masking, edge computation, fanout, observer
// notification, snapshot, and deoptimization. Each write-mask run is one
// aligned 32-bit lane; repeated writes to a lane coalesce in place.
#define TALOS_RT_SCALAR_NBA_MAX_BITS UINT64_C(64)
#define TALOS_RT_GENERATED_NBA_MAX_BITS UINT64_C(256)
typedef struct talos_rt_generated_nba_accumulator_256 {
  uint64_t value[4];
  uint64_t unknown[4];
  uint64_t write_mask[4];
  uint32_t valid;
  uint32_t exec_region;
  // Bits rewritten with a different value since the last barrier. Merging
  // keeps only the final value, so a change watcher must still wake on a
  // round trip such as 0 -> 1 -> 0 (IEEE 1800-2017 4.6(b), 9.4.2). Set only
  // for roots whose watchers are all change-only; cleared at commit.
  uint64_t transient[4];
} talos_rt_generated_nba_accumulator_256;

// A certified clock activation writes each slot at most once per time slot.
// The compiler owns its payload and validity flag; the common NBA barrier
// consumes it directly. There is no enqueue operation or growable storage.
typedef struct talos_rt_generated_nba_slot {
  int64_t *offset;
  uint64_t *value;
  uint64_t *unknown;
  uint32_t *valid;
  // Fixed commit scratch: old value/unknown, then new value/unknown. Store
  // the complete certified batch before publishing any transition.
  uint64_t *transition;
  uint64_t bit_width;
} talos_rt_generated_nba_slot;

typedef struct talos_rt_static_nba_root {
  uint32_t commit_node;
  uint32_t static_state;
  uint64_t bit_width;
  talos_rt_generated_nba_accumulator_256 *generated_accumulator;
  const talos_rt_generated_nba_slot *generated_slots;
  uint64_t generated_slot_count;
} talos_rt_static_nba_root;

typedef struct talos_rt_static_nba_site {
  uint64_t site;
  uint32_t root;
  talos_rt_static_nba_storage storage;
} talos_rt_static_nba_site;

// LRM 4.6(b), 10.4.2: runtime initializers and generated clock activations
// append to the same ordered batch for each marked root.
typedef struct talos_rt_native_ordered_nba_bridge {
  void *queue;
  const uint8_t *roots;
} talos_rt_native_ordered_nba_bridge;

// Internal routing and orthogonal behavior flags encoded in
// static_fanout_entry::reserved. Generic plans require the RUNTIME route. The
// names keep the revision-coupled compiler and runtime ABI from assigning
// independent meanings to numeric literals.
#define TALOS_RT_FANOUT_RUNTIME UINT32_C(0)
#define TALOS_RT_FANOUT_DIRECT UINT32_C(1)
#define TALOS_RT_FANOUT_PERIODIC_ALIAS UINT32_C(2)
#define TALOS_RT_FANOUT_ROUTE_MASK UINT32_C(3)
// Suppress a publication from the same logical process when that write must
// not satisfy the future wait, such as a nested control not reached yet.
#define TALOS_RT_FANOUT_SUPPRESS_ACTIVE_SELF (UINT32_C(1) << 2)

typedef struct talos_rt_static_fanout_entry {
  uint32_t static_state;
  uint32_t actor_slot;
  uint32_t continuation;
  talos_rt_wait_edge_kind edge;
  uint32_t compute_node;
  uint32_t reserved;
  uint64_t low_bit;
  uint64_t bit_width;
  // Generated clock-kernel target. `compute_node` remains the exact generic
  // fallback identity; native ingress uses this owner/bit pair directly.
  uint32_t kernel;
  uint32_t merged_bit;
} talos_rt_static_fanout_entry;

typedef struct talos_rt_native_clock_kernel {
  uint32_t static_state;
  talos_rt_wait_edge_kind edge;
  uint64_t low_bit;
  uint64_t bit_width;
  uint64_t *ingress_mask;
  uint32_t ingress_word_count;
  uint32_t reserved;
  // Compiler-owned permanent-method eligibility. The runtime seeds a bit
  // once the fallback actor reaches the corresponding typed wait; generated
  // eval/NBA code consumes it without inspecting coroutine state.
  uint64_t *active_mask;
} talos_rt_native_clock_kernel;

#define TALOS_RT_MERGED_FRAGMENT_SHARED UINT32_C(1)
#define TALOS_RT_MERGED_FRAGMENT_FALLBACK UINT32_C(2)
typedef struct talos_rt_native_merged_fragment {
  uint32_t actor_slot;
  uint32_t continuation;
  uint32_t kernel;
  uint32_t bit;
  uint32_t compute_node;
  uint32_t flags;
  // Private generated body for clean native execution. A null pointer keeps
  // this stable ingress as an explicit fine-scheduler fallback boundary.
  talos_rt_status (*execute)(talos_rt_context *context);
} talos_rt_native_merged_fragment;

#define TALOS_RT_STATIC_ROOT_READ UINT32_C(1)
#define TALOS_RT_STATIC_ROOT_WRITE UINT32_C(2)
// Write-capable plans carry one zero-state completeness record per actor.
// Unknown dependencies remain conservatively affected by every disturbance.
#define TALOS_RT_STATIC_ROOT_UNSCOPED UINT32_C(4)
#define TALOS_RT_STATIC_ROOT_COMPLETE UINT32_C(8)

typedef struct talos_rt_static_actor_root {
  uint32_t actor_slot;
  uint32_t static_state;
  uint32_t flags;
  uint32_t reserved;
} talos_rt_static_actor_root;

typedef talos_rt_status (*talos_rt_native_schedule_bind)(
    void *mutable_state, talos_rt_context *context, uint32_t actor_slot,
    talos_rt_process_instance_v1 *instance);
// The runtime may replace an occupied slot when entering or returning from a
// task call, and passes a null instance when releasing a slot. Implementations
// must store the supplied instance without inspecting the former actor.
typedef talos_rt_status (*talos_rt_native_schedule_run)(
    void *mutable_state, talos_rt_context *context);
typedef talos_rt_status (*talos_rt_native_schedule_snapshot)(
    void *mutable_state, talos_rt_context *context,
    talos_rt_aot_deopt_snapshot *out_snapshot);
typedef talos_rt_status (*talos_rt_native_schedule_nba_commit)(
    void *mutable_state, talos_rt_context *context, uint32_t barrier_region,
    uint32_t *out_changed);
typedef talos_rt_status (*talos_rt_native_timeslot_coordinator)(
    void *mutable_state, talos_rt_context *context);
typedef void (*talos_rt_native_promotion_invalidate)(void);
typedef uint32_t (*talos_rt_native_promotion_ready)(void);
typedef void (*talos_rt_native_promotion_invalidate_range)(
    uint64_t bit_offset, uint64_t bit_width);
typedef talos_rt_status (*talos_rt_native_checkpoint_callback)(
    talos_rt_context *context);

// Immutable compiler-verified reverse index. Entries are sorted by begin; the
// midpoint of each balanced subtree carries its maximum end. Certificate IDs
// index idempotent proof-state actions for unknown-plane changes (LRM 6.11.2,
// 38.34); all fields have target-independent widths.
typedef struct talos_rt_native_promotion_dependency {
  uint64_t begin;
  uint64_t end;
  uint64_t subtree_end;
  uint64_t certificate;
} talos_rt_native_promotion_dependency;

// Cold proof-state actions. A kernel has a latch and ORs mask into its pending
// word on invalidation. An NBA-root certificate has only word/mask and clears
// that knownness bit. A routed body has route_slot/fallback, restores its
// four-state entry pointer and ORs mask into its pending word. These roles
// are compiler-verified and disjoint.
// route_slot points at a generated function-pointer global. The runtime copies
// the fallback representation without calling it or aliasing its function type.
typedef struct talos_rt_native_promotion_certificate {
  uint8_t *latch;
  uint64_t *word;
  uint64_t mask;
  void *route_slot;
  talos_rt_native_promotion_invalidate fallback;
} talos_rt_native_promotion_certificate;

// Internal generated-code helper over compiler-verified immutable tables.
// It never advances time, executes actors, or accesses canonical design state.
void talos_rt_v1_native_promotion_invalidate_ranges(
    const talos_rt_native_promotion_dependency *dependencies,
    uint64_t dependency_count,
    const talos_rt_native_promotion_certificate *certificates,
    talos_rt_native_promotion_invalidate invalidate_aggregate,
    uint64_t bit_offset, uint64_t bit_width);

// Monotonic X/Z-to-known recovery requests rechecking affected failed proofs;
// it never clears a valid kernel, route, or NBA-root certificate.
void talos_rt_v1_native_promotion_recheck_ranges(
    const talos_rt_native_promotion_dependency *dependencies,
    uint64_t dependency_count,
    const talos_rt_native_promotion_certificate *certificates,
    talos_rt_native_promotion_invalidate request_recheck, uint64_t bit_offset,
    uint64_t bit_width);

// Equivalent narrow-query helpers over an optional compiler-verified bucket
// index. starts has bucket_count + 1 uint32_t entries. Each packed uint64_t
// record has certificate ID in bits 16..63 and relative begin/end in bits
// 8..15/0..7. Bucket width is 64; its rounded extent cannot overflow uint64_t.
// Both helpers preserve the existing idempotent proof actions and invoke only
// the generated aggregate metadata action, once when any dependency overlaps.
void talos_rt_v1_native_promotion_invalidate_buckets(
    const uint32_t *starts, const uint64_t *records, uint64_t bucket_count,
    const talos_rt_native_promotion_certificate *certificates,
    talos_rt_native_promotion_invalidate invalidate_aggregate,
    uint64_t bit_offset, uint64_t bit_width);
void talos_rt_v1_native_promotion_recheck_buckets(
    const uint32_t *starts, const uint64_t *records, uint64_t bucket_count,
    const talos_rt_native_promotion_certificate *certificates,
    talos_rt_native_promotion_invalidate request_recheck, uint64_t bit_offset,
    uint64_t bit_width);

typedef struct talos_rt_native_schedule_plan {
  uint32_t size;
  uint64_t graph_layout_checksum;
  void *mutable_state;
  uint64_t mutable_state_size;
  uint32_t actor_capacity;
  uint32_t flags;
  uint8_t *state_value;
  uint8_t *state_unknown;
  uint64_t state_bit_count;
  talos_rt_native_schedule_bind bind;
  talos_rt_native_schedule_run run;
  talos_rt_native_schedule_snapshot fallback_snapshot;
  // The prototype ABI accepts exactly sizeof(this structure).
  const talos_rt_static_nba_root *nba_roots;
  uint32_t nba_root_count;
  uint32_t nba_reserved;
  const talos_rt_static_nba_site *nba_sites;
  uint64_t nba_site_count;
  const talos_rt_static_fanout_entry *fanout_entries;
  uint64_t fanout_entry_count;
  const talos_rt_static_actor_root *actor_roots;
  uint64_t actor_root_count;
  talos_rt_native_schedule_nba_commit nba_commit;
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
  const talos_rt_native_clock_kernel *clock_kernels;
  uint32_t clock_kernel_count;
  uint32_t clock_kernel_reserved;
  const talos_rt_native_merged_fragment *merged_fragments;
  uint64_t merged_fragment_count;
  talos_rt_native_timeslot_coordinator timeslot_coordinator;
  // Cold-path invalidation for the generated two-state closure. External
  // X/Z writes and transactional fine-scheduler handoffs clear the selected
  // variant here; the generated coordinator rescans only after quiescence.
  talos_rt_native_promotion_invalidate promotion_invalidate;
  // Cold query used only by a transient Tier-2 transaction. It returns one
  // after the candidate closure's canonical unknown plane is clear, allowing
  // the runtime to hand control directly to the two-state Tier-1 route.
  talos_rt_native_promotion_ready promotion_ready;
  // Optional exact canonical-range proof invalidation. It must invalidate
  // every dependent value-domain certificate before specialized work resumes,
  // preserving unrelated certificates. Scheduling disturbance is tracked
  // separately. Unknown footprints still use promotion_invalidate above.
  talos_rt_native_promotion_invalidate_range promotion_invalidate_range;
  // Optional knownness recovery notification. Queue failed dependent proofs
  // for a safe-boundary recheck without revoking positive certificates.
  // Like invalidation, this hook cannot execute actors or observers.
  talos_rt_native_promotion_invalidate_range promotion_recheck_range;
  // Generated ordered updates share the ordinary NBA barrier. This count
  // makes their pending work visible without allocating scheduler events.
  const uint32_t *nba_ordered_count;
  const talos_rt_native_ordered_nba_bridge *nba_ordered_bridge;
} talos_rt_native_schedule_plan;

// Serial generated-simulator scheduler. The scheduler owns an instance after
// a successful add. Bit zero selects final-phase work and bits 1-3 encode the
// executable home region. A zero flags value retains the historical ordinary
// Active-process behavior.
talos_rt_status
talos_rt_v1_scheduler_add(talos_rt_context *context,
                            talos_rt_process_instance_v1 *instance,
                            uint32_t flags);
// Ranked form used by generated simulators. Lower ranks execute first within
// an event region; equal ranks retain deterministic insertion order.
talos_rt_status
talos_rt_v1_scheduler_add_ranked(talos_rt_context *context,
                                   talos_rt_process_instance_v1 *instance,
                                   uint32_t flags, uint32_t schedule_rank);
talos_rt_status talos_rt_v1_scheduler_add_planned(
    talos_rt_context *context, talos_rt_process_instance_v1 *instance,
    uint32_t flags, uint32_t initial_rank, const uint32_t *continuations,
    const uint32_t *ranks, uint32_t continuation_count);
// Execute one newly added process synchronously until its first suspension.
// This atomically establishes a scheduler-owned wait before its spawning
// process continues, without making the parent wait for the occurrence.
talos_rt_status
talos_rt_v1_scheduler_prime(talos_rt_context *context,
                              talos_rt_process_instance_v1 *instance);
talos_rt_status talos_rt_v1_scheduler_install_aot(
    talos_rt_context *context, const talos_rt_native_schedule_plan *plan);
// OR one stable merged-fragment bit into a generated kernel's ingress.  The
// operation is idempotent until the generated coordinator drains the mask.
talos_rt_status talos_rt_v1_scheduler_activate_clock_kernel(
    talos_rt_context *context, uint32_t kernel, uint32_t merged_bit);
// Execute all clock masks accumulated in the current time slot.  A generated
// coordinator owns the shared-kernel drain and the single NBA/fanout epilogue.
talos_rt_status
talos_rt_v1_scheduler_run_clock_coordinator(talos_rt_context *context);
// Keep a generated eval continuation active across the common NBA barrier.
// Prepare drains runtime Active/Inactive work without reentering generated
// eval or advancing time. UINT32_MAX means no barrier is pending. Generated
// ingress must be checked again before running the compiled NBA prefix.
talos_rt_status
talos_rt_v1_scheduler_prepare_eval_nba(talos_rt_context *context,
                                         uint32_t *out_region);
// As above, including captured transaction-local NBA work. The only admitted
// local region is NBA; UINT32_MAX denotes no local capture. This does not
// publish, commit, or reevaluate any RHS or target (IEEE 1800-2023 4.9.4).
talos_rt_status talos_rt_v1_scheduler_prepare_eval_nba_local(
    talos_rt_context *context, uint32_t local_region, uint32_t *out_region);
// Write-capable generated continuations also return TIER_UNAVAILABLE before
// an actor without a complete storage-effects certificate executes. The
// caller exports its captured pending work before that actor can invoke VPI.
// Off/read generated continuations use the service above.
talos_rt_status talos_rt_v1_scheduler_prepare_eval_nba_local_write(
    talos_rt_context *context, uint32_t local_region, uint32_t *out_region);
// The caller has consumed the compiled NBA prefix for this region. Complete
// the remaining queues before returning to any Active reader or observer.
talos_rt_status
talos_rt_v1_scheduler_complete_eval_nba(talos_rt_context *context,
                                          uint32_t region, uint32_t changed);
// Enter/leave one compiler-proven clean fragment without resuming its
// coroutine. The actor remains suspended at the same stable continuation so a
// transactional fallback can use the original frame immediately.
talos_rt_status talos_rt_v1_scheduler_direct_fragment_enter(
    talos_rt_context *context, uint32_t actor_slot, uint32_t continuation,
    talos_rt_process_instance_v1 **out_instance);
talos_rt_status
talos_rt_v1_scheduler_direct_fragment_leave(talos_rt_context *context,
                                              uint32_t actor_slot);
// Experimental eval-mode entry: execute one statically bound actor directly
// inside the scheduler-owned clean transaction.
talos_rt_status
talos_rt_v1_scheduler_execute_aot_actor(talos_rt_context *context,
                                          uint32_t actor_slot);
// Returns nonzero when a priority signal-resumed process must run before the
// generated coordinator executes another owner in the current event region.
uint32_t
talos_rt_v1_scheduler_priority_signal_pending(talos_rt_context *context);
// Return nonzero when a generated native schedule must hand control back to
// the runtime after closing the current Active/NBA step. This includes a
// runtime error and pending Postponed coverage work.
uint32_t talos_rt_v1_scheduler_handoff_pending(talos_rt_context *context);
// Publish one exact cold continuation after the generated coordinator
// returns. The callback is invoked outside the hot Tier-1/Tier-2 call graph;
// its generated thunk resumes the same slot's ready-mask/NBA transaction.
talos_rt_status talos_rt_v1_scheduler_queue_aot_checkpoint(
    talos_rt_context *context, uint32_t actor_slot, uint32_t continuation,
    talos_rt_native_checkpoint_callback callback);
talos_rt_status talos_rt_v1_scheduler_add_aot(
    talos_rt_context *context, talos_rt_process_instance_v1 *instance,
    uint32_t flags, uint32_t actor_slot, uint32_t initial_rank,
    const uint32_t *continuations, const uint32_t *ranks,
    uint32_t continuation_count, const uint32_t *bytecode_continuations,
    uint32_t bytecode_continuation_count);
talos_rt_status talos_rt_v1_scheduler_run_aot_nodes(
    talos_rt_context *context, const talos_rt_native_schedule_node *nodes,
    uint32_t node_count);
// Drain time zero to quiescence, validate the proven periodic actors against
// their actual coroutine state, and detach only those deadlines for generated
// run_until ownership. `next_edges` has one element per clock.
talos_rt_status talos_rt_v1_scheduler_prepare_periodic_aot(
    talos_rt_context *context, const talos_rt_native_schedule_node *nodes,
    uint32_t node_count, const talos_rt_native_periodic_clock_v1 *clocks,
    uint32_t clock_count, const talos_rt_native_periodic_alias_v1 *aliases,
    uint32_t alias_count, uint64_t *next_edges,
    talos_rt_native_periodic_control_v1 *out_control);
// Restore detached periodic actors before the generated coordinator returns a
// Tier-2/3 boundary to the runtime.
talos_rt_status talos_rt_v1_scheduler_handoff_periodic_aot(
    talos_rt_context *context,
    const talos_rt_native_periodic_clock_v1 *clocks, uint32_t clock_count,
    const uint64_t *next_edges);
// Drain the current slot without advancing or releasing generated clocks.
// Refresh the deadline after runtime continuations have installed new waits.
talos_rt_status talos_rt_v1_scheduler_drain_periodic_aot(
    talos_rt_context *context,
    talos_rt_native_periodic_control_v1 *control);
talos_rt_status
talos_rt_v1_scheduler_enter_periodic_aot(talos_rt_context *context,
                                           uint64_t time);
// Materialize the runtime-owned hybrid scheduler records at an AOT region
// boundary. Returned pointers remain context-owned until scheduler mutation.
talos_rt_status talos_rt_v1_scheduler_snapshot_aot(
    talos_rt_context *context, talos_rt_aot_deopt_snapshot *out_snapshot);
// Return nonzero when an actor/root access may use compiler-emitted planes.
// Deposits are transient until slot quiescence; force/assign ownership remains
// dirty until its corresponding release.
uint32_t talos_rt_v1_static_specialization_guard(talos_rt_context *context,
                                                   uint32_t actor_slot,
                                                   uint32_t static_state,
                                                   uint32_t flags);
uint32_t
talos_rt_v1_static_nba_specialization_guard(talos_rt_context *context,
                                              uint32_t root_index);
// Full-only visibility guard for conditionally private captures (38.36.1).
uint32_t
talos_rt_v1_static_nba_specialization_guard_write(talos_rt_context *context,
                                                    uint32_t root_index);
// Return the scheduler-owned tagged logical identity used by await/join
// records. The token is never a host address and is not reused within a
// context. Native identities carry TALOS_RT_LOGICAL_PROCESS_NATIVE_TAG.
uint64_t
talos_rt_v1_scheduler_process_token(talos_rt_context *context,
                                      talos_rt_process_instance_v1 *instance);
// Return the shared tagged identity of the executing logical process, or zero
// when called outside a scheduled logical-process activation.
uint64_t talos_rt_v1_process_current(talos_rt_context *context);
// Query one nonnull logical-process identity. Completed identities remain
// valid through the context-owned tombstone sets.
talos_rt_status
talos_rt_v1_process_status(talos_rt_context *context,
                             uint64_t logical_process,
                             talos_rt_process_state *out_state);
// Read or restore a process's independent random stream. Context-owned
// tombstones retain the two state words after FINISHED or KILLED.
talos_rt_status
talos_rt_v1_process_random_get(talos_rt_context *context,
                                 uint64_t logical_process,
                                 talos_rt_random_state_v1 *out_state);
talos_rt_status
talos_rt_v1_process_random_set(talos_rt_context *context,
                                 uint64_t logical_process,
                                 const talos_rt_random_state_v1 *state);
// Apply one IEEE process-object control transaction. External targets are
// updated before return. A control of the executing logical process returns a
// disposition which the caller must publish as its fragment action. Returns
// TALOS_RT_TIER_UNAVAILABLE while a native AOT plan owns scheduler actors.
talos_rt_status talos_rt_v1_process_control(
    talos_rt_context *context, uint64_t logical_process,
    talos_rt_process_control_kind kind,
    talos_rt_process_control_disposition *out_disposition);
// Recursively terminate every live descendant of the currently executing
// logical process. Task activations retain their caller's logical identity.
talos_rt_status
talos_rt_v1_scheduler_disable_children(talos_rt_context *context);
// Install a new context-wide $monitor process. `design_process` selects the
// design-bytecode token namespace; native process tokens use zero.
talos_rt_status talos_rt_v1_monitor_register(talos_rt_context *context,
                                                 uint64_t process_token,
                                                 uint32_t design_process);
// Register a monitor using the shared tagged logical-process identity.
talos_rt_status
talos_rt_v1_monitor_register_logical(talos_rt_context *context,
                                       uint64_t logical_process);
talos_rt_status talos_rt_v1_monitor_control(talos_rt_context *context,
                                                uint32_t enabled);
uint32_t talos_rt_v1_monitor_current(talos_rt_context *context);
// Dynamic named-block activations are inherited by spawned logical
// processes. A nonzero activation selects one lexical activation. With a zero
// activation, all_activations selects either the innermost inherited lexical
// activation or every live activation with the exact target ID.
talos_rt_status talos_rt_v1_control_enter(talos_rt_context *context,
                                              uint64_t target_id,
                                              uint64_t *out_activation);
// Attach the current activation's canonical-frame continuation used when a
// different logical process disables the block. Registration does not yield.
talos_rt_status talos_rt_v1_control_boundary(talos_rt_context *context,
                                                 uint64_t activation,
                                                 uint32_t continuation);
talos_rt_status talos_rt_v1_control_leave(talos_rt_context *context,
                                              uint64_t activation);
talos_rt_status talos_rt_v1_control_disable(talos_rt_context *context,
                                                uint64_t target_id,
                                                uint64_t activation,
                                                uint32_t all_activations);
// Disable with the same scheduling semantics and return the exact outermost
// affected activation in the current logical process. Zero means the current
// process was unaffected. The returned token is a value, not a live control.
talos_rt_status talos_rt_v1_control_disable_selected(
    talos_rt_context *context, uint64_t target_id, uint64_t activation,
    uint32_t all_activations, uint64_t *out_selected);
// Consume the escape request produced when a disable in a task selected a
// named block owned by an older task activation in the same logical process.
uint32_t talos_rt_v1_control_escape_pending(talos_rt_context *context);
// Return one exactly once for each nonzero site ID in a context and zero on
// later claims. This guards descriptor-backed static local initialization.
uint32_t talos_rt_v1_static_once(talos_rt_context *context,
                                   uint64_t site_id);
// Return one for the first execution of a deferred-immediate assertion site
// by the current logical process in a time slot, and zero for repeats.
uint32_t talos_rt_v1_deferred_once(talos_rt_context *context,
                                     uint64_t site_id);
// Enqueue one deferred-immediate assertion report for the current logical
// process. Every encounter has an independent pending ticket until it matures
// or reaches a process flush point. The ticket is opaque and context-local.
uint64_t talos_rt_v1_deferred_enqueue(talos_rt_context *context,
                                        uint64_t site_id);
// Site enqueue carrying the stable identity of a specifically labeled
// assertion. A later disable of that assertion cancels every pending ticket
// with the same assertion identity without flushing unrelated reports.
uint64_t talos_rt_v1_deferred_enqueue_for_assertion(
    talos_rt_context *context, uint64_t site_id, uint64_t assertion_id);
// Apply one IEEE 1800 assertion-control action (1 through 11) to one
// compiler-resolved assertion identity. Kill also cancels its queued deferred
// reports; Off leaves already queued reports intact. A locked identity ignores
// every control action except Unlock (2).
talos_rt_status talos_rt_v1_assertion_control(talos_rt_context *context,
                                                  uint32_t action,
                                                  uint64_t assertion_id);
// Return one when a new attempt may start for the assertion identity.
uint32_t talos_rt_v1_assertion_enabled(talos_rt_context *context,
                                         uint64_t assertion_id);
// Snapshot pass/fail action enablement for a new attempt. Bit 0 is
// nonvacuous pass, bit 1 is vacuous pass, and bit 2 is fail.
uint32_t talos_rt_v1_assertion_action_state(talos_rt_context *context,
                                              uint64_t assertion_id);
// Return the generation advanced by each effective Kill for one assertion.
// A locked assertion ignores Kill and therefore retains its generation.
uint64_t talos_rt_v1_assertion_kill_epoch(talos_rt_context *context,
                                            uint64_t assertion_id);
// Consume a deferred report ticket and return one iff it remains pending for
// its originating logical process.
uint32_t talos_rt_v1_deferred_mature(talos_rt_context *context,
                                       uint64_t ticket);
// Copy one nonblocking assignment into the current time slot. The scheduler
// applies queued updates in call order after active work reaches quiescence.
// A UINT64_MAX bit offset is an out-of-range dynamic selection and is ignored.
talos_rt_status talos_rt_v1_scheduler_nba(
    talos_rt_context *context, uint8_t *value_plane, uint8_t *unknown_plane,
    uint64_t plane_bit_count, uint64_t bit_offset, uint64_t bit_width,
    uint64_t delay, const uint8_t *value, const uint8_t *unknown);
// Queue a packed-view NBA while retaining the exact boundary of the input
// reference. The signed low bit is relative to that view. Unknown,
// unrepresentable, or nonoverlapping selections are ignored; partial overlaps
// copy only the corresponding source bits.
talos_rt_status talos_rt_v1_scheduler_packed_slice_nba(
    talos_rt_context *context, uint8_t *value_plane, uint8_t *unknown_plane,
    uint64_t plane_bit_count, uint64_t base_handle, uint64_t base_bit_width,
    int64_t low_bit, uint32_t low_bit_valid, uint64_t source_bit_width,
    uint64_t delay, uint64_t static_site, uint64_t clocking_output,
    const uint8_t *value, const uint8_t *unknown);
// Queue an update to a net driver's contribution. The scheduler commits it in
// the same NBA/Re-NBA region as a variable NBA and then resolves affected nets.
talos_rt_status talos_rt_v1_scheduler_driver_nba(
    talos_rt_context *context, uint8_t *value_plane, uint8_t *unknown_plane,
    uint64_t plane_bit_count, uint64_t bit_offset, uint64_t bit_width,
    uint64_t delay, const uint8_t *value, const uint8_t *unknown);
#define TALOS_RT_INERTIAL_DRIVER_VECTOR_DELAY UINT32_C(1)
#define TALOS_RT_INERTIAL_DRIVER_DEFER_RESOLUTION UINT32_C(2)
#define TALOS_RT_INERTIAL_DRIVER_PUBLISH_RAW UINT32_C(4)
#define TALOS_RT_INERTIAL_DRIVER_REAL32 UINT32_C(8)
#define TALOS_RT_INERTIAL_DRIVER_REAL64 UINT32_C(16)
#define TALOS_RT_INERTIAL_PATH_ON_DETECT UINT32_C(32)
#define TALOS_RT_INERTIAL_PATH_SHOW_CANCELLED UINT32_C(64)
#define TALOS_RT_INERTIAL_PATH_EXACT_TRANSITIONS UINT32_C(128)
// Schedule an IEEE inertial gate or continuous-assignment driver update.
// Re-evaluating the same site cancels a distinct pending target; an identical
// target retains its original deadline. UINT64_MAX denotes a suppressed
// dynamic selection and cancels any pending target at the site.
talos_rt_status talos_rt_v1_scheduler_inertial_driver(
    talos_rt_context *context, uint8_t *value_plane, uint8_t *unknown_plane,
    uint64_t plane_bit_count, uint64_t bit_offset, uint64_t bit_width,
    uint64_t code_unit, uint32_t component, uint32_t flags, uint64_t rise_delay,
    uint64_t fall_delay, uint64_t turnoff_delay, const uint8_t *value,
    const uint8_t *unknown);
// Schedule one statically unrolled module-path delay group. Group zero starts
// a new per-bit generation batch; stale queue entries are rejected by keyed
// generation at maturity instead of being erased from the global queue.
talos_rt_status talos_rt_v1_scheduler_inertial_path_driver(
    talos_rt_context *context, uint8_t *value_plane, uint8_t *unknown_plane,
    uint64_t plane_bit_count, uint64_t bit_offset, uint64_t bit_width,
    uint64_t code_unit, uint32_t component, uint32_t group,
    uint32_t group_count, uint32_t flags, uint64_t rise_delay,
    uint64_t fall_delay, uint64_t turnoff_delay, const uint8_t *value,
    const uint8_t *unknown, const uint8_t *active_mask,
    const uint8_t *rise_mask, const uint8_t *fall_mask,
    const uint8_t *turnoff_mask);
talos_rt_status talos_rt_v1_scheduler_inertial_path_driver_pulse(
    talos_rt_context *context, uint8_t *value_plane, uint8_t *unknown_plane,
    uint64_t plane_bit_count, uint64_t bit_offset, uint64_t bit_width,
    uint64_t code_unit, uint32_t component, uint32_t group,
    uint32_t group_count, uint32_t flags, uint64_t rise_delay,
    uint64_t fall_delay, uint64_t turnoff_delay, uint64_t pulse_reject,
    uint64_t pulse_error, const uint8_t *value, const uint8_t *unknown,
    const uint8_t *active_mask, const uint8_t *rise_mask,
    const uint8_t *fall_mask, const uint8_t *turnoff_mask,
    const uint8_t *pulse_transition_masks);
// Schedule one masked procedural-storage module-path delay group. Bits outside
// `write_mask` are untouched. Unqualified blocking bits publish before the
// call returns; nonblocking bits retain the NBA region at their due time.
talos_rt_status talos_rt_v1_scheduler_inertial_path_storage(
    talos_rt_context *context, uint8_t *value_plane, uint8_t *unknown_plane,
    uint64_t plane_bit_count, uint64_t bit_offset, uint64_t bit_width,
    uint64_t site_id, uint32_t component, uint32_t group, uint32_t group_count,
    uint32_t nonblocking, uint64_t rise_delay, uint64_t fall_delay,
    uint64_t turnoff_delay, const uint8_t *value, const uint8_t *unknown,
    const uint8_t *write_mask, const uint8_t *active_mask,
    const uint8_t *rise_mask, const uint8_t *fall_mask,
    const uint8_t *turnoff_mask);
talos_rt_status talos_rt_v1_scheduler_inertial_path_storage_pulse(
    talos_rt_context *context, uint8_t *value_plane, uint8_t *unknown_plane,
    uint64_t plane_bit_count, uint64_t bit_offset, uint64_t bit_width,
    uint64_t site_id, uint32_t component, uint32_t group, uint32_t group_count,
    uint32_t nonblocking, uint32_t pulse_flags, uint64_t rise_delay,
    uint64_t fall_delay, uint64_t turnoff_delay, uint64_t pulse_reject,
    uint64_t pulse_error, const uint8_t *value, const uint8_t *unknown,
    const uint8_t *write_mask, const uint8_t *active_mask,
    const uint8_t *rise_mask, const uint8_t *fall_mask,
    const uint8_t *turnoff_mask, const uint8_t *pulse_transition_masks);
// Schedule the polarity-specific driver banks of one three-state primitive as
// one inertial update. The transition planes carry the logical gate result
// used for delay selection; both banks mature before the net is resolved.
talos_rt_status talos_rt_v1_scheduler_inertial_driver_strength_pair(
    talos_rt_context *context, uint8_t *value_plane, uint8_t *unknown_plane,
    uint64_t plane_bit_count, uint64_t low_bit_offset, uint64_t high_bit_offset,
    uint64_t bit_width, uint64_t code_unit, uint32_t component,
    uint64_t rise_delay, uint64_t fall_delay, uint64_t turnoff_delay,
    const uint8_t *low_value, const uint8_t *low_unknown,
    const uint8_t *high_value, const uint8_t *high_unknown,
    const uint8_t *transition_value, const uint8_t *transition_unknown);
// Schedule one statically unrolled module-path delay group for the two
// complementary strength banks of a conditional primitive. The banks mature
// atomically and resolve only after the high-bank event is committed.
talos_rt_status talos_rt_v1_scheduler_inertial_path_strength_pair_pulse(
    talos_rt_context *context, uint8_t *value_plane, uint8_t *unknown_plane,
    uint64_t plane_bit_count, uint64_t low_bit_offset, uint64_t high_bit_offset,
    uint64_t bit_width, uint64_t code_unit, uint32_t component, uint32_t group,
    uint32_t group_count, uint32_t pulse_flags, uint64_t rise_delay,
    uint64_t fall_delay, uint64_t turnoff_delay, uint64_t pulse_reject,
    uint64_t pulse_error, const uint8_t *low_value, const uint8_t *low_unknown,
    const uint8_t *high_value, const uint8_t *high_unknown,
    const uint8_t *transition_value, const uint8_t *transition_unknown,
    const uint8_t *active_mask, const uint8_t *rise_mask,
    const uint8_t *fall_mask, const uint8_t *turnoff_mask,
    const uint8_t *pulse_transition_masks);
talos_rt_status talos_rt_v1_scheduler_inertial_path_strength_pair(
    talos_rt_context *context, uint8_t *value_plane, uint8_t *unknown_plane,
    uint64_t plane_bit_count, uint64_t low_bit_offset, uint64_t high_bit_offset,
    uint64_t bit_width, uint64_t code_unit, uint32_t component, uint32_t group,
    uint32_t group_count, uint64_t rise_delay, uint64_t fall_delay,
    uint64_t turnoff_delay, const uint8_t *low_value,
    const uint8_t *low_unknown, const uint8_t *high_value,
    const uint8_t *high_unknown, const uint8_t *transition_value,
    const uint8_t *transition_unknown, const uint8_t *active_mask,
    const uint8_t *rise_mask, const uint8_t *fall_mask,
    const uint8_t *turnoff_mask);
// Resolve a canonical driver-state range after an immediate native store.
// Net declarations with propagation delays schedule their post-resolution
// visible transitions; other nets publish immediately.
talos_rt_status
talos_rt_v1_scheduler_resolve_drivers(talos_rt_context *context,
                                        uint64_t begin, uint64_t end);
// Resolve only nets reached by drivers whose bytecode descriptors carry
// TALOS_RT_DB_DRIVER_INITIAL_X. This runs before user processes are spawned.
talos_rt_status
talos_rt_v1_scheduler_resolve_initial_drivers(talos_rt_context *context);
// Clocking-output forms retain the clock-variable identity so coincident
// synchronous drives can be checked and resolved bitwise at the NBA barrier.
talos_rt_status talos_rt_v1_scheduler_clocking_nba(
    talos_rt_context *context, uint8_t *value_plane, uint8_t *unknown_plane,
    uint64_t plane_bit_count, uint64_t bit_offset, uint64_t bit_width,
    uint64_t delay, const uint8_t *value, const uint8_t *unknown,
    uint64_t clocking_output);
talos_rt_status talos_rt_v1_scheduler_clocking_driver_nba(
    talos_rt_context *context, uint8_t *value_plane, uint8_t *unknown_plane,
    uint64_t plane_bit_count, uint64_t bit_offset, uint64_t bit_width,
    uint64_t delay, const uint8_t *value, const uint8_t *unknown,
    uint64_t clocking_output);
// Site-aware form shared by native and design-bytecode fragments. The site
// identity is validated against an installed static schedule when one is
// present; otherwise this has exactly the generic scheduler semantics.
talos_rt_status talos_rt_v1_scheduler_static_nba(
    talos_rt_context *context, uint64_t site, uint8_t *value_plane,
    uint8_t *unknown_plane, uint64_t plane_bit_count, uint64_t bit_offset,
    uint64_t bit_width, const uint8_t *value, const uint8_t *unknown);
// AOT-only packed staging entry. The compiler has already resolved the site
// to an installed root and proved the destination to be wholly in range.
talos_rt_status talos_rt_v1_scheduler_static_nba_packed(
    talos_rt_context *context, uint32_t root, uint8_t *value_plane,
    uint8_t *unknown_plane, uint64_t plane_bit_count, uint64_t root_offset,
    uint64_t bit_width, uint64_t value, uint64_t unknown);
// Revision-coupled generated staging for a compiler-proven fixed wide root in
// a VPI-off static-control schedule. Lifecycle, root bounds, and plane layout
// are established by the installed plan, so this avoids redundant hot checks.
void talos_rt_v1_static_nba_stage_wide(talos_rt_context *context,
                                         uint32_t root, uint64_t root_offset,
                                         uint64_t bit_width, uint64_t value,
                                         uint64_t unknown,
                                         uint32_t has_unknown);
// Claim one statically resolved narrow root update for this event slot. If a
// generic update has already claimed the root, the runtime stages this and
// subsequent claims through the ordered generic queue until slot quiescence.
talos_rt_status talos_rt_v1_static_nba_claim(
    talos_rt_context *context, uint32_t root, uint8_t *value_plane,
    uint8_t *unknown_plane, uint64_t plane_bit_count, uint64_t root_offset,
    uint64_t bit_width, uint64_t value, uint64_t unknown);
// Commit one valid fixed accumulator at an NBA barrier. Generated callbacks
// invoke roots in compute-graph order and OR their transition result into
// out_changed.
talos_rt_status
talos_rt_v1_static_nba_commit_root(talos_rt_context *context, uint32_t root,
                                     uint32_t barrier_region,
                                     uint32_t *out_changed);
// Commit the leading compute-graph-ordered accumulator roots in one runtime
// call. Generated callbacks use this bulk form to avoid one ABI crossing per
// root; the single-root entry remains available for mixed/deoptimized paths.
talos_rt_status talos_rt_v1_static_nba_commit_roots(
    talos_rt_context *context, uint32_t root_count, uint32_t barrier_region,
    uint32_t *out_changed);

// schedule.nba.batch: compiler-certified private scalar roots. Guards that
// fail leave the root staged for the subsequent canonical barrier. Candidates
// must be distinct and ordered; direct change fanout includes transient bits.
talos_rt_status talos_rt_v1_static_nba_commit_batch(
    talos_rt_context *context, const uint32_t *roots, uint32_t count,
    uint32_t barrier_region, uint32_t *out_changed);
// A compiled batch names its installed plan and runs only with shared state,
// no overrides/observers, and no canonical or slow-root staging. All layout
// and fanout decisions have already been certified by Schedule.
uint32_t talos_rt_v1_static_nba_batch_guard(
    talos_rt_context *context, const talos_rt_native_schedule_plan *plan,
    uint32_t barrier_region);
void talos_rt_v1_static_nba_batch_knownness(talos_rt_context *context,
                                              uint64_t offset, uint64_t width,
                                              uint64_t old_unknown,
                                              uint64_t new_unknown);
void talos_rt_v1_static_nba_batch_finish(talos_rt_context *context,
                                           uint64_t stages, uint64_t commits,
                                           uint32_t ingress_pending);

// Cold commit of one generated ordered record after external mutation. It
// honors force/assign masks and publishes through canonical runtime fanout.
talos_rt_status talos_rt_v1_static_nba_commit_ordered(
    talos_rt_context *context, uint32_t root, int64_t offset, uint64_t width,
    uint64_t value, uint64_t unknown);
// Generated clean-superstep NBA commit code checks this once per barrier.
// A false result retains the validating generic commit path for VPI,
// force/release, bytecode mutation, and transactional handoff.
uint32_t
talos_rt_v1_static_nba_direct_commit_guard(talos_rt_context *context);
// Account for scalar accumulators committed directly by one generated
// callback. Keeping this batched preserves diagnostics without a call per
// root.
void talos_rt_v1_static_nba_account_generated_commits(
    talos_rt_context *context, uint32_t count);
// Schedule one whole managed string word. The queued word remains a precise
// tagged root through commit, and equal byte contents do not publish a signal
// transition even when the immutable handles differ.
talos_rt_status talos_rt_v1_scheduler_string_nba(
    talos_rt_context *context, uint8_t *value_plane, uint64_t plane_bit_count,
    uint64_t bit_offset, uint64_t delay, talos_rt_string_v1 value);
// Schedule one class-property update. The destination and a managed value, if
// present, remain precise GC roots through commit. `unknown` is either null or
// a second `plane_size` byte plane immediately following the value plane in
// the property layout.
talos_rt_status talos_rt_v1_scheduler_managed_nba(
    talos_rt_context *context, talos_rt_object_v1 *destination,
    uint64_t offset, const void *value, const void *unknown,
    uint64_t plane_size, uint64_t delay);
// Schedule a partial update of a class property, or of a reference-path
// element when `offset` is UINT64_MAX: at commit, only the bits set in the
// `plane_size` byte `mask` take their values (and unknown bits) from `value`
// and `unknown`; the others keep the destination's value then.
talos_rt_status talos_rt_v1_scheduler_managed_nba_masked(
    talos_rt_context *context, talos_rt_object_v1 *destination,
    uint64_t offset, const void *value, const void *unknown, const void *mask,
    uint64_t plane_size, uint64_t delay);
// Record a changed native-state range. `edges` is a bitmask of the edge kinds
// observed on the range's least-significant bit. Signal-width wait records use
// the final wait-entry word for their watched width.
#define TALOS_RT_SIGNAL_CHANGE (UINT32_C(1) << 0)
#define TALOS_RT_SIGNAL_POSEDGE (UINT32_C(1) << 1)
#define TALOS_RT_SIGNAL_NEGEDGE (UINT32_C(1) << 2)
void talos_rt_v1_scheduler_signal(talos_rt_context *context,
                                    uint64_t bit_offset, uint64_t bit_width,
                                    uint32_t edges);
void talos_rt_v1_scheduler_signal_transition(
    talos_rt_context *context, uint64_t bit_offset, uint64_t bit_width,
    const uint8_t *old_value, const uint8_t *old_unknown,
    const uint8_t *new_value, const uint8_t *new_unknown);
// Revision-coupled clean-AOT leaf for compiler-proven fixed packed roots.
// Scalar planes and a validated root-relative range allow exact static fanout
// without generic handle decoding, transition buffers, or observer scans.
void talos_rt_v1_scheduler_static_transition(
    talos_rt_context *context, uint32_t static_state, uint64_t low_bit,
    uint64_t bit_width, uint64_t old_value, uint64_t old_unknown,
    uint64_t new_value, uint64_t new_unknown);
// LRM 9.4.2: a generated activation keeps its source event control inactive
// while publishing a blocking write. This leaf delivers transitions under
// that actor's identity without executing an actor or advancing time.
void talos_rt_v1_scheduler_static_transition_owned(
    talos_rt_context *context, uint32_t actor_slot, uint32_t static_state,
    uint64_t low_bit, uint64_t bit_width, uint64_t old_value,
    uint64_t old_unknown, uint64_t new_value, uint64_t new_unknown);
// Bracket one statement that the caller executes as one event (IEEE 1800-2023
// 4.7): between begin and end there is no time control and no call that may
// observe signal values or run another process. While no observer of
// individual occurrences exists (VPI value change 38.36.1, computed or
// conditional waits 9.4.3, coverage, clock occurrences, force/assign, runtime
// subscriptions of the root), static transitions published in between are
// delivered at end as one transition per root window carrying the OR of every
// write's change and edge masks. That wakes exactly the waiters per-write
// publication wakes (9.4.2); otherwise each publishes immediately. Batches
// nest; only the outermost end delivers.
talos_rt_status
talos_rt_v1_scheduler_publication_batch_begin(talos_rt_context *context);
talos_rt_status
talos_rt_v1_scheduler_publication_batch_end(talos_rt_context *context);
// Activate a compiler-grouped set of exact static-fanout compute nodes. This
// is an internal clean-transaction leaf; dynamic/VPI handoff continues to use
// ordinary scalar transition publication.
void talos_rt_v1_scheduler_activate_static_nodes(talos_rt_context *context,
                                                   const uint64_t *node_words,
                                                   uint32_t word_count);
void talos_rt_v1_scheduler_real_transition(talos_rt_context *context,
                                             uint64_t bit_offset,
                                             uint32_t bit_width,
                                             const void *old_value,
                                             const void *new_value);
void talos_rt_v1_scheduler_event(talos_rt_context *context,
                                   uint64_t stable_id, uint32_t nonblocking);
// Allocate one fresh named-event synchronization identity. Dynamic event IDs
// remain valid for the lifetime of the simulation context.
talos_rt_status
talos_rt_v1_scheduler_event_create(talos_rt_context *context,
                                     uint64_t *out_stable_id);
// Trigger immediately, or enqueue a nonblocking named-event occurrence after
// `delay` design-precision ticks. A nonzero delay with a blocking trigger is
// invalid and records a scheduler failure.
void talos_rt_v1_scheduler_event_after(talos_rt_context *context,
                                         uint64_t stable_id,
                                         uint32_t nonblocking, uint64_t delay);
// Schedule, replace, or cancel the one delayed nonblocking occurrence owned
// by a compiler-private timing event. `active == 0` cancels; otherwise the
// deadline uses the scheduler's saturating time arithmetic. This cold service
// leaves ordinary named-event scheduling and its ABI path unchanged.
void talos_rt_v1_scheduler_event_replace_after(talos_rt_context *context,
                                                 uint64_t stable_id,
                                                 uint32_t active,
                                                 uint64_t delay);
uint32_t talos_rt_v1_scheduler_event_triggered(talos_rt_context *context,
                                                 uint64_t stable_id);
// Register/query one statically bound clocking-output event. Registration is
// idempotent and installs a sparse pay-to-play signal subscription; `current`
// is nonzero only after the requested edge has occurred at the current
// numeric simulation time.
talos_rt_status
talos_rt_v1_clocking_output_track(talos_rt_context *context,
                                    uint64_t stable_id, uint64_t bit_width,
                                    uint32_t edge);
uint32_t talos_rt_v1_clocking_output_current(talos_rt_context *context,
                                               uint64_t stable_id,
                                               uint64_t bit_width,
                                               uint32_t edge);
uint32_t talos_rt_v1_scheduler_wait_order_failed(talos_rt_context *context);
// Consume one finalized clock-occurrence cohort for the active logical
// process and exact compiler site. Zero means no complete cohort is pending.
uint64_t talos_rt_v1_clock_occurrence_consume(talos_rt_context *context,
                                                uint64_t occurrence_site);
// Update one static IEEE 1800 `$nochange` pending-window state. A zero
// occurrence mask finalizes the slot and returns its accumulated reports.
uint64_t talos_rt_v1_nochange_update(talos_rt_context *context,
                                       uint64_t occurrence_site,
                                       uint64_t occurrence_mask,
                                       int64_t start_offset,
                                       int64_t end_offset);
void talos_rt_v1_scheduler_fail(talos_rt_context *context,
                                  talos_rt_status status);
// Register one compiler-assigned static-state object. Static handles retain
// these root bounds when views apply signed offsets, so partial out-of-range
// accesses cannot spill into an adjacent object in the shared bit plane.
talos_rt_status
talos_rt_v1_native_state_register_static(talos_rt_context *context,
                                           uint32_t id, uint64_t bit_offset,
                                           uint64_t bit_width);
// Seed the canonical design image from compiler-emitted native planes after
// static-state registration and before VPI startup/root-process creation. The
// planes remain bound for sparse Preponed capture and runtime-originated
// state publication while the context is alive.
talos_rt_status talos_rt_v1_native_state_sync(talos_rt_context *context,
                                                  uint8_t *value,
                                                  uint8_t *unknown,
                                                  uint64_t bit_count);
// Sorted, disjoint bit ranges. Each plane pattern is either zero or one.
typedef struct talos_rt_native_state_fill_v1 {
  uint64_t bit_offset;
  uint64_t bit_width;
  uint64_t value;
  uint64_t unknown;
} talos_rt_native_state_fill_v1;

// Initialize generated planes once per context, before binding or registration.
// Each allocation includes ceil(bit_count/8) bytes plus an eight-byte guard.
talos_rt_status talos_rt_v1_native_state_initialize(
    talos_rt_context *context, uint8_t *value, uint8_t *unknown,
    uint64_t bit_count, const talos_rt_native_state_fill_v1 *fills,
    uint64_t fill_count);

// One storage root summarized by a bit of the generated unknown summary.
// Whenever the root's summary bit is clear, bits [bit_offset, bit_offset +
// bit_width) of the generated unknown plane match the root's pattern: the
// `pattern_count` absolute (offset, width) ranges starting at pair
// `first_pattern` hold X/Z, and every other bit holds none.
typedef struct talos_rt_native_unknown_root_v1 {
  uint64_t bit_offset;
  uint64_t bit_width;
  uint32_t first_pattern;
  uint32_t pattern_count;
} talos_rt_native_unknown_root_v1;

// Bind the generated unknown summary after initialization: one bit per root,
// `(root_count + 63) / 64` words. Generated stores set a root's bit before
// any later test when they write X/Z outside its pattern or a known bit
// inside it, and every runtime writer of the generated unknown plane does the
// same, so a clear bit lets generated code use the pattern as the root's
// unknown plane. The runtime sets every bit here; refresh clears them.
// `patterns` holds (offset, width) pairs.
talos_rt_status talos_rt_v1_native_state_bind_unknown_summary(
    talos_rt_context *context, uint8_t *unknown, uint64_t bit_count,
    uint64_t *summary, const talos_rt_native_unknown_root_v1 *roots,
    uint64_t root_count, const uint64_t *patterns, uint64_t pattern_count);
// Rescan the dirty roots selected by `gate`, (word, mask) pairs of summary
// words, clearing the bits of roots whose unknown planes match their
// patterns. Returns nonzero when no selected bit remains set. `backoff`, when
// not null, is the gate's state in generated memory: the low 32 bits count
// failed tests generated code answers without calling, the high 32 bits the
// current delay. A failed refresh doubles the delay up to a bound and waits
// that many tests; a successful one clears both.
uint32_t talos_rt_v1_native_state_refresh_unknown_summary(
    talos_rt_context *context, const uint64_t *gate, uint64_t word_count,
    uint64_t *backoff);
// Set the summary bits of every root overlapping bits
// [bit_offset, bit_offset + bit_width) of the generated unknown plane, which
// a write may have made differ from their patterns.
void talos_rt_v1_native_state_mark_unknown(talos_rt_context *context,
                                             uint64_t bit_offset,
                                             uint64_t bit_width);

// Share generated storage before any static roots/processes are registered.
// Both planes must be 8-byte aligned, contain ceil(bit_count/64) limbs, and
// outlive the context. The compiler must exclude source-order observation
// snapshots (IEEE 1800-2023 4.6(a), 9.4.2). Active VPI observation detaches a
// canonical snapshot; unused capabilities may share. Compiler initializers
// remain authoritative.
talos_rt_status
talos_rt_v1_native_state_bind_shared(talos_rt_context *context,
                                       uint8_t *value, uint8_t *unknown,
                                       uint64_t bit_count);
// Bind the generated direct-access guard after state synchronization. Both
// schedulers retain ordinary publications and ordered updates; a native
// plan separately guards NBA accumulation. The flag outlives the context.
talos_rt_status
talos_rt_v1_native_state_bind_specialization(talos_rt_context *context,
                                               uint32_t *fast);
// Bind canonical retained continuous contributions after native state sync.
// The returned planes remain stable until context destruction. Generated
// clean stores update the exact written bits and set the corresponding mask.
talos_rt_status
talos_rt_v1_native_state_bind_continuous(talos_rt_context *context,
                                           uint8_t **value, uint8_t **unknown,
                                           uint8_t **mask);
// Whole static packed language force/assign services. `assign` selects
// procedural assign ownership; release with `assign` selects deassign.
talos_rt_status talos_rt_v1_native_override(
    talos_rt_context *context, uint8_t *global_value, uint8_t *global_unknown,
    uint64_t global_bit_count, uint64_t handle, uint64_t bit_width,
    uint32_t descriptor_kind, uint32_t assign, const uint8_t *value,
    const uint8_t *unknown);
talos_rt_status talos_rt_v1_native_dynamic_override(
    talos_rt_context *context, uint8_t *global_value, uint8_t *global_unknown,
    uint64_t global_bit_count, uint64_t handle, uint64_t bit_width,
    uint32_t descriptor_kind, uint32_t assign, uint64_t owner_process,
    uint32_t claim, const uint8_t *value, const uint8_t *unknown);
talos_rt_status talos_rt_v1_native_release_override(
    talos_rt_context *context, uint8_t *global_value, uint8_t *global_unknown,
    uint64_t global_bit_count, uint64_t handle, uint64_t bit_width,
    uint32_t descriptor_kind, uint32_t assign);
uint64_t talos_rt_v1_native_state_static_handle(uint32_t id);
uint64_t talos_rt_v1_native_handle_offset(uint64_t handle, int64_t offset);
// IEEE 1800-2017 Annex D.2 scalar-net driver query. Z contributions are
// excluded; paired conditional-primitive strength banks count as one driver.
talos_rt_status
talos_rt_v1_net_count_drivers(talos_rt_context *context,
                                uint64_t net_handle, uint32_t *out_forced,
                                uint32_t *out_total, uint32_t *out_zero,
                                uint32_t *out_one, uint32_t *out_unknown);
// Publish one normalized active-high four-state tranif/rtranif control.
talos_rt_status talos_rt_v1_pass_switch_control(talos_rt_context *context,
                                                    uint32_t pass_switch_id,
                                                    uint32_t value,
                                                    uint32_t unknown);
talos_rt_status talos_rt_v1_pass_switch_control_delayed(
    talos_rt_context *context, uint32_t pass_switch_id, uint32_t value,
    uint32_t unknown, uint64_t turn_on_delay, uint64_t turn_off_delay,
    uint64_t unknown_delay);
talos_rt_status
talos_rt_v1_mos_drive_delayed(talos_rt_context *context,
                                uint32_t pass_switch_id, uint32_t value,
                                uint32_t unknown, uint64_t rise_delay,
                                uint64_t fall_delay, uint64_t turnoff_delay);
talos_rt_status talos_rt_v1_native_state_alloc(talos_rt_context *context,
                                                   uint64_t bit_width,
                                                   const uint8_t *value,
                                                   const uint8_t *unknown,
                                                   uint64_t *out_handle);
// Allocate automatic state and publish all embedded managed roots as one
// ownership transaction. On failure no automatic state remains registered and
// out_handle is reset to UINT64_MAX.
talos_rt_status talos_rt_v1_native_state_alloc_with_roots(
    talos_rt_context *context, uint64_t bit_width, const uint8_t *value,
    const uint8_t *unknown, const uint64_t *bit_offsets, uint64_t count,
    uint64_t *out_handle);
talos_rt_status talos_rt_v1_native_state_alloc_with_typed_roots(
    talos_rt_context *context, uint64_t bit_width, const uint8_t *value,
    const uint8_t *unknown, const talos_rt_managed_root_slot_v1 *slots,
    uint64_t count, uint64_t *out_handle);
talos_rt_status talos_rt_v1_native_state_retain(talos_rt_context *context,
                                                    uint64_t handle);
// Attach precise managed-word offsets to one automatic state allocation.
// Offsets are in bits from the value plane and must be distinct 64-bit words;
// tagged inline strings remain allocation-free roots.
talos_rt_status talos_rt_v1_native_state_register_managed_roots(
    talos_rt_context *context, uint64_t handle, const uint64_t *bit_offsets,
    uint64_t count);
talos_rt_status
talos_rt_v1_native_state_release(talos_rt_context *context, uint64_t handle,
                                   uint32_t owner_reference);
talos_rt_status talos_rt_v1_native_state_load_plane(
    talos_rt_context *context, const uint8_t *global_plane,
    uint64_t global_bit_count, uint64_t handle, uint64_t bit_width,
    uint32_t unknown_plane, uint32_t fallback, uint8_t *out_value);
talos_rt_status talos_rt_v1_native_state_store_plane(
    talos_rt_context *context, uint8_t *global_plane,
    uint64_t global_bit_count, uint64_t handle, uint64_t bit_width,
    uint32_t unknown_plane, const uint8_t *value, uint8_t *out_changed);
talos_rt_status talos_rt_v1_native_state_store_continuous_plane(
    talos_rt_context *context, uint8_t *global_plane,
    uint64_t global_bit_count, uint64_t handle, uint64_t bit_width,
    uint32_t unknown_plane, const uint8_t *value, uint8_t *out_changed);
void talos_rt_v1_scheduler_notify(talos_rt_context *context);
// Request orderly design-wide termination. The scheduler stops selecting
// ordinary processes and pending updates, then runs final processes. A request
// made from a final process stops the remaining final processes immediately.
// `verbosity` is retained for SystemVerilog compatibility; diagnostic text is
// implementation-defined and this runtime currently emits none.
talos_rt_status talos_rt_v1_scheduler_finish(talos_rt_context *context,
                                                 uint32_t verbosity);
// IEEE 1800-2017 24.7: terminate every logical process belonging to the
// current program instance. Calling from a design-owned process is invalid.
talos_rt_status
talos_rt_v1_scheduler_program_exit(talos_rt_context *context);
// Compiler-facing ownership hook for a newly scheduled program initial.
// Descendant spawns inherit the current owner without another generated call.
talos_rt_status
talos_rt_v1_scheduler_program_register(talos_rt_context *context,
                                         uint64_t logical_process,
                                         uint64_t program_owner);
// Batch-mode implementation of SystemVerilog's interactive suspension task.
// With no debugger to resume the design, this requests the same orderly,
// successful final-process phase as finish while retaining the distinct ABI
// entry point and bytecode intrinsic.
talos_rt_status talos_rt_v1_scheduler_stop(talos_rt_context *context,
                                               uint32_t verbosity);
talos_rt_status talos_rt_v1_scheduler_fatal(talos_rt_context *context,
                                                uint32_t verbosity);
// Record a run-time error without requesting simulation termination. The
// scheduler continues normally and reports an unsuccessful final status.
talos_rt_status talos_rt_v1_scheduler_error(talos_rt_context *context);
// Read the termination latch only. This query is not a synchronization barrier
// for other context state; the caller must keep the context alive.
uint32_t
talos_rt_v1_scheduler_termination_requested(talos_rt_context *context);
// IEEE 1800-2023 24.7: whether the executing thread originates in an initial
// procedure of a program block, so that `$exit` terminates that program.
uint32_t talos_rt_v1_scheduler_program_owned(talos_rt_context *context);
uint64_t talos_rt_v1_scheduler_time(talos_rt_context *context);

// Read one packed value from the once-per-time-slot Preponed snapshot. The
// stable handle and width are compiler-resolved; automatic storage is rejected
// because its lifetime is not part of the canonical design plane.
talos_rt_status talos_rt_v1_sampled_read(talos_rt_context *context,
                                             uint64_t stable_id,
                                             uint64_t bit_width,
                                             uint8_t *out_value,
                                             uint8_t *out_unknown);

// Return `depth` enabled invocations before `current`, then advance the
// compiler-assigned ring when gate is true. Missing history has the IEEE
// default sampled value (X for four-state, zero for two-state).
talos_rt_status talos_rt_v1_sampled_history(
    talos_rt_context *context, uint64_t site_id, uint64_t bit_width,
    uint64_t depth, uint32_t four_state, uint32_t gate,
    const uint8_t *current_value, const uint8_t *current_unknown,
    uint8_t *out_value, uint8_t *out_unknown);

// Advance and read compiler-planned history shared by all call sites using an
// explicit alternate clock. `depth` is the oldest retained age, so the ring
// stores depth + 1 samples and age zero is the most recent enabled clock tick.
talos_rt_status talos_rt_v1_clocked_sample_update(
    talos_rt_context *context, uint64_t site_id, uint64_t bit_width,
    uint64_t depth, uint32_t four_state, uint32_t gate,
    const uint8_t *current_value, const uint8_t *current_unknown);
talos_rt_status
talos_rt_v1_clocked_sample_read(talos_rt_context *context, uint64_t site_id,
                                  uint64_t bit_width, uint64_t depth,
                                  uint64_t age, uint32_t four_state,
                                  uint8_t *out_value, uint8_t *out_unknown);
talos_rt_status talos_rt_v1_scheduler_run(talos_rt_context *context);
talos_rt_status talos_rt_v1_scheduler_run_aot(talos_rt_context *context);

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
talos_rt_status talos_rt_v1_dump_open(talos_rt_context *context,
                                          const uint8_t *path,
                                          uint64_t path_size);
talos_rt_status talos_rt_v1_dump_open_string(talos_rt_context *context,
                                                 talos_rt_string_v1 path);
// Declare the `$timescale` written into the header, as a decimal exponent in
// seconds (-15..2). The compiler knows the elaborated design precision and
// emits this before the first dump call. Without it the dump falls back to the
// DPI time precision, and then to the compiler's own default of 1ns.
talos_rt_status talos_rt_v1_dump_timescale(talos_rt_context *context,
                                               int32_t exponent);
// `levels` is the IEEE `$dumpvars` depth: zero selects every level below the
// named scope. A null or empty scope selects the design root. Repeated calls
// accumulate; the plan is materialized once, at the end of the time slot in
// which the first selection was made.
talos_rt_status talos_rt_v1_dump_vars(talos_rt_context *context,
                                          uint64_t levels, const uint8_t *scope,
                                          uint64_t scope_size);
// Emit every selected variable at the current time regardless of change.
talos_rt_status talos_rt_v1_dump_all(talos_rt_context *context);
// `$dumpoff` / `$dumpon`. Suspension emits one all-X checkpoint; resumption
// re-emits every current value.
talos_rt_status talos_rt_v1_dump_control(talos_rt_context *context,
                                             uint32_t enabled);
// `$dumplimit`. Zero is a zero-byte maximum. Once the written size reaches the
// limit, a limit comment is appended and the file is closed.
talos_rt_status talos_rt_v1_dump_limit(talos_rt_context *context,
                                           uint64_t bytes);
talos_rt_status talos_rt_v1_dump_flush(talos_rt_context *context);
talos_rt_status talos_rt_v1_dump_close(talos_rt_context *context);

// Extended VCD sessions are independent of the ordinary VCD session and are
// keyed by filename. Repeated selections for one file accumulate module
// scopes. An empty control path targets all sessions; an unknown nonempty path
// is ignored. Actions are 0=off, 1=on, 2=checkpoint, 3=flush, and 4=limit.
talos_rt_status talos_rt_v1_dump_ports(talos_rt_context *context,
                                           talos_rt_string_v1 path,
                                           talos_rt_string_v1 scope,
                                           int32_t timescale_exponent);
talos_rt_status talos_rt_v1_dump_ports_control(talos_rt_context *context,
                                                   talos_rt_string_v1 path,
                                                   uint32_t action,
                                                   uint64_t value);

typedef uint32_t talos_rt_fragment_code_kind;
enum { TALOS_RT_FRAGMENT_NATIVE = 0, TALOS_RT_FRAGMENT_BYTECODE = 1 };

typedef struct talos_rt_fragment_descriptor_v1 {
  talos_rt_handle_v1 handle;
  talos_rt_fragment_code_kind code_kind;
  uint32_t flags;
  union {
    talos_rt_native_fragment_v1 native_entry;
    talos_rt_bytecode_v1 bytecode;
  } code;
} talos_rt_fragment_descriptor_v1;

// Dispatch one immutable fragment descriptor. Native and bytecode fragments
// have identical context/frame/continuation inputs and action results.
talos_rt_status talos_rt_v1_fragment_execute(
    const talos_rt_fragment_descriptor_v1 *descriptor,
    talos_rt_context *context, void *frame, uint64_t frame_size,
    uint32_t continuation, talos_rt_fragment_action_v1 *out_action);

// Test and differential-execution entry point for bytecode fragments. A
// nonzero instruction_limit bounds this invocation and reports
// TALOS_RT_STEP_LIMIT when exhausted.
talos_rt_status talos_rt_v1_bytecode_execute_bounded(
    const talos_rt_fragment_descriptor_v1 *descriptor,
    talos_rt_context *context, void *frame, uint64_t frame_size,
    uint32_t continuation, uint64_t instruction_limit,
    talos_rt_fragment_action_v1 *out_action);

typedef uint32_t talos_rt_arg_kind;
enum {
  TALOS_RT_ARG_EMPTY = 0,
  TALOS_RT_ARG_LOGIC = 1,
  TALOS_RT_ARG_STRING = 2,
  TALOS_RT_ARG_REAL = 3,
  TALOS_RT_ARG_TIME = 4,
  // data points to one talos_rt_string_v1 word; size must be zero.
  TALOS_RT_ARG_MANAGED_STRING = 5,
  // data points to one talos_rt_managed_word_v1 naming a sequential
  // container; size must be zero.
  TALOS_RT_ARG_MANAGED_CONTAINER = 6,
  // data points to one talos_rt_managed_word_v1 naming a class object; size
  // must be zero. This representation is accepted by %p.
  TALOS_RT_ARG_MANAGED_OBJECT = 7,
  // data points to one uint64_t stable scope identity; size must be zero.
  // Zero is the null virtual-interface handle. This representation is
  // accepted by %p.
  TALOS_RT_ARG_VIRTUAL_INTERFACE = 8,
  // data points to one uint64_t stable process identity; size must be zero.
  // Zero is the null process handle. This representation is accepted by %p.
  TALOS_RT_ARG_PROCESS = 9,
  // data points to an talos_rt_enum_arg_v1; size and unknown must be zero.
  TALOS_RT_ARG_ENUM = 10,
  // data points to an talos_rt_net_arg_v1; size and unknown must be zero.
  // Its packed snapshot serves every ordinary conversion, while %v queries
  // the named resolved net bit for its exact strength range.
  TALOS_RT_ARG_NET = 11,
  // data points to an talos_rt_raw_aggregate_arg_v1; size, flags, and
  // unknown must be zero. The runtime selects its pattern, %u, or %z string.
  TALOS_RT_ARG_RAW_AGGREGATE = 12
};

typedef uint32_t talos_rt_arg_flags;
enum {
  TALOS_RT_ARG_SIGNED = 1u << 0,
  TALOS_RT_ARG_FORMAT_STRING = 1u << 1,
  // Marks the sole format expression of $sformat/$sformatf. It owns format
  // substitutions in the following arguments; values left after those
  // substitutions continue as ordinary output-list items.
  TALOS_RT_ARG_DESIGNATED_FORMAT = 1u << 2,
  // A direct $realtime result uses scope time precision for its default
  // unformatted rendering. Explicit format conversions remain unchanged.
  TALOS_RT_ARG_REAL_TIME = 1u << 3
};

// Runtime-only pairing of an enum's executable packed representation with
// the compiler-selected mnemonic. Keeping the pair in one logical format
// argument lets dynamic format strings choose %p or a numeric conversion
// without a runtime enum-table lookup.
typedef struct talos_rt_enum_arg_v1 {
  uint64_t width;
  uint32_t flags;
  uint32_t reserved;
  const uint64_t *value;
  const uint64_t *unknown;
  talos_rt_string_v1 name;
} talos_rt_enum_arg_v1;

// Runtime-only pairing of a direct net read with its stable net handle. This
// deliberately has the same ABI layout as talos_rt_enum_arg_v1 so native
// lowering can share the compact stack descriptor materializer.
typedef struct talos_rt_net_arg_v1 {
  uint64_t width;
  uint32_t flags;
  // Nonzero when the value and handle originate in native execution. This
  // selects the compiler-emitted state planes when no AOT schedule plan owns
  // them; bytecode descriptors leave this zero and use canonical state.
  uint32_t native_state;
  const uint64_t *value;
  const uint64_t *unknown;
  uint64_t handle;
} talos_rt_net_arg_v1;

// Compiler-built renderings of a recursively integral unpacked struct or
// untagged union. Each raw string concatenates one independently 32-bit-padded
// record per scalar leaf in declaration order; a union contributes its first
// member only.
typedef struct talos_rt_raw_aggregate_arg_v1 {
  talos_rt_string_v1 pattern;
  talos_rt_string_v1 two_state;
  talos_rt_string_v1 four_state;
} talos_rt_raw_aggregate_arg_v1;

// LOGIC: size is the bit width, data points to little-endian uint64_t value
// words, and unknown points to matching unknown words (or is null for known
// data). STRING: size is the byte count and data points to those bytes. REAL
// and TIME point to one double or uint64_t respectively and ignore unknown.
typedef struct talos_rt_arg_v1 {
  talos_rt_arg_kind kind;
  talos_rt_arg_flags flags;
  uint64_t size;
  const void *data;
  const uint64_t *unknown;
} talos_rt_arg_v1;

typedef uint32_t talos_rt_radix;
enum {
  TALOS_RT_RADIX_BINARY = 2,
  TALOS_RT_RADIX_OCTAL = 8,
  TALOS_RT_RADIX_DECIMAL = 10,
  TALOS_RT_RADIX_HEX = 16
};

typedef struct talos_rt_format_env_v1 {
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
} talos_rt_format_env_v1;

// $dist_* distribution selector. UNIFORM takes (start, end), NORMAL takes
// (mean, standard deviation), ERLANG takes (k, mean), and the rest take a
// single parameter: a mean for EXPONENTIAL and POISSON, degrees of freedom
// for CHI_SQUARE and T.
typedef uint32_t talos_rt_distribution;
enum {
  TALOS_RT_DISTRIBUTION_UNIFORM = 0,
  TALOS_RT_DISTRIBUTION_NORMAL = 1,
  TALOS_RT_DISTRIBUTION_EXPONENTIAL = 2,
  TALOS_RT_DISTRIBUTION_POISSON = 3,
  TALOS_RT_DISTRIBUTION_CHI_SQUARE = 4,
  TALOS_RT_DISTRIBUTION_T = 5,
  TALOS_RT_DISTRIBUTION_ERLANG = 6
};

typedef uint32_t talos_rt_seek_origin;
enum {
  TALOS_RT_SEEK_SET = 0,
  TALOS_RT_SEEK_CUR = 1,
  TALOS_RT_SEEK_END = 2
};

// PCG-XSH-RR stream state. The increment is always odd for runtime-created
// streams. Both words are explicit so generated code can snapshot and step an
// object-local stream without a runtime call.
struct talos_rt_random_state_v1 {
  uint64_t state;
  uint64_t increment;
};

// Shared PCG-XSH-RR parameters used by process and object-local streams.
#define TALOS_RT_RANDOM_MULTIPLIER UINT64_C(6364136223846793005)
#define TALOS_RT_RANDOM_DEFAULT_INCREMENT UINT64_C(1442695040888963407)

// Signed coordinates in half-minimum-subnormal units. These representations
// retain every finite value and signed zero without host big-integer types.
#define TALOS_RT_REAL64_RANK_BITS UINT32_C(2100)
#define TALOS_RT_REAL64_RANK_WORDS UINT32_C(33)
#define TALOS_RT_REAL32_RANK_BITS UINT32_C(279)
#define TALOS_RT_REAL32_RANK_WORDS UINT32_C(5)
#define TALOS_RT_REAL_LOWER_OPEN UINT32_C(1)
#define TALOS_RT_REAL_UPPER_OPEN UINT32_C(2)

// Versioned stack program executed by the constrained-random fallback. The
// byte representation is explicitly little-endian and does not use these C
// types as an in-memory wire format. Version 1 has a 24-byte header followed by
// instruction_count fixed-width 16-byte instructions. Version 2 has a 32-byte
// header, also uses 16-byte instructions, and follows them with a little-endian
// u64 literal-word pool. Its header adds literal_word_count and a zero reserved
// u32. Its instructions contain opcode:u8, flags:u8, reserved:u16, width:u32,
// operand:u32, and auxiliary:u32. PUSH_LITERAL auxiliary is a word-pool index;
// END_SOFT auxiliary is its priority; END_DIST_WEIGHT_BIT auxiliary is its
// exported bit index; every other auxiliary is zero. This allows
// arbitrary-width bit-vector expressions without changing the compact
// version-1 runtime fast path. END_HARD and END_SOFT use
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
#define TALOS_RT_RANDOM_PROGRAM_MAGIC UINT32_C(0x3152444f)
#define TALOS_RT_RANDOM_PROGRAM_VERSION_V1 UINT16_C(1)
#define TALOS_RT_RANDOM_PROGRAM_VERSION_V2 UINT16_C(2)
#define TALOS_RT_RANDOM_PROGRAM_VERSION TALOS_RT_RANDOM_PROGRAM_VERSION_V1
#define TALOS_RT_RANDOM_PROGRAM_HEADER_SIZE UINT16_C(24)
#define TALOS_RT_RANDOM_INSTRUCTION_SIZE UINT16_C(16)
#define TALOS_RT_RANDOM_PROGRAM_HEADER_SIZE_V2 UINT16_C(32)
#define TALOS_RT_RANDOM_INSTRUCTION_SIZE_V2 UINT16_C(16)
#define TALOS_RT_RANDOM_PROGRAM_HAS_SOFT UINT32_C(1)
#define TALOS_RT_RANDOM_PROGRAM_HAS_SOLVE_BEFORE UINT32_C(2)
#define TALOS_RT_RANDOM_PROGRAM_HAS_DIST UINT32_C(4)
#define TALOS_RT_RANDOM_PROGRAM_HAS_DOMAINS UINT32_C(8)
// Sample integral residuals uniformly over legal assignments or each enabled
// solve-order projection, conditional on the accepted preceding layers.
// Requires a stateful entry point and a compiler-generated sampling plan; a
// nonzero max_attempts enables sampling without bounding the exact finite
// count. Programs without an exact flag use the bounded traversal. Weighted
// programs use EXACT_WEIGHTED instead.
#define TALOS_RT_RANDOM_PROGRAM_EXACT_UNIFORM UINT32_C(32)
// Function arguments subdivide the constraints into priority stages. Higher
// stages ignore lower-stage constraints and become state for later stages.
// Requires EXACT_UNIFORM; failed later stages commit no assignment or RNG
// state.
#define TALOS_RT_RANDOM_PROGRAM_HAS_FUNCTION_PRIORITY UINT32_C(64)
// Exact weighted target projections with arbitrary-precision unsigned masses.
// Requires a compiled sampling plan and stateful sampling. Dist records use
// RAW_WEIGHT; their
// coefficient field is an item identity and the reserved u16 is zero for hard
// distributions or one plus the owning soft priority. Split intervals sharing
// an item identity form one range item, including the default complement.
// Requires HAS_DIST; independent function-priority stages are not supported.
// The RPN hard/soft predicates must include each distribution's membership
// and nonzero-weight support. END_DIST_GUARD supplies optional activation;
// its operand is a group index, and omitting it means unconditional activation.
#define TALOS_RT_RANDOM_PROGRAM_EXACT_WEIGHTED UINT32_C(128)
// Priority source records may mask their references and arguments by a live
// capture. Guards removed by 18.5.12 contribute no solve-stage dependency.
// Requires HAS_FUNCTION_PRIORITY; a nonzero reserved record word is one plus
// the first of two consecutive captures (references, arguments). Each row is
// masked independently, including overlapping body and guard references.
// Zero retains the static source masks.
#define TALOS_RT_RANDOM_PROGRAM_HAS_GUARD_INPUT_MASKS UINT32_C(256)
// A compiler-generated Boolean sampling plan follows the residual metadata.
// Arithmetic is bit-blasted by compiler Z3; the target needs no Z3 dependency.
// A versioned trailer bounds the original residual and the plan independently.
#define TALOS_RT_RANDOM_PROGRAM_HAS_SAMPLER_PLAN UINT32_C(512)
// Typed IEEE operations require an exact compiled plan and stateful uniform
// sampling. They are never evaluated by the legacy integral interpreter.
#define TALOS_RT_RANDOM_PROGRAM_HAS_REAL UINT32_C(1024)
#define TALOS_RT_RANDOM_SOLVE_EDGE_HEADER_SIZE UINT16_C(4)
#define TALOS_RT_RANDOM_SOLVE_EDGE_SIZE UINT16_C(24)
// V2 edges contain ceil(aggregate_width/64) little-endian words for each
// before/after mask, followed by a 32-bit constraint block and edge flag word.
// Their record size is 16 * assignment_word_count + this fixed tail.
#define TALOS_RT_RANDOM_SOLVE_EDGE_TAIL_SIZE_V2 UINT16_C(8)
// In priority programs the final edge word carries these flags instead of
// being reserved. Explicit ordering edges use zero; argument edges use one.
#define TALOS_RT_RANDOM_SOLVE_EDGE_FUNCTION_PRIORITY UINT32_C(1)
// Priority programs follow their solve edges with a count and optional source
// input records: end-instruction index, reserved word, and two assignment
// mask rows for referenced variables and function argument variables. Count
// zero infers dependencies from the Boolean program; otherwise
// every hard/soft end has exactly one record, preserving source references
// that algebraic simplification could remove.
#define TALOS_RT_RANDOM_PRIORITY_INPUT_HEADER_SIZE UINT16_C(4)
#define TALOS_RT_RANDOM_PRIORITY_INPUT_RECORD_HEADER_SIZE UINT16_C(8)
#define TALOS_RT_RANDOM_DIST_HEADER_SIZE UINT16_C(8)
#define TALOS_RT_RANDOM_DIST_RECORD_SIZE UINT16_C(48)
#define TALOS_RT_RANDOM_DIST_WEIGHT_SIGNED UINT32_C(1)
#define TALOS_RT_RANDOM_DIST_TARGET_SIGNED UINT32_C(2)
#define TALOS_RT_RANDOM_DIST_RAW_WEIGHT UINT32_C(4)
#define TALOS_RT_RANDOM_DIST_PER_RANGE UINT32_C(8)
// For captured intervals, the lower/cardinality fields instead identify two
// 64-bit captures containing inclusive, sign-biased lower/upper bounds. An
// upper bound below the lower bound denotes an empty intersection.
#define TALOS_RT_RANDOM_DIST_CAPTURED_INTERVAL UINT32_C(16)
// One per-range item represents the complement of all explicit intervals.
// Its wire interval is the complete target domain, resolved at each call.
#define TALOS_RT_RANDOM_DIST_DEFAULT_COMPLEMENT UINT32_C(32)
// The weight capture names a compiler-generated expression rather than live
// capture data. Consecutive END_DIST_WEIGHT_BIT instructions export its bits.
#define TALOS_RT_RANDOM_DIST_EXPRESSION_WEIGHT UINT32_C(64)
// The distributed value is an expression rather than an assignment field
// (IEEE 1800-2023 18.5.3). Its bits are exported by END_DIST_TARGET_BIT, the
// record's target offset is zero, and its width is the expression width.
#define TALOS_RT_RANDOM_DIST_EXPRESSION_TARGET UINT32_C(128)
#define TALOS_RT_RANDOM_DOMAIN_HEADER_SIZE UINT16_C(8)
#define TALOS_RT_RANDOM_DOMAIN_RECORD_SIZE UINT16_C(32)
// V2 domain record: width is u32; mask/value are u64 literal-pool word indices.
// A zero record flag retains the inline u16 width and u64 mask/value format.
#define TALOS_RT_RANDOM_DOMAIN_LITERAL_WORDS UINT32_C(1)
#define TALOS_RT_RANDOM_INSTRUCTION_SIGNED UINT8_C(1)
#define TALOS_RT_RANDOM_UNMASKED_CONSTRAINT_V1 UINT32_MAX

typedef enum talos_rt_random_opcode_v1 {
  TALOS_RT_RANDOM_PUSH_VARIABLE_V1 = 1,
  TALOS_RT_RANDOM_PUSH_CAPTURE_V1 = 2,
  TALOS_RT_RANDOM_CAST_V1 = 3,
  TALOS_RT_RANDOM_POS_V1 = 4,
  TALOS_RT_RANDOM_NEG_V1 = 5,
  TALOS_RT_RANDOM_BIT_NOT_V1 = 6,
  TALOS_RT_RANDOM_REDUCE_AND_V1 = 7,
  TALOS_RT_RANDOM_REDUCE_OR_V1 = 8,
  TALOS_RT_RANDOM_REDUCE_XOR_V1 = 9,
  TALOS_RT_RANDOM_REDUCE_NAND_V1 = 10,
  TALOS_RT_RANDOM_REDUCE_NOR_V1 = 11,
  TALOS_RT_RANDOM_REDUCE_XNOR_V1 = 12,
  TALOS_RT_RANDOM_LOGICAL_NOT_V1 = 13,
  TALOS_RT_RANDOM_ADD_V1 = 14,
  TALOS_RT_RANDOM_SUB_V1 = 15,
  TALOS_RT_RANDOM_MUL_V1 = 16,
  TALOS_RT_RANDOM_BIT_AND_V1 = 17,
  TALOS_RT_RANDOM_BIT_OR_V1 = 18,
  TALOS_RT_RANDOM_BIT_XOR_V1 = 19,
  TALOS_RT_RANDOM_BIT_XNOR_V1 = 20,
  TALOS_RT_RANDOM_EQ_V1 = 21,
  TALOS_RT_RANDOM_NE_V1 = 22,
  TALOS_RT_RANDOM_GE_V1 = 23,
  TALOS_RT_RANDOM_GT_V1 = 24,
  TALOS_RT_RANDOM_LE_V1 = 25,
  TALOS_RT_RANDOM_LT_V1 = 26,
  TALOS_RT_RANDOM_LOGICAL_AND_V1 = 27,
  TALOS_RT_RANDOM_LOGICAL_OR_V1 = 28,
  TALOS_RT_RANDOM_LOGICAL_IMPLIES_V1 = 29,
  TALOS_RT_RANDOM_LOGICAL_EQUIV_V1 = 30,
  TALOS_RT_RANDOM_SELECT_V1 = 31,
  TALOS_RT_RANDOM_END_HARD_V1 = 32,
  TALOS_RT_RANDOM_END_SOFT_V1 = 33,
  TALOS_RT_RANDOM_PUSH_LITERAL_V1 = 34,
  TALOS_RT_RANDOM_DIV_V1 = 35,
  TALOS_RT_RANDOM_MOD_V1 = 36,
  TALOS_RT_RANDOM_SHIFT_LEFT_V1 = 37,
  TALOS_RT_RANDOM_SHIFT_RIGHT_V1 = 38,
  TALOS_RT_RANDOM_SHIFT_RIGHT_ARITH_V1 = 39,
  TALOS_RT_RANDOM_POWER_V1 = 40,
  // Push a Boolean testing the live constraint-mode mask. Compiler-side
  // planning assumes every block is enabled; generated proposals are used
  // only on that all-enabled edge. Used by disable soft across blocks.
  TALOS_RT_RANDOM_PUSH_CONSTRAINT_ENABLED_V1 = 41,
  // Unary two-state bitstream functions. Their unsigned input retains its
  // own width; the count/result is truncated to the instruction's width.
  TALOS_RT_RANDOM_COUNT_ONES_V1 = 42,
  TALOS_RT_RANDOM_CLOG2_V1 = 43,
  // Pops a distribution's activation guard. Operand is the group index;
  // flags and auxiliary are zero. Valid only under EXACT_WEIGHTED.
  TALOS_RT_RANDOM_END_DIST_GUARD_V1 = 44,
  // Keep the stack top in the next temporary slot without popping it, or
  // push a previously stored value. Slots are immutable and are recomputed
  // for each assignment, including across hard/soft constraint boundaries.
  // Operand is the slot index; flags and auxiliary/immediate are zero.
  // Width must equal the stored value's width (these operations do not cast).
  TALOS_RT_RANDOM_STORE_VALUE_V1 = 45,
  TALOS_RT_RANDOM_LOAD_VALUE_V1 = 46,
  // Mark equal-width integer payloads as IEEE binary32/binary64 values.
  TALOS_RT_RANDOM_REAL32_BITS_V1 = 47,
  TALOS_RT_RANDOM_REAL64_BITS_V1 = 48,
  // Decode signed half-minimum-subnormal coordinates. Clients constrain
  // property domains with REAL_RANK_VALID independently of user constraints.
  TALOS_RT_RANDOM_REAL32_FROM_RANK_V1 = 49,
  TALOS_RT_RANDOM_REAL64_FROM_RANK_V1 = 50,
  // Convert an integral or IEEE operand to the declared IEEE precision.
  // SIGNED describes an integral input. Floating conversion uses IEEE RNE.
  TALOS_RT_RANDOM_REAL_CAST_V1 = 51,
  TALOS_RT_RANDOM_REAL_RANK_VALID_V1 = 52,
  // Recover a bitcast payload. Computed NaN payload semantics remain open.
  TALOS_RT_RANDOM_REAL_TO_BITS_V1 = 53,
  // IEEE 1800 integral casts round halfway away from zero; $rtoi truncates.
  // Both return destination-width low bits, with nonfinite two-state zero.
  TALOS_RT_RANDOM_REAL_TO_INTEGER_V1 = 54,
  TALOS_RT_RANDOM_REAL_TRUNCATE_INTEGER_V1 = 55,
  // One-bit finite predicate for the conversion's four-state unknown plane.
  TALOS_RT_RANDOM_REAL_IS_FINITE_V1 = 56,
  // Export one bit of the stack top to the compiled sampling plan. Operand
  // identifies the weight capture; auxiliary is the ascending bit index.
  // Width is the complete expression width. Only the final bit pops the value.
  // Valid only under EXACT_WEIGHTED; flags are zero.
  TALOS_RT_RANDOM_END_DIST_WEIGHT_BIT_V1 = 57,
  // Export one bit of a distributed expression to the compiled sampling plan.
  // Operand is the distribution group; auxiliary is the ascending bit index.
  // Width is the group's target width. Only the final bit pops the value.
  // Valid only under EXACT_WEIGHTED; flags are zero.
  TALOS_RT_RANDOM_END_DIST_TARGET_BIT_V1 = 58
} talos_rt_random_opcode_v1;

// Context and error handling. Operations on one live context may be called
// concurrently; last_error is isolated per calling thread. The caller must
// keep the context alive until all operations finish and must not race destroy
// with another runtime call.
talos_rt_status
talos_rt_v1_context_create(talos_rt_context **out_context);
talos_rt_status
talos_rt_v1_context_configure_argv(talos_rt_context *context, int argc,
                                     const char *const *argv);
typedef uint32_t talos_rt_coverage_metric_v1;
enum {
  TALOS_RT_COVERAGE_STATEMENT = 1,
  TALOS_RT_COVERAGE_TOGGLE = 2,
  TALOS_RT_COVERAGE_FUNCTIONAL = 3,
  TALOS_RT_COVERAGE_FSM = 4,
  TALOS_RT_COVERAGE_ASSERTION = 5
};
typedef uint32_t talos_rt_coverage_persistence_v1;
enum {
  TALOS_RT_COVERAGE_PERSIST_LINE = 1u << 0,
  TALOS_RT_COVERAGE_PERSIST_TOGGLE = 1u << 1,
  TALOS_RT_COVERAGE_PERSIST_FUNCTIONAL = 1u << 2,
  TALOS_RT_COVERAGE_PERSIST_ASSERTION = 1u << 3,
  TALOS_RT_COVERAGE_PERSIST_FSM = 1u << 4,
  TALOS_RT_COVERAGE_PERSIST_ALL =
      TALOS_RT_COVERAGE_PERSIST_LINE | TALOS_RT_COVERAGE_PERSIST_TOGGLE |
      TALOS_RT_COVERAGE_PERSIST_FUNCTIONAL |
      TALOS_RT_COVERAGE_PERSIST_ASSERTION | TALOS_RT_COVERAGE_PERSIST_FSM
};
talos_rt_status talos_rt_v1_coverage_finalize(
    talos_rt_context *context, uint64_t line_count, uint64_t toggle_bit_count,
    const uint8_t *initial_value, const uint8_t *initial_unknown,
    uint32_t persistence_mask);
talos_rt_status talos_rt_v1_coverage_point_hit(talos_rt_context *context,
                                                   uint64_t point,
                                                   uint32_t enabled);
talos_rt_status talos_rt_v1_coverage_toggle_transition(
    talos_rt_context *context, uint64_t first_bit, uint64_t bit_count,
    const uint8_t *value, const uint8_t *unknown);
talos_rt_status
talos_rt_v1_coverage_toggle_bind(talos_rt_context *context,
                                   uint64_t first_bit, uint64_t bit_count,
                                   uint64_t state_handle);
talos_rt_status
talos_rt_v1_coverage_toggle_seal(talos_rt_context *context);
// Bind `bit_count` bits of static state, starting at `state_handle`, to bits
// [shift, shift + bit_count) of the state vector of the FSM whose schema
// states carry `machine`. Every binding precedes the toggle seal, which also
// records each FSM's initial state.
talos_rt_status talos_rt_v1_coverage_fsm_bind(talos_rt_context *context,
                                                  uint64_t machine,
                                                  uint64_t shift,
                                                  uint64_t bit_count,
                                                  uint64_t state_handle);
talos_rt_status talos_rt_v1_coverage_reset(
    talos_rt_context *context, talos_rt_coverage_metric_v1 metric,
    const uint8_t *current_value, const uint8_t *current_unknown);
talos_rt_status talos_rt_v1_coverage_query(
    talos_rt_context *context, talos_rt_coverage_metric_v1 metric,
    uint64_t *out_covered, uint64_t *out_total, double *out_percentage);
// Clause 40 accessors take the IEEE SV_COV_* integer values verbatim. Their
// return value reports ABI/operational failure; the out parameter receives the
// language-defined result, including SV_COV_ERROR, NOCOV, and OVERFLOW.
// Definition selectors are runtime strings. Instance selectors are stable
// schema scope identities, never physical table indices.
talos_rt_status talos_rt_v1_coverage_control_definition(
    talos_rt_context *context, int32_t control, int32_t coverage_type,
    int32_t scope_definition, talos_rt_string_v1 definition,
    int32_t *out_status);
talos_rt_status talos_rt_v1_coverage_control_instance(
    talos_rt_context *context, int32_t control, int32_t coverage_type,
    int32_t scope_definition, uint64_t coverage_scope_id, int32_t *out_status);
talos_rt_status talos_rt_v1_coverage_query_definition(
    talos_rt_context *context, int32_t coverage_type,
    int32_t scope_definition, talos_rt_string_v1 definition, uint32_t maximum,
    int32_t *out_value);
talos_rt_status talos_rt_v1_coverage_query_instance(
    talos_rt_context *context, int32_t coverage_type,
    int32_t scope_definition, uint64_t coverage_scope_id, uint32_t maximum,
    int32_t *out_value);
// Clause 40 metric-specific persistence. The ABI status reports operational
// failures; out_status receives the language-defined SV_COV_* result.
talos_rt_status talos_rt_v1_coverage_database_save(
    talos_rt_context *context, int32_t coverage_type,
    talos_rt_string_v1 name, int32_t *out_status);
talos_rt_status talos_rt_v1_coverage_database_merge(
    talos_rt_context *context, int32_t coverage_type,
    talos_rt_string_v1 name, int32_t *out_status);
talos_rt_status talos_rt_v1_coverage_load(talos_rt_context *context,
                                              const char *path,
                                              uint64_t path_size);
talos_rt_status talos_rt_v1_coverage_save(talos_rt_context *context,
                                              const char *path,
                                              uint64_t path_size,
                                              uint32_t run_status,
                                              uint64_t simulation_time);
talos_rt_status talos_rt_v1_coverage_snapshot(talos_rt_context *context,
                                                  uint32_t run_status,
                                                  uint64_t simulation_time);
talos_rt_status
talos_rt_v1_functional_coverage_get(talos_rt_context *context,
                                      double *out_percentage);
talos_rt_status
talos_rt_v1_functional_coverage_set_db_name(talos_rt_context *context,
                                              talos_rt_string_v1 name);
talos_rt_status
talos_rt_v1_functional_coverage_load_db(talos_rt_context *context,
                                          talos_rt_string_v1 name);
talos_rt_status talos_rt_v1_context_seed(talos_rt_context *context,
                                             uint64_t seed);

/// One schema-indexed value transported by the functional-coverage
/// ABI. Integral planes are little-endian byte strings of `value_size` bytes;
/// the final byte is zero-padded above `bit_width`. Four-state values carry a
/// second plane with the same layout. Real and shortreal values point at one
/// native double or float, respectively.
/// Argument references carry the exact owner/payload/tag triple used by
/// argument_ref_load and have null value planes. String values carry an owned
/// context string word in `payload` and have zero width and null value planes.
typedef uint32_t talos_rt_functional_value_kind_v1;
enum {
  TALOS_RT_FUNCTIONAL_VALUE_INTEGRAL = 1,
  TALOS_RT_FUNCTIONAL_VALUE_FOUR_STATE = 2,
  TALOS_RT_FUNCTIONAL_VALUE_REAL = 3,
  TALOS_RT_FUNCTIONAL_VALUE_ARGUMENT_REF = 4,
  TALOS_RT_FUNCTIONAL_VALUE_STRING = 5,
  // A managed container in `owner` (or null for an empty value), with every
  // other payload field zero. Set and TupleQueue expressions are consumed
  // synchronously. Container constructor formals retain captured array and
  // queue values using their shared schema type descriptor.
  TALOS_RT_FUNCTIONAL_VALUE_MANAGED_CONTAINER = 6,
  // An owned fixed aggregate snapshot in a managed reference path. All other
  // fields are zero; the shared aggregate layout identifies its typed storage.
  TALOS_RT_FUNCTIONAL_VALUE_MANAGED_AGGREGATE = 7,
  // A nullable class handle in owner. Captures retain object identity; all
  // other fields are zero. The schema records the nominal runtime class ID.
  TALOS_RT_FUNCTIONAL_VALUE_CLASS_HANDLE = 8,
  /* Opaque C pointer word in payload; all other value fields are zero. */
  TALOS_RT_FUNCTIONAL_VALUE_CHANDLE = 9,
  // Canonical scheduler identity in payload; null is UINT64_MAX. All other
  // value fields are zero. Copies retain the shared synchronization identity.
  TALOS_RT_FUNCTIONAL_VALUE_EVENT = 10,
  // Context-local covergroup identity in payload; zero is null. All other
  // value fields are zero. The formal's value_type names a FunctionalType.
  TALOS_RT_FUNCTIONAL_VALUE_COVERGROUP = 11,
  // Shared scheduler process identity in payload; zero is null. All other
  // value fields are zero. Finished and killed identities remain valid.
  TALOS_RT_FUNCTIONAL_VALUE_PROCESS = 12,
  // Elaborated interface scope identity in payload; zero is null. All other
  // value fields are zero. The formal's value_type includes its modport view.
  TALOS_RT_FUNCTIONAL_VALUE_VIRTUAL_INTERFACE = 13,
  // Managed semaphore handle in owner; null is allowed. All other value
  // fields are zero. Inputs preserve the shared bucket's identity.
  TALOS_RT_FUNCTIONAL_VALUE_SEMAPHORE = 14,
  // Shared mailbox handle in owner, with message type in the schema. Null is
  // allowed; all other value fields are zero. The bound is instance state.
  TALOS_RT_FUNCTIONAL_VALUE_MAILBOX = 15,
  // IEEE binary32 payload: bit_width=32, value_size=4, unknown=null.
  // Captures preserve the source bits, including signed zero and NaN payloads.
  TALOS_RT_FUNCTIONAL_VALUE_SHORTREAL = 16
};
typedef struct talos_rt_functional_value_v1 {
  uint64_t id;
  uint64_t bit_width;
  uint64_t value_size;
  const void *value;
  const void *unknown;
  talos_rt_object_v1 *owner;
  uint64_t payload;
  talos_rt_functional_value_kind_v1 kind;
  uint32_t argument_ref_kind;
} talos_rt_functional_value_v1;

// Private construction tokens do not create instances or consume instance IDs.
// Candidate extraction fills an empty, non-null CrossQueueType, using its
// registered element descriptor. Every token must be released in its context.
talos_rt_status talos_rt_v1_covergroup_prepare(
    talos_rt_context *context, uint64_t type_id,
    const talos_rt_functional_value_v1 *expressions,
    uint64_t expression_count, uint64_t *out_token);
talos_rt_status talos_rt_v1_covergroup_candidates(
    talos_rt_context *context, uint64_t token, uint64_t selector,
    talos_rt_object_v1 *queue, uint32_t maximum_candidates);
talos_rt_status
talos_rt_v1_covergroup_bind_with(talos_rt_context *context, uint64_t token,
                                   uint64_t selector,
                                   talos_rt_object_v1 *queue);
talos_rt_status
talos_rt_v1_covergroup_release_preparation(talos_rt_context *context,
                                             uint64_t token);
talos_rt_status talos_rt_v1_covergroup_create(
    talos_rt_context *context, uint64_t type_id,
    const talos_rt_functional_value_v1 *formals, uint64_t formal_count,
    const talos_rt_functional_value_v1 *expressions,
    uint64_t expression_count, talos_rt_covergroup_v1 *out_handle);
talos_rt_status
talos_rt_v1_covergroup_set_enabled(talos_rt_context *context,
                                     talos_rt_covergroup_v1 handle,
                                     uint64_t item_id, uint32_t enabled);
talos_rt_status
talos_rt_v1_covergroup_set_name(talos_rt_context *context,
                                  talos_rt_covergroup_v1 handle,
                                  talos_rt_string_v1 name);
typedef uint32_t talos_rt_covergroup_instance_option_v1;
enum {
  TALOS_RT_COVERGROUP_OPTION_WEIGHT = 1,
  TALOS_RT_COVERGROUP_OPTION_GOAL = 2,
  TALOS_RT_COVERGROUP_OPTION_COMMENT = 3,
  TALOS_RT_COVERGROUP_OPTION_AT_LEAST = 4,
  TALOS_RT_COVERGROUP_OPTION_MERGE_INSTANCES = 5,
  TALOS_RT_COVERGROUP_OPTION_CROSS_NUM_PRINT_MISSING = 6,
  TALOS_RT_COVERGROUP_OPTION_DISTRIBUTE_FIRST = 7,
  TALOS_RT_COVERGROUP_OPTION_AUTO_BIN_MAX = 8,
  TALOS_RT_COVERGROUP_OPTION_DETECT_OVERLAP = 9,
  TALOS_RT_COVERGROUP_OPTION_PER_INSTANCE = 10,
  TALOS_RT_COVERGROUP_OPTION_GET_INST_COVERAGE = 11,
  TALOS_RT_COVERGROUP_OPTION_CROSS_RETAIN_AUTO_BINS = 12,
  TALOS_RT_COVERGROUP_OPTION_NAME = 13
};
talos_rt_status talos_rt_v1_covergroup_set_integer_option(
    talos_rt_context *context, talos_rt_covergroup_v1 handle,
    uint64_t item_id, talos_rt_covergroup_instance_option_v1 option,
    int64_t value);
talos_rt_status talos_rt_v1_covergroup_get_integer_option(
    talos_rt_context *context, talos_rt_covergroup_v1 handle,
    uint64_t item_id, talos_rt_covergroup_instance_option_v1 option,
    int64_t *out_value);
talos_rt_status talos_rt_v1_covergroup_set_string_option(
    talos_rt_context *context, talos_rt_covergroup_v1 handle,
    uint64_t item_id, talos_rt_covergroup_instance_option_v1 option,
    talos_rt_string_v1 value);
// Allocate a string snapshot in the active lane; name is group-only and
// comment accepts a group or a stable item selector.
talos_rt_status talos_rt_v1_covergroup_get_string_option(
    talos_rt_gc_lane_v1 *lane, talos_rt_covergroup_v1 handle,
    uint64_t item_id, talos_rt_covergroup_instance_option_v1 option,
    talos_rt_string_v1 *out_value);
talos_rt_status talos_rt_v1_covergroup_set_type_integer_option(
    talos_rt_context *context, uint64_t type_id, uint64_t item_id,
    talos_rt_covergroup_instance_option_v1 option, int64_t value);
talos_rt_status talos_rt_v1_covergroup_set_type_string_option(
    talos_rt_context *context, uint64_t type_id, uint64_t item_id,
    talos_rt_covergroup_instance_option_v1 option,
    talos_rt_string_v1 value);
talos_rt_status
talos_rt_v1_covergroup_sample_enabled(talos_rt_context *context,
                                        talos_rt_covergroup_v1 handle,
                                        uint32_t *out_enabled);
talos_rt_status talos_rt_v1_covergroup_sample(
    talos_rt_context *context, talos_rt_covergroup_v1 handle,
    const talos_rt_functional_value_v1 *expressions,
    uint64_t expression_count);
// Register one covergroup clocking event with the active detached owner
// process. `event_plan` is one computed-observer record whose primary
// observers retain their construction-time results and whose optional
// condition observers implement event iff clauses. Dependencies may name
// global, static, or live automatic packed state, or a managed class-field
// watch resolved from an exact-alias argument-reference capture. `strobe` is
// the compiler's schema expectation; registration rejects it if it
// differs from the resolved instance policy. False selects synchronous
// sampling and true selects one Postponed sample per numeric time. The bound
// sample observer must return known i1 true.
talos_rt_status talos_rt_v1_covergroup_clock_event_register(
    talos_rt_context *context,
    const talos_rt_computed_wait_record_v1 *event_plan,
    uint64_t event_plan_size, uint32_t strobe, uint64_t observer_code_unit,
    const talos_rt_computed_capture_v1 *captures, uint32_t capture_count);
// IEEE 1800 covergroup block-event sampling is synchronous and independent of
// type_option.strobe. Registrations are context-owned because the covergroup
// instance, unlike the process that called new(), remains live for the rest of
// the simulation. A nonnull receiver restricts a class method or named block
// event to that exact object instance.
typedef uint32_t talos_rt_covergroup_block_event_kind_v1;
enum {
  TALOS_RT_COVERGROUP_BLOCK_EVENT_BEGIN = 0,
  TALOS_RT_COVERGROUP_BLOCK_EVENT_END = 1
};
talos_rt_status talos_rt_v1_covergroup_block_event_register(
    talos_rt_context *context, talos_rt_covergroup_v1 handle,
    talos_rt_object_v1 *receiver, uint64_t observer_code_unit,
    const talos_rt_computed_capture_v1 *captures, uint32_t capture_count,
    const uint64_t *target_ids,
    const talos_rt_covergroup_block_event_kind_v1 *event_kinds,
    uint32_t event_count);
talos_rt_status talos_rt_v1_covergroup_block_event_fire(
    talos_rt_context *context, uint64_t target_id,
    talos_rt_covergroup_block_event_kind_v1 event_kind,
    talos_rt_object_v1 *receiver);
// String reads use a 64-bit managed String value (value_size == 8) and no
// unknown plane. Input formals return their immutable captured value; ref
// formals load the current typed value from their retained storage binding.
// ARGUMENT_REF reads instead project the borrowed retained reference as three
// 64-bit words (managed owner, payload, reference kind); bit_width is 192,
// value_size is 24, and out_unknown is null. Only ref formals permit this mode.
// Generated native callers provide their global state planes. Null planes
// select the context's canonical state for bytecode and runtime-only callers.
talos_rt_status talos_rt_v1_covergroup_formal_read(
    talos_rt_context *context, talos_rt_covergroup_v1 handle,
    uint64_t formal_id, talos_rt_functional_value_kind_v1 kind,
    uint64_t bit_width, uint64_t value_size, const uint8_t *state_value,
    const uint8_t *state_unknown, uint64_t state_bit_count, void *out_value,
    void *out_unknown);
talos_rt_status talos_rt_v1_covergroup_instance_query(
    talos_rt_context *context, talos_rt_covergroup_v1 handle,
    uint64_t item_id, double *out_percentage, int32_t *out_covered,
    int32_t *out_total);
talos_rt_status talos_rt_v1_covergroup_type_query(
    talos_rt_context *context, uint64_t type_id, uint64_t item_id,
    double *out_percentage, int32_t *out_covered, int32_t *out_total);
talos_rt_status talos_rt_v1_random_next(talos_rt_context *context,
                                            uint64_t *out_value);
// IEEE 1800-2017 20.15.1 and normative Annex N `$random`. This context-global
// legacy stream is deliberately independent of the process-local `$urandom`
// stream exposed by random_next.
talos_rt_status talos_rt_v1_random_legacy(talos_rt_context *context,
                                              int32_t *out_value);
talos_rt_status talos_rt_v1_random_seed(talos_rt_context *context,
                                            uint64_t seed);
talos_rt_status talos_rt_v1_random_bounded(talos_rt_context *context,
                                               uint64_t bound,
                                               uint64_t *out_value);
// $dist_* draws (IEEE 1800 20.15 and normative Annex N). The explicit seed is
// the source inout state and `out_next_seed` returns its value after the draw.
// `first` and `second` carry shape parameters in source order; distributions
// taking one parameter ignore `second`.
talos_rt_status
talos_rt_v1_random_distribution(talos_rt_context *context,
                                  talos_rt_distribution distribution,
                                  int32_t seed, int32_t first, int32_t second,
                                  int32_t *out_value, int32_t *out_next_seed);

// IEEE 1800-2017 20.16 queue manager. `id`, `first`, and `second` use the
// standard two-plane 32-bit representation so job and information identifiers
// retain X/Z exactly. Results are 64-bit logic values: REMOVE uses their low
// 32 bits, FULL returns 0/1, and EXAM can return a full simulation-time value.
// `unit_scale` is the number of design-precision ticks in the calling scope's
// time unit and controls the rounding of time statistics.
talos_rt_status talos_rt_v1_stochastic_queue(
    talos_rt_context *context, talos_rt_stochastic_queue_action_v1 action,
    uint32_t id_value, uint32_t id_unknown, uint32_t first_value,
    uint32_t first_unknown, uint32_t second_value, uint32_t second_unknown,
    uint64_t unit_scale, uint64_t *out_primary_value,
    uint64_t *out_primary_unknown, uint64_t *out_secondary_value,
    uint64_t *out_secondary_unknown,
    talos_rt_stochastic_queue_status_v1 *out_queue_status);
// Advance a keyed randc permutation over exactly 2^width values. Widths 1..32
// are supported. The caller owns key/position storage and explicitly rekeys
// whenever the returned position wraps to zero.
talos_rt_status talos_rt_v1_random_cycle_next(uint64_t key,
                                                  uint64_t position,
                                                  uint32_t width,
                                                  uint64_t *out_next_position,
                                                  uint64_t *out_value);
// Execute a compiler-produced residual constraint program. `start` selects
// the first aggregate assignment and `max_attempts` is a deterministic work
// bound. A successful solve writes both outputs. Exhaustion is reported by
// out_success == 0 and is not a runtime API error.
talos_rt_status talos_rt_v1_random_solve(
    talos_rt_context *context, const uint8_t *program, uint64_t program_size,
    uint64_t start, uint64_t max_attempts, const uint64_t *captures,
    uint64_t capture_count, uint64_t *out_assignment, uint32_t *out_success);
// As above, but enumerate only bits selected by `mutable_mask`; every other
// aggregate bit retains its value from `start` in every candidate.
talos_rt_status talos_rt_v1_random_solve_masked(
    talos_rt_context *context, const uint8_t *program, uint64_t program_size,
    uint64_t start, uint64_t mutable_mask, uint64_t max_attempts,
    const uint64_t *captures, uint64_t capture_count, uint64_t *out_assignment,
    uint32_t *out_success);
// As above, with a disabled-bit mask for compiler-assigned constraint blocks.
// Program constraints marked as unmasked remain active.
talos_rt_status talos_rt_v1_random_solve_modes(
    talos_rt_context *context, const uint8_t *program, uint64_t program_size,
    uint64_t start, uint64_t mutable_mask, uint64_t constraint_mask,
    uint64_t max_attempts, const uint64_t *captures, uint64_t capture_count,
    uint64_t *out_assignment, uint32_t *out_success);
// Stateful form used by object randomization. Ordered fallback sampling draws
// unbiased bounded indices from the supplied PCG stream and returns its
// advanced state. Non-ordered programs leave the state unchanged. The
// increment must be odd. Stateless solve entry points reject programs with an
// active solve-before edge because they cannot preserve its distribution.
talos_rt_status talos_rt_v1_random_solve_modes_state(
    talos_rt_context *context, const uint8_t *program, uint64_t program_size,
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
talos_rt_status talos_rt_v1_random_solve_wide_modes_state(
    talos_rt_context *context, const uint8_t *program, uint64_t program_size,
    const uint64_t *start_words, const uint64_t *mutable_mask_words,
    uint64_t assignment_word_count, uint64_t constraint_mask,
    uint64_t max_attempts, uint64_t rng_state, uint64_t rng_increment,
    const uint64_t *capture_words, uint64_t capture_word_count,
    const uint32_t *capture_widths, uint64_t capture_count,
    uint64_t *out_assignment_words, uint32_t *out_success,
    uint64_t *out_rng_state);
// Project one cyclic field from a version-2 compiler sampling plan. Histories
// are immutable managed strings that callers keep rooted like other strings.
// No field, history or supplied RNG is changed on an unsuccessful stage.
// Only the selected field is patched in the returned aggregate; later fields
// are existential completion variables. All output words are initialized.
talos_rt_status talos_rt_v1_random_cycle_sample(
    talos_rt_context *context, const uint8_t *program, uint64_t program_size,
    const uint64_t *start_words, const uint64_t *mutable_mask_words,
    uint64_t assignment_word_count, uint64_t constraint_mask,
    uint64_t rng_state, uint64_t rng_increment, uint64_t key, uint64_t position,
    talos_rt_string_v1 domain, uint32_t field_offset, uint32_t field_width,
    const uint64_t *capture_words, uint64_t capture_word_count,
    const uint32_t *capture_widths, uint64_t capture_count,
    uint64_t *out_assignment_words, uint32_t *out_success,
    uint64_t *out_rng_state, uint64_t *out_key, uint64_t *out_position,
    talos_rt_string_v1 *out_domain);
talos_rt_status
talos_rt_v1_random_get_state(talos_rt_context *context,
                               talos_rt_random_state_v1 *out_state);
// Compiler startup bindings for IEEE 18.14.1. Scope IDs are nonzero opaque
// lexical identities. Every scope starts with the configured default seed;
// entering resumes its stream and suppresses the synthetic startup process
// identity. Leave requires the matching, most recent activation token.
talos_rt_status talos_rt_v1_random_initialization_enter(
    talos_rt_context *context, uint64_t scope, uint64_t *out_activation);
talos_rt_status
talos_rt_v1_random_initialization_leave(talos_rt_context *context,
                                          uint64_t activation);
talos_rt_status
talos_rt_v1_random_set_state(talos_rt_context *context,
                               const talos_rt_random_state_v1 *state);
void talos_rt_v1_random_state_seed(talos_rt_random_state_v1 *state,
                                     uint64_t seed, uint64_t sequence);
uint32_t talos_rt_v1_random_state_next32(talos_rt_random_state_v1 *state);
uint64_t talos_rt_v1_random_state_next64(talos_rt_random_state_v1 *state);
talos_rt_status
talos_rt_v1_random_state_bounded(talos_rt_random_state_v1 *state,
                                   uint64_t bound, uint64_t *out_value);
// Coordinates use canonical little-endian 64-bit words and zero padding above
// RANK_BITS. Nonfinite values and ranks outside the finite range are rejected;
// failed conversions preserve the destination. Decoding rounds towards zero.
talos_rt_status talos_rt_v1_random_real64_to_rank(
    double value, uint64_t *out_words, uint64_t word_count);
talos_rt_status talos_rt_v1_random_real32_to_rank(
    float value, uint64_t *out_words, uint64_t word_count);
talos_rt_status talos_rt_v1_random_real64_from_rank(
    const uint64_t *words, uint64_t word_count, double *out_value);
talos_rt_status talos_rt_v1_random_real32_from_rank(
    const uint64_t *words, uint64_t word_count, float *out_value);
// Sample an interval with mass proportional to numeric spacing. Default
// bounds are closed; OPEN flags exclude their endpoints. A singleton needs
// no draw. An empty finite interval returns OK with success=0, preserving the
// value and RNG. Invalid arguments preserve all outputs. These are leaf
// operations for compiler interval plans; they do not interpret constraints.
talos_rt_status talos_rt_v1_random_real64_interval(
    talos_rt_random_state_v1 *state, double lower, double upper, uint32_t flags,
    double *out_value, uint32_t *out_success);
talos_rt_status talos_rt_v1_random_real32_interval(
    talos_rt_random_state_v1 *state, float lower, float upper, uint32_t flags,
    float *out_value, uint32_t *out_success);
uint32_t talos_rt_v1_import_id(const uint8_t *symbol, uint64_t symbol_size);
talos_rt_status talos_rt_v1_context_register_import(
    talos_rt_context *context, uint32_t import_id,
    talos_rt_import_callback_v1 callback, void *user_data);
// Generated simulators use the signature form so malformed bytecode cannot
// dispatch a register layout to an incompatible native thunk. A zero
// signature is reserved for the generic host-registration API above.
talos_rt_status talos_rt_v1_context_register_import_signature(
    talos_rt_context *context, uint32_t import_id, uint64_t abi_signature,
    talos_rt_import_callback_v1 callback, void *user_data);
talos_rt_status talos_rt_v1_import_call(
    talos_rt_context *context, const talos_rt_import_site_v1 *site,
    const talos_rt_import_input_v1 *inputs, uint32_t input_count,
    talos_rt_import_output_v1 *outputs, uint32_t output_count);
// Specialized non-context boundary used by generated native code. It has no
// active DPI scope/caller state and never touches the thread-local context
// chain; context imports use talos_rt_v1_import_call instead.
talos_rt_status talos_rt_v1_import_call_noncontext(
    talos_rt_context *context, const talos_rt_import_site_v1 *site,
    const talos_rt_import_input_v1 *inputs, uint32_t input_count,
    talos_rt_import_output_v1 *outputs, uint32_t output_count);
talos_rt_status talos_rt_v1_import_call_guarded(
    talos_rt_status prior_status, talos_rt_context *context,
    const talos_rt_import_site_v1 *site,
    const talos_rt_import_input_v1 *inputs, uint32_t input_count,
    talos_rt_import_output_v1 *outputs, uint32_t output_count);
talos_rt_status talos_rt_v1_import_call_noncontext_guarded(
    talos_rt_status prior_status, talos_rt_context *context,
    const talos_rt_import_site_v1 *site,
    const talos_rt_import_input_v1 *inputs, uint32_t input_count,
    talos_rt_import_output_v1 *outputs, uint32_t output_count);
// Enter a scope-specific exported SystemVerilog function from generated C.
// The active DPI call supplies the hidden context and scope. A call made
// outside that dynamic extent returns INVALID_LIFECYCLE.
talos_rt_status talos_rt_v1_export_call(
    uint32_t export_id, uint64_t abi_signature,
    const talos_rt_import_input_v1 *inputs, uint32_t input_count,
    talos_rt_import_output_v1 *outputs, uint32_t output_count);
talos_rt_status talos_rt_v1_export_call_guarded(
    talos_rt_status prior_status, uint32_t export_id, uint64_t abi_signature,
    const talos_rt_import_input_v1 *inputs, uint32_t input_count,
    talos_rt_import_output_v1 *outputs, uint32_t output_count);
// Latch wrapper-side marshalling failures into the active import call.
talos_rt_status talos_rt_v1_dpi_export_status(talos_rt_status status);
// String export outputs are copied into per-thread C storage by export_call.
// The address remains valid until the next exported call on this thread.
const char *talos_rt_v1_export_string(uint32_t output_index);
// Active scope-specific DPI call context, or null outside that dynamic extent.
talos_rt_context *talos_rt_v1_dpi_current_context(void);
// Marshal DPI's 32-bit svBitVecVal/svLogicVecVal words to and from the
// runtime's independent 64-bit value/unknown planes.
void talos_rt_v1_dpi_export_unpack_vector(const void *source, void *value,
                                            void *unknown, uint32_t width,
                                            uint32_t four_state);
void talos_rt_v1_dpi_export_pack_vector(void *destination, const void *value,
                                          const void *unknown, uint32_t width,
                                          uint32_t four_state);
// Copy one null-terminated DPI C string into simulator-owned managed storage.
// A null C pointer is invalid: IEEE 1800 requires a valid initialized string
// address for a string result or copy-out.
talos_rt_status
talos_rt_v1_dpi_string_copy(talos_rt_context *context, const char *string,
                              talos_rt_string_v1 *out_string);
void talos_rt_v1_context_destroy(talos_rt_context *context);
const char *talos_rt_v1_status_string(talos_rt_status status);
void talos_rt_v1_buffer_release(talos_rt_buffer_v1 *buffer);
talos_rt_status talos_rt_v1_last_error(talos_rt_context *context,
                                           talos_rt_buffer_v1 *out_message);
// Report a simulation that ended on a runtime error. A standalone simulator's
// `main` calls this with the scheduler's status before destroying the context,
// so a run that fails says why instead of exiting on a bare status. A
// successful run and a design-requested $fatal report nothing.
void talos_rt_v1_scheduler_report_status(talos_rt_context *context,
                                           talos_rt_status status);

// Formatting and display. Input arrays remain caller-owned and must not be
// mutated until the call returns. format() consumes every supplied argument.
// display() treats STRING arguments carrying FORMAT_STRING as format strings
// that consume following items; other items use the requested default radix.
// DESIGNATED_FORMAT additionally gives that format ownership of every
// remaining item, as required for $sformat/$sformatf; surplus items are
// diagnosed and ignored.
talos_rt_status
talos_rt_v1_format(talos_rt_context *context, const char *format,
                     uint64_t format_size, const talos_rt_arg_v1 *arguments,
                     uint64_t argument_count,
                     const talos_rt_format_env_v1 *environment,
                     talos_rt_buffer_v1 *out_buffer);
talos_rt_status talos_rt_v1_string_output_format(
    talos_rt_context *context, talos_rt_radix default_radix,
    const talos_rt_arg_v1 *items, uint64_t item_count,
    const talos_rt_format_env_v1 *environment,
    talos_rt_string_v1 *out_string);
// $timeformat (IEEE 1800 20.4.2). `units` is the decimal exponent in seconds
// of the unit %t reports in; `fraction_digits` and `width` are its precision
// and minimum field width, the latter covering the suffix too. The override
// applies to every later %t in the design.
talos_rt_status
talos_rt_v1_time_format(talos_rt_context *context, int32_t units,
                          uint32_t fraction_digits, const char *suffix,
                          uint64_t suffix_size, uint32_t width);
// IEEE 1800-2017 21.3.4.3 `%t` input conversion. `time_multiplier` is the
// caller's time unit in design-precision ticks and `time_precision` is the
// decimal exponent in seconds of one such tick. The current `$timeformat`
// supplies the input units and decimal rounding; the inactive default is zero
// fractional digits in design-precision units.
double talos_rt_v1_time_scan_scale(talos_rt_context *context, double value,
                                     uint64_t time_multiplier,
                                     int32_t time_precision);
talos_rt_status
talos_rt_v1_display(talos_rt_context *context, uint32_t descriptor,
                      uint32_t append_newline, talos_rt_radix default_radix,
                      const talos_rt_arg_v1 *items, uint64_t item_count,
                      const talos_rt_format_env_v1 *environment);

// Generated, non-monitor stdout output. Arguments are self-contained packed
// snapshots/literal bytes/reals, never managed objects or net handles. This
// service neither reads canonical design state nor re-enters the scheduler.
talos_rt_status talos_rt_v1_eval_display(
    talos_rt_context *context, uint32_t descriptor, uint32_t append_newline,
    talos_rt_radix default_radix, const talos_rt_arg_v1 *items,
    uint64_t item_count, const talos_rt_format_env_v1 *environment);

// File channels. Calls using one context are serialized around each libc stream
// operation. Descriptor close/reuse must be coordinated by the caller so no
// thread begins an operation after another thread closes that descriptor. The
// one-argument fopen form produces an MCD; the mode-taking
// form produces a descriptor with bit 31 set. A descriptor of zero passed to
// flush means all runtime-owned streams plus stdout.
talos_rt_status talos_rt_v1_file_open_mcd(talos_rt_context *context,
                                              const char *path,
                                              uint64_t path_size,
                                              uint32_t *out_descriptor);
talos_rt_status talos_rt_v1_file_open(talos_rt_context *context,
                                          const char *path, uint64_t path_size,
                                          const char *mode, uint64_t mode_size,
                                          uint32_t *out_descriptor);
talos_rt_status
talos_rt_v1_file_open_string_mcd(talos_rt_context *context,
                                   talos_rt_string_v1 path,
                                   uint32_t *out_descriptor);
talos_rt_status talos_rt_v1_file_open_string(talos_rt_context *context,
                                                 talos_rt_string_v1 path,
                                                 talos_rt_string_v1 mode,
                                                 uint32_t *out_descriptor);
talos_rt_status talos_rt_v1_file_close(talos_rt_context *context,
                                           uint32_t descriptor);
talos_rt_status talos_rt_v1_file_flush(talos_rt_context *context,
                                           uint32_t descriptor);
talos_rt_status talos_rt_v1_file_write(talos_rt_context *context,
                                           uint32_t descriptor,
                                           const void *data, uint64_t size,
                                           uint64_t *out_written);
talos_rt_status talos_rt_v1_file_read(talos_rt_context *context,
                                          uint32_t descriptor, void *data,
                                          uint64_t size, uint64_t *out_read);
talos_rt_status talos_rt_v1_file_readmem_token(
    talos_rt_context *context, uint32_t descriptor, uint32_t radix,
    uint64_t bit_width, void *value, uint64_t value_size, void *unknown,
    uint64_t unknown_size, uint32_t *out_kind, uint64_t *out_address);
talos_rt_status talos_rt_v1_file_getc(talos_rt_context *context,
                                          uint32_t descriptor,
                                          uint8_t *out_byte);
talos_rt_status talos_rt_v1_file_ungetc(talos_rt_context *context,
                                            uint32_t descriptor, uint8_t byte);
talos_rt_status talos_rt_v1_file_getline(talos_rt_context *context,
                                             uint32_t descriptor,
                                             uint64_t max_bytes,
                                             talos_rt_buffer_v1 *out_line);
talos_rt_status talos_rt_v1_file_getline_string(
    talos_rt_context *context, talos_rt_gc_lane_v1 *lane,
    uint32_t descriptor, talos_rt_string_v1 *out_string, uint32_t *out_count);
// Consume one formatted field at the descriptor's current position. Disabled
// calls leave the stream untouched so lowering can stop after a mismatch. `%m`
// matches its prefix successfully without consuming a field byte. `out_eof`
// denotes scanf input failure from either end-of-file or a host read error;
// file_eof and file_error distinguish the descriptor indicators.
talos_rt_status talos_rt_v1_file_scan_field(
    talos_rt_context *context, talos_rt_gc_lane_v1 *lane,
    uint32_t descriptor, uint32_t enabled, const char *prefix,
    uint64_t prefix_size, uint32_t specifier, uint64_t width,
    talos_rt_string_v1 *out_field, uint32_t *out_ok, uint32_t *out_eof);
talos_rt_status talos_rt_v1_file_scan_dynamic(
    talos_rt_context *context, talos_rt_gc_lane_v1 *lane,
    uint32_t descriptor, talos_rt_string_v1 format, uint32_t plan_cursor,
    uint32_t enabled, uint32_t finalize, uint64_t allowed_specifiers,
    uint64_t raw_two_state_bytes, uint64_t raw_four_state_bytes,
    talos_rt_string_v1 *out_field, uint32_t *out_plan_cursor,
    uint32_t *out_conversion_kind, uint32_t *out_ok, uint32_t *out_eof);
talos_rt_status talos_rt_v1_file_scan_raw(
    talos_rt_context *context, uint32_t descriptor, uint32_t enabled,
    const char *prefix, uint64_t prefix_size, uint64_t raw_size,
    uint64_t bit_width, uint32_t four_state, uint64_t max_width, void *value,
    uint64_t value_size, void *unknown, uint64_t unknown_size, uint32_t *out_ok,
    uint32_t *out_eof);
talos_rt_status talos_rt_v1_file_eof(talos_rt_context *context,
                                         uint32_t descriptor,
                                         uint32_t *out_is_eof);
talos_rt_status talos_rt_v1_file_error(talos_rt_context *context,
                                           uint32_t descriptor,
                                           int32_t *out_error_code,
                                           talos_rt_buffer_v1 *out_message);
// $ferror form: the message lands in a managed string instead of a buffer the
// caller has to release. A descriptor with no pending error yields code zero
// and an empty string.
talos_rt_status talos_rt_v1_file_error_string(
    talos_rt_context *context, talos_rt_gc_lane_v1 *lane,
    uint32_t descriptor, talos_rt_string_v1 *out_message,
    int32_t *out_error_code);
// Command-line plusargs. Arguments introduced by '+' are recorded by
// configure_argv with that prefix stripped; both queries match the caller's
// text as a prefix of a recorded argument, taking the first that matches.
// value() additionally returns the matched argument's remaining text, which
// the caller converts according to the format specifier it stripped off.
talos_rt_status talos_rt_v1_plusarg_test(talos_rt_context *context,
                                             talos_rt_string_v1 name,
                                             uint32_t *out_found);
talos_rt_status talos_rt_v1_plusarg_value(talos_rt_context *context,
                                              talos_rt_gc_lane_v1 *lane,
                                              talos_rt_string_v1 prefix,
                                              talos_rt_string_v1 *out_tail,
                                              uint32_t *out_found);
// Parse a runtime $value$plusargs format. `out_conversion` is zero for string,
// one for real, or the integral radix. Invalid formats report no match.
talos_rt_status talos_rt_v1_plusarg_scan(talos_rt_context *context,
                                             talos_rt_gc_lane_v1 *lane,
                                             talos_rt_string_v1 format,
                                             talos_rt_string_v1 *out_tail,
                                             uint32_t *out_conversion,
                                             uint32_t *out_found);
// Strict Clause 21.6 conversions. Unlike the general string conversion
// helpers, every non-leading character must belong to the selected field;
// malformed integral input produces an all-X result at the exact width.
talos_rt_status talos_rt_v1_plusarg_parse_logic(
    talos_rt_string_v1 string, uint32_t radix, uint64_t bit_width,
    void *value, uint64_t value_size, void *unknown, uint64_t unknown_size);
talos_rt_status talos_rt_v1_plusarg_parse_real(talos_rt_string_v1 string,
                                                   double *out_value);

// Execute a host command and return its normalized exit status. A process
// terminated by a signal reports 128 plus the signal number.
talos_rt_status talos_rt_v1_system(talos_rt_context *context,
                                       talos_rt_string_v1 command,
                                       int32_t *out_status);

talos_rt_status talos_rt_v1_file_seek(talos_rt_context *context,
                                          uint32_t descriptor, int64_t offset,
                                          talos_rt_seek_origin origin);
talos_rt_status talos_rt_v1_file_tell(talos_rt_context *context,
                                          uint32_t descriptor,
                                          int64_t *out_offset);
talos_rt_status talos_rt_v1_file_rewind(talos_rt_context *context,
                                            uint32_t descriptor);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // TALOS_RUNTIME_RUNTIME_H
