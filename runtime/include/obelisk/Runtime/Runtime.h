//===- Runtime.h - Obelisk native runtime C ABI -----------------*- C -*-===//

#ifndef OBELISK_RUNTIME_RUNTIME_H
#define OBELISK_RUNTIME_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Names ending in _v1 and this generation number identify the current in-tree
// schema. They are not a backward-compatibility promise: generated code,
// bytecode, and libobelisk_rt must come from the same Obelisk source revision.
// Any compiler update requires regenerating and relinking the simulator.
#define OBELISK_RT_ABI_GENERATION 1u

typedef struct obelisk_rt_context obelisk_rt_context;

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
  OBELISK_RT_DESCRIPTOR_FUNCTION = 8
};

typedef struct obelisk_rt_handle_v1 {
  obelisk_rt_descriptor_kind kind;
  uint32_t generation;
  uint64_t id;
} obelisk_rt_handle_v1;

// Design-wide executable metadata is deliberately split from optional VPI
// reflection metadata.  Both payloads are immutable, pointer-free byte images;
// the only native pointers are this linker-resolved pointer/size pair.  A
// descriptor with a null reflection image is valid and is emitted for vpi=off.
#define OBELISK_RT_EXECUTION_DESCRIPTOR_VERSION 1u
#define OBELISK_RT_EXECUTION_HAS_BYTECODE (UINT32_C(1) << 0)
#define OBELISK_RT_EXECUTION_HAS_DESIGN_DATABASE (UINT32_C(1) << 1)
#define OBELISK_RT_EXECUTION_VPI_READ (UINT32_C(1) << 2)
#define OBELISK_RT_EXECUTION_VPI_WRITE (UINT32_C(1) << 3)

typedef struct obelisk_rt_execution_descriptor_v1 {
  uint32_t version;
  uint32_t abi_generation;
  uint32_t flags;
  uint32_t reserved;
  const uint8_t *bytecode;
  uint64_t bytecode_size;
  const uint8_t *design_database;
  uint64_t design_database_size;
  uint64_t state_bit_count;
  uint64_t checksum;
} obelisk_rt_execution_descriptor_v1;

// One of these immutable records is attached to the canonical process
// descriptor for every process encoded in the design image.
typedef struct obelisk_rt_design_bytecode_entry_v1 {
  const obelisk_rt_execution_descriptor_v1 *execution;
  uint32_t function;
  uint32_t reserved;
} obelisk_rt_design_bytecode_entry_v1;

#define OBELISK_RT_DESIGN_BYTECODE_VERSION 2u
#define OBELISK_RT_DESIGN_BYTECODE_HEADER_SIZE 208u
#define OBELISK_RT_DESIGN_BYTECODE_INSTRUCTION_SIZE 32u

typedef uint8_t obelisk_rt_design_register_kind;
enum {
  OBELISK_RT_DBREG_INVALID = 0,
  OBELISK_RT_DBREG_BITS = 1,
  OBELISK_RT_DBREG_LOGIC = 2,
  OBELISK_RT_DBREG_HANDLE = 3,
  OBELISK_RT_DBREG_STATUS = 4,
  OBELISK_RT_DBREG_RESOURCE = 5,
  OBELISK_RT_DBREG_BYTES = 6
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
  OBELISK_RT_DB_HANDLE_ID = 39
};

// COMPARE flags. Case equality compares both four-state planes and always
// returns a known result; the other predicates return X when either operand
// has unknown bits.
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
  OBELISK_RT_DB_CMP_CASE_NE = 11
};

