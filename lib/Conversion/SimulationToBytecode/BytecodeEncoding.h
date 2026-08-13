//===- BytecodeEncoding.h - Bytecode instruction ABI names -----*- C++ -*-===//
//
// Private compiler names for the single runtime bytecode ABI.
//
//===----------------------------------------------------------------------===//

#ifndef OBELISK_LIB_CONVERSION_SIMULATIONTOBYTECODE_BYTECODEENCODING_H
#define OBELISK_LIB_CONVERSION_SIMULATIONTOBYTECODE_BYTECODEENCODING_H

#include "obelisk/Runtime/Runtime.h"

#include <cstdint>

namespace obelisk::bytecode {

constexpr uint32_t kInvalidRegister = UINT32_MAX;

constexpr uint32_t kIntrinsicDisplay = OBELISK_RT_INTRINSIC_V1_DISPLAY;
constexpr uint32_t kIntrinsicFinish = OBELISK_RT_INTRINSIC_V1_FINISH;
constexpr uint32_t kIntrinsicStop = OBELISK_RT_INTRINSIC_V1_STOP;
constexpr uint32_t kIntrinsicFatal = OBELISK_RT_INTRINSIC_V1_FATAL;
constexpr uint32_t kIntrinsicTerminationRequested =
    OBELISK_RT_INTRINSIC_V1_TERMINATION_REQUESTED;
constexpr uint32_t kIntrinsicTimeNow = OBELISK_RT_INTRINSIC_V1_TIME_NOW;
constexpr uint32_t kIntrinsicSampledRead =
    OBELISK_RT_INTRINSIC_V1_SAMPLED_READ;
constexpr uint32_t kIntrinsicSampledHistory =
    OBELISK_RT_INTRINSIC_V1_SAMPLED_HISTORY;
constexpr uint32_t kIntrinsicClockedSampleUpdate =
    OBELISK_RT_INTRINSIC_V1_CLOCKED_SAMPLE_UPDATE;
constexpr uint32_t kIntrinsicClockedSampleRead =
    OBELISK_RT_INTRINSIC_V1_CLOCKED_SAMPLE_READ;
constexpr uint32_t kIntrinsicTimeToReal = OBELISK_RT_INTRINSIC_V1_TIME_TO_REAL;
constexpr uint32_t kIntrinsicTimeFromReal =
    OBELISK_RT_INTRINSIC_V1_TIME_FROM_REAL;
constexpr uint32_t kIntrinsicRealFromInteger =
    OBELISK_RT_INTRINSIC_V1_REAL_FROM_INTEGER;
constexpr uint32_t kIntrinsicRealToInteger =
    OBELISK_RT_INTRINSIC_V1_REAL_TO_INTEGER;
constexpr uint32_t kIntrinsicCountBits = OBELISK_RT_INTRINSIC_V1_COUNT_BITS;
constexpr uint32_t kIntrinsicClog2 = OBELISK_RT_INTRINSIC_V1_CLOG2;
constexpr uint32_t kIntrinsicFileOpenMCD =
    OBELISK_RT_INTRINSIC_V1_FILE_OPEN_MCD;
constexpr uint32_t kIntrinsicFileOpen = OBELISK_RT_INTRINSIC_V1_FILE_OPEN;
constexpr uint32_t kIntrinsicFileOpenStringMCD =
    OBELISK_RT_INTRINSIC_V1_FILE_OPEN_STRING_MCD;
constexpr uint32_t kIntrinsicFileOpenString =
    OBELISK_RT_INTRINSIC_V1_FILE_OPEN_STRING;
constexpr uint32_t kIntrinsicFileGetlineString =
    OBELISK_RT_INTRINSIC_V1_FILE_GETLINE_STRING;
constexpr uint32_t kIntrinsicFileScanField =
    OBELISK_RT_INTRINSIC_V1_FILE_SCAN_FIELD;
constexpr uint32_t kIntrinsicFileReadMemToken =
    OBELISK_RT_INTRINSIC_V1_FILE_READMEM_TOKEN;
constexpr uint32_t kIntrinsicFileErrorString =
    OBELISK_RT_INTRINSIC_V1_FILE_ERROR_STRING;
constexpr uint32_t kIntrinsicTimeFormat = OBELISK_RT_INTRINSIC_V1_TIME_FORMAT;
constexpr uint32_t kIntrinsicPlusargTest = OBELISK_RT_INTRINSIC_V1_PLUSARG_TEST;
constexpr uint32_t kIntrinsicPlusargValue =
    OBELISK_RT_INTRINSIC_V1_PLUSARG_VALUE;
constexpr uint32_t kIntrinsicFileClose = OBELISK_RT_INTRINSIC_V1_FILE_CLOSE;
constexpr uint32_t kIntrinsicFileFlush = OBELISK_RT_INTRINSIC_V1_FILE_FLUSH;
constexpr uint32_t kIntrinsicFileGetc = OBELISK_RT_INTRINSIC_V1_FILE_GETC;
constexpr uint32_t kIntrinsicFileUngetc = OBELISK_RT_INTRINSIC_V1_FILE_UNGETC;
constexpr uint32_t kIntrinsicFileGetline = OBELISK_RT_INTRINSIC_V1_FILE_GETLINE;
constexpr uint32_t kIntrinsicFileReadPacked =
    OBELISK_RT_INTRINSIC_V1_FILE_READ_PACKED;
constexpr uint32_t kIntrinsicFileEof = OBELISK_RT_INTRINSIC_V1_FILE_EOF;
constexpr uint32_t kIntrinsicFileSeek = OBELISK_RT_INTRINSIC_V1_FILE_SEEK;
constexpr uint32_t kIntrinsicFileTell = OBELISK_RT_INTRINSIC_V1_FILE_TELL;
constexpr uint32_t kIntrinsicFileRewind = OBELISK_RT_INTRINSIC_V1_FILE_REWIND;
constexpr uint32_t kIntrinsicSpawn = OBELISK_RT_INTRINSIC_V1_SPAWN;
constexpr uint32_t kIntrinsicProcessCurrent =
    OBELISK_RT_INTRINSIC_V1_PROCESS_CURRENT;
constexpr uint32_t kIntrinsicProcessStatus =
    OBELISK_RT_INTRINSIC_V1_PROCESS_STATUS;
constexpr uint32_t kIntrinsicProcessRandomGet =
    OBELISK_RT_INTRINSIC_V1_PROCESS_RANDOM_GET;
constexpr uint32_t kIntrinsicProcessRandomSet =
    OBELISK_RT_INTRINSIC_V1_PROCESS_RANDOM_SET;
constexpr uint32_t kIntrinsicNBA = OBELISK_RT_INTRINSIC_V1_NBA;
constexpr uint32_t kIntrinsicStaticNBA = OBELISK_RT_INTRINSIC_V1_STATIC_NBA;
constexpr uint32_t kIntrinsicEventTrigger =
    OBELISK_RT_INTRINSIC_V1_EVENT_TRIGGER;
constexpr uint32_t kIntrinsicEventTriggered =
    OBELISK_RT_INTRINSIC_V1_EVENT_TRIGGERED;
constexpr uint32_t kIntrinsicStateAlloc = OBELISK_RT_INTRINSIC_V1_STATE_ALLOC;
constexpr uint32_t kIntrinsicStateAllocTyped =
    OBELISK_RT_INTRINSIC_V1_STATE_ALLOC_TYPED;
constexpr uint32_t kIntrinsicDisableChildren =
    OBELISK_RT_INTRINSIC_V1_DISABLE_CHILDREN;
constexpr uint32_t kIntrinsicControlEnter =
    OBELISK_RT_INTRINSIC_V1_CONTROL_ENTER;
constexpr uint32_t kIntrinsicControlLeave =
    OBELISK_RT_INTRINSIC_V1_CONTROL_LEAVE;
constexpr uint32_t kIntrinsicControlDisable =
    OBELISK_RT_INTRINSIC_V1_CONTROL_DISABLE;
constexpr uint32_t kIntrinsicStaticOnce = OBELISK_RT_INTRINSIC_V1_STATIC_ONCE;
constexpr uint32_t kIntrinsicDeferredOnce =
    OBELISK_RT_INTRINSIC_V1_DEFERRED_ONCE;
constexpr uint32_t kIntrinsicDeferredEnqueue =
    OBELISK_RT_INTRINSIC_V1_DEFERRED_ENQUEUE;
constexpr uint32_t kIntrinsicDeferredMature =
    OBELISK_RT_INTRINSIC_V1_DEFERRED_MATURE;
constexpr uint32_t kIntrinsicAssertionControl =
    OBELISK_RT_INTRINSIC_V1_ASSERTION_CONTROL;
constexpr uint32_t kIntrinsicAssertionEnabled =
    OBELISK_RT_INTRINSIC_V1_ASSERTION_ENABLED;
constexpr uint32_t kIntrinsicAssertionActionState =
    OBELISK_RT_INTRINSIC_V1_ASSERTION_ACTION_STATE;
constexpr uint32_t kIntrinsicMonitorRegister =
    OBELISK_RT_INTRINSIC_V1_MONITOR_REGISTER;
constexpr uint32_t kIntrinsicMonitorControl =
    OBELISK_RT_INTRINSIC_V1_MONITOR_CONTROL;
constexpr uint32_t kIntrinsicMonitorCurrent =
    OBELISK_RT_INTRINSIC_V1_MONITOR_CURRENT;
constexpr uint32_t kIntrinsicDumpOpen = OBELISK_RT_INTRINSIC_V1_DUMP_OPEN;
constexpr uint32_t kIntrinsicDumpOpenString =
    OBELISK_RT_INTRINSIC_V1_DUMP_OPEN_STRING;
constexpr uint32_t kIntrinsicDumpTimescale =
    OBELISK_RT_INTRINSIC_V1_DUMP_TIMESCALE;
constexpr uint32_t kIntrinsicDumpVars = OBELISK_RT_INTRINSIC_V1_DUMP_VARS;
constexpr uint32_t kIntrinsicDumpAll = OBELISK_RT_INTRINSIC_V1_DUMP_ALL;
constexpr uint32_t kIntrinsicDumpControl = OBELISK_RT_INTRINSIC_V1_DUMP_CONTROL;
constexpr uint32_t kIntrinsicDumpLimit = OBELISK_RT_INTRINSIC_V1_DUMP_LIMIT;
constexpr uint32_t kIntrinsicDumpFlush = OBELISK_RT_INTRINSIC_V1_DUMP_FLUSH;
constexpr uint32_t kIntrinsicImport = OBELISK_RT_INTRINSIC_V1_IMPORT;
constexpr uint32_t kIntrinsicDPIImport = OBELISK_RT_INTRINSIC_V1_DPI_IMPORT;
constexpr uint32_t kIntrinsicClassAlloc = OBELISK_RT_INTRINSIC_V1_CLASS_ALLOC;
constexpr uint32_t kIntrinsicClassCopy = OBELISK_RT_INTRINSIC_V1_CLASS_COPY;
constexpr uint32_t kIntrinsicClassIsInstance =
    OBELISK_RT_INTRINSIC_V1_CLASS_IS_INSTANCE;
constexpr uint32_t kIntrinsicClassCast = OBELISK_RT_INTRINSIC_V1_CLASS_CAST;
constexpr uint32_t kIntrinsicClassFieldRef =
    OBELISK_RT_INTRINSIC_V1_CLASS_FIELD_REF;
constexpr uint32_t kIntrinsicManagedLoad = OBELISK_RT_INTRINSIC_V1_MANAGED_LOAD;
constexpr uint32_t kIntrinsicManagedStore =
    OBELISK_RT_INTRINSIC_V1_MANAGED_STORE;
constexpr uint32_t kIntrinsicManagedNBA = OBELISK_RT_INTRINSIC_V1_MANAGED_NBA;
constexpr uint32_t kIntrinsicWeakCreate = OBELISK_RT_INTRINSIC_V1_WEAK_CREATE;
constexpr uint32_t kIntrinsicWeakGet = OBELISK_RT_INTRINSIC_V1_WEAK_GET;
constexpr uint32_t kIntrinsicWeakClear = OBELISK_RT_INTRINSIC_V1_WEAK_CLEAR;
constexpr uint32_t kIntrinsicGCSafepoint = OBELISK_RT_INTRINSIC_V1_GC_SAFEPOINT;
constexpr uint32_t kIntrinsicClassID = OBELISK_RT_INTRINSIC_V1_CLASS_ID;
constexpr uint32_t kIntrinsicCovergroupCreate =
    OBELISK_RT_INTRINSIC_V1_COVERGROUP_CREATE;
constexpr uint32_t kIntrinsicCovergroupSetEnabled =
    OBELISK_RT_INTRINSIC_V1_COVERGROUP_SET_ENABLED;
constexpr uint32_t kIntrinsicCovergroupSampleEnabled =
    OBELISK_RT_INTRINSIC_V1_COVERGROUP_SAMPLE_ENABLED;
constexpr uint32_t kIntrinsicCovergroupBinHit =
    OBELISK_RT_INTRINSIC_V1_COVERGROUP_BIN_HIT;
constexpr uint32_t kIntrinsicCovergroupSample =
    OBELISK_RT_INTRINSIC_V1_COVERGROUP_SAMPLE;
constexpr uint32_t kIntrinsicCovergroupInstanceQuery =
    OBELISK_RT_INTRINSIC_V1_COVERGROUP_INSTANCE_QUERY;
constexpr uint32_t kIntrinsicCovergroupTypeQuery =
    OBELISK_RT_INTRINSIC_V1_COVERGROUP_TYPE_QUERY;
constexpr uint32_t kIntrinsicArgumentRefFromRef =
    OBELISK_RT_INTRINSIC_V1_ARGUMENT_REF_FROM_REF;
constexpr uint32_t kIntrinsicArgumentRefFromManaged =
    OBELISK_RT_INTRINSIC_V1_ARGUMENT_REF_FROM_MANAGED;
constexpr uint32_t kIntrinsicArgumentRefLoad =
    OBELISK_RT_INTRINSIC_V1_ARGUMENT_REF_LOAD;
constexpr uint32_t kIntrinsicArgumentRefStore =
    OBELISK_RT_INTRINSIC_V1_ARGUMENT_REF_STORE;
constexpr uint32_t kIntrinsicManagedRootExtract =
    OBELISK_RT_INTRINSIC_V1_MANAGED_ROOT_EXTRACT;
constexpr uint32_t kIntrinsicManagedCandidateRoot =
    OBELISK_RT_INTRINSIC_V1_MANAGED_CANDIDATE_ROOT;
constexpr uint32_t kIntrinsicReferencePathIndex =
    OBELISK_RT_INTRINSIC_V1_REFERENCE_PATH_INDEX;
constexpr uint32_t kIntrinsicArgumentRefFromPath =
    OBELISK_RT_INTRINSIC_V1_ARGUMENT_REF_FROM_PATH;
constexpr uint32_t kIntrinsicStringLiteral =
    OBELISK_RT_INTRINSIC_V1_STRING_LITERAL;
constexpr uint32_t kIntrinsicStringFromPacked =
    OBELISK_RT_INTRINSIC_V1_STRING_FROM_PACKED;
constexpr uint32_t kIntrinsicStringToPacked =
    OBELISK_RT_INTRINSIC_V1_STRING_TO_PACKED;
constexpr uint32_t kIntrinsicStringConcat =
    OBELISK_RT_INTRINSIC_V1_STRING_CONCAT;
constexpr uint32_t kIntrinsicStringRepeat =
    OBELISK_RT_INTRINSIC_V1_STRING_REPEAT;
constexpr uint32_t kIntrinsicStringLength =
    OBELISK_RT_INTRINSIC_V1_STRING_LENGTH;
constexpr uint32_t kIntrinsicStringGetc = OBELISK_RT_INTRINSIC_V1_STRING_GETC;
constexpr uint32_t kIntrinsicStringPutc = OBELISK_RT_INTRINSIC_V1_STRING_PUTC;
constexpr uint32_t kIntrinsicStringSubstr =
    OBELISK_RT_INTRINSIC_V1_STRING_SUBSTR;
constexpr uint32_t kIntrinsicStringCompare =
    OBELISK_RT_INTRINSIC_V1_STRING_COMPARE;
constexpr uint32_t kIntrinsicStringCaseConvert =
    OBELISK_RT_INTRINSIC_V1_STRING_CASE_CONVERT;
constexpr uint32_t kIntrinsicStringScanField =
    OBELISK_RT_INTRINSIC_V1_STRING_SCAN_FIELD;
constexpr uint32_t kIntrinsicStringParseInteger =
    OBELISK_RT_INTRINSIC_V1_STRING_PARSE_INTEGER;
constexpr uint32_t kIntrinsicStringParseReal =
    OBELISK_RT_INTRINSIC_V1_STRING_PARSE_REAL;
constexpr uint32_t kIntrinsicStringFormatInteger =
    OBELISK_RT_INTRINSIC_V1_STRING_FORMAT_INTEGER;
constexpr uint32_t kIntrinsicStringFormatReal =
    OBELISK_RT_INTRINSIC_V1_STRING_FORMAT_REAL;
constexpr uint32_t kIntrinsicContainerSize =
    OBELISK_RT_INTRINSIC_V1_CONTAINER_SIZE;
constexpr uint32_t kIntrinsicContainerCreateLike =
    OBELISK_RT_INTRINSIC_V1_CONTAINER_CREATE_LIKE;
constexpr uint32_t kIntrinsicContainerRead =
    OBELISK_RT_INTRINSIC_V1_CONTAINER_READ;
constexpr uint32_t kIntrinsicContainerWrite =
    OBELISK_RT_INTRINSIC_V1_CONTAINER_WRITE;
constexpr uint32_t kIntrinsicContainerCreate =
    OBELISK_RT_INTRINSIC_V1_CONTAINER_CREATE;
constexpr uint32_t kIntrinsicContainerClone =
    OBELISK_RT_INTRINSIC_V1_CONTAINER_CLONE;
constexpr uint32_t kIntrinsicContainerDelete =
    OBELISK_RT_INTRINSIC_V1_CONTAINER_DELETE;
constexpr uint32_t kIntrinsicRandomBounded =
    OBELISK_RT_INTRINSIC_V1_RANDOM_BOUNDED;
constexpr uint32_t kIntrinsicRandomDistribution =
    OBELISK_RT_INTRINSIC_V1_RANDOM_DISTRIBUTION;
constexpr uint32_t kIntrinsicRandomSolve = OBELISK_RT_INTRINSIC_V1_RANDOM_SOLVE;
constexpr uint32_t kIntrinsicRandomSolveState =
    OBELISK_RT_INTRINSIC_V1_RANDOM_SOLVE_STATE;
constexpr uint32_t kIntrinsicRandomSolveWideState =
    OBELISK_RT_INTRINSIC_V1_RANDOM_SOLVE_WIDE_STATE;
constexpr uint32_t kIntrinsicRandomCycleNext =
    OBELISK_RT_INTRINSIC_V1_RANDOM_CYCLE_NEXT;
constexpr uint32_t kIntrinsicRandomNext = OBELISK_RT_INTRINSIC_V1_RANDOM_NEXT;
constexpr uint32_t kIntrinsicRandomSeed = OBELISK_RT_INTRINSIC_V1_RANDOM_SEED;
constexpr uint32_t kIntrinsicQueueDelete = OBELISK_RT_INTRINSIC_V1_QUEUE_DELETE;
constexpr uint32_t kIntrinsicQueueInsert = OBELISK_RT_INTRINSIC_V1_QUEUE_INSERT;
constexpr uint32_t kIntrinsicMailboxCreate =
    OBELISK_RT_INTRINSIC_V1_MAILBOX_CREATE;
constexpr uint32_t kIntrinsicMailboxNum = OBELISK_RT_INTRINSIC_V1_MAILBOX_NUM;
constexpr uint32_t kIntrinsicMailboxTryPut =
    OBELISK_RT_INTRINSIC_V1_MAILBOX_TRY_PUT;
constexpr uint32_t kIntrinsicMailboxTryPeek =
    OBELISK_RT_INTRINSIC_V1_MAILBOX_TRY_PEEK;
constexpr uint32_t kIntrinsicMailboxTryGet =
    OBELISK_RT_INTRINSIC_V1_MAILBOX_TRY_GET;
constexpr uint32_t kIntrinsicSemaphoreCreate =
    OBELISK_RT_INTRINSIC_V1_SEMAPHORE_CREATE;
constexpr uint32_t kIntrinsicSemaphorePut =
    OBELISK_RT_INTRINSIC_V1_SEMAPHORE_PUT;
constexpr uint32_t kIntrinsicSemaphoreTryGet =
    OBELISK_RT_INTRINSIC_V1_SEMAPHORE_TRY_GET;
constexpr uint32_t kIntrinsicAssocCreate = OBELISK_RT_INTRINSIC_V1_ASSOC_CREATE;
constexpr uint32_t kIntrinsicAssocRead = OBELISK_RT_INTRINSIC_V1_ASSOC_READ;
constexpr uint32_t kIntrinsicAssocWrite = OBELISK_RT_INTRINSIC_V1_ASSOC_WRITE;
constexpr uint32_t kIntrinsicAssocExists = OBELISK_RT_INTRINSIC_V1_ASSOC_EXISTS;
constexpr uint32_t kIntrinsicAssocDelete = OBELISK_RT_INTRINSIC_V1_ASSOC_DELETE;
constexpr uint32_t kIntrinsicAssocDefault =
    OBELISK_RT_INTRINSIC_V1_ASSOC_DEFAULT;
constexpr uint32_t kIntrinsicAssocTraverse =
    OBELISK_RT_INTRINSIC_V1_ASSOC_TRAVERSE;
constexpr uint32_t kIntrinsicReferencePathAssoc =
    OBELISK_RT_INTRINSIC_V1_REFERENCE_PATH_ASSOC;

enum Opcode : uint16_t {
  Nop = OBELISK_RT_DB_NOP,
  Constant = OBELISK_RT_DB_CONSTANT,
  Move = OBELISK_RT_DB_MOVE,
  Not = OBELISK_RT_DB_NOT,
  And = OBELISK_RT_DB_AND,
  Or = OBELISK_RT_DB_OR,
  Xor = OBELISK_RT_DB_XOR,
  Add = OBELISK_RT_DB_ADD,
  Sub = OBELISK_RT_DB_SUB,
  Mul = OBELISK_RT_DB_MUL,
  UDiv = OBELISK_RT_DB_UDIV,
  SDiv = OBELISK_RT_DB_SDIV,
  URem = OBELISK_RT_DB_UREM,
  SRem = OBELISK_RT_DB_SREM,
  Shl = OBELISK_RT_DB_SHL,
  LShr = OBELISK_RT_DB_LSHR,
  AShr = OBELISK_RT_DB_ASHR,
  Compare = OBELISK_RT_DB_COMPARE,
  Select = OBELISK_RT_DB_SELECT,
  Reduce = OBELISK_RT_DB_REDUCE,
  Concat = OBELISK_RT_DB_CONCAT,
  Extract = OBELISK_RT_DB_EXTRACT,
  Insert = OBELISK_RT_DB_INSERT,
  LoadFrame = OBELISK_RT_DB_LOAD_FRAME,
  StoreFrame = OBELISK_RT_DB_STORE_FRAME,
  MakeHandle = OBELISK_RT_DB_MAKE_HANDLE,
  HandleOffset = OBELISK_RT_DB_HANDLE_OFFSET,
  LoadState = OBELISK_RT_DB_LOAD_STATE,
  StoreState = OBELISK_RT_DB_STORE_STATE,
  Jump = OBELISK_RT_DB_JUMP,
  Branch = OBELISK_RT_DB_BRANCH,
  Call = OBELISK_RT_DB_CALL,
  Return = OBELISK_RT_DB_RETURN,
  Continue = OBELISK_RT_DB_CONTINUE,
  Suspend = OBELISK_RT_DB_SUSPEND,
  Terminate = OBELISK_RT_DB_TERMINATE,
  Intrinsic = OBELISK_RT_DB_INTRINSIC,
  Fail = OBELISK_RT_DB_FAIL,
  MakeLocalHandle = OBELISK_RT_DB_MAKE_LOCAL_HANDLE,
  HandleID = OBELISK_RT_DB_HANDLE_ID,
  TaskCall = OBELISK_RT_DB_TASK_CALL,
  VirtualCall = OBELISK_RT_DB_VIRTUAL_CALL,
  ClearFrameRoot = OBELISK_RT_DB_CLEAR_FRAME_ROOT,
  OverrideState = OBELISK_RT_DB_OVERRIDE_STATE,
  ReleaseState = OBELISK_RT_DB_RELEASE_STATE,
  FAdd = OBELISK_RT_DB_FADD,
  FSub = OBELISK_RT_DB_FSUB,
  FMul = OBELISK_RT_DB_FMUL,
  FDiv = OBELISK_RT_DB_FDIV,
  FNeg = OBELISK_RT_DB_FNEG,
  FCompare = OBELISK_RT_DB_FCOMPARE,
  FExt = OBELISK_RT_DB_FEXT,
  FTrunc = OBELISK_RT_DB_FTRUNC,
  FPow = OBELISK_RT_DB_FPOW,
  VirtualTaskCall = OBELISK_RT_DB_VIRTUAL_TASK_CALL,
  FrameRoot = OBELISK_RT_DB_FRAME_ROOT,
  InterfaceCall = OBELISK_RT_DB_INTERFACE_CALL,
  InterfaceTaskCall = OBELISK_RT_DB_INTERFACE_TASK_CALL,
  ProcessControl = OBELISK_RT_DB_PROCESS_CONTROL,
};

} // namespace obelisk::bytecode

#endif // OBELISK_LIB_CONVERSION_SIMULATIONTOBYTECODE_BYTECODEENCODING_H
