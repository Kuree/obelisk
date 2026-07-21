//===- Runtime.h - Obelisk native runtime C ABI -----------------*- C -*-===//

#ifndef OBELISK_RUNTIME_RUNTIME_H
#define OBELISK_RUNTIME_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OBELISK_RT_ABI_VERSION 1u

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
  OBELISK_RT_DESCRIPTOR_FRAGMENT = 7
};

typedef struct obelisk_rt_handle_v1 {
  obelisk_rt_descriptor_kind kind;
  uint32_t generation;
  uint64_t id;
} obelisk_rt_handle_v1;

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

// No action or descriptor flags are defined in ABI v1. Callers and native
// fragments must initialize flags to this value.
#define OBELISK_RT_FRAGMENT_FLAGS_NONE UINT32_C(0)

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
  OBELISK_RT_BC_TYPE_BOOL = 3
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
  OBELISK_RT_BC_TERMINATE = 19
};

typedef struct obelisk_rt_bytecode_v1 {
  const uint8_t *code;
  uint64_t code_size;
  const struct obelisk_rt_bytecode_entry_v1 *entries;
  uint32_t entry_count;
  uint32_t register_count;
  uint64_t register_offset;
  // Generated immutable descriptors may point at a dedicated zero-initialized
  // validation record. The runtime validates the full entry table once and
  // then uses logarithmic continuation lookup. Null requests a full defensive
  // validation on every dispatch.
  struct obelisk_rt_bytecode_validation_v1 *validation;
} obelisk_rt_bytecode_v1;

typedef struct obelisk_rt_bytecode_entry_v1 {
  uint32_t continuation;
  uint32_t instruction;
} obelisk_rt_bytecode_entry_v1;

typedef struct obelisk_rt_bytecode_validation_v1 {
  // Runtime-owned state. Initialize the whole record to zero and never mutate
  // it or the associated bytecode descriptor after first dispatch.
  uint32_t state;
  uint32_t reserved;
} obelisk_rt_bytecode_validation_v1;

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
  const char *location;
  uint64_t location_size;
  uint32_t time_width;
  uint32_t reserved;
  const char *time_suffix;
  uint64_t time_suffix_size;
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