typedef uint32_t obelisk_rt_intrinsic_id;
enum {
  OBELISK_RT_INTRINSIC_V1_FORMAT = UINT32_C(0x00010001),
  OBELISK_RT_INTRINSIC_V1_DISPLAY = UINT32_C(0x00010002),
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
  OBELISK_RT_INTRINSIC_V1_SPAWN = UINT32_C(0x00010200),
  OBELISK_RT_INTRINSIC_V1_NBA = UINT32_C(0x00010201),
  OBELISK_RT_INTRINSIC_V1_EVENT_TRIGGER = UINT32_C(0x00010202),
  OBELISK_RT_INTRINSIC_V1_STATE_ALLOC = UINT32_C(0x00010203),
  OBELISK_RT_INTRINSIC_V1_IMPORT = UINT32_C(0x00010300),
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
  OBELISK_RT_INTRINSIC_V1_VPI_TYPE_CHILD = UINT32_C(0x0001100a),
  // Reserved, deliberately not implemented in this generation.
  OBELISK_RT_INTRINSIC_V2_VPI_CALLBACK = UINT32_C(0x00021000),
  OBELISK_RT_INTRINSIC_V2_VPI_FORCE = UINT32_C(0x00021001),
  OBELISK_RT_INTRINSIC_V2_VPI_RELEASE = UINT32_C(0x00021002)
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

// VPI bytecode intrinsics use known two-state i64 section offsets as cursors
// and a trailing `status` output. Traversal failures (including EOF) are
// written to that output instead of aborting the interpreter. INFO outputs
// kind, capabilities, stable ID, type cursor, left, right, width, status.
// NAME outputs string-section offset, byte count, status. TYPE_INFO outputs
// the fields of obelisk_rt_design_type_info_v1 in declaration order followed
// by status. READ and WRITE infer the value width from their numeric register.

// The design database is a DWARF-like byte image. All cursors and links are
// section-relative offsets, never native pointers.
#define OBELISK_RT_DESIGN_VERSION 3u
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
  OBELISK_RT_FRAGMENT_TERMINATE = 2
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
  OBELISK_RT_SUSPEND_FRONTIER = 7
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
#define OBELISK_RT_FRAME_LAYOUT_VERSION 1u

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
  OBELISK_RT_FRAME_FOUR_STATE_UNKNOWN = 1u << 1
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
#define OBELISK_RT_WAIT_RECORD_VERSION 1u
#define OBELISK_RT_WAIT_RECORD_SIGNAL_WIDTH_VERSION 2u
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
  uint32_t abi_generation;
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
};

// Create exactly one allocation containing [canonical frame][padding][shared
// scratch tail]. The tail is the maximum of native coroutine storage and
// bytecode registers and is reused without copying when tiers change.
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

// Validate and traverse the optional reflection image with checked cursors.
// Returned names are immutable spans into the database and remain valid for
// the lifetime of the execution descriptor.
obelisk_rt_status obelisk_rt_v1_design_validate(
    const obelisk_rt_execution_descriptor_v1 *execution);
obelisk_rt_status obelisk_rt_v1_design_root(
    const obelisk_rt_execution_descriptor_v1 *execution,
    obelisk_rt_design_cursor_v1 *out_cursor);
obelisk_rt_status obelisk_rt_v1_design_child(
    const obelisk_rt_execution_descriptor_v1 *execution,
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
obelisk_rt_status obelisk_rt_v1_design_lookup(
    const obelisk_rt_execution_descriptor_v1 *execution, const uint8_t *name,
    uint64_t name_size, obelisk_rt_design_cursor_v1 *out_cursor);
obelisk_rt_status obelisk_rt_v1_design_info(
    const obelisk_rt_execution_descriptor_v1 *execution,
    obelisk_rt_design_cursor_v1 cursor, obelisk_rt_design_info_v1 *out_info);
obelisk_rt_status obelisk_rt_v1_design_type_info(
    const obelisk_rt_execution_descriptor_v1 *execution,
    obelisk_rt_design_cursor_v1 cursor,
    obelisk_rt_design_type_info_v1 *out_info);
obelisk_rt_status obelisk_rt_v1_design_type_child(
    const obelisk_rt_execution_descriptor_v1 *execution,
    obelisk_rt_design_cursor_v1 cursor, uint64_t index,
    obelisk_rt_design_cursor_v1 *out_cursor);
obelisk_rt_status obelisk_rt_v1_design_name(
    const obelisk_rt_execution_descriptor_v1 *execution,
    obelisk_rt_design_cursor_v1 cursor, const uint8_t **out_data,
    uint64_t *out_size);
obelisk_rt_status obelisk_rt_v1_design_read(
    obelisk_rt_context *context, obelisk_rt_design_cursor_v1 cursor,
    uint64_t *value, uint64_t *unknown, uint64_t bit_width);
obelisk_rt_status obelisk_rt_v1_design_write(
    obelisk_rt_context *context, obelisk_rt_design_cursor_v1 cursor,
    const uint64_t *value, const uint64_t *unknown, uint64_t bit_width);

// Serial generated-simulator scheduler. The scheduler owns an instance after
// a successful add. Phase zero is ordinary work; phase one is final blocks.
obelisk_rt_status obelisk_rt_v1_scheduler_add(
    obelisk_rt_context *context, obelisk_rt_process_instance_v1 *instance,
    uint32_t phase);
// Ranked form used by generated simulators. Lower ranks execute first within
// an event region; equal ranks retain deterministic insertion order.
obelisk_rt_status obelisk_rt_v1_scheduler_add_ranked(
    obelisk_rt_context *context, obelisk_rt_process_instance_v1 *instance,
    uint32_t phase, uint32_t schedule_rank);
obelisk_rt_status obelisk_rt_v1_scheduler_add_planned(
    obelisk_rt_context *context, obelisk_rt_process_instance_v1 *instance,
    uint32_t phase, uint32_t initial_rank, const uint32_t *continuations,
    const uint32_t *ranks, uint32_t continuation_count);
// Return the scheduler-owned stable identity used by await/join records. The
// token is never a host address and is not reused within a context.
uint64_t obelisk_rt_v1_scheduler_process_token(
    obelisk_rt_context *context, obelisk_rt_process_instance_v1 *instance);
// Copy one nonblocking assignment into the current time slot. The scheduler
// applies queued updates in call order after active work reaches quiescence.
// A UINT64_MAX bit offset is an out-of-range dynamic selection and is ignored.
obelisk_rt_status obelisk_rt_v1_scheduler_nba(
    obelisk_rt_context *context, uint8_t *value_plane,
    uint8_t *unknown_plane, uint64_t plane_bit_count, uint64_t bit_offset,
    uint64_t bit_width, uint64_t delay, const uint8_t *value,
    const uint8_t *unknown);
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
void obelisk_rt_v1_scheduler_event(obelisk_rt_context *context,
                                   uint64_t stable_id, uint32_t nonblocking);
void obelisk_rt_v1_scheduler_fail(obelisk_rt_context *context,
                                  obelisk_rt_status status);
// Register one compiler-assigned static-state object. Static handles retain
// these root bounds when views apply signed offsets, so partial out-of-range
// accesses cannot spill into an adjacent object in the shared bit plane.
obelisk_rt_status obelisk_rt_v1_native_state_register_static(
    obelisk_rt_context *context, uint32_t id, uint64_t bit_offset,
    uint64_t bit_width);
uint64_t obelisk_rt_v1_native_state_static_handle(uint32_t id);
uint64_t obelisk_rt_v1_native_handle_offset(uint64_t handle, int64_t offset);
obelisk_rt_status obelisk_rt_v1_native_state_alloc(
    obelisk_rt_context *context, uint64_t bit_width, const uint8_t *value,
    const uint8_t *unknown, uint64_t *out_handle);
obelisk_rt_status obelisk_rt_v1_native_state_retain(
    obelisk_rt_context *context, uint64_t handle);
obelisk_rt_status obelisk_rt_v1_native_state_release(
    obelisk_rt_context *context, uint64_t handle, uint32_t owner_reference);
obelisk_rt_status obelisk_rt_v1_native_state_load_plane(
    obelisk_rt_context *context, const uint8_t *global_plane,
    uint64_t global_bit_count, uint64_t handle, uint64_t bit_width,
    uint32_t unknown_plane, uint32_t fallback, uint8_t *out_value);
obelisk_rt_status obelisk_rt_v1_native_state_store_plane(
    obelisk_rt_context *context, uint8_t *global_plane,
    uint64_t global_bit_count, uint64_t handle, uint64_t bit_width,
    uint32_t unknown_plane, const uint8_t *value, uint8_t *out_changed);
void obelisk_rt_v1_scheduler_notify(obelisk_rt_context *context);
obelisk_rt_status obelisk_rt_v1_scheduler_run(obelisk_rt_context *context);

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
  OBELISK_RT_ARG_TIME = 4
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
  uint32_t reserved;
  const char *time_suffix;
  uint64_t time_suffix_size;
  uint64_t time_multiplier;
} obelisk_rt_format_env_v1;

typedef uint32_t obelisk_rt_seek_origin;
enum {
  OBELISK_RT_SEEK_SET = 0,
  OBELISK_RT_SEEK_CUR = 1,
  OBELISK_RT_SEEK_END = 2
};

// Context and error handling. Operations on one live context may be called
// concurrently; last_error is isolated per calling thread. The caller must
// keep the context alive until all operations finish and must not race destroy
// with another runtime call.
obelisk_rt_status
obelisk_rt_v1_context_create(obelisk_rt_context **out_context);
uint32_t obelisk_rt_v1_import_id(const uint8_t *symbol,
                                 uint64_t symbol_size);
obelisk_rt_status obelisk_rt_v1_context_register_import(
    obelisk_rt_context *context, uint32_t import_id,
    obelisk_rt_import_callback_v1 callback, void *user_data);
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
obelisk_rt_status obelisk_rt_v1_file_eof(obelisk_rt_context *context,
                                         uint32_t descriptor,
                                         uint32_t *out_is_eof);
obelisk_rt_status obelisk_rt_v1_file_error(obelisk_rt_context *context,
                                           uint32_t descriptor,
                                           int32_t *out_error_code,
                                           obelisk_rt_buffer_v1 *out_message);
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
