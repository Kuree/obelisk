//===- SimulationToBytecode.cpp - Strict design-wide bytecode encoder -----===//

#include "obelisk/Conversion/SimulationToBytecode.h"

#include "obelisk/Analysis/NetConnectivityAnalysis.h"
#include "obelisk/Analysis/StateDomainAnalysis.h"
#include "obelisk/Conversion/SimulationToLLVMCoroutine.h"
#include "obelisk/Dialect/Runtime/RuntimeTypes.h"
#include "obelisk/Runtime/Runtime.h"

#include "mlir/Analysis/Liveness.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/Math/IR/Math.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "mlir/Pass/Pass.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/TypeSwitch.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/Error.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <string>

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_ENCODEOBELISKSIMTOBYTECODEPASS
#include "obelisk/Conversion/Passes.h.inc"

namespace {

constexpr uint32_t kExecutionHasBytecode = OBELISK_RT_EXECUTION_HAS_BYTECODE;
constexpr uint32_t kExecutionHasDatabase =
    OBELISK_RT_EXECUTION_HAS_DESIGN_DATABASE;
constexpr uint32_t kExecutionVPIRead = OBELISK_RT_EXECUTION_VPI_READ;
constexpr uint32_t kExecutionVPIWrite = OBELISK_RT_EXECUTION_VPI_WRITE;
constexpr uint32_t kExecutionRequireBytecode =
    OBELISK_RT_EXECUTION_REQUIRE_BYTECODE;
constexpr uint32_t kDatabaseProfileRead = OBELISK_RT_DESIGN_PROFILE_READ;
constexpr uint32_t kDatabaseProfileWrite = OBELISK_RT_DESIGN_PROFILE_WRITE;
constexpr uint32_t kInvalidRegister = UINT32_MAX;

uint32_t executableRegion(sim::EventRegion region) {
  switch (region) {
  case sim::EventRegion::Active:
    return OBELISK_RT_REGION_ACTIVE;
  case sim::EventRegion::Observed:
    return OBELISK_RT_REGION_OBSERVED;
  case sim::EventRegion::Reactive:
    return OBELISK_RT_REGION_REACTIVE;
  case sim::EventRegion::Postponed:
    return OBELISK_RT_REGION_POSTPONED;
  default:
    return UINT32_MAX;
  }
}
constexpr uint32_t kIntrinsicDisplay = OBELISK_RT_INTRINSIC_V1_DISPLAY;
constexpr uint32_t kIntrinsicFinish = OBELISK_RT_INTRINSIC_V1_FINISH;
constexpr uint32_t kIntrinsicFatal = OBELISK_RT_INTRINSIC_V1_FATAL;
constexpr uint32_t kIntrinsicTerminationRequested =
    OBELISK_RT_INTRINSIC_V1_TERMINATION_REQUESTED;
constexpr uint32_t kIntrinsicTimeNow = OBELISK_RT_INTRINSIC_V1_TIME_NOW;
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
constexpr uint32_t kIntrinsicNBA = OBELISK_RT_INTRINSIC_V1_NBA;
constexpr uint32_t kIntrinsicEventTrigger =
    OBELISK_RT_INTRINSIC_V1_EVENT_TRIGGER;
constexpr uint32_t kIntrinsicEventTriggered =
    OBELISK_RT_INTRINSIC_V1_EVENT_TRIGGERED;
constexpr uint32_t kIntrinsicStateAlloc = OBELISK_RT_INTRINSIC_V1_STATE_ALLOC;
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
constexpr uint32_t kIntrinsicMonitorRegister =
    OBELISK_RT_INTRINSIC_V1_MONITOR_REGISTER;
constexpr uint32_t kIntrinsicMonitorControl =
    OBELISK_RT_INTRINSIC_V1_MONITOR_CONTROL;
constexpr uint32_t kIntrinsicMonitorCurrent =
    OBELISK_RT_INTRINSIC_V1_MONITOR_CURRENT;
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
constexpr uint32_t kIntrinsicRandomNext =
    OBELISK_RT_INTRINSIC_V1_RANDOM_NEXT;
constexpr uint32_t kIntrinsicRandomSeed =
    OBELISK_RT_INTRINSIC_V1_RANDOM_SEED;
constexpr uint32_t kIntrinsicQueueDelete =
    OBELISK_RT_INTRINSIC_V1_QUEUE_DELETE;
constexpr uint32_t kIntrinsicQueueInsert =
    OBELISK_RT_INTRINSIC_V1_QUEUE_INSERT;
constexpr uint32_t kIntrinsicAssocCreate =
    OBELISK_RT_INTRINSIC_V1_ASSOC_CREATE;
constexpr uint32_t kIntrinsicAssocRead = OBELISK_RT_INTRINSIC_V1_ASSOC_READ;
constexpr uint32_t kIntrinsicAssocWrite = OBELISK_RT_INTRINSIC_V1_ASSOC_WRITE;
constexpr uint32_t kIntrinsicAssocExists =
    OBELISK_RT_INTRINSIC_V1_ASSOC_EXISTS;
constexpr uint32_t kIntrinsicAssocDelete =
    OBELISK_RT_INTRINSIC_V1_ASSOC_DELETE;
constexpr uint32_t kIntrinsicAssocDefault =
    OBELISK_RT_INTRINSIC_V1_ASSOC_DEFAULT;
constexpr uint32_t kIntrinsicAssocTraverse =
    OBELISK_RT_INTRINSIC_V1_ASSOC_TRAVERSE;
constexpr uint32_t kIntrinsicReferencePathAssoc =
    OBELISK_RT_INTRINSIC_V1_REFERENCE_PATH_ASSOC;

bool isObserverCaptureBridge(Block &block) {
  if (block.getOperations().size() != 1)
    return false;
  auto branch = dyn_cast<cf::BranchOp>(block.getTerminator());
  return branch && branch->hasAttr("obelisk_sim.observer_capture_bridge");
}

uint32_t resumeActionFlags(Operation *operation) {
  auto region = operation->getAttrOfType<sim::EventRegionAttr>("resume_region");
  if (!region)
    return 0;
  uint32_t ordinal = executableRegion(region.getValue());
  return ordinal == UINT32_MAX ? UINT32_MAX
                               : OBELISK_RT_ACTION_RESUME_REGION(ordinal);
}

Block *lookupComputeGraphBlock(sim::SimFuncOp function, uint32_t ordinal) {
  for (Block &block : function.getBody()) {
    if (isObserverCaptureBridge(block))
      continue;
    if (ordinal-- == 0)
      return &block;
  }
  return nullptr;
}

enum RegisterKind : uint8_t {
  Invalid = OBELISK_RT_DBREG_INVALID,
  Bits = OBELISK_RT_DBREG_BITS,
  Logic = OBELISK_RT_DBREG_LOGIC,
  Handle = OBELISK_RT_DBREG_HANDLE,
  Status = OBELISK_RT_DBREG_STATUS,
  Resource = OBELISK_RT_DBREG_RESOURCE,
  Bytes = OBELISK_RT_DBREG_BYTES,
  Managed = OBELISK_RT_DBREG_MANAGED,
  ManagedRef = OBELISK_RT_DBREG_MANAGED_REF,
  ArgumentRef = OBELISK_RT_DBREG_ARGUMENT_REF,
  String = OBELISK_RT_DBREG_STRING,
  Real32 = OBELISK_RT_DBREG_REAL32,
  Real64 = OBELISK_RT_DBREG_REAL64,
};

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
};

struct Layout {
  uint8_t kind = Invalid;
  uint8_t flags = 0;
  uint32_t width = 0;
  uint64_t offset = 0;
  uint64_t size = 0;
  uint64_t auxiliary = 0;
};

bool isManagedAggregateWord(uint8_t kind) {
  return kind == Managed || kind == String;
}

struct Instruction {
  uint16_t opcode = Nop;
  uint16_t flags = 0;
  uint32_t destination = 0;
  uint32_t source0 = 0;
  uint32_t source1 = 0;
  uint32_t source2 = 0;
  uint32_t auxiliary = 0;
  uint64_t immediate = 0;
};

struct OperandMap {
  uint32_t destination;
  uint32_t source;
};

struct Continuation {
  uint32_t function;
  uint32_t id;
  uint64_t instruction;
  uint32_t scheduleRank;
};

struct IntrinsicSignature {
  uint32_t id;
  uint32_t inputCount;
  uint32_t outputCount;
  uint32_t flags;
};

struct IntrinsicSite {
  uint32_t intrinsic;
  uint32_t firstOperand;
  uint32_t inputCount;
  uint32_t outputCount;
};

struct CaptureRecord {
  uint32_t function;
  uint32_t argument;
  uint64_t valueOffset;
  uint64_t unknownOffset;
  uint64_t planeSize;
};

struct FunctionPlan {
  sim::SimFuncOp function;
  uint32_t index = 0;
  uint64_t stableID = 0;
  llvm::MapVector<Value, uint32_t> registers;
  SmallVector<Layout> layouts;
  SmallVector<uint32_t> resultRegisters;
  DenseMap<Block *, uint64_t> blockPCs;
  SmallVector<std::pair<uint64_t, Block *>> branches;
  SmallVector<Continuation> continuations;
  std::unique_ptr<SimulationProcessFrameAnalysis> frame;
  uint64_t firstInstruction = 0;
  uint64_t instructionCount = 0;
  uint64_t scratchSize = 0;
  uint64_t scratchAlignment = 8;
  uint32_t twoStateLogicRegisters = 0;
  uint32_t initialScheduleRank = UINT32_MAX;
  DenseMap<Block *, uint32_t> blockScheduleRanks;
  std::unique_ptr<Liveness> liveness;
  struct ManagedRootShadow {
    Value value;
    uint64_t bitOffset;
    uint32_t reg;
  };
  SmallVector<ManagedRootShadow> managedRootShadows;
};

struct StateLayout {
  struct Net {
    uint64_t id;
    uint64_t offset;
    uint32_t width;
    bool fourState;
    sim::NetResolutionKind resolution;
  };
  struct Driver {
    uint64_t id;
    uint64_t netID;
    uint64_t offset;
    uint64_t netOffset;
    uint32_t width;
    uint32_t drivenLow;
    uint32_t drivenWidth;
    bool fourState;
    sim::NetResolutionKind resolution;
  };
  struct Connection {
    uint64_t lhsOffset;
    uint64_t rhsOffset;
    uint64_t width;
    sim::NetResolutionKind lhsResolution;
    sim::NetResolutionKind rhsResolution;
    bool rhsReversed;
  };
  DenseMap<uint64_t, uint64_t> storage;
  DenseMap<uint64_t, uint64_t> nets;
  DenseMap<uint64_t, uint64_t> drivers;
  SmallVector<Net> netLayouts;
  SmallVector<Driver> driverLayouts;
  SmallVector<Connection> connections;
  uint64_t bits = 0;
};

void append16(SmallVectorImpl<uint8_t> &output, uint16_t value) {
  output.push_back(static_cast<uint8_t>(value));
  output.push_back(static_cast<uint8_t>(value >> 8));
}
void append32(SmallVectorImpl<uint8_t> &output, uint32_t value) {
  for (unsigned byte = 0; byte != 4; ++byte)
    output.push_back(static_cast<uint8_t>(value >> (byte * 8)));
}
void append64(SmallVectorImpl<uint8_t> &output, uint64_t value) {
  for (unsigned byte = 0; byte != 8; ++byte)
    output.push_back(static_cast<uint8_t>(value >> (byte * 8)));
}
void write32(SmallVectorImpl<uint8_t> &output, uint64_t offset,
             uint32_t value) {
  for (unsigned byte = 0; byte != 4; ++byte)
    output[offset + byte] = static_cast<uint8_t>(value >> (byte * 8));
}
void write64(SmallVectorImpl<uint8_t> &output, uint64_t offset,
             uint64_t value) {
  for (unsigned byte = 0; byte != 8; ++byte)
    output[offset + byte] = static_cast<uint8_t>(value >> (byte * 8));
}
void alignTo(SmallVectorImpl<uint8_t> &output, uint64_t alignment) {
  while (output.size() % alignment != 0)
    output.push_back(0);
}

uint64_t checksum(ArrayRef<uint8_t> data, uint64_t checksumOffset) {
  uint64_t hash = UINT64_C(14695981039346656037);
  for (auto [index, byte] : llvm::enumerate(data)) {
    uint8_t hashed =
        index >= checksumOffset && index < checksumOffset + 8 ? 0 : byte;
    hash ^= hashed;
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

uint64_t stableHash(StringRef text) {
  uint64_t hash = UINT64_C(14695981039346656037);
  for (unsigned char byte : text.bytes()) {
    hash ^= byte;
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

uint32_t stableImportID(StringRef text) {
  uint64_t hash = stableHash(text);
  uint32_t result = static_cast<uint32_t>(hash ^ (hash >> 32));
  return result == 0 ? 1 : result;
}

bool containsLogic(Type type) {
  if (sim::isManagedHandleType(type))
    return false;
  bool found = false;
  type.walk([&](sim::LogicType) { found = true; });
  return found;
}

std::optional<uint32_t> simulationWidth(Type type) {
  if (sim::isManagedHandleType(type))
    return 64;
  if (std::optional<unsigned> packed = sim::getPackedWidth(type))
    return *packed;
  std::optional<uint64_t> span = sim::getProvenanceSpan(type);
  if (auto unionType = dyn_cast<sim::UnpackedUnionType>(type);
      unionType && unionType.getIsTagged() && span) {
    uint64_t tagBits = llvm::Log2_64_Ceil(
        static_cast<uint64_t>(sim::getAggregateNumElements(type)) + 1);
    if (tagBits > UINT64_MAX - *span)
      return std::nullopt;
    *span += tagBits;
  }
  if (!span || *span == 0 || *span > UINT32_MAX)
    return std::nullopt;
  return static_cast<uint32_t>(*span);
}

std::optional<uint64_t> unionPayloadSpan(Type type) {
  if (auto packed = dyn_cast<sim::PackedUnionType>(type)) {
    std::optional<unsigned> width = sim::getPackedWidth(type);
    if (!width || packed.getTagBits() > *width)
      return std::nullopt;
    return static_cast<uint64_t>(*width - packed.getTagBits());
  }
  if (isa<sim::UnpackedUnionType>(type))
    return sim::getProvenanceSpan(type);
  return std::nullopt;
}

FailureOr<Layout> getLayout(Type type) {
  Layout layout;
  if (auto integer = dyn_cast<IntegerType>(type)) {
    layout.kind = Bits;
    layout.width = integer.getWidth();
    layout.flags = integer.isSigned() ? 1 : 0;
  } else if (type.isF32()) {
    layout.kind = Real32;
    layout.width = 32;
  } else if (type.isF64()) {
    layout.kind = Real64;
    layout.width = 64;
  } else if (auto logic = dyn_cast<sim::LogicType>(type)) {
    layout.kind = Logic;
    layout.width = logic.getWidth();
  } else if (isa<sim::TimeType>(type)) {
    layout.kind = Bits;
    layout.width = 64;
  } else if (isa<sim::ControlType>(type)) {
    layout.kind = Bits;
    layout.width = 64;
  } else if (isa<sim::RefType, sim::NetType, sim::DriverType, sim::EventType,
                 sim::ProcessType, sim::ContextType, sim::ObserverType,
                 runtime::ContextType>(type)) {
    layout.kind = Handle;
    layout.width = 256;
  } else if (isa<runtime::StatusType>(type)) {
    layout.kind = Status;
    layout.width = 64;
  } else if (isa<sim::BytesType>(type)) {
    layout.kind = Bytes;
    layout.width = 128;
  } else if (isa<sim::StringType>(type)) {
    layout.kind = String;
    layout.width = 64;
  } else if (sim::isManagedHandleType(type)) {
    layout.kind = Managed;
    layout.width = 64;
  } else if (isa<sim::ManagedRefType>(type)) {
    layout.kind = ManagedRef;
    layout.width = 128;
  } else if (isa<sim::ArgumentRefType>(type)) {
    layout.kind = ArgumentRef;
    layout.width = 192;
  } else if (std::optional<uint32_t> width = simulationWidth(type)) {
    layout.kind = containsLogic(type) ? Logic : Bits;
    layout.width = static_cast<uint32_t>(*width);
  } else {
    return failure();
  }
  uint64_t limbs = (uint64_t{layout.width} + 63) / 64;
  switch (layout.kind) {
  case Bits:
    layout.size = limbs * 8;
    break;
  case Logic:
    layout.size = limbs * 16;
    break;
  case Handle:
    layout.size = 32;
    break;
  case Status:
  case Resource:
    layout.size = 8;
    break;
  case Bytes:
    layout.size = 16;
    break;
  case Managed:
  case String:
    layout.size = 8;
    break;
  case Real32:
    layout.size = 4;
    break;
  case Real64:
    layout.size = 8;
    break;
  case ManagedRef:
    layout.size = 16;
    break;
  case ArgumentRef:
    layout.size = 24;
    break;
  default:
    return failure();
  }
  return layout;
}

struct ManagedValueStorage {
  uint64_t planeSize;
  uint32_t alignment;
  bool fourState;
};

FailureOr<ManagedValueStorage>
getManagedValueStorage(Type type, const llvm::DataLayout &dataLayout) {
  llvm::LLVMContext llvmContext;
  llvm::Type *nativeType = nullptr;
  bool fourState = containsLogic(type);
  if (auto logic = dyn_cast<sim::LogicType>(type))
    nativeType = llvm::IntegerType::get(llvmContext, logic.getWidth());
  else if (auto integer = dyn_cast<IntegerType>(type))
    nativeType = llvm::IntegerType::get(llvmContext, integer.getWidth());
  else if (type.isF32())
    nativeType = llvm::Type::getFloatTy(llvmContext);
  else if (type.isF64())
    nativeType = llvm::Type::getDoubleTy(llvmContext);
  else if (isa<sim::TimeType>(type))
    nativeType = llvm::Type::getInt64Ty(llvmContext);
  else if (sim::isManagedHandleType(type))
    nativeType = llvm::PointerType::get(llvmContext, 0);
  else if (std::optional<uint32_t> width = simulationWidth(type))
    nativeType = llvm::IntegerType::get(llvmContext, *width);
  if (!nativeType)
    return failure();
  llvm::TypeSize nativeSize = dataLayout.getTypeAllocSize(nativeType);
  uint64_t planeSize = nativeSize.isScalable() ? 0 : nativeSize.getFixedValue();
  uint32_t alignment =
      static_cast<uint32_t>(dataLayout.getABITypeAlign(nativeType).value());
  if (planeSize == 0 || alignment == 0 || (alignment & (alignment - 1)) != 0)
    return failure();
  return ManagedValueStorage{planeSize, alignment, fourState};
}

FailureOr<StateLayout> buildStateLayout(sim::SimDesignOp design) {
  StateLayout result;
  auto allocate = [&](Type type, uint64_t &offset) -> LogicalResult {
    std::optional<uint32_t> width = simulationWidth(type);
    SmallVector<uint64_t, 2> managedRootOffsets;
    if (!sim::getManagedHandleOffsets(type, managedRootOffsets))
      return failure();
    if (!managedRootOffsets.empty())
      result.bits = llvm::alignTo(result.bits, uint64_t{64});
    if (!width || *width == 0 ||
        result.bits > std::numeric_limits<uint64_t>::max() - *width)
      return failure();
    offset = result.bits;
    result.bits += *width;
    return success();
  };
  WalkResult walked = design.walk([&](Operation *operation) {
    if (auto declaration = dyn_cast<sim::SimStorageDeclOp>(operation)) {
      uint64_t offset;
      if (failed(allocate(declaration.getType(), offset)))
        return declaration.emitOpError(
                   "bytecode storage must have fixed width"),
               WalkResult::interrupt();
      result.storage[declaration.getId()] = offset;
    } else if (auto declaration = dyn_cast<sim::SimNetDeclOp>(operation)) {
      uint64_t offset;
      if (failed(allocate(declaration.getType(), offset)))
        return declaration.emitOpError("bytecode net must have fixed width"),
               WalkResult::interrupt();
      result.nets[declaration.getId()] = offset;
      result.netLayouts.push_back({declaration.getId(), offset,
                                   *simulationWidth(declaration.getType()),
                                   containsLogic(declaration.getType()),
                                   declaration.getResolutionKind()});
    } else if (auto declaration = dyn_cast<sim::SimDriverDeclOp>(operation)) {
      auto net = result.nets.find(declaration.getNetId());
      if (net == result.nets.end())
        return declaration.emitOpError("driver references unknown net"),
               WalkResult::interrupt();
      uint64_t offset;
      if (failed(allocate(declaration.getType(), offset)))
        return declaration.emitOpError("bytecode driver must have fixed width"),
               WalkResult::interrupt();
      uint32_t width = *simulationWidth(declaration.getType());
      uint64_t drivenLow =
          declaration.getDrivenLowAttr()
              ? declaration.getDrivenLowAttr().getValue().getZExtValue()
              : 0;
      uint64_t drivenWidth =
          declaration.getDrivenWidthAttr()
              ? declaration.getDrivenWidthAttr().getValue().getZExtValue()
              : width;
      if (drivenLow > UINT32_MAX || drivenWidth > UINT32_MAX ||
          drivenLow > width || drivenWidth > width - drivenLow)
        return declaration.emitOpError("has an invalid driven range"),
               WalkResult::interrupt();
      result.drivers[declaration.getId()] = offset;
      auto netLayout =
          llvm::find_if(result.netLayouts, [&](const auto &layout) {
            return layout.id == declaration.getNetId();
          });
      if (netLayout == result.netLayouts.end())
        return declaration.emitOpError("driver references unknown net layout"),
               WalkResult::interrupt();
      result.driverLayouts.push_back(
          {declaration.getId(), declaration.getNetId(), offset, net->second,
           width, static_cast<uint32_t>(drivenLow),
           static_cast<uint32_t>(drivenWidth),
           containsLogic(declaration.getType()), netLayout->resolution});
    }
    return WalkResult::advance();
  });
  if (walked.wasInterrupted())
    return failure();
  using ScalarConnection =
      std::pair<sim::NetResolutionKind, sim::NetResolutionKind>;
  std::map<std::pair<uint64_t, uint64_t>, ScalarConnection> scalarConnections;
  for (sim::SimNetConnectDeclOp connection :
       design.getBody().getOps<sim::SimNetConnectDeclOp>()) {
    auto lhs = llvm::find_if(result.netLayouts, [&](const auto &layout) {
      return layout.id == connection.getLhsNetId();
    });
    auto rhs = llvm::find_if(result.netLayouts, [&](const auto &layout) {
      return layout.id == connection.getRhsNetId();
    });
    if (lhs == result.netLayouts.end() || rhs == result.netLayouts.end())
      return connection.emitOpError("references an unknown bytecode net"),
             failure();
    for (uint64_t bit = 0; bit != connection.getWidth(); ++bit) {
      uint64_t lhsBit = lhs->offset + connection.getLhsOffset() + bit;
      uint64_t rhsBit = rhs->offset + (connection.getRhsReversed()
                                           ? connection.getRhsOffset() - bit
                                           : connection.getRhsOffset() + bit);
      sim::NetResolutionKind lhsResolution = lhs->resolution;
      sim::NetResolutionKind rhsResolution = rhs->resolution;
      if (rhsBit < lhsBit) {
        std::swap(lhsBit, rhsBit);
        std::swap(lhsResolution, rhsResolution);
      }
      if (lhsBit == rhsBit)
        continue;
      auto [found, inserted] = scalarConnections.try_emplace(
          std::pair{lhsBit, rhsBit},
          ScalarConnection{lhsResolution, rhsResolution});
      if (!inserted &&
          found->second != ScalarConnection{lhsResolution, rhsResolution})
        return connection.emitOpError(
                   "has inconsistent duplicate scalar connectivity"),
               failure();
    }
  }
  for (auto scalar = scalarConnections.begin();
       scalar != scalarConnections.end();) {
    auto [lhsOffset, rhsOffset] = scalar->first;
    auto [lhsResolution, rhsResolution] = scalar->second;
    uint64_t width = 1;
    int direction = 0;
    auto next = std::next(scalar);
    while (next != scalarConnections.end()) {
      if (next->second != scalar->second ||
          next->first.first != lhsOffset + width)
        break;
      int candidateDirection = 0;
      if (next->first.second == rhsOffset + width)
        candidateDirection = 1;
      else if (rhsOffset >= width && next->first.second == rhsOffset - width)
        candidateDirection = -1;
      if (candidateDirection == 0 ||
          (direction != 0 && direction != candidateDirection))
        break;
      direction = candidateDirection;
      ++width;
      ++next;
    }
    result.connections.push_back({lhsOffset, rhsOffset, width, lhsResolution,
                                  rhsResolution, direction < 0});
    scalar = next;
  }

  analysis::NetConnectivityAnalysis connectivity(design);
  DenseMap<std::pair<uint64_t, uint64_t>, uint64_t> uwireDrivers;
  for (const StateLayout::Driver &driver : result.driverLayouts) {
    if (driver.resolution != sim::NetResolutionKind::UWire)
      continue;
    for (uint64_t bit = driver.drivenLow;
         bit != uint64_t{driver.drivenLow} + driver.drivenWidth; ++bit) {
      ArrayRef<analysis::NetBit> component =
          connectivity.getComponent({driver.netID, bit});
      analysis::NetBit canonical = component.empty()
                                       ? analysis::NetBit{driver.netID, bit}
                                       : component.front();
      if (++uwireDrivers[{canonical.net, canonical.offset}] > 1)
        return design.emitOpError()
                   << "uwire connectivity component " << canonical.net << "["
                   << canonical.offset << "] has more than one driver",
               failure();
    }
  }
  result.bits = std::max<uint64_t>(result.bits, 8);
  return result;
}

class Encoder {
public:
  Encoder(sim::SimDesignOp design, const SimulationBytecodeOptions &options,
          const llvm::DataLayout &dataLayout)
      : design(design), options(options), dataLayout(dataLayout) {}

  FailureOr<EncodedSimulationDesign> encode() {
    if (failed(prepareClassLayouts()))
      return failure();
    FailureOr<StateLayout> builtState = buildStateLayout(design);
    if (failed(builtState))
      return failure();
    state = *builtState;
    if (failed(planTwoStateRegisters()) || failed(planFunctions()) ||
        failed(planScheduleRanks()) || failed(encodeFunctions()))
      return failure();
    EncodedSimulationDesign result;
    result.bytecode = serializeBytecode();
    if (result.bytecode.empty())
      return failure();
    uint32_t profile = getVPIProfile();
    if (profile == UINT32_MAX)
      return failure();
    if (profile != 0) {
      result.designDatabase = serializeDatabase(profile);
      if (result.designDatabase.empty())
        return failure();
    }
    result.stateBitCount = state.bits;
    result.executionFlags = kExecutionHasBytecode;
    if (options.requireBytecode)
      result.executionFlags |= kExecutionRequireBytecode;
    if (profile != 0) {
      result.executionFlags |= kExecutionHasDatabase | kExecutionVPIRead;
      if (profile & kDatabaseProfileWrite)
        result.executionFlags |= kExecutionVPIWrite;
    }
    for (FunctionPlan &plan : plans)
      result.functions.push_back({plan.function.getSymName().str(), plan.index,
                                  plan.scratchSize, plan.scratchAlignment,
                                  plan.twoStateLogicRegisters});
    return result;
  }

private:
  LogicalResult prepareClassLayouts() {
    llvm::StringMap<sim::SimClassDeclOp> classes;
    llvm::StringMap<SmallVector<sim::SimClassFieldDeclOp>> fields;
    design.walk([&](sim::SimClassDeclOp declaration) {
      classes[declaration.getSymName()] = declaration;
      classIDs[declaration.getSymName()] = declaration.getId();
    });
    design.walk([&](sim::SimClassFieldDeclOp field) {
      fields[field.getOwner()].push_back(field);
    });
    for (auto &entry : fields)
      llvm::sort(entry.second, [](auto lhs, auto rhs) {
        return lhs.getOrdinal() < rhs.getOrdinal();
      });

    struct PartialLayout {
      uint64_t size = 8;
      uint32_t alignment = 8;
    };
    llvm::StringMap<PartialLayout> layouts;
    llvm::StringSet<> active;
    llvm::DataLayout local(dataLayout.getStringRepresentation());
    std::function<LogicalResult(sim::SimClassDeclOp)> compute =
        [&](sim::SimClassDeclOp declaration) -> LogicalResult {
      if (layouts.count(declaration.getSymName()))
        return success();
      if (!active.insert(declaration.getSymName()).second)
        return declaration.emitOpError("class layout inheritance cycle");
      PartialLayout layout;
      if (auto baseName = declaration.getBase()) {
        auto base = classes.find(*baseName);
        if (base == classes.end() || failed(compute(base->second)))
          return declaration.emitOpError("class layout has an unknown base");
        layout = layouts[base->getKey()];
      }
      // Native weak-reference wrappers reserve one inline referent pointer
      // before declared fields. Whole-design bytecode uses the same managed
      // object descriptor and must therefore account for that ABI slot even
      // though bytecode never interprets its contents directly.
      if (declaration.getWeakReferentAttr()) {
        layout.size = llvm::alignTo(layout.size, uint64_t{8});
        if (layout.size > UINT64_MAX - 8)
          return declaration.emitOpError("weak referent layout overflows");
        layout.size += 8;
        layout.alignment = std::max<uint32_t>(layout.alignment, 8);
      }
      for (sim::SimClassFieldDeclOp field : fields[declaration.getSymName()]) {
        if (field.getIsStatic())
          continue;
        Type fieldType = field.getType();
        FailureOr<ManagedValueStorage> storage =
            getManagedValueStorage(fieldType, local);
        if (failed(storage))
          return field.emitOpError(
              "class property has no fixed bytecode layout");
        uint64_t offset = llvm::alignTo(
            layout.size, static_cast<uint64_t>(storage->alignment));
        uint64_t planes = storage->fourState ? 2 : 1;
        if (storage->planeSize >
            (std::numeric_limits<uint64_t>::max() - offset) / planes)
          return field.emitOpError("class property layout overflows");
        layout.size = offset + storage->planeSize * planes;
        layout.alignment =
            std::max<uint32_t>(layout.alignment, storage->alignment);
        if (auto existing = field->getAttrOfType<IntegerAttr>("offset");
            existing && existing.getValue().getZExtValue() != offset)
          return field.emitOpError(
              "native and bytecode class layouts disagree");
        field->setAttr("offset",
                       IntegerAttr::get(
                           IntegerType::get(design.getContext(), 64), offset));
      }
      layout.size =
          llvm::alignTo(layout.size, static_cast<uint64_t>(layout.alignment));
      active.erase(declaration.getSymName());
      layouts[declaration.getSymName()] = layout;
      return success();
    };
    for (const auto &entry : classes)
      if (failed(compute(entry.second)))
        return failure();
    return success();
  }

  FailureOr<uint64_t> classID(SymbolRefAttr symbol, Operation *anchor) const {
    auto found = classIDs.find(symbol.getRootReference().getValue());
    if (found == classIDs.end()) {
      anchor->emitOpError("references an unknown managed class");
      return failure();
    }
    return found->second;
  }

  /// Use the whole-design X/Z proof for local bytecode scratch values. Exact
  /// ABI boundaries deliberately remain four-state: process frames, CFG maps,
  /// calls, and returns all use representation-preserving copies. Within a
  /// block, bytecode operations that require identical layouts form constraint
  /// components; one unproven member keeps the entire component four-state.
  LogicalResult planTwoStateRegisters() {
    FailureOr<StateDomainAnalysis> analysis =
        StateDomainAnalysis::compute(design);
    if (failed(analysis))
      return failure();

    DenseSet<Value> candidates;
    DenseSet<Value> forcedFourState;
    DenseMap<Value, SmallVector<Value>> compatibleLayouts;
    auto isLogic = [](Value value) {
      return value && isa<sim::LogicType>(value.getType());
    };
    auto consider = [&](Value value) {
      if (!isLogic(value))
        return;
      if (analysis->isTwoState(value))
        candidates.insert(value);
      else
        forcedFourState.insert(value);
    };
    auto force = [&](Value value) {
      if (isLogic(value))
        forcedFourState.insert(value);
    };
    auto constrain = [&](Value lhs, Value rhs) {
      if (!isLogic(lhs) || !isLogic(rhs))
        return;
      compatibleLayouts[lhs].push_back(rhs);
      compatibleLayouts[rhs].push_back(lhs);
    };
    auto constrainResultTo = [&](Value result, ValueRange operands) {
      for (Value operand : operands)
        constrain(result, operand);
    };

    for (sim::SimFuncOp function :
         design.getBody().front().getOps<sim::SimFuncOp>()) {
      if (function.isExternal())
        continue;
      for (Block &block : function.getBody()) {
        // Block arguments participate in parallel CFG or canonical-frame
        // copies. Keep those stable and specialize computations after them.
        for (BlockArgument argument : block.getArguments()) {
          consider(argument);
          force(argument);
        }
        for (Operation &operation : block) {
          for (Value result : operation.getResults())
            consider(result);

          if (isa<BranchOpInterface, sim::SimCallOp, sim::SimTaskCallOp,
                  sim::SimDPICallOp, sim::SimSpawnOp, sim::SimReturnOp>(
                  operation)) {
            for (Value operand : operation.getOperands())
              force(operand);
            for (Value result : operation.getResults())
              force(result);
          }

          // Automatic logic storage remains four-state for its full lifetime,
          // even when its initializer is known.  Later stores through escaped
          // references must retain X/Z rather than inheriting the initializer's
          // compact one-plane register layout.
          if (auto alloc = dyn_cast<sim::SimRefAllocOp>(operation))
            force(alloc.getInitialValue());

          if (auto op = dyn_cast<sim::SimLogicUnaryOp>(operation)) {
            if (op.getKind() != sim::UnaryKind::LogicalNot)
              constrain(op.getResult(), op.getInput());
          } else if (auto op = dyn_cast<sim::SimLogicBinaryOp>(operation)) {
            constrainResultTo(op.getResult(), op.getOperands());
          } else if (auto op = dyn_cast<sim::SimLogicMuxOp>(operation)) {
            constrain(op.getResult(), op.getTrueValue());
            constrain(op.getResult(), op.getFalseValue());
            force(op.getCondition());
          } else if (auto op = dyn_cast<sim::SimLogicShiftOp>(operation)) {
            constrain(op.getResult(), op.getInput());
          } else if (auto op = dyn_cast<sim::SimLogicCompareOp>(operation)) {
            constrain(op.getLhs(), op.getRhs());
          } else if (auto op = dyn_cast<sim::SimLogicConcatOp>(operation)) {
            constrainResultTo(op.getResult(), op.getInputs());
          } else if (auto op = dyn_cast<sim::SimLogicReplicateOp>(operation)) {
            constrain(op.getResult(), op.getInput());
          } else if (auto op = dyn_cast<sim::SimLogicInsertOp>(operation)) {
            constrain(op.getResult(), op.getInput());
          } else if (auto op = dyn_cast<arith::SelectOp>(operation)) {
            constrain(op.getResult(), op.getTrueValue());
            constrain(op.getResult(), op.getFalseValue());
          } else if (auto op = dyn_cast<sim::SimAggregateInsertOp>(operation)) {
            constrain(op.getResult(), op.getInput());
          }
        }
      }
    }

    // A bytecode instruction that requires representation-compatible
    // registers makes its entire undirected component four-state when any
    // member is unproven or fixed by an ABI boundary. Propagate from those
    // roots once instead of repeatedly rescanning all constraints.
    SmallVector<Value> worklist(forcedFourState.begin(), forcedFourState.end());
    for (size_t index = 0; index != worklist.size(); ++index) {
      Value value = worklist[index];
      candidates.erase(value);
      auto found = compatibleLayouts.find(value);
      if (found == compatibleLayouts.end())
        continue;
      for (Value adjacent : found->second)
        if (forcedFourState.insert(adjacent).second)
          worklist.push_back(adjacent);
    }
    twoStateLogicRegisters = std::move(candidates);
    return success();
  }

  FailureOr<Layout> getValueLayout(Value value) const {
    FailureOr<Layout> layout = getLayout(value.getType());
    if (failed(layout) || !isa<sim::LogicType>(value.getType()) ||
        !twoStateLogicRegisters.contains(value))
      return layout;
    layout->kind = Bits;
    layout->size = ((uint64_t{layout->width} + 63) / 64) * 8;
    return layout;
  }

  LogicalResult planFunctions() {
    SmallVector<sim::SimFuncOp> functions;
    for (sim::SimFuncOp function : design.getBody().getOps<sim::SimFuncOp>()) {
      if (function.isExternal())
        externalFunctions[function.getSymName()] = function;
      else
        functions.push_back(function);
    }
    auto getStableID = [](sim::SimFuncOp function) {
      return function.getCodeUnitId().value_or(
          stableHash(function.getSymName()) &
          static_cast<uint64_t>(std::numeric_limits<int64_t>::max()));
    };
    llvm::sort(functions, [&](sim::SimFuncOp left, sim::SimFuncOp right) {
      return std::make_tuple(getStableID(left), left.getSymName()) <
             std::make_tuple(getStableID(right), right.getSymName());
    });
    if (functions.empty())
      return design.emitOpError("contains no executable functions");
    plans.reserve(functions.size());
    DenseMap<uint64_t, sim::SimFuncOp> stableIDs;
    for (auto [index, function] : llvm::enumerate(functions)) {
      FunctionPlan &plan = plans.emplace_back();
      plan.function = function;
      plan.liveness = std::make_unique<Liveness>(function);
      plan.index = static_cast<uint32_t>(index);
      plan.stableID = getStableID(function);
      if (plan.stableID == 0)
        return function.emitOpError("executable code-unit ID must be nonzero");
      auto [collision, inserted] =
          stableIDs.try_emplace(plan.stableID, function);
      if (!inserted) {
        function.emitOpError()
            << "duplicate executable code-unit ID " << plan.stableID;
        collision->second.emitRemark("first function with this ID is here");
        return failure();
      }
      indices[function.getSymName()] = plan.index;
    }
    for (FunctionPlan &plan : plans) {
      FunctionType type = plan.function.getFunctionType();
      auto allocateLayout = [&](Layout layout) -> uint32_t {
        uint64_t aligned = llvm::alignTo(plan.scratchSize, uint64_t{8});
        layout.offset = aligned;
        plan.scratchSize = aligned + layout.size;
        plan.layouts.push_back(layout);
        return plan.layouts.size() - 1;
      };
      auto allocateType = [&](Type type) -> FailureOr<uint32_t> {
        FailureOr<Layout> layout = getLayout(type);
        if (failed(layout))
          return failure();
        return allocateLayout(*layout);
      };
      auto allocateValue = [&](Value value) -> FailureOr<uint32_t> {
        FailureOr<Layout> layout = getValueLayout(value);
        if (failed(layout))
          return failure();
        if (layout->kind == Bits && isa<sim::LogicType>(value.getType()))
          ++plan.twoStateLogicRegisters;
        return allocateLayout(*layout);
      };
      Block &entry = plan.function.getBody().front();
      if (entry.getNumArguments() != type.getNumInputs())
        return plan.function.emitOpError("entry signature is inconsistent");
      for (BlockArgument argument : entry.getArguments()) {
        FailureOr<uint32_t> reg = allocateValue(argument);
        if (failed(reg))
          return argument.getOwner()->getParentOp()->emitError()
                 << "cannot encode argument type " << argument.getType();
        plan.registers.insert({argument, *reg});
      }
      for (Type result : type.getResults()) {
        FailureOr<uint32_t> reg = allocateType(result);
        if (failed(reg))
          return plan.function.emitOpError()
                 << "cannot encode result type " << result;
        plan.resultRegisters.push_back(*reg);
      }
      for (Block &block : plan.function.getBody()) {
        if (&block != &entry)
          for (BlockArgument argument : block.getArguments()) {
            FailureOr<uint32_t> reg = allocateValue(argument);
            if (failed(reg))
              return plan.function.emitOpError()
                     << "cannot encode block argument type "
                     << argument.getType();
            plan.registers.insert({argument, *reg});
          }
        for (Operation &operation : block)
          for (Value result : operation.getResults()) {
            FailureOr<uint32_t> reg = allocateValue(result);
            if (failed(reg))
              return operation.emitOpError()
                     << "cannot encode result type " << result.getType();
            plan.registers.insert({result, *reg});
          }
      }
      plan.scratchSize = llvm::alignTo(plan.scratchSize, uint64_t{8});
      if (plan.function.getEntryKind() != sim::EntryKind::Function &&
          plan.function.getEntryKind() != sim::EntryKind::Observer) {
        FailureOr<std::unique_ptr<SimulationProcessFrameAnalysis>> frame =
            SimulationProcessFrameAnalysis::create(plan.function, dataLayout);
        if (failed(frame))
          return failure();
        plan.frame = std::move(*frame);
        if (plan.frame->getFrameSize() >=
            OBELISK_RT_DESIGN_FUNCTION_FRAME_SIZE_LIMIT)
          return plan.function.emitOpError(
              "process frame is too large for bytecode function flags");
        ArrayRef<ProcessFrameValue> captures =
            plan.frame->getEntryCaptureLayout();
        if (captures.size() != entry.getNumArguments())
          return plan.function.emitOpError(
              "entry capture layout is incomplete");
        for (auto [argument, capture] : llvm::enumerate(captures))
          captureRecords.push_back(
              {plan.index, static_cast<uint32_t>(argument), capture.valueOffset,
               capture.hasSecondaryStorage() ? capture.getSecondaryOffset()
                                             : UINT64_MAX,
               capture.storageSize});
      }
    }
    return success();
  }

  LogicalResult planScheduleRanks() {
    uint32_t fallback = 0;
    for (FunctionPlan &plan : plans) {
      if (plan.function.getEntryKind() == sim::EntryKind::Function ||
          plan.function.getEntryKind() == sim::EntryKind::Observer)
        continue;
      plan.initialScheduleRank = fallback;
      for (Block &block : plan.function.getBody())
        plan.blockScheduleRanks[&block] = fallback;
      if (fallback != UINT32_MAX)
        ++fallback;
    }

    sim::ComputeGraphAttr graph = design.getComputeGraphAttr();
    if (!graph)
      return success();
    ArrayAttr nodes = graph.getNodes();
    uint32_t rank = 0;
    for (Attribute regionAttribute : graph.getRegions()) {
      auto region = dyn_cast<sim::ComputeRegionAttr>(regionAttribute);
      if (!region || (region.getKind() != sim::ComputeRegionKind::Active &&
                      region.getKind() != sim::ComputeRegionKind::Observed &&
                      region.getKind() != sim::ComputeRegionKind::Reactive &&
                      region.getKind() != sim::ComputeRegionKind::Postponed))
        continue;
      for (Attribute groupAttribute : region.getGroups()) {
        auto group = dyn_cast<sim::ComputeGroupAttr>(groupAttribute);
        if (!group)
          continue;
        for (int64_t member : group.getFragments().asArrayRef()) {
          if (member < 0 || static_cast<uint64_t>(member) >= nodes.size())
            continue;
          auto fragment = dyn_cast<sim::ComputeFragmentAttr>(nodes[member]);
          if (!fragment)
            continue;
          auto found = indices.find(fragment.getFunction().getValue());
          if (found == indices.end())
            continue;
          FunctionPlan &plan = plans[found->second];
          Block *block =
              lookupComputeGraphBlock(plan.function, fragment.getBlock());
          if (!block)
            return plan.function.emitOpError(
                "compute-graph fragment block is out of range");
          plan.blockScheduleRanks[block] = rank;
          if (fragment.getBlock() == 0)
            plan.initialScheduleRank = rank;
        }
        if (rank != UINT32_MAX)
          ++rank;
      }
    }
    return success();
  }

  uint32_t reg(const FunctionPlan &plan, Value value) const {
    auto found = plan.registers.find(value);
    return found == plan.registers.end() ? kInvalidRegister : found->second;
  }

  uint32_t temporary(FunctionPlan &plan, Type type) {
    FailureOr<Layout> layout = getLayout(type);
    if (failed(layout))
      return kInvalidRegister;
    layout->offset = llvm::alignTo(plan.scratchSize, uint64_t{8});
    plan.scratchSize = layout->offset + layout->size;
    plan.layouts.push_back(*layout);
    return plan.layouts.size() - 1;
  }

  uint32_t temporaryLike(FunctionPlan &plan, Type type, Value model) {
    FailureOr<Layout> layout = getLayout(type);
    uint32_t modelRegister = reg(plan, model);
    if (failed(layout) || modelRegister == kInvalidRegister)
      return kInvalidRegister;
    if (isa<sim::LogicType>(type)) {
      layout->kind = plan.layouts[modelRegister].kind;
      layout->size = ((uint64_t{layout->width} + 63) / 64) *
                     (layout->kind == Logic ? 16 : 8);
      if (layout->kind == Bits)
        ++plan.twoStateLogicRegisters;
    }
    layout->offset = llvm::alignTo(plan.scratchSize, uint64_t{8});
    plan.scratchSize = layout->offset + layout->size;
    plan.layouts.push_back(*layout);
    return plan.layouts.size() - 1;
  }

  uint64_t addConstant(const Layout &layout, const APInt &value,
                       const APInt *unknown = nullptr) {
    uint64_t offset = constants.size();
    uint64_t limbs = (uint64_t{layout.width} + 63) / 64;
    auto appendPlane = [&](const APInt &plane) {
      for (uint64_t limb = 0; limb != limbs; ++limb) {
        unsigned bits = std::min<uint64_t>(64, layout.width - limb * 64);
        append64(constants, plane.extractBitsAsZExtValue(bits, limb * 64));
      }
    };
    appendPlane(value);
    if (layout.kind == Logic) {
      if (unknown)
        appendPlane(*unknown);
      else
        constants.resize(constants.size() + limbs * 8, 0);
    }
    return offset;
  }

  uint64_t addZeroConstant(const Layout &layout) {
    if (auto found = zeroConstants.find(layout.size);
        found != zeroConstants.end())
      return found->second;
    uint64_t offset = constants.size();
    constants.resize(constants.size() + layout.size, 0);
    zeroConstants.try_emplace(layout.size, offset);
    return offset;
  }

  uint64_t addRawConstant(ArrayRef<uint8_t> bytes) {
    uint64_t offset = constants.size();
    llvm::append_range(constants, bytes);
    return offset;
  }

  uint64_t addBytesConstant(ArrayRef<uint8_t> bytes) {
    uint64_t dataOffset = constants.size();
    llvm::append_range(constants, bytes);
    uint64_t descriptorOffset = constants.size();
    append64(constants, dataOffset);
    append64(constants, bytes.size());
    return descriptorOffset;
  }

  uint32_t addIntrinsicSignature(uint32_t id, uint32_t inputCount,
                                 uint32_t outputCount, uint32_t flags = 0) {
    for (auto [index, signature] : llvm::enumerate(intrinsicSignatures))
      if (signature.id == id && signature.inputCount == inputCount &&
          signature.outputCount == outputCount && signature.flags == flags)
        return static_cast<uint32_t>(index);
    intrinsicSignatures.push_back({id, inputCount, outputCount, flags});
    return intrinsicSignatures.size() - 1;
  }

  LogicalResult emitIntrinsicRegisters(FunctionPlan &plan, uint32_t id,
                                       ArrayRef<uint32_t> inputs,
                                       ArrayRef<uint32_t> outputs,
                                       uint32_t flags = 0) {
    if (inputs.size() > UINT32_MAX || outputs.size() > UINT32_MAX ||
        operandMaps.size() > UINT32_MAX || intrinsicSites.size() > UINT32_MAX)
      return plan.function.emitOpError("bytecode intrinsic table is too large");
    uint32_t firstOperand = static_cast<uint32_t>(operandMaps.size());
    for (uint32_t input : inputs) {
      if (input == kInvalidRegister || input >= plan.layouts.size())
        return plan.function.emitOpError(
            "bytecode intrinsic input is unmapped");
      operandMaps.push_back({0, input});
    }
    for (uint32_t output : outputs) {
      if (output == kInvalidRegister || output >= plan.layouts.size())
        return plan.function.emitOpError(
            "bytecode intrinsic output is unmapped");
      operandMaps.push_back({output, 0});
    }
    uint32_t signature =
        addIntrinsicSignature(id, static_cast<uint32_t>(inputs.size()),
                              static_cast<uint32_t>(outputs.size()), flags);
    uint32_t site = intrinsicSites.size();
    intrinsicSites.push_back({signature, firstOperand,
                              static_cast<uint32_t>(inputs.size()),
                              static_cast<uint32_t>(outputs.size())});
    emit({Intrinsic, 0, 0, 0, 0, 0, 0, site});
    return success();
  }

  LogicalResult emitIntrinsic(FunctionPlan &plan, uint32_t id,
                              ArrayRef<Value> inputs, ArrayRef<Value> outputs,
                              uint32_t flags = 0) {
    SmallVector<uint32_t> inputRegisters, outputRegisters;
    llvm::transform(inputs, std::back_inserter(inputRegisters),
                    [&](Value value) { return reg(plan, value); });
    llvm::transform(outputs, std::back_inserter(outputRegisters),
                    [&](Value value) { return reg(plan, value); });
    return emitIntrinsicRegisters(plan, id, inputRegisters, outputRegisters,
                                  flags);
  }

  uint32_t emitBytesConstant(FunctionPlan &plan, ArrayRef<uint8_t> bytes) {
    uint32_t result =
        temporary(plan, sim::BytesType::get(plan.function.getContext()));
    if (result != kInvalidRegister)
      emit({Constant, 0, result, 0, 0, 0, 0, addBytesConstant(bytes)});
    return result;
  }

  uint32_t emitU64Constant(FunctionPlan &plan, uint64_t value) {
    Type i64 = IntegerType::get(plan.function.getContext(), 64);
    uint32_t result = temporary(plan, i64);
    if (result != kInvalidRegister)
      emit({Constant, 0, result, 0, 0, 0, 0,
            addConstant(plan.layouts[result], APInt(64, value))});
    return result;
  }

  LogicalResult encodeClassDirectCall(FunctionPlan &plan,
                                      sim::SimClassDirectCallOp call) {
    auto found = indices.find(call.getCallee());
    if (found == indices.end())
      return call.emitOpError("class method has no bytecode body");
    FunctionPlan &callee = plans[found->second];
    SmallVector<Value> arguments{plan.function.getBody().front().getArgument(0),
                                 call.getReceiver()};
    llvm::append_range(arguments, call.getArguments());
    auto inputs =
        addMap(callee, callee.function.getBody().front().getArguments(), plan,
               arguments);
    uint64_t firstOutputs = operandMaps.size();
    for (auto [destination, source] :
         llvm::zip_equal(call.getResults(), callee.resultRegisters))
      operandMaps.push_back({reg(plan, destination), source});
    emit({Call, 0, 0, callee.index, static_cast<uint32_t>(inputs.first),
          static_cast<uint32_t>(inputs.second),
          static_cast<uint32_t>(firstOutputs), call.getNumResults()});
    return success();
  }

  LogicalResult encodeClassVirtualCall(FunctionPlan &plan,
                                       sim::SimClassVirtualCallOp call) {
    if (call.getSlot() > UINT32_MAX || call.getNumResults() > UINT16_MAX)
      return call.emitOpError("virtual call exceeds the bytecode ABI");
    SmallVector<Value> arguments{plan.function.getBody().front().getArgument(0),
                                 call.getReceiver()};
    llvm::append_range(arguments, call.getArguments());
    if (arguments.size() > UINT32_MAX || operandMaps.size() > UINT32_MAX)
      return call.emitOpError("virtual argument map exceeds the bytecode ABI");
    uint32_t firstInputs = operandMaps.size();
    for (auto [index, argument] : llvm::enumerate(arguments))
      operandMaps.push_back(
          {static_cast<uint32_t>(index), reg(plan, argument)});
    uint32_t firstOutputs = operandMaps.size();
    for (auto [index, result] : llvm::enumerate(call.getResults()))
      operandMaps.push_back(
          {reg(plan, result), static_cast<uint32_t>(arguments.size() + index)});
    emit({VirtualCall, static_cast<uint16_t>(call.getNumResults()),
          static_cast<uint32_t>(call.getSlot()), reg(plan, call.getReceiver()),
          firstInputs, static_cast<uint32_t>(arguments.size()), firstOutputs,
          call.getSignatureId()});
    return success();
  }

  LogicalResult encodeDisplay(FunctionPlan &plan, sim::SimDisplayOp op) {
    SmallVector<uint8_t> metadata;
    append32(metadata, 1);
    append32(metadata, op.getAppendNewline() ? 1 : 0);
    append32(metadata, op.getDefaultRadix());
    append32(metadata, op.getItemFlags().size());
    StringRef scope = op.getScope().value_or("");
    if (scope.empty())
      scope = plan.function.getSymName();
    StringRef library = op.getLibraryCell().value_or("");
    append64(metadata, scope.size());
    append64(metadata, library.size());
    append64(metadata, op.getTimeMultiplier().value_or(1));
    for (int32_t flag : op.getItemFlags())
      append32(metadata, static_cast<uint32_t>(flag));
    llvm::append_range(metadata, scope.bytes());
    llvm::append_range(metadata, library.bytes());
    uint32_t metadataRegister = emitBytesConstant(plan, metadata);
    if (metadataRegister == kInvalidRegister)
      return op.emitOpError("cannot allocate display metadata register");
    SmallVector<uint32_t> inputs{metadataRegister,
                                 reg(plan, op.getDescriptor())};
    for (Value item : op.getItems())
      inputs.push_back(reg(plan, item));
    return emitIntrinsicRegisters(plan, kIntrinsicDisplay, inputs, {});
  }

  uint64_t emit(Instruction instruction) {
    instructions.push_back(instruction);
    return instructions.size() - 1;
  }

  std::pair<uint64_t, uint64_t> addMap(FunctionPlan &destinationPlan,
                                       ValueRange destination,
                                       FunctionPlan &sourcePlan,
                                       ValueRange source) {
    uint64_t first = operandMaps.size();
    for (auto [destinationValue, sourceValue] :
         llvm::zip_equal(destination, source))
      operandMaps.push_back({reg(destinationPlan, destinationValue),
                             reg(sourcePlan, sourceValue)});
    return {first, destination.size()};
  }

  std::pair<uint64_t, uint64_t> addRegistersMap(ArrayRef<uint32_t> destinations,
                                                FunctionPlan &sourcePlan,
                                                ValueRange source) {
    uint64_t first = operandMaps.size();
    for (auto [destination, sourceValue] :
         llvm::zip_equal(destinations, source))
      operandMaps.push_back({destination, reg(sourcePlan, sourceValue)});
    return {first, destinations.size()};
  }

  LogicalResult encodeFunctions() {
    for (FunctionPlan &plan : plans) {
      plan.firstInstruction = instructions.size();
      for (Block &block : plan.function.getBody()) {
        plan.blockPCs[&block] = instructions.size();
        for (Operation &operation : block) {
          if (mayCollect(&operation)) {
            if (failed(emitAggregateManagedRoots(plan, &operation)))
              return failure();
            emitDeadManagedClears(plan, &operation);
          }
          if (failed(encodeOperation(plan, &operation)))
            return failure();
        }
      }
      for (auto [instruction, target] : plan.branches) {
        auto found = plan.blockPCs.find(target);
        if (found == plan.blockPCs.end())
          return plan.function.emitOpError("bytecode branch target is missing");
        instructions[instruction].immediate = found->second;
      }
      if (failed(emitContinuationEntries(plan)))
        return failure();
      plan.instructionCount = instructions.size() - plan.firstInstruction;
      plan.scratchSize = llvm::alignTo(plan.scratchSize, uint64_t{8});
    }
    return success();
  }

  static bool mayCollect(Operation *operation) {
    return isa<sim::SimClassAllocOp, sim::SimClassCopyOp, sim::SimWeakCreateOp,
               sim::SimReferencePathIndexOp, sim::SimReferencePathAssocOp,
               sim::SimContainerCreateLikeOp,
               sim::SimContainerCreateOp, sim::SimContainerCloneOp,
               sim::SimContainerWriteOp, sim::SimQueueInsertOp,
               sim::SimAssocCreateOp,
               sim::SimAssocWriteOp, sim::SimAssocSetDefaultOp,
               sim::SimAssocTraverseOp, sim::SimArgumentRefStoreOp,
               sim::SimReferencePathNBAEnqueueOp, sim::SimGCSafepointOp,
               sim::SimStringLiteralOp, sim::SimStringFromPackedOp,
               sim::SimStringConcatOp, sim::SimStringRepeatOp,
               sim::SimStringPutcOp, sim::SimStringSubstrOp,
               sim::SimStringCaseConvertOp, sim::SimStringFormatIntegerOp,
               sim::SimStringFormatRealOp, sim::SimFileGetlineStringOp,
               sim::SimCallOp, sim::SimClassDirectCallOp,
               sim::SimClassVirtualCallOp, sim::SimDPICallOp>(operation);
  }

  LogicalResult emitAggregateManagedRoots(FunctionPlan &plan,
                                          Operation *operation) {
    const LivenessBlockInfo *blockInfo =
        plan.liveness->getLiveness(operation->getBlock());
    if (!blockInfo)
      return success();
    Liveness::ValueSetT live = blockInfo->currentlyLiveValues(operation);
    for (const auto &registerEntry : plan.registers) {
      Value value = registerEntry.first;
      uint32_t source = registerEntry.second;
      SmallVector<uint64_t, 2> offsets;
      if (!sim::getManagedHandleOffsets(value.getType(), offsets))
        return operation->emitError(
            "value has no fixed bytecode managed root layout");
      // Scalar managed registers and tagged string words are enumerated
      // directly from the live bytecode frame. Only aggregate payloads need
      // object-pointer shadows for their embedded managed slots.
      if (offsets.empty() || plan.layouts[source].kind == Managed ||
          plan.layouts[source].kind == String)
        continue;
      for (uint64_t bitOffset : offsets) {
        auto found = llvm::find_if(
            plan.managedRootShadows,
            [&](const FunctionPlan::ManagedRootShadow &shadow) {
              return shadow.value == value && shadow.bitOffset == bitOffset;
            });
        uint32_t shadow;
        if (found == plan.managedRootShadows.end()) {
          Layout layout;
          layout.kind = Managed;
          layout.width = 64;
          layout.size = 8;
          layout.offset = llvm::alignTo(plan.scratchSize, uint64_t{8});
          plan.scratchSize = layout.offset + layout.size;
          plan.layouts.push_back(layout);
          shadow = plan.layouts.size() - 1;
          plan.managedRootShadows.push_back({value, bitOffset, shadow});
        } else {
          shadow = found->reg;
        }
        if (!live.contains(value) || value.getDefiningOp() == operation) {
          emit({Constant, 0, shadow, 0, 0, 0, 0,
                addZeroConstant(plan.layouts[shadow])});
          continue;
        }
        if (failed(emitIntrinsicRegisters(
                plan, kIntrinsicManagedRootExtract,
                {source, emitU64Constant(plan, bitOffset)}, {shadow})))
          return failure();
      }
    }
    return success();
  }

  void emitDeadManagedClears(FunctionPlan &plan, Operation *operation) {
    const LivenessBlockInfo *blockInfo =
        plan.liveness->getLiveness(operation->getBlock());
    if (!blockInfo)
      return;
    Liveness::ValueSetT live = blockInfo->currentlyLiveValues(operation);
    for (auto [value, reg] : plan.registers) {
      const Layout &layout = plan.layouts[reg];
      if (layout.kind != Managed && layout.kind != ManagedRef &&
          layout.kind != ArgumentRef)
        continue;
      if (live.contains(value) && value.getDefiningOp() != operation)
        continue;
      emit({Constant, 0, reg, 0, 0, 0, 0, addZeroConstant(layout)});
    }
  }

  LogicalResult emitContinuationEntries(FunctionPlan &plan) {
    auto emitEntry = [&](uint32_t id, Block *block,
                         ArrayRef<ProcessFrameValue> slots) -> LogicalResult {
      uint64_t entryPC = instructions.size();
      auto restore = [&](ValueRange values,
                         ArrayRef<ProcessFrameValue> valueSlots,
                         bool consumeRoots) {
        if (values.size() != valueSlots.size())
          return failure();
        for (auto [argument, slot] : llvm::zip_equal(values, valueSlots)) {
          if (slot.valueOffset == UINT64_MAX) {
            uint32_t destination = reg(plan, argument);
            emit({Constant, 0, destination, 0, 0, 0, 0,
                  addZeroConstant(plan.layouts[destination])});
            continue;
          }
          if (slot.storageSize > UINT32_MAX ||
              (slot.hasSecondaryStorage() && slot.storageSize > UINT32_MAX / 2))
            return failure();
          uint64_t transferSize =
              slot.storageSize * (slot.hasSecondaryStorage() ? 2 : 1);
          emitFrameTransfer(plan, LoadFrame, argument, slot.valueOffset,
                            static_cast<uint32_t>(transferSize));
          if (consumeRoots)
            for (uint64_t rootOffset : slot.managedRootOffsets)
              emit({ClearFrameRoot, 0, 0, 0, 0, 0, 0,
                    slot.valueOffset + rootOffset});
        }
        return success();
      };
      Block *functionEntry = &plan.function.getBody().front();
      // Scratch registers are intentionally cleared for every dispatch. Entry
      // captures are immutable SSA values and may remain live in any resumed
      // block even after suspension-live threading has made block operands
      // explicit, so reconstruct them at every non-entry continuation.
      if (id != 0 && failed(restore(functionEntry->getArguments(),
                                    plan.frame->getEntryCaptureLayout(),
                                    /*consumeRoots=*/false)))
        return plan.function.emitOpError(
            "canonical entry capture exceeds the bytecode ABI limit");
      if (failed(restore(block->getArguments(), slots,
                         /*consumeRoots=*/id != 0)))
        return plan.function.emitOpError(
            "canonical frame transfer exceeds the bytecode ABI limit");
      uint64_t jump = emit({Jump});
      instructions[jump].immediate = plan.blockPCs.lookup(block);
      auto rank = plan.blockScheduleRanks.find(block);
      if (rank == plan.blockScheduleRanks.end())
        return plan.function.emitOpError(
            "continuation block has no schedule rank");
      plan.continuations.push_back({plan.index, id, entryPC, rank->second});
      return success();
    };
    Block *entry = &plan.function.getBody().front();
    if (!plan.frame) {
      plan.continuations.push_back(
          {plan.index, 0, plan.blockPCs.lookup(entry), UINT32_MAX});
      return success();
    }
    if (failed(emitEntry(0, entry, plan.frame->getEntryCaptureLayout())))
      return failure();
    for (uint32_t id : plan.frame->getContinuations()) {
      if (id == 0)
        continue;
      Block *block = nullptr;
      for (const ProcessSuspension &suspension : plan.frame->getSuspensions())
        if (suspension.continuationID == id) {
          block = suspension.continuation;
          break;
        }
      if (!block)
        return plan.function.emitOpError("missing continuation block");
      if (failed(emitEntry(id, block, plan.frame->getContinuationLayout(id))))
        return failure();
    }
    llvm::sort(plan.continuations,
               [](const Continuation &left, const Continuation &right) {
                 return left.id < right.id;
               });
    return success();
  }

  LogicalResult emitConstant(FunctionPlan &plan, Value result,
                             const APInt &value,
                             const APInt *unknown = nullptr) {
    uint32_t destination = reg(plan, result);
    Layout layout = plan.layouts[destination];
    emit({Constant, 0, destination, 0, 0, 0, 0,
          addConstant(layout, value, unknown)});
    return success();
  }

  LogicalResult encodeOperation(FunctionPlan &plan, Operation *operation) {
    if (auto constant = dyn_cast<arith::ConstantOp>(operation)) {
      if (auto integer = dyn_cast<IntegerAttr>(constant.getValue()))
        return emitConstant(plan, constant.getResult(), integer.getValue());
      if (auto floating = dyn_cast<FloatAttr>(constant.getValue()))
        return emitConstant(plan, constant.getResult(),
                            floating.getValue().bitcastToAPInt());
      return operation->emitOpError(
          "bytecode requires integer or floating-point constants");
    }
    auto binary = [&](uint16_t opcode, Value result, Value left, Value right) {
      emit({opcode, 0, reg(plan, result), reg(plan, left), reg(plan, right)});
      return success();
    };
    if (auto op = dyn_cast<arith::AddIOp>(operation))
      return binary(Add, op.getResult(), op.getLhs(), op.getRhs());
    if (auto op = dyn_cast<arith::SubIOp>(operation))
      return binary(Sub, op.getResult(), op.getLhs(), op.getRhs());
    if (auto op = dyn_cast<arith::MulIOp>(operation))
      return binary(Mul, op.getResult(), op.getLhs(), op.getRhs());
    if (auto op = dyn_cast<arith::AddFOp>(operation))
      return binary(FAdd, op.getResult(), op.getLhs(), op.getRhs());
    if (auto op = dyn_cast<arith::SubFOp>(operation))
      return binary(FSub, op.getResult(), op.getLhs(), op.getRhs());
    if (auto op = dyn_cast<arith::MulFOp>(operation))
      return binary(FMul, op.getResult(), op.getLhs(), op.getRhs());
    if (auto op = dyn_cast<arith::DivFOp>(operation))
      return binary(FDiv, op.getResult(), op.getLhs(), op.getRhs());
    if (auto op = dyn_cast<arith::NegFOp>(operation)) {
      emit({FNeg, 0, reg(plan, op.getResult()), reg(plan, op.getOperand())});
      return success();
    }
    if (auto op = dyn_cast<arith::DivUIOp>(operation))
      return binary(UDiv, op.getResult(), op.getLhs(), op.getRhs());
    if (auto op = dyn_cast<arith::DivSIOp>(operation))
      return binary(SDiv, op.getResult(), op.getLhs(), op.getRhs());
    if (auto op = dyn_cast<arith::RemUIOp>(operation))
      return binary(URem, op.getResult(), op.getLhs(), op.getRhs());
    if (auto op = dyn_cast<arith::RemSIOp>(operation))
      return binary(SRem, op.getResult(), op.getLhs(), op.getRhs());
    if (auto op = dyn_cast<arith::AndIOp>(operation))
      return binary(And, op.getResult(), op.getLhs(), op.getRhs());
    if (auto op = dyn_cast<arith::OrIOp>(operation))
      return binary(Or, op.getResult(), op.getLhs(), op.getRhs());
    if (auto op = dyn_cast<arith::XOrIOp>(operation))
      return binary(Xor, op.getResult(), op.getLhs(), op.getRhs());
    if (auto op = dyn_cast<arith::ShLIOp>(operation))
      return binary(Shl, op.getResult(), op.getLhs(), op.getRhs());
    if (auto op = dyn_cast<arith::ShRUIOp>(operation))
      return binary(LShr, op.getResult(), op.getLhs(), op.getRhs());
    if (auto op = dyn_cast<arith::ShRSIOp>(operation))
      return binary(AShr, op.getResult(), op.getLhs(), op.getRhs());
    if (auto op = dyn_cast<arith::CmpIOp>(operation)) {
      uint16_t predicate = 0;
      switch (op.getPredicate()) {
      case arith::CmpIPredicate::eq:
        predicate = 0;
        break;
      case arith::CmpIPredicate::ne:
        predicate = 1;
        break;
      case arith::CmpIPredicate::ult:
        predicate = 2;
        break;
      case arith::CmpIPredicate::ule:
        predicate = 3;
        break;
      case arith::CmpIPredicate::ugt:
        predicate = 4;
        break;
      case arith::CmpIPredicate::uge:
        predicate = 5;
        break;
      case arith::CmpIPredicate::slt:
        predicate = 6;
        break;
      case arith::CmpIPredicate::sle:
        predicate = 7;
        break;
      case arith::CmpIPredicate::sgt:
        predicate = 8;
        break;
      case arith::CmpIPredicate::sge:
        predicate = 9;
        break;
      }
      emit({Compare, predicate, reg(plan, op.getResult()),
            reg(plan, op.getLhs()), reg(plan, op.getRhs())});
      return success();
    }
    if (auto op = dyn_cast<arith::CmpFOp>(operation)) {
      uint32_t predicate;
      switch (op.getPredicate()) {
      case arith::CmpFPredicate::OEQ:
        predicate = 0;
        break;
      case arith::CmpFPredicate::UNE:
        predicate = 1;
        break;
      case arith::CmpFPredicate::OLT:
        predicate = 2;
        break;
      case arith::CmpFPredicate::OLE:
        predicate = 3;
        break;
      case arith::CmpFPredicate::OGT:
        predicate = 4;
        break;
      case arith::CmpFPredicate::OGE:
        predicate = 5;
        break;
      default:
        return op.emitOpError(
            "floating comparison predicate is not executable");
      }
      emit({FCompare, static_cast<uint16_t>(predicate),
            reg(plan, op.getResult()), reg(plan, op.getLhs()),
            reg(plan, op.getRhs())});
      return success();
    }
    if (auto op = dyn_cast<arith::SelectOp>(operation)) {
      emit({Select, 0, reg(plan, op.getResult()), reg(plan, op.getTrueValue()),
            reg(plan, op.getFalseValue()), reg(plan, op.getCondition())});
      return success();
    }
    if (auto op = dyn_cast<arith::ExtUIOp>(operation)) {
      emit({Extract, 0, reg(plan, op.getResult()), reg(plan, op.getIn()),
            kInvalidRegister});
      return success();
    }
    if (auto op = dyn_cast<arith::ExtSIOp>(operation)) {
      emit({Extract, 1, reg(plan, op.getResult()), reg(plan, op.getIn()),
            kInvalidRegister});
      return success();
    }
    if (auto op = dyn_cast<arith::TruncIOp>(operation)) {
      emit({Extract, 0, reg(plan, op.getResult()), reg(plan, op.getIn()),
            kInvalidRegister});
      return success();
    }
    if (auto op = dyn_cast<arith::ExtFOp>(operation)) {
      emit({FExt, 0, reg(plan, op.getResult()), reg(plan, op.getIn())});
      return success();
    }
    if (auto op = dyn_cast<arith::TruncFOp>(operation)) {
      emit({FTrunc, 0, reg(plan, op.getResult()), reg(plan, op.getIn())});
      return success();
    }
    if (auto op = dyn_cast<math::PowFOp>(operation))
      return binary(FPow, op.getResult(), op.getLhs(), op.getRhs());
    if (auto op = dyn_cast<arith::IndexCastOp>(operation)) {
      emit({Extract, 0, reg(plan, op.getResult()), reg(plan, op.getIn()),
            kInvalidRegister});
      return success();
    }
    if (auto branch = dyn_cast<cf::BranchOp>(operation)) {
      auto mapping = addMap(plan, branch.getDest()->getArguments(), plan,
                            branch.getDestOperands());
      uint64_t encoded = emit({Jump, 0, 0, static_cast<uint32_t>(mapping.first),
                               static_cast<uint32_t>(mapping.second)});
      plan.branches.push_back({encoded, branch.getDest()});
      return success();
    }
    if (auto branch = dyn_cast<cf::CondBranchOp>(operation)) {
      auto trueMap = addMap(plan, branch.getTrueDest()->getArguments(), plan,
                            branch.getTrueDestOperands());
      uint64_t conditional = emit({Branch, 0, reg(plan, branch.getCondition()),
                                   static_cast<uint32_t>(trueMap.first),
                                   static_cast<uint32_t>(trueMap.second)});
      plan.branches.push_back({conditional, branch.getTrueDest()});
      auto falseMap = addMap(plan, branch.getFalseDest()->getArguments(), plan,
                             branch.getFalseDestOperands());
      uint64_t fallback =
          emit({Jump, 0, 0, static_cast<uint32_t>(falseMap.first),
                static_cast<uint32_t>(falseMap.second)});
      plan.branches.push_back({fallback, branch.getFalseDest()});
      return success();
    }
    if (auto switchOp = dyn_cast<cf::SwitchOp>(operation)) {
      SmallVector<APInt> values;
      if (auto cases = switchOp.getCaseValues())
        llvm::append_range(values, cases->getValues<APInt>());
      auto destinations = switchOp.getCaseDestinations();
      auto caseOperands = switchOp.getCaseOperands();
      if (values.size() != destinations.size() ||
          values.size() != caseOperands.size())
        return switchOp.emitOpError("malformed bytecode switch cases");
      for (auto [value, destination, operands] :
           llvm::zip_equal(values, destinations, caseOperands)) {
        uint32_t caseValue = temporary(plan, switchOp.getFlag().getType());
        uint32_t match =
            temporary(plan, IntegerType::get(operation->getContext(), 1));
        if (caseValue == kInvalidRegister || match == kInvalidRegister)
          return failure();
        emit({Constant, 0, caseValue, 0, 0, 0, 0,
              addConstant(plan.layouts[caseValue], value)});
        emit({Compare, 0, match, reg(plan, switchOp.getFlag()), caseValue});
        auto mapping =
            addMap(plan, destination->getArguments(), plan, operands);
        uint64_t branch =
            emit({Branch, 0, match, static_cast<uint32_t>(mapping.first),
                  static_cast<uint32_t>(mapping.second)});
        plan.branches.push_back({branch, destination});
      }
      auto mapping =
          addMap(plan, switchOp.getDefaultDestination()->getArguments(), plan,
                 switchOp.getDefaultOperands());
      uint64_t fallback =
          emit({Jump, 0, 0, static_cast<uint32_t>(mapping.first),
                static_cast<uint32_t>(mapping.second)});
      plan.branches.push_back({fallback, switchOp.getDefaultDestination()});
      return success();
    }
    if (auto constant = dyn_cast<sim::SimBytesConstantOp>(operation)) {
      ArrayRef<uint8_t> bytes(
          reinterpret_cast<const uint8_t *>(constant.getValue().data()),
          constant.getValue().size());
      emit({Constant, 0, reg(plan, constant.getResult()), 0, 0, 0, 0,
            addBytesConstant(bytes)});
      return success();
    }
    if (auto op = dyn_cast<sim::SimContextRuntimeOp>(operation)) {
      emit({Move, 0, reg(plan, op.getResult()), reg(plan, op.getContext())});
      return success();
    }
    if (auto op = dyn_cast<sim::SimStatusCheckOp>(operation)) {
      emit({Fail, 0, 0, reg(plan, op.getStatus())});
      return success();
    }
    if (auto op = dyn_cast<sim::SimFinishOp>(operation))
      return emitIntrinsic(plan, kIntrinsicFinish, {op.getVerbosity()}, {});
    if (auto op = dyn_cast<sim::SimStopOp>(operation))
      return emitIntrinsic(plan, kIntrinsicFinish, {op.getVerbosity()}, {});
    if (auto op = dyn_cast<sim::SimFatalOp>(operation))
      return emitIntrinsic(plan, kIntrinsicFatal, {op.getVerbosity()}, {});
    if (auto op = dyn_cast<sim::SimTerminationRequestedOp>(operation))
      return emitIntrinsic(plan, kIntrinsicTerminationRequested, {},
                           {op.getResult()});
    if (auto op = dyn_cast<sim::SimTimeNowOp>(operation))
      return emitIntrinsic(plan, kIntrinsicTimeNow, {}, {op.getResult()});
    if (auto op = dyn_cast<sim::SimDisplayOp>(operation))
      return encodeDisplay(plan, op);
    if (auto op = dyn_cast<sim::SimFileOpenMCDOp>(operation))
      return emitIntrinsic(plan, kIntrinsicFileOpenMCD, {op.getPath()},
                           {op.getDescriptor()});
    if (auto op = dyn_cast<sim::SimFileOpenOp>(operation))
      return emitIntrinsic(plan, kIntrinsicFileOpen,
                           {op.getPath(), op.getMode()}, {op.getDescriptor()});
    if (auto op = dyn_cast<sim::SimFileOpenStringMCDOp>(operation))
      return emitIntrinsic(plan, kIntrinsicFileOpenStringMCD, {op.getPath()},
                           {op.getDescriptor()});
    if (auto op = dyn_cast<sim::SimFileOpenStringOp>(operation))
      return emitIntrinsic(plan, kIntrinsicFileOpenString,
                           {op.getPath(), op.getMode()}, {op.getDescriptor()});
    if (auto op = dyn_cast<sim::SimFileGetlineStringOp>(operation))
      return emitIntrinsic(plan, kIntrinsicFileGetlineString,
                           {op.getDescriptor()}, {op.getData(), op.getCount()});
    if (auto op = dyn_cast<sim::SimFileCloseOp>(operation))
      return emitIntrinsic(plan, kIntrinsicFileClose, {op.getDescriptor()}, {});
    if (auto op = dyn_cast<sim::SimFileFlushOp>(operation))
      return emitIntrinsic(plan, kIntrinsicFileFlush, {op.getDescriptor()}, {});
    if (auto op = dyn_cast<sim::SimFileGetcOp>(operation))
      return emitIntrinsic(plan, kIntrinsicFileGetc, {op.getDescriptor()},
                           {op.getResult()});
    if (auto op = dyn_cast<sim::SimFileUngetcOp>(operation))
      return emitIntrinsic(plan, kIntrinsicFileUngetc,
                           {op.getByte(), op.getDescriptor()},
                           {op.getResult()});
    if (auto op = dyn_cast<sim::SimFileGetlineOp>(operation))
      return emitIntrinsic(plan, kIntrinsicFileGetline, {op.getDescriptor()},
                           {op.getData(), op.getCount()});
    if (auto op = dyn_cast<sim::SimFileReadPackedOp>(operation))
      return emitIntrinsic(plan, kIntrinsicFileReadPacked, {op.getDescriptor()},
                           {op.getData(), op.getCount()});
    if (auto op = dyn_cast<sim::SimFileEofOp>(operation))
      return emitIntrinsic(plan, kIntrinsicFileEof, {op.getDescriptor()},
                           {op.getResult()});
    if (auto op = dyn_cast<sim::SimFileSeekOp>(operation))
      return emitIntrinsic(plan, kIntrinsicFileSeek,
                           {op.getDescriptor(), op.getOffset(), op.getOrigin()},
                           {op.getResult()});
    if (auto op = dyn_cast<sim::SimFileTellOp>(operation))
      return emitIntrinsic(plan, kIntrinsicFileTell, {op.getDescriptor()},
                           {op.getResult()});
    if (auto op = dyn_cast<sim::SimFileRewindOp>(operation))
      return emitIntrinsic(plan, kIntrinsicFileRewind, {op.getDescriptor()},
                           {op.getResult()});
    if (auto op = dyn_cast<sim::SimSpawnOp>(operation)) {
      auto found = indices.find(op.getCallee());
      if (found == indices.end())
        return op.emitOpError("spawn target has no bytecode body");
      FunctionPlan &callee = plans[found->second];
      if (!callee.frame ||
          callee.function.getEntryKind() == sim::EntryKind::Function)
        return op.emitOpError("spawn target is not a simulation process");
      SmallVector<Value> captures(op.getOperands());
      return emitIntrinsic(plan, kIntrinsicSpawn, captures, {op.getProcess()},
                           found->second);
    }
    if (auto op = dyn_cast<sim::SimNBAEnqueueOp>(operation)) {
      SmallVector<Value> inputs{op.getValue(), op.getDestination()};
      if (op.getDelay())
        inputs.push_back(op.getDelay());
      return emitIntrinsic(plan, kIntrinsicNBA, inputs, {});
    }
    if (auto op = dyn_cast<sim::SimEventTriggerOp>(operation)) {
      SmallVector<Value> inputs{op.getEvent()};
      if (op.getDelay())
        inputs.push_back(op.getDelay());
      return emitIntrinsic(plan, kIntrinsicEventTrigger, inputs, {},
                           op.getNonblocking() ? 1 : 0);
    }
    if (auto op = dyn_cast<sim::SimEventTriggeredOp>(operation))
      return emitIntrinsic(plan, kIntrinsicEventTriggered, {op.getEvent()},
                           {op.getResult()});
    if (auto op = dyn_cast<sim::SimEventEqualOp>(operation)) {
      emit({Compare, 0, reg(plan, op.getResult()), reg(plan, op.getLhs()),
            reg(plan, op.getRhs())});
      return success();
    }
    if (auto op = dyn_cast<sim::SimContainerSizeOp>(operation))
      return emitIntrinsic(plan, kIntrinsicContainerSize, {op.getContainer()},
                           {op.getResult()});
    if (auto op = dyn_cast<sim::SimContainerCreateLikeOp>(operation))
      return emitIntrinsic(plan, kIntrinsicContainerCreateLike,
                           {op.getPreferred(), op.getFallback(), op.getSize()},
                           {op.getResult()});
    if (auto op = dyn_cast<sim::SimContainerCreateOp>(operation)) {
      SmallVector<uint8_t> traceSlots;
      for (auto [offset, kind] :
           llvm::zip_equal(op.getTraceOffsets(), op.getTraceKinds())) {
        append64(traceSlots, static_cast<uint64_t>(offset));
        append32(traceSlots, static_cast<uint32_t>(kind));
        append32(traceSlots, 0);
      }
      return emitIntrinsicRegisters(
          plan, kIntrinsicContainerCreate,
          {emitU64Constant(plan, op.getContainerKind()),
           emitU64Constant(plan, op.getTypeId()),
           emitU64Constant(plan, op.getElementKind()),
           emitU64Constant(plan, op.getElementFlags()),
           emitU64Constant(plan, op.getValueSize()),
           emitU64Constant(plan, op.getAlignment()),
           emitU64Constant(plan, op.getBitWidth()),
           emitBytesConstant(plan, traceSlots), reg(plan, op.getSize()),
           emitU64Constant(plan, op.getBound())},
          {reg(plan, op.getResult())});
    }
    if (auto op = dyn_cast<sim::SimContainerCloneOp>(operation))
      return emitIntrinsic(plan, kIntrinsicContainerClone, {op.getInput()},
                           {op.getResult()});
    if (auto op = dyn_cast<sim::SimContainerDeleteOp>(operation))
      return emitIntrinsic(plan, kIntrinsicContainerDelete, {op.getContainer()},
                           {});
    if (auto op = dyn_cast<sim::SimQueueDeleteOp>(operation))
      return emitIntrinsic(plan, kIntrinsicQueueDelete,
                           {op.getQueue(), op.getIndex()}, {});
    if (auto op = dyn_cast<sim::SimQueueInsertOp>(operation))
      return emitIntrinsic(plan, kIntrinsicQueueInsert,
                           {op.getQueue(), op.getIndex(), op.getValue()}, {});
    if (auto op = dyn_cast<sim::SimRandomNextOp>(operation))
      return emitIntrinsic(plan, kIntrinsicRandomNext, {}, {op.getResult()});
    if (auto op = dyn_cast<sim::SimRandomSeedOp>(operation))
      return emitIntrinsic(plan, kIntrinsicRandomSeed, {op.getSeed()}, {});
    if (auto op = dyn_cast<sim::SimRandomBoundedOp>(operation))
      return emitIntrinsic(plan, kIntrinsicRandomBounded, {op.getBound()},
                           {op.getResult()});
    if (auto op = dyn_cast<sim::SimContainerReadOp>(operation))
      return emitIntrinsic(plan, kIntrinsicContainerRead,
                           {op.getContainer(), op.getIndex()},
                           {op.getResult()});
    if (auto op = dyn_cast<sim::SimContainerWriteOp>(operation))
      return emitIntrinsic(plan, kIntrinsicContainerWrite,
                           {op.getContainer(), op.getIndex(), op.getValue()},
                           {});
    if (auto op = dyn_cast<sim::SimAssocCreateOp>(operation)) {
      SmallVector<uint8_t> traceSlots;
      for (auto [offset, kind] :
           llvm::zip_equal(op.getTraceOffsets(), op.getTraceKinds())) {
        append64(traceSlots, static_cast<uint64_t>(offset));
        append32(traceSlots, static_cast<uint32_t>(kind));
        append32(traceSlots, 0);
      }
      return emitIntrinsicRegisters(
          plan, kIntrinsicAssocCreate,
          {emitU64Constant(plan, op.getTypeId()),
           emitU64Constant(plan, op.getElementKind()),
           emitU64Constant(plan, op.getElementFlags()),
           emitU64Constant(plan, op.getValueSize()),
           emitU64Constant(plan, op.getAlignment()),
           emitU64Constant(plan, op.getBitWidth()),
           emitBytesConstant(plan, traceSlots),
           emitU64Constant(plan, op.getKeyKind()),
           emitU64Constant(plan, op.getKeyWidth())},
          {reg(plan, op.getResult())});
    }
    if (auto op = dyn_cast<sim::SimAssocReadOp>(operation))
      return emitIntrinsic(plan, kIntrinsicAssocRead,
                           {op.getArray(), op.getKey()}, {op.getResult()});
    if (auto op = dyn_cast<sim::SimAssocWriteOp>(operation))
      return emitIntrinsic(plan, kIntrinsicAssocWrite,
                           {op.getArray(), op.getKey(), op.getValue()}, {});
    if (auto op = dyn_cast<sim::SimAssocExistsOp>(operation))
      return emitIntrinsic(plan, kIntrinsicAssocExists,
                           {op.getArray(), op.getKey()}, {op.getResult()});
    if (auto op = dyn_cast<sim::SimAssocDeleteOp>(operation))
      return emitIntrinsic(plan, kIntrinsicAssocDelete,
                           {op.getArray(), op.getKey()}, {});
    if (auto op = dyn_cast<sim::SimAssocSetDefaultOp>(operation))
      return emitIntrinsic(plan, kIntrinsicAssocDefault,
                           {op.getArray(), op.getValue()}, {});
    if (auto op = dyn_cast<sim::SimAssocTraverseOp>(operation))
      return emitIntrinsicRegisters(
          plan, kIntrinsicAssocTraverse,
          {reg(plan, op.getArray()), reg(plan, op.getKey()),
           emitU64Constant(plan,
                           static_cast<uint64_t>(
                               static_cast<int64_t>(
                                   static_cast<int32_t>(op.getDirection())))),
           emitU64Constant(plan, op.getEndpoint() ? 1 : 0)},
          {reg(plan, op.getResultKey()), reg(plan, op.getSuccess())});
    if (auto op = dyn_cast<sim::SimStringLiteralOp>(operation)) {
      StringRef value = op.getValue();
      uint32_t bytes = emitBytesConstant(
          plan, ArrayRef<uint8_t>(
                    reinterpret_cast<const uint8_t *>(value.data()),
                    value.size()));
      if (bytes == kInvalidRegister)
        return op.emitOpError("cannot allocate literal byte register");
      return emitIntrinsicRegisters(plan, kIntrinsicStringLiteral, {bytes},
                                    {reg(plan, op.getResult())});
    }
    if (auto op = dyn_cast<sim::SimStringFromPackedOp>(operation))
      return emitIntrinsic(plan, kIntrinsicStringFromPacked, {op.getInput()},
                           {op.getResult()});
    if (auto op = dyn_cast<sim::SimStringToPackedOp>(operation))
      return emitIntrinsic(plan, kIntrinsicStringToPacked, {op.getInput()},
                           {op.getResult()});
    if (auto op = dyn_cast<sim::SimStringConcatOp>(operation)) {
      SmallVector<Value> inputs(op.getInputs());
      return emitIntrinsic(plan, kIntrinsicStringConcat, inputs,
                           {op.getResult()});
    }
    if (auto op = dyn_cast<sim::SimStringRepeatOp>(operation))
      return emitIntrinsic(plan, kIntrinsicStringRepeat,
                           {op.getInput(), op.getCount()}, {op.getResult()});
    if (auto op = dyn_cast<sim::SimStringLengthOp>(operation))
      return emitIntrinsic(plan, kIntrinsicStringLength, {op.getInput()},
                           {op.getResult()});
    if (auto op = dyn_cast<sim::SimStringGetcOp>(operation))
      return emitIntrinsic(plan, kIntrinsicStringGetc,
                           {op.getInput(), op.getIndex()}, {op.getResult()});
    if (auto op = dyn_cast<sim::SimStringPutcOp>(operation))
      return emitIntrinsic(plan, kIntrinsicStringPutc,
                           {op.getInput(), op.getIndex(), op.getCharacter()},
                           {op.getResult()});
    if (auto op = dyn_cast<sim::SimStringSubstrOp>(operation))
      return emitIntrinsic(
          plan, kIntrinsicStringSubstr,
          {op.getInput(), op.getLeft(), op.getRight()}, {op.getResult()});
    if (auto op = dyn_cast<sim::SimStringCompareOp>(operation)) {
      uint32_t mode =
          emitU64Constant(plan, op.getCaseInsensitive() ? 1 : 0);
      return emitIntrinsicRegisters(
          plan, kIntrinsicStringCompare,
          {reg(plan, op.getLhs()), reg(plan, op.getRhs()), mode},
          {reg(plan, op.getResult())});
    }
    if (auto op = dyn_cast<sim::SimStringCaseConvertOp>(operation)) {
      uint32_t mode = emitU64Constant(plan, op.getToUpper() ? 1 : 0);
      return emitIntrinsicRegisters(
          plan, kIntrinsicStringCaseConvert,
          {reg(plan, op.getInput()), mode}, {reg(plan, op.getResult())});
    }
    if (auto op = dyn_cast<sim::SimStringParseIntegerOp>(operation)) {
      uint32_t radix = emitU64Constant(plan, op.getRadix());
      return emitIntrinsicRegisters(
          plan, kIntrinsicStringParseInteger,
          {reg(plan, op.getInput()), radix}, {reg(plan, op.getResult())});
    }
    if (auto op = dyn_cast<sim::SimStringParseRealOp>(operation))
      return emitIntrinsic(plan, kIntrinsicStringParseReal, {op.getInput()},
                           {op.getResult()});
    if (auto op = dyn_cast<sim::SimStringFormatIntegerOp>(operation)) {
      uint32_t radix = emitU64Constant(plan, op.getRadix());
      uint32_t signedMode =
          emitU64Constant(plan, op.getIsSigned() ? 1 : 0);
      return emitIntrinsicRegisters(
          plan, kIntrinsicStringFormatInteger,
          {reg(plan, op.getInput()), radix, signedMode},
          {reg(plan, op.getResult())});
    }
    if (auto op = dyn_cast<sim::SimStringFormatRealOp>(operation))
      return emitIntrinsic(plan, kIntrinsicStringFormatReal, {op.getInput()},
                           {op.getResult()});
    if (isa<sim::SimClassNullOp, sim::SimManagedNullOp>(operation)) {
      uint32_t destination = reg(plan, operation->getResult(0));
      emit({Constant, 0, destination, 0, 0, 0, 0,
            addZeroConstant(plan.layouts[destination])});
      return success();
    }
    if (auto op = dyn_cast<sim::SimManagedIsNullOp>(operation)) {
      uint32_t input = reg(plan, op.getInput());
      uint32_t zero = addZeroConstant(plan.layouts[input]);
      uint32_t constant = temporary(plan, op.getInput().getType());
      if (constant == kInvalidRegister)
        return failure();
      emit({Constant, 0, constant, 0, 0, 0, 0, zero});
      emit({Compare, OBELISK_RT_DB_CMP_EQ, reg(plan, op.getResult()), input,
            constant});
      return success();
    }
    if (isa<sim::SimEventNullOp>(operation)) {
      uint32_t destination = reg(plan, operation->getResult(0));
      const Layout &layout = plan.layouts[destination];
      if (layout.kind != Handle || layout.size != 32)
        return operation->emitOpError(
            "event null requires the canonical handle layout");
      SmallVector<uint8_t, 32> bytes(layout.size, 0);
      write32(bytes, 0, OBELISK_RT_DESCRIPTOR_EVENT);
      write64(bytes, 8, UINT64_MAX);
      write64(bytes, 16, UINT64_MAX);
      emit({Constant, 0, destination, 0, 0, 0, 0, addRawConstant(bytes)});
      return success();
    }
    if (auto op = dyn_cast<sim::SimReferencePathIndexOp>(operation))
      return emitIntrinsic(
          plan, kIntrinsicReferencePathIndex,
          {op.getContainer(), op.getIndex(), op.getOwnerReference()},
          {op.getResult()});
    if (auto op = dyn_cast<sim::SimReferencePathAssocOp>(operation))
      return emitIntrinsic(
          plan, kIntrinsicReferencePathAssoc,
          {op.getArray(), op.getKey(), op.getOwnerReference()},
          {op.getResult()});
    if (auto op = dyn_cast<sim::SimArgumentRefFromPathOp>(operation))
      return emitIntrinsic(plan, kIntrinsicArgumentRefFromPath, {op.getInput()},
                           {op.getResult()});
    if (auto op = dyn_cast<sim::SimClassAllocOp>(operation)) {
      auto type = cast<sim::ClassHandleType>(op.getResult().getType());
      FailureOr<uint64_t> id = classID(type.getClassName(), operation);
      if (failed(id))
        return failure();
      uint32_t classRegister = emitU64Constant(plan, *id);
      return emitIntrinsicRegisters(plan, kIntrinsicClassAlloc, {classRegister},
                                    {reg(plan, op.getResult())});
    }
    if (auto op = dyn_cast<sim::SimClassCopyOp>(operation)) {
      auto type = cast<sim::ClassHandleType>(op.getResult().getType());
      FailureOr<uint64_t> id = classID(type.getClassName(), operation);
      if (failed(id))
        return failure();
      uint32_t classRegister = emitU64Constant(plan, *id);
      return emitIntrinsicRegisters(plan, kIntrinsicClassCopy,
                                    {reg(plan, op.getSource()), classRegister},
                                    {reg(plan, op.getResult())});
    }
    if (auto op = dyn_cast<sim::SimClassIsInstanceOp>(operation)) {
      FailureOr<uint64_t> id = classID(op.getTargetAttr(), operation);
      if (failed(id))
        return failure();
      uint32_t classRegister = emitU64Constant(plan, *id);
      return emitIntrinsicRegisters(plan, kIntrinsicClassIsInstance,
                                    {reg(plan, op.getObject()), classRegister},
                                    {reg(plan, op.getResult())});
    }
    if (auto op = dyn_cast<sim::SimClassIdOp>(operation))
      return emitIntrinsic(plan, kIntrinsicClassID, {op.getObject()},
                           {op.getResult()});
    if (auto op = dyn_cast<sim::SimClassCastOp>(operation)) {
      auto type = cast<sim::ClassHandleType>(op.getResult().getType());
      FailureOr<uint64_t> id = classID(type.getClassName(), operation);
      if (failed(id))
        return failure();
      uint32_t classRegister = emitU64Constant(plan, *id);
      return emitIntrinsicRegisters(plan, kIntrinsicClassCast,
                                    {reg(plan, op.getObject()), classRegister},
                                    {reg(plan, op.getResult())});
    }
    if (auto op = dyn_cast<sim::SimClassFieldRefOp>(operation)) {
      auto field =
          SymbolTable::lookupNearestSymbolFrom<sim::SimClassFieldDeclOp>(
              op, op.getFieldAttr());
      auto offset =
          field ? field->getAttrOfType<IntegerAttr>("offset") : IntegerAttr{};
      if (!field || !offset)
        return op.emitOpError("managed field has no bytecode layout");
      uint32_t offsetRegister =
          emitU64Constant(plan, offset.getValue().getZExtValue());
      return emitIntrinsicRegisters(plan, kIntrinsicClassFieldRef,
                                    {reg(plan, op.getObject()), offsetRegister},
                                    {reg(plan, op.getResult())});
    }
    if (auto op = dyn_cast<sim::SimArgumentRefFromRefOp>(operation))
      return emitIntrinsic(plan, kIntrinsicArgumentRefFromRef, {op.getInput()},
                           {op.getResult()});
    if (auto op = dyn_cast<sim::SimArgumentRefFromManagedOp>(operation))
      return emitIntrinsic(plan, kIntrinsicArgumentRefFromManaged,
                           {op.getInput()}, {op.getResult()});
    if (auto op = dyn_cast<sim::SimArgumentRefLoadOp>(operation)) {
      FailureOr<ManagedValueStorage> storage =
          getManagedValueStorage(op.getResult().getType(), dataLayout);
      std::optional<uint32_t> width = simulationWidth(op.getResult().getType());
      if (failed(storage) || !width)
        return op.emitOpError("argument reference has no bytecode layout");
      uint32_t flags =
          (storage->fourState ? 1u : 0u) |
          ((isa<sim::StringType>(op.getResult().getType())
                ? OBELISK_RT_ARGUMENT_VALUE_STRING
            : sim::isManagedHandleType(op.getResult().getType())
                ? OBELISK_RT_ARGUMENT_VALUE_CLASS
                : OBELISK_RT_ARGUMENT_VALUE_BITS)
           << 1);
      return emitIntrinsicRegisters(plan, kIntrinsicArgumentRefLoad,
                                    {reg(plan, op.getReference()),
                                     emitU64Constant(plan, storage->planeSize),
                                     emitU64Constant(plan, *width),
                                     emitU64Constant(plan, flags)},
                                    {reg(plan, op.getResult())});
    }
    if (auto op = dyn_cast<sim::SimArgumentRefStoreOp>(operation)) {
      FailureOr<ManagedValueStorage> storage =
          getManagedValueStorage(op.getValue().getType(), dataLayout);
      std::optional<uint32_t> width = simulationWidth(op.getValue().getType());
      if (failed(storage) || !width)
        return op.emitOpError("argument reference has no bytecode layout");
      uint32_t flags =
          (storage->fourState ? 1u : 0u) |
          ((isa<sim::StringType>(op.getValue().getType())
                ? OBELISK_RT_ARGUMENT_VALUE_STRING
            : sim::isManagedHandleType(op.getValue().getType())
                ? OBELISK_RT_ARGUMENT_VALUE_CLASS
                : OBELISK_RT_ARGUMENT_VALUE_BITS)
           << 1);
      return emitIntrinsicRegisters(
          plan, kIntrinsicArgumentRefStore,
          {reg(plan, op.getReference()), reg(plan, op.getValue()),
           emitU64Constant(plan, storage->planeSize),
           emitU64Constant(plan, *width), emitU64Constant(plan, flags)},
          {});
    }
    if (auto op = dyn_cast<sim::SimManagedLoadOp>(operation)) {
      FailureOr<ManagedValueStorage> storage =
          getManagedValueStorage(op.getResult().getType(), dataLayout);
      if (failed(storage))
        return op.emitOpError("managed result has no bytecode field layout");
      uint32_t sizeRegister = emitU64Constant(plan, storage->planeSize);
      return emitIntrinsicRegisters(
          plan, kIntrinsicManagedLoad,
          {reg(plan, op.getReference()), sizeRegister},
          {reg(plan, op.getResult())});
    }
    if (auto op = dyn_cast<sim::SimManagedStoreOp>(operation)) {
      FailureOr<ManagedValueStorage> storage =
          getManagedValueStorage(op.getValue().getType(), dataLayout);
      if (failed(storage))
        return op.emitOpError("managed value has no bytecode field layout");
      uint32_t sizeRegister = emitU64Constant(plan, storage->planeSize);
      return emitIntrinsicRegisters(plan, kIntrinsicManagedStore,
                                    {reg(plan, op.getReference()),
                                     reg(plan, op.getValue()), sizeRegister},
                                    {});
    }
    if (auto op = dyn_cast<sim::SimManagedNBAEnqueueOp>(operation)) {
      FailureOr<ManagedValueStorage> storage =
          getManagedValueStorage(op.getValue().getType(), dataLayout);
      if (failed(storage))
        return op.emitOpError("managed NBA value has no bytecode field layout");
      SmallVector<uint32_t> inputs{reg(plan, op.getDestination()),
                                   reg(plan, op.getValue()),
                                   emitU64Constant(plan, storage->planeSize)};
      if (op.getDelay())
        inputs.push_back(reg(plan, op.getDelay()));
      return emitIntrinsicRegisters(plan, kIntrinsicManagedNBA, inputs, {});
    }
    if (auto op = dyn_cast<sim::SimReferencePathNBAEnqueueOp>(operation)) {
      FailureOr<ManagedValueStorage> storage =
          getManagedValueStorage(op.getValue().getType(), dataLayout);
      if (failed(storage))
        return op.emitOpError(
            "reference-path NBA value has no bytecode field layout");
      SmallVector<uint32_t> inputs{reg(plan, op.getDestination()),
                                   reg(plan, op.getValue()),
                                   emitU64Constant(plan, storage->planeSize)};
      if (op.getDelay())
        inputs.push_back(reg(plan, op.getDelay()));
      return emitIntrinsicRegisters(plan, kIntrinsicManagedNBA, inputs, {});
    }
    if (auto op = dyn_cast<sim::SimClassDirectCallOp>(operation))
      return encodeClassDirectCall(plan, op);
    if (auto op = dyn_cast<sim::SimClassVirtualCallOp>(operation))
      return encodeClassVirtualCall(plan, op);
    if (auto op = dyn_cast<sim::SimWeakCreateOp>(operation)) {
      auto wrapperType = cast<sim::ClassHandleType>(op.getResult().getType());
      FailureOr<uint64_t> id = classID(wrapperType.getClassName(), operation);
      if (failed(id))
        return failure();
      return emitIntrinsicRegisters(
          plan, kIntrinsicWeakCreate,
          {reg(plan, op.getReferent()), emitU64Constant(plan, *id)},
          {reg(plan, op.getResult())});
    }
    if (auto op = dyn_cast<sim::SimWeakGetOp>(operation))
      return emitIntrinsic(plan, kIntrinsicWeakGet, {op.getWeak()},
                           {op.getResult()});
    if (auto op = dyn_cast<sim::SimWeakClearOp>(operation))
      return emitIntrinsic(plan, kIntrinsicWeakClear, {op.getWeak()}, {});
    if (isa<sim::SimGCSafepointOp>(operation))
      return emitIntrinsic(plan, kIntrinsicGCSafepoint, {}, {});
    if (auto call = dyn_cast<sim::SimCallOp>(operation))
      return encodeCall(plan, call);
    if (auto call = dyn_cast<sim::SimTaskCallOp>(operation))
      return encodeTaskCall(plan, call);
    if (auto call = dyn_cast<sim::SimDPICallOp>(operation))
      return encodeDPICall(plan, call);
    if (auto returnOp = dyn_cast<sim::SimReturnOp>(operation))
      return encodeReturn(plan, returnOp);
    if (auto constant = dyn_cast<sim::SimLogicConstantOp>(operation)) {
      APInt unknown = constant.getUnknown();
      return emitConstant(plan, constant.getResult(), constant.getValue(),
                          &unknown);
    }
    if (auto op = dyn_cast<sim::SimLogicCountBitsOp>(operation)) {
      SmallVector<Value> inputs{op.getInput()};
      llvm::append_range(inputs, op.getControls());
      return emitIntrinsic(plan, kIntrinsicCountBits, inputs, {op.getResult()});
    }
    if (auto op = dyn_cast<sim::SimLogicClog2Op>(operation))
      return emitIntrinsic(plan, kIntrinsicClog2, {op.getInput()},
                           {op.getResult()});
    if (auto constant = dyn_cast<sim::SimTimeConstantOp>(operation)) {
      APInt value(64, constant.getValue());
      return emitConstant(plan, constant.getResult(), value);
    }
    if (auto add = dyn_cast<sim::SimTimeAddOp>(operation))
      return binary(Add, add.getResult(), add.getLhs(), add.getRhs());
    if (auto op = dyn_cast<sim::SimTimeToRealOp>(operation)) {
      uint32_t scale = temporary(plan, IntegerType::get(op.getContext(), 64));
      if (scale == kInvalidRegister)
        return failure();
      emit({Constant, 0, scale, 0, 0, 0, 0,
            addConstant(plan.layouts[scale], APInt(64, op.getScale()))});
      return emitIntrinsicRegisters(plan, kIntrinsicTimeToReal,
                                    {reg(plan, op.getInput()), scale},
                                    {reg(plan, op.getResult())});
    }
    if (auto op = dyn_cast<sim::SimTimeFromRealOp>(operation)) {
      Type i64 = IntegerType::get(op.getContext(), 64);
      uint32_t scale = temporary(plan, i64);
      uint32_t quantum = temporary(plan, i64);
      if (scale == kInvalidRegister || quantum == kInvalidRegister)
        return failure();
      emit({Constant, 0, scale, 0, 0, 0, 0,
            addConstant(plan.layouts[scale], APInt(64, op.getScale()))});
      emit({Constant, 0, quantum, 0, 0, 0, 0,
            addConstant(plan.layouts[quantum], APInt(64, op.getQuantum()))});
      return emitIntrinsicRegisters(plan, kIntrinsicTimeFromReal,
                                    {reg(plan, op.getInput()), scale, quantum},
                                    {reg(plan, op.getResult())});
    }
    if (auto op = dyn_cast<sim::SimRealFromIntegerOp>(operation))
      return emitIntrinsic(plan, kIntrinsicRealFromInteger, {op.getInput()},
                           {op.getResult()}, op.getIsSigned() ? 1 : 0);
    if (auto op = dyn_cast<sim::SimRealToIntegerOp>(operation))
      return emitIntrinsic(plan, kIntrinsicRealToInteger, {op.getInput()},
                           {op.getResult()}, op.getIsSigned() ? 1 : 0);
    if (auto scale = dyn_cast<sim::SimTimeScaleOp>(operation)) {
      uint32_t multiplier = temporary(plan, scale.getResult().getType());
      if (multiplier == kInvalidRegister)
        return failure();
      APInt value(64, scale.getScale());
      emit({Constant, 0, multiplier, 0, 0, 0, 0,
            addConstant(plan.layouts[multiplier], value)});
      emit({Mul, 0, reg(plan, scale.getResult()), reg(plan, scale.getInput()),
            multiplier});
      return success();
    }
    if (isa<sim::SimLogicFromBitsOp, sim::SimLogicToBitsOp,
            sim::SimPackedFlattenOp, sim::SimPackedUnflattenOp>(operation)) {
      emit({Extract, 0, reg(plan, operation->getResult(0)),
            reg(plan, operation->getOperand(0)), kInvalidRegister});
      return success();
    }
    if (auto op = dyn_cast<sim::SimLogicResizeOp>(operation)) {
      emit({Extract, static_cast<uint16_t>(op.getIsSigned()),
            reg(plan, op.getResult()), reg(plan, op.getInput()),
            kInvalidRegister});
      return success();
    }
    if (auto op = dyn_cast<sim::SimLogicIsTrueOp>(operation)) {
      emit({Reduce, 6, reg(plan, op.getResult()), reg(plan, op.getInput())});
      return success();
    }
    if (auto op = dyn_cast<sim::SimLogicMuxOp>(operation)) {
      emit({Select, 1, reg(plan, op.getResult()), reg(plan, op.getTrueValue()),
            reg(plan, op.getFalseValue()), reg(plan, op.getCondition())});
      return success();
    }
    if (auto op = dyn_cast<sim::SimLogicUnaryOp>(operation)) {
      switch (op.getKind()) {
      case sim::UnaryKind::Plus:
        emit({Move, 0, reg(plan, op.getResult()), reg(plan, op.getInput())});
        break;
      case sim::UnaryKind::BitNot:
        emit({Not, 0, reg(plan, op.getResult()), reg(plan, op.getInput())});
        break;
      case sim::UnaryKind::LogicalNot:
        emit({Reduce, 7, reg(plan, op.getResult()), reg(plan, op.getInput())});
        break;
      case sim::UnaryKind::Negate: {
        uint32_t zero =
            temporaryLike(plan, op.getInput().getType(), op.getResult());
        if (zero == kInvalidRegister)
          return failure();
        emit({Constant, 0, zero, 0, 0, 0, 0,
              addZeroConstant(plan.layouts[zero])});
        emit({Sub, 0, reg(plan, op.getResult()), zero,
              reg(plan, op.getInput())});
        break;
      }
      }
      return success();
    }
    if (auto op = dyn_cast<sim::SimLogicReductionOp>(operation)) {
      emit({Reduce, static_cast<uint16_t>(op.getKind()),
            reg(plan, op.getResult()), reg(plan, op.getInput())});
      return success();
    }
    if (auto op = dyn_cast<sim::SimLogicBinaryOp>(operation))
      return encodeLogicBinary(plan, op);
    if (auto op = dyn_cast<sim::SimLogicLogicalOp>(operation)) {
      Type truthType = sim::LogicType::get(op.getContext(), 1);
      uint32_t leftTruth = temporaryLike(plan, truthType, op.getResult());
      uint32_t rightTruth = temporaryLike(plan, truthType, op.getResult());
      if (leftTruth == kInvalidRegister || rightTruth == kInvalidRegister)
        return failure();
      emit({Reduce, 8, leftTruth, reg(plan, op.getLhs())});
      emit({Reduce, 8, rightTruth, reg(plan, op.getRhs())});
      emit({op.getKind() == sim::LogicalKind::And ? And : Or, 0,
            reg(plan, op.getResult()), leftTruth, rightTruth});
      return success();
    }
    if (auto op = dyn_cast<sim::SimLogicShiftOp>(operation)) {
      uint16_t opcode = op.getKind() == sim::ShiftKind::Left    ? Shl
                        : op.getKind() == sim::ShiftKind::Right ? LShr
                                                                : AShr;
      return binary(opcode, op.getResult(), op.getInput(), op.getAmount());
    }
    if (auto op = dyn_cast<sim::SimLogicCompareOp>(operation))
      return encodeLogicCompare(plan, op);
    if (auto op = dyn_cast<sim::SimLogicExtractOp>(operation)) {
      emit({Extract, 0, reg(plan, op.getResult()), reg(plan, op.getInput()),
            kInvalidRegister, 0, 0, op.getLowBit()});
      return success();
    }
    if (auto op = dyn_cast<sim::SimLogicDynExtractOp>(operation)) {
      emit({Extract, 0, reg(plan, op.getResult()), reg(plan, op.getInput()),
            reg(plan, op.getLowBit())});
      return success();
    }
    if (auto op = dyn_cast<sim::SimBitsDynExtractOp>(operation)) {
      emit({Extract, 0, reg(plan, op.getResult()), reg(plan, op.getInput()),
            reg(plan, op.getLowBit())});
      return success();
    }
    if (auto op = dyn_cast<sim::SimLogicInsertOp>(operation)) {
      emit({Insert, 0, reg(plan, op.getResult()), reg(plan, op.getInput()),
            reg(plan, op.getReplacement()), 0, 0, op.getLowBit()});
      return success();
    }
    if (auto op = dyn_cast<sim::SimLogicConcatOp>(operation))
      return encodeConcat(plan, op);
    if (auto op = dyn_cast<sim::SimLogicReplicateOp>(operation))
      return encodeReplicate(plan, op);
    if (auto op = dyn_cast<sim::SimAggregateDefaultOp>(operation)) {
      uint32_t destination = reg(plan, op.getResult());
      Layout layout = plan.layouts[destination];
      APInt value = APInt::getZero(layout.width);
      APInt unknown = layout.kind == Logic ? APInt::getAllOnes(layout.width)
                                           : APInt::getZero(layout.width);
      return emitConstant(plan, op.getResult(), value,
                          layout.kind == Logic ? &unknown : nullptr);
    }
    if (auto op = dyn_cast<sim::SimAggregateConstructOp>(operation)) {
      uint32_t destination = reg(plan, op.getResult());
      emit({Constant, 0, destination, 0, 0, 0, 0,
            addZeroConstant(plan.layouts[destination])});
      for (auto [index, element] : llvm::enumerate(op.getElements())) {
        auto subelement = sim::getAggregateProvenanceSubelement(
            op.getResult().getType(), index);
        if (!subelement)
          return op.emitOpError("aggregate element has no packed provenance");
        uint32_t elementRegister = reg(plan, element);
        uint16_t flags =
            isManagedAggregateWord(plan.layouts[elementRegister].kind)
                ? OBELISK_RT_DB_AGGREGATE_MANAGED
                : 0;
        emit({Insert, flags, destination, destination, elementRegister, 0, 0,
              subelement->first});
      }
      return success();
    }
    if (auto op = dyn_cast<sim::SimAggregateExtractOp>(operation)) {
      auto subelement = sim::getAggregateProvenanceSubelement(
          op.getInput().getType(), static_cast<unsigned>(op.getIndex()));
      if (!subelement)
        return op.emitOpError("aggregate element has no packed provenance");
      uint32_t destination = reg(plan, op.getResult());
      uint16_t flags = isManagedAggregateWord(plan.layouts[destination].kind)
                           ? OBELISK_RT_DB_AGGREGATE_MANAGED
                           : 0;
      emit({Extract, flags, destination, reg(plan, op.getInput()),
            kInvalidRegister, 0, 0, subelement->first});
      return success();
    }
    if (auto op = dyn_cast<sim::SimAggregateInsertOp>(operation)) {
      auto subelement = sim::getAggregateProvenanceSubelement(
          op.getInput().getType(), static_cast<unsigned>(op.getIndex()));
      if (!subelement)
        return op.emitOpError("aggregate element has no packed provenance");
      uint32_t replacement = reg(plan, op.getReplacement());
      uint16_t flags = isManagedAggregateWord(plan.layouts[replacement].kind)
                           ? OBELISK_RT_DB_AGGREGATE_MANAGED
                           : 0;
      emit({Insert, flags, reg(plan, op.getResult()), reg(plan, op.getInput()),
            replacement, 0, 0, subelement->first});
      return success();
    }
    if (auto op = dyn_cast<sim::SimArrayDynExtractOp>(operation))
      return encodeArrayExtract(plan, op);
    if (auto op = dyn_cast<sim::SimUnionConstructOp>(operation))
      return encodeUnionConstruct(plan, op);
    if (auto op = dyn_cast<sim::SimUnionExtractOp>(operation)) {
      uint32_t destination = reg(plan, op.getResult());
      uint16_t flags = isManagedAggregateWord(plan.layouts[destination].kind)
                           ? OBELISK_RT_DB_AGGREGATE_MANAGED
                           : 0;
      emit({Extract, flags, destination, reg(plan, op.getInput()),
            kInvalidRegister});
      return success();
    }
    if (auto op = dyn_cast<sim::SimUnionIsActiveOp>(operation))
      return encodeUnionIsActive(plan, op);
    if (auto op = dyn_cast<sim::SimRefAllocOp>(operation)) {
      std::optional<uint32_t> width =
          simulationWidth(op.getInitialValue().getType());
      if (!width)
        return op.emitOpError("automatic storage has no fixed width");
      SmallVector<uint32_t> inputs{reg(plan, op.getInitialValue())};
      SmallVector<uint64_t, 2> rootOffsets;
      if (!sim::getManagedHandleOffsets(op.getInitialValue().getType(),
                                        rootOffsets))
        return op.emitOpError("automatic storage has no managed root layout");
      if (plan.layouts[inputs.front()].kind != Managed)
        for (uint64_t offset : rootOffsets)
          inputs.push_back(emitU64Constant(plan, offset));
      return emitIntrinsicRegisters(plan, kIntrinsicStateAlloc, inputs,
                                    {reg(plan, op.getResult())});
    }
    if (isa<sim::SimDisableChildrenOp>(operation))
      return emitIntrinsic(plan, kIntrinsicDisableChildren, {}, {});
    if (auto op = dyn_cast<sim::SimControlEnterOp>(operation)) {
      if (op.getTargetId() == 0 || op.getTargetId() > UINT32_MAX)
        return op.emitOpError("control target ID does not fit bytecode");
      return emitIntrinsic(plan, kIntrinsicControlEnter, {}, {op.getControl()},
                           static_cast<uint32_t>(op.getTargetId()));
    }
    if (auto op = dyn_cast<sim::SimControlLeaveOp>(operation))
      return emitIntrinsic(plan, kIntrinsicControlLeave, {op.getControl()}, {});
    if (auto op = dyn_cast<sim::SimControlDisableOp>(operation)) {
      if (op.getTargetId() == 0 || op.getTargetId() > INT32_MAX)
        return op.emitOpError("control target ID does not fit bytecode");
      SmallVector<Value> inputs;
      if (op.getActivation())
        inputs.push_back(op.getActivation());
      uint32_t flags = static_cast<uint32_t>(op.getTargetId());
      if (op.getHierarchical())
        flags |= UINT32_C(1) << 31;
      return emitIntrinsic(plan, kIntrinsicControlDisable, inputs, {}, flags);
    }
    if (auto op = dyn_cast<sim::SimStaticOnceOp>(operation)) {
      if (op.getId() == 0 || op.getId() > UINT32_MAX)
        return op.emitOpError("static initialization ID does not fit bytecode");
      return emitIntrinsic(plan, kIntrinsicStaticOnce, {}, {op.getFirst()},
                           static_cast<uint32_t>(op.getId()));
    }
    if (auto op = dyn_cast<sim::SimDeferredOnceOp>(operation)) {
      if (op.getId() == 0)
        return op.emitOpError("deferred assertion ID must be positive");
      return emitIntrinsicRegisters(
          plan, kIntrinsicDeferredOnce,
          {emitU64Constant(plan, static_cast<uint64_t>(op.getId()))},
          {reg(plan, op.getFirst())});
    }
    if (auto op = dyn_cast<sim::SimMonitorRegisterOp>(operation))
      return emitIntrinsic(plan, kIntrinsicMonitorRegister, {op.getProcess()},
                           {});
    if (auto op = dyn_cast<sim::SimMonitorControlOp>(operation))
      return emitIntrinsic(plan, kIntrinsicMonitorControl, {}, {},
                           op.getEnabled() ? 1 : 0);
    if (auto op = dyn_cast<sim::SimMonitorCurrentOp>(operation))
      return emitIntrinsic(plan, kIntrinsicMonitorCurrent, {},
                           {op.getCurrent()});
    if (auto op = dyn_cast<sim::SimContextStorageOp>(operation))
      return encodeHandle(plan, op.getResult(), op.getId(), state.storage, 2);
    if (auto op = dyn_cast<sim::SimContextNetOp>(operation))
      return encodeHandle(plan, op.getResult(), op.getId(), state.nets, 3);
    if (auto op = dyn_cast<sim::SimContextDriverOp>(operation))
      return encodeHandle(plan, op.getResult(), op.getId(), state.drivers, 4);
    if (auto op = dyn_cast<sim::SimContextEventOp>(operation)) {
      emit({MakeHandle, 0, reg(plan, op.getResult()), 5, 0, 0, 0, op.getId()});
      return success();
    }
    if (auto op = dyn_cast<sim::SimRefExtractOp>(operation))
      return encodeHandleOffset(plan, op.getResult(), op.getInput(),
                                op.getLowBit(), Value{});
    if (auto op = dyn_cast<sim::SimNetExtractOp>(operation))
      return encodeHandleOffset(plan, op.getResult(), op.getInput(),
                                op.getLowBit(), Value{});
    if (auto op = dyn_cast<sim::SimDriverExtractOp>(operation))
      return encodeHandleOffset(plan, op.getResult(), op.getInput(),
                                op.getLowBit(), Value{});
    if (auto op = dyn_cast<sim::SimRefDynExtractOp>(operation))
      return encodeHandleOffset(plan, op.getResult(), op.getInput(), 0,
                                op.getLowBit());
    if (auto op = dyn_cast<sim::SimDriverDynExtractOp>(operation))
      return encodeHandleOffset(plan, op.getResult(), op.getInput(), 0,
                                op.getLowBit());
    if (auto op = dyn_cast<sim::SimRefSubelementOp>(operation))
      return encodeSubelementView(plan, op.getResult(), op.getInput(),
                                  op.getIndices(), op.getOperation());
    if (auto op = dyn_cast<sim::SimDriverSubelementOp>(operation))
      return encodeSubelementView(plan, op.getResult(), op.getInput(),
                                  op.getIndices(), op.getOperation());
    if (auto op = dyn_cast<sim::SimRefArrayElementOp>(operation))
      return encodeArrayView(plan, op.getResult(), op.getInput(), op.getIndex(),
                             op.getOperation());
    if (auto op = dyn_cast<sim::SimDriverArrayElementOp>(operation))
      return encodeArrayView(plan, op.getResult(), op.getInput(), op.getIndex(),
                             op.getOperation());
    if (auto op = dyn_cast<sim::SimRefLoadOp>(operation)) {
      emit({LoadState, 0, reg(plan, op.getResult()),
            reg(plan, op.getReference())});
      return success();
    }
    if (auto op = dyn_cast<sim::SimNetReadOp>(operation)) {
      emit({LoadState, 0, reg(plan, op.getResult()), reg(plan, op.getNet())});
      return success();
    }
    if (auto op = dyn_cast<sim::SimRefStoreOp>(operation)) {
      emit({StoreState, 0, 0, reg(plan, op.getReference()),
            reg(plan, op.getValue())});
      return success();
    }
    if (auto op = dyn_cast<sim::SimOverrideOp>(operation)) {
      emit({OverrideState, static_cast<uint16_t>(op.getIsAssign() ? 1 : 0), 0,
            reg(plan, op.getTarget()), reg(plan, op.getValue())});
      return success();
    }
    if (auto op = dyn_cast<sim::SimReleaseOverrideOp>(operation)) {
      emit({ReleaseState, static_cast<uint16_t>(op.getIsAssign() ? 1 : 0), 0,
            reg(plan, op.getTarget())});
      return success();
    }
    if (auto op = dyn_cast<sim::SimDriverDriveOp>(operation)) {
      emit({StoreState, 0, 0, reg(plan, op.getDriver()),
            reg(plan, op.getValue())});
      return success();
    }
    if (isa<sim::SimObserverBindOp>(operation))
      return success();
    if (auto suspend = dyn_cast<sim::SimSuspendDelayOp>(operation))
      return encodeWait(plan, suspend.getOperation(),
                        suspend.getContinuationOperands(), 1, 0, {}, {},
                        suspend.getDelay());
    if (auto suspend = dyn_cast<sim::SimSuspendChangeOp>(operation)) {
      uint32_t edge = 0;
      return encodeWait(plan, suspend.getOperation(),
                        suspend.getContinuationOperands(), 2, 0,
                        ArrayRef<uint32_t>(&edge, 1), {suspend.getWatched()});
    }
    if (auto suspend = dyn_cast<sim::SimSuspendLevelOp>(operation)) {
      uint32_t edge = 0;
      return encodeWait(plan, suspend.getOperation(),
                        suspend.getContinuationOperands(), 2,
                        OBELISK_RT_WAIT_LEVEL_TRUE,
                        ArrayRef<uint32_t>(&edge, 1), {suspend.getWatched()});
    }
    if (auto suspend = dyn_cast<sim::SimSuspendEdgeOp>(operation)) {
      uint32_t edge = static_cast<uint32_t>(suspend.getEdge());
      return encodeWait(plan, suspend.getOperation(),
                        suspend.getContinuationOperands(), 3, 0,
                        ArrayRef<uint32_t>(&edge, 1), {suspend.getWatched()});
    }
    if (auto suspend = dyn_cast<sim::SimSuspendEdgeIffOp>(operation)) {
      SmallVector<uint32_t> edges{static_cast<uint32_t>(suspend.getEdge()),
                                  OBELISK_RT_WAIT_EDGE_NONE};
      SmallVector<Value> watched{suspend.getWatched(), suspend.getCondition()};
      return encodeWait(plan, suspend.getOperation(),
                        suspend.getContinuationOperands(), 3,
                        OBELISK_RT_WAIT_EDGE_IFF, edges, watched);
    }
    if (auto suspend = dyn_cast<sim::SimSuspendAnyOp>(operation)) {
      SmallVector<uint32_t> edges;
      for (int32_t edge : suspend.getEdges())
        edges.push_back(static_cast<uint32_t>(edge));
      SmallVector<Value> watched(suspend.getWatched());
      return encodeWait(plan, suspend.getOperation(),
                        suspend.getContinuationOperands(), 3, 0, edges,
                        watched);
    }
    if (auto suspend = dyn_cast<sim::SimSuspendEventOp>(operation)) {
      uint32_t edge = UINT32_MAX;
      return encodeWait(plan, suspend.getOperation(),
                        suspend.getContinuationOperands(), 4, 0,
                        ArrayRef<uint32_t>(&edge, 1), {suspend.getEvent()});
    }
    if (auto suspend = dyn_cast<sim::SimSuspendForeverOp>(operation))
      return encodeWait(plan, suspend.getOperation(),
                        suspend.getContinuationOperands(), 7, 0, {}, {});
    if (auto suspend = dyn_cast<sim::SimSuspendAwaitOp>(operation)) {
      uint32_t edge = UINT32_MAX;
      return encodeWait(plan, suspend.getOperation(),
                        suspend.getContinuationOperands(), 5, 0,
                        ArrayRef<uint32_t>(&edge, 1), {suspend.getProcess()});
    }
    if (auto suspend = dyn_cast<sim::SimSuspendJoinOp>(operation)) {
      SmallVector<uint32_t> edges(suspend.getProcesses().size(), UINT32_MAX);
      SmallVector<Value> processes(suspend.getProcesses());
      return encodeWait(
          plan, suspend.getOperation(), suspend.getContinuationOperands(), 6,
          static_cast<uint32_t>(suspend.getKind()), edges, processes);
    }
    if (auto suspend = dyn_cast<sim::SimSuspendChildrenOp>(operation))
      return encodeWait(plan, suspend.getOperation(),
                        suspend.getContinuationOperands(), 9, 0, {}, {});
    if (auto suspend = dyn_cast<sim::SimSuspendObserveOp>(operation))
      return encodeObserverWait(plan, suspend);
    return operation->emitOpError()
           << "has no design-bytecode semantics (the normalized legality set "
              "is closed, so executable fallback is forbidden)";
  }

  LogicalResult encodeCall(FunctionPlan &plan, sim::SimCallOp call) {
    auto found = indices.find(call.getCallee());
    if (found == indices.end()) {
      sim::SimFuncOp declaration = externalFunctions.lookup(call.getCallee());
      if (!declaration)
        return call.emitOpError(
            "callee has no bytecode body or import declaration");
      uint32_t importID = stableImportID(declaration.getSymName());
      auto inserted =
          importSymbols.try_emplace(importID, declaration.getSymName().str());
      if (!inserted.second &&
          inserted.first->second != declaration.getSymName())
        return call.emitOpError()
               << "import ID collision between '" << inserted.first->second
               << "' and '" << declaration.getSymName() << "'";
      SmallVector<uint32_t> inputs;
      for (Value operand : call.getOperands()) {
        if (isa<sim::ContextType, runtime::ContextType>(operand.getType()))
          continue;
        uint32_t input = reg(plan, operand);
        if (input == kInvalidRegister || (plan.layouts[input].kind != Bits &&
                                          plan.layouts[input].kind != Logic &&
                                          plan.layouts[input].kind != Handle &&
                                          plan.layouts[input].kind != Status))
          return call.emitOpError() << "generation-one imports require "
                                       "numeric, handle, or status inputs";
        inputs.push_back(input);
      }
      SmallVector<uint32_t> outputs;
      for (Value result : call.getResults()) {
        uint32_t output = reg(plan, result);
        if (output == kInvalidRegister ||
            (plan.layouts[output].kind != Bits &&
             plan.layouts[output].kind != Logic &&
             plan.layouts[output].kind != Handle &&
             plan.layouts[output].kind != Status))
          return call.emitOpError() << "generation-one imports require "
                                       "numeric, handle, or status results";
        outputs.push_back(output);
      }
      return emitIntrinsicRegisters(plan, kIntrinsicImport, inputs, outputs,
                                    importID);
    }
    FunctionPlan &callee = plans[found->second];
    Block &calleeEntry = callee.function.getBody().front();
    auto inputs =
        addMap(callee, calleeEntry.getArguments(), plan, call.getOperands());
    SmallVector<Value> synthetic;
    uint64_t firstOutputs = operandMaps.size();
    for (auto [destination, source] :
         llvm::zip_equal(call.getResults(), callee.resultRegisters))
      operandMaps.push_back({reg(plan, destination), source});
    emit({Call, 0, 0, callee.index, static_cast<uint32_t>(inputs.first),
          static_cast<uint32_t>(inputs.second),
          static_cast<uint32_t>(firstOutputs), call.getNumResults()});
    return success();
  }

  LogicalResult encodeTaskCall(FunctionPlan &plan, sim::SimTaskCallOp call) {
    if (!plan.frame)
      return call.emitOpError("task call has no canonical caller frame");
    const ProcessSuspension *suspension =
        plan.frame->getSuspension(call.getOperation());
    if (!suspension)
      return call.emitOpError("task call is missing frame analysis");
    ArrayRef<ProcessFrameValue> slots =
        plan.frame->getContinuationLayout(suspension->continuationID);
    if (slots.size() != call.getContinuationOperands().size())
      return call.emitOpError("task continuation frame arity mismatch");
    for (auto [value, slot] :
         llvm::zip_equal(call.getContinuationOperands(), slots)) {
      if (slot.storageSize > UINT32_MAX ||
          (slot.hasSecondaryStorage() && slot.storageSize > UINT32_MAX / 2))
        return call.emitOpError(
            "canonical frame transfer exceeds the bytecode ABI limit");
      uint64_t transferSize =
          slot.storageSize * (slot.hasSecondaryStorage() ? 2 : 1);
      emitFrameTransfer(plan, StoreFrame, value, slot.valueOffset,
                        static_cast<uint32_t>(transferSize));
    }
    auto found = indices.find(call.getCallee());
    if (found == indices.end())
      return call.emitOpError("task target has no bytecode body");
    FunctionPlan &callee = plans[found->second];
    if (!callee.frame || callee.function.getEntryKind() != sim::EntryKind::Task)
      return call.emitOpError("task target is not an activation entry");
    Block &calleeEntry = callee.function.getBody().front();
    auto inputs =
        addMap(callee, calleeEntry.getArguments(), plan, call.getArguments());
    emit({TaskCall, 0, 0, callee.index, static_cast<uint32_t>(inputs.first),
          static_cast<uint32_t>(inputs.second), 0, suspension->continuationID});
    return success();
  }

  LogicalResult encodeDPICall(FunctionPlan &plan, sim::SimDPICallOp call) {
    uint32_t importID = call.getImportId();
    auto inserted =
        importSymbols.try_emplace(importID, call.getCIdentifier().str());
    if (!inserted.second && inserted.first->second != call.getCIdentifier())
      return call.emitOpError()
             << "import ID collision between '" << inserted.first->second
             << "' and '" << call.getCIdentifier() << "'";
    SmallVector<uint8_t> metadata;
    append32(metadata, OBELISK_RT_VERSION);
    uint32_t flags = (call.getIsPure() ? OBELISK_RT_IMPORT_PURE : 0) |
                     (call.getIsContext() ? OBELISK_RT_IMPORT_CONTEXT : 0) |
                     (call.getIsTask() ? OBELISK_RT_IMPORT_TASK : 0);
    append32(metadata, flags);
    append32(metadata, importID);
    append32(metadata, 0);
    append64(metadata, call.getScopeId());
    append32(metadata, call.getSourceLine());
    append32(metadata, call.getSourceColumn());
    append64(metadata, call.getSourceFile().size());
    uint64_t logicalInputs = call.getArguments().size();
    if (logicalInputs > call.getAbiSignature().size())
      return call.emitOpError("DPI ABI signature has too few inputs");
    uint64_t logicalOutputs = call.getAbiSignature().size() - logicalInputs;
    append64(metadata,
             sim::getDPISignatureHash(call.getAbiSignature(), logicalInputs));
    append32(metadata, static_cast<uint32_t>(logicalInputs));
    append32(metadata, static_cast<uint32_t>(logicalOutputs));
    for (Attribute attribute : call.getAbiSignature()) {
      auto abi = cast<sim::DPIABIAttr>(attribute);
      append32(metadata, static_cast<uint32_t>(abi.getKind()));
      append32(metadata, static_cast<uint32_t>(abi.getDirection()));
      append32(metadata, abi.getWidth());
      append32(metadata,
               (abi.getFourState() ? 1u : 0u) | (abi.getIsSigned() ? 2u : 0u));
    }
    llvm::append_range(metadata,
                       ArrayRef<uint8_t>(reinterpret_cast<const uint8_t *>(
                                             call.getSourceFile().data()),
                                         call.getSourceFile().size()));
    SmallVector<uint32_t> inputs{emitBytesConstant(plan, metadata)};
    for (Value operand : call.getArguments()) {
      uint32_t input = reg(plan, operand);
      if (input == kInvalidRegister || (plan.layouts[input].kind != Bits &&
                                        plan.layouts[input].kind != Logic &&
                                        plan.layouts[input].kind != Status))
        return call.emitOpError(
            "DPI imports require fixed packed integral inputs");
      inputs.push_back(input);
    }
    SmallVector<uint32_t> outputs;
    for (Value result : call.getResults()) {
      uint32_t output = reg(plan, result);
      if (output == kInvalidRegister || (plan.layouts[output].kind != Bits &&
                                         plan.layouts[output].kind != Logic &&
                                         plan.layouts[output].kind != Status))
        return call.emitOpError(
            "DPI imports require fixed packed integral results");
      outputs.push_back(output);
    }
    if (outputs.size() != logicalOutputs + 1 ||
        plan.layouts[outputs.back()].kind != Status)
      return call.emitOpError(
          "DPI import must return data results followed by runtime status");
    return emitIntrinsicRegisters(plan, kIntrinsicDPIImport, inputs, outputs);
  }

  LogicalResult encodeReturn(FunctionPlan &plan, sim::SimReturnOp op) {
    if (plan.function.getEntryKind() != sim::EntryKind::Function &&
        plan.function.getEntryKind() != sim::EntryKind::Observer) {
      if (!op.getOperands().empty())
        return op.emitOpError("process return cannot carry values");
      emit({Terminate});
      return success();
    }
    auto results =
        addRegistersMap(plan.resultRegisters, plan, op.getOperands());
    emit({Return, 0, 0, static_cast<uint32_t>(results.first),
          static_cast<uint32_t>(results.second)});
    return success();
  }

  LogicalResult encodeLogicBinary(FunctionPlan &plan,
                                  sim::SimLogicBinaryOp op) {
    uint16_t opcode = 0;
    bool invert = false;
    switch (op.getKind()) {
    case sim::BinaryKind::Add:
      opcode = Add;
      break;
    case sim::BinaryKind::Sub:
      opcode = Sub;
      break;
    case sim::BinaryKind::Mul:
      opcode = Mul;
      break;
    case sim::BinaryKind::UDiv:
      opcode = UDiv;
      break;
    case sim::BinaryKind::SDiv:
      opcode = SDiv;
      break;
    case sim::BinaryKind::UMod:
      opcode = URem;
      break;
    case sim::BinaryKind::SMod:
      opcode = SRem;
      break;
    case sim::BinaryKind::And:
      opcode = And;
      break;
    case sim::BinaryKind::Or:
      opcode = Or;
      break;
    case sim::BinaryKind::Xor:
      opcode = Xor;
      break;
    case sim::BinaryKind::Xnor:
      opcode = Xor;
      invert = true;
      break;
    }
    emit({opcode, 0, reg(plan, op.getResult()), reg(plan, op.getLhs()),
          reg(plan, op.getRhs())});
    if (invert)
      emit({Not, 0, reg(plan, op.getResult()), reg(plan, op.getResult())});
    return success();
  }

  LogicalResult encodeLogicCompare(FunctionPlan &plan,
                                   sim::SimLogicCompareOp op) {
    static constexpr uint16_t map[] = {
        OBELISK_RT_DB_CMP_EQ,       OBELISK_RT_DB_CMP_NE,
        OBELISK_RT_DB_CMP_CASE_EQ,  OBELISK_RT_DB_CMP_CASE_NE,
        OBELISK_RT_DB_CMP_ULT,      OBELISK_RT_DB_CMP_ULE,
        OBELISK_RT_DB_CMP_UGT,      OBELISK_RT_DB_CMP_UGE,
        OBELISK_RT_DB_CMP_SLT,      OBELISK_RT_DB_CMP_SLE,
        OBELISK_RT_DB_CMP_SGT,      OBELISK_RT_DB_CMP_SGE,
        OBELISK_RT_DB_CMP_WILD_EQ,  OBELISK_RT_DB_CMP_WILD_NE,
        OBELISK_RT_DB_CMP_CASEZ_EQ, OBELISK_RT_DB_CMP_CASEXZ_EQ};
    unsigned kind = static_cast<unsigned>(op.getKind());
    if (kind >= std::size(map))
      return op.emitOpError("invalid comparison kind");
    emit({Compare, map[kind], reg(plan, op.getResult()), reg(plan, op.getLhs()),
          reg(plan, op.getRhs())});
    return success();
  }

  LogicalResult encodeConcat(FunctionPlan &plan, sim::SimLogicConcatOp op) {
    if (op.getInputs().empty())
      return op.emitOpError("empty concatenation");
    if (op.getInputs().size() == 1) {
      emit({Move, 0, reg(plan, op.getResult()), reg(plan, op.getInputs()[0])});
      return success();
    }
    Value left = op.getInputs()[0];
    uint32_t leftRegister = reg(plan, left);
    unsigned accumulatedWidth = cast<sim::LogicType>(left.getType()).getWidth();
    for (unsigned index = 1; index < op.getInputs().size(); ++index) {
      Value right = op.getInputs()[index];
      accumulatedWidth += cast<sim::LogicType>(right.getType()).getWidth();
      uint32_t destination;
      if (index + 1 == op.getInputs().size()) {
        destination = reg(plan, op.getResult());
      } else {
        destination = temporaryLike(
            plan, sim::LogicType::get(op.getContext(), accumulatedWidth),
            op.getResult());
        if (destination == kInvalidRegister)
          return failure();
      }
      emit({Concat, 0, destination, leftRegister, reg(plan, right)});
      leftRegister = destination;
    }
    return success();
  }

  LogicalResult encodeReplicate(FunctionPlan &plan,
                                sim::SimLogicReplicateOp op) {
    uint64_t count = op.getCount();
    if (count == 0)
      return op.emitOpError("zero replication count");
    if (count == 1) {
      emit({Move, 0, reg(plan, op.getResult()), reg(plan, op.getInput())});
      return success();
    }
    uint32_t accumulated = reg(plan, op.getInput());
    unsigned inputWidth =
        cast<sim::LogicType>(op.getInput().getType()).getWidth();
    for (uint64_t copy = 1; copy != count; ++copy) {
      uint32_t destination;
      if (copy + 1 == count) {
        destination = reg(plan, op.getResult());
      } else {
        uint64_t width = uint64_t{inputWidth} * (copy + 1);
        if (width > std::numeric_limits<unsigned>::max())
          return op.emitOpError("replication width exceeds bytecode ABI");
        destination = temporaryLike(
            plan,
            sim::LogicType::get(op.getContext(), static_cast<unsigned>(width)),
            op.getResult());
        if (destination == kInvalidRegister)
          return failure();
      }
      emit({Concat, 0, destination, accumulated, reg(plan, op.getInput())});
      accumulated = destination;
    }
    return success();
  }

  FailureOr<uint32_t> encodeArrayOffset(FunctionPlan &plan, Type array,
                                        Value indexValue, Operation *anchor) {
    int64_t left = 0, right = 0;
    bool packed = false;
    Type element;
    if (auto type = dyn_cast<sim::PackedArrayType>(array)) {
      left = type.getLeft();
      right = type.getRight();
      packed = true;
      element = type.getElementType();
    } else if (auto type = dyn_cast<sim::UnpackedArrayType>(array)) {
      left = type.getLeft();
      right = type.getRight();
      element = type.getElementType();
    } else {
      anchor->emitOpError("dynamic extraction requires a fixed array");
      return failure();
    }
    std::optional<uint64_t> span = sim::getProvenanceSpan(element);
    uint64_t count = sim::getAggregateNumElements(array);
    if (!span || count == 0) {
      anchor->emitOpError("array element has no fixed packed span");
      return failure();
    }

    MLIRContext *context = anchor->getContext();
    Type calculationType = containsLogic(indexValue.getType())
                               ? Type(sim::LogicType::get(context, 64))
                               : Type(IntegerType::get(context, 64));
    FailureOr<Layout> sourceLayout = getLayout(indexValue.getType());
    if (failed(sourceLayout)) {
      anchor->emitOpError("array index has no bytecode layout");
      return failure();
    }
    uint32_t index = temporary(plan, calculationType);
    uint32_t roundTrip = kInvalidRegister;
    uint32_t fits = kInvalidRegister;
    if (sourceLayout->width > 64) {
      roundTrip = temporary(plan, indexValue.getType());
      fits = temporary(plan, IntegerType::get(context, 1));
    }
    uint32_t leftReg = temporary(plan, calculationType);
    uint32_t rightReg = temporary(plan, calculationType);
    uint32_t lower = temporary(plan, IntegerType::get(context, 1));
    uint32_t upper = temporary(plan, IntegerType::get(context, 1));
    uint32_t valid = temporary(plan, IntegerType::get(context, 1));
    uint32_t ordinal = temporary(plan, calculationType);
    uint32_t scalar = temporary(plan, calculationType);
    uint32_t offset = temporary(plan, calculationType);
    uint32_t invalid = temporary(plan, calculationType);
    if (index == kInvalidRegister || leftReg == kInvalidRegister ||
        rightReg == kInvalidRegister || lower == kInvalidRegister ||
        upper == kInvalidRegister || valid == kInvalidRegister ||
        ordinal == kInvalidRegister || scalar == kInvalidRegister ||
        offset == kInvalidRegister || invalid == kInvalidRegister ||
        (sourceLayout->width > 64 &&
         (roundTrip == kInvalidRegister || fits == kInvalidRegister)))
      return failure();
    emit({Extract, 1, index, reg(plan, indexValue), kInvalidRegister});
    if (sourceLayout->width > 64) {
      // Narrowing alone would turn an overflowing or high-plane-X index into
      // an apparently valid low 64-bit value. Require the original index to
      // equal a signed round trip through the runtime's i64 coordinate type.
      emit({Extract, 1, roundTrip, index, kInvalidRegister});
      emit({Compare, OBELISK_RT_DB_CMP_EQ, fits, reg(plan, indexValue),
            roundTrip});
    }
    auto constant = [&](uint32_t destination, const APInt &value) {
      emit({Constant, 0, destination, 0, 0, 0, 0,
            addConstant(plan.layouts[destination], value)});
    };
    constant(leftReg, APInt(64, static_cast<uint64_t>(left), true));
    constant(rightReg, APInt(64, static_cast<uint64_t>(right), true));
    if (left >= right) {
      emit({Compare, 7, lower, index, leftReg});
      emit({Compare, 9, upper, index, rightReg});
      emit({Sub, 0, ordinal, leftReg, index});
    } else {
      emit({Compare, 9, lower, index, leftReg});
      emit({Compare, 7, upper, index, rightReg});
      emit({Sub, 0, ordinal, index, leftReg});
    }
    emit({And, 0, valid, lower, upper});
    if (sourceLayout->width > 64)
      emit({And, 0, valid, valid, fits});
    if (packed) {
      constant(scalar, APInt(64, count - 1));
      emit({Sub, 0, ordinal, scalar, ordinal});
    }
    constant(scalar, APInt(64, *span));
    emit({Mul, 0, offset, ordinal, scalar});
    constant(invalid, APInt(64, *simulationWidth(array)));
    emit({Select, 0, offset, offset, invalid, valid});
    return offset;
  }

  LogicalResult encodeArrayExtract(FunctionPlan &plan,
                                   sim::SimArrayDynExtractOp op) {
    FailureOr<uint32_t> offset = encodeArrayOffset(
        plan, op.getInput().getType(), op.getIndex(), op.getOperation());
    if (failed(offset))
      return failure();
    uint32_t destination = reg(plan, op.getResult());
    uint16_t flags = isManagedAggregateWord(plan.layouts[destination].kind)
                         ? OBELISK_RT_DB_AGGREGATE_MANAGED
                         : 0;
    emit({Extract, flags, destination, reg(plan, op.getInput()), *offset});
    return success();
  }

  LogicalResult encodeUnionConstruct(FunctionPlan &plan,
                                     sim::SimUnionConstructOp op) {
    Type unionType = op.getResult().getType();
    std::optional<uint64_t> payloadSpan = unionPayloadSpan(unionType);
    std::optional<uint32_t> width = simulationWidth(unionType);
    if (!payloadSpan || !width || *payloadSpan > *width)
      return op.emitOpError("union has no fixed packed representation");
    uint32_t destination = reg(plan, op.getResult());
    uint32_t value = reg(plan, op.getValue());
    uint16_t flags = isManagedAggregateWord(plan.layouts[value].kind)
                         ? OBELISK_RT_DB_AGGREGATE_MANAGED
                         : 0;
    emit({Extract, flags, destination, value, kInvalidRegister});
    uint64_t tag = 0;
    unsigned tagBits = 0;
    if (auto packed = dyn_cast<sim::PackedUnionType>(unionType);
        packed && packed.getIsTagged()) {
      tag = op.getIndex();
      tagBits = packed.getTagBits();
    } else if (auto unpacked = dyn_cast<sim::UnpackedUnionType>(unionType);
               unpacked && unpacked.getIsTagged()) {
      tag = static_cast<uint64_t>(op.getIndex()) + 1;
      tagBits = llvm::Log2_64_Ceil(
          static_cast<uint64_t>(sim::getAggregateNumElements(unionType)) + 1);
    }
    if (tagBits != 0) {
      uint32_t tagRegister = temporary(plan, unionType);
      if (tagRegister == kInvalidRegister)
        return failure();
      APInt encoded(*width, tag);
      encoded <<= *payloadSpan;
      emit({Constant, 0, tagRegister, 0, 0, 0, 0,
            addConstant(plan.layouts[tagRegister], encoded)});
      emit({Or, 0, destination, destination, tagRegister});
    }
    return success();
  }

  LogicalResult encodeUnionIsActive(FunctionPlan &plan,
                                    sim::SimUnionIsActiveOp op) {
    Type unionType = op.getInput().getType();
    std::optional<uint64_t> payloadSpan = unionPayloadSpan(unionType);
    if (!payloadSpan)
      return op.emitOpError("tagged union has no packed representation");
    unsigned tagBits = 0;
    uint64_t expected = 0;
    if (auto packed = dyn_cast<sim::PackedUnionType>(unionType)) {
      tagBits = packed.getTagBits();
      expected = op.getIndex();
    } else if (auto unpacked = dyn_cast<sim::UnpackedUnionType>(unionType)) {
      tagBits = llvm::Log2_64_Ceil(
          static_cast<uint64_t>(sim::getAggregateNumElements(unionType)) + 1);
      expected = static_cast<uint64_t>(op.getIndex()) + 1;
    }
    if (tagBits == 0) {
      uint32_t destination = reg(plan, op.getResult());
      emit({Constant, 0, destination, 0, 0, 0, 0,
            addConstant(plan.layouts[destination], APInt(1, 1))});
      return success();
    }
    Type tagType = sim::LogicType::get(op.getContext(), tagBits);
    uint32_t tag = temporaryLike(plan, tagType, op.getInput());
    uint32_t expectedTag = temporaryLike(plan, tagType, op.getInput());
    if (tag == kInvalidRegister || expectedTag == kInvalidRegister)
      return failure();
    emit({Extract, 0, tag, reg(plan, op.getInput()), kInvalidRegister, 0, 0,
          *payloadSpan});
    emit({Constant, 0, expectedTag, 0, 0, 0, 0,
          addConstant(plan.layouts[expectedTag], APInt(tagBits, expected))});
    emit({Compare, OBELISK_RT_DB_CMP_CASE_EQ, reg(plan, op.getResult()), tag,
          expectedTag});
    return success();
  }

  template <typename Map>
  LogicalResult encodeHandle(FunctionPlan &plan, Value result, uint64_t id,
                             const Map &map, uint32_t kind) {
    auto found = map.find(id);
    if (found == map.end())
      return result.getDefiningOp()->emitOpError("unknown state descriptor");
    Type element;
    if (auto reference = dyn_cast<sim::RefType>(result.getType()))
      element = reference.getElementType();
    else if (auto net = dyn_cast<sim::NetType>(result.getType()))
      element = net.getElementType();
    else if (auto driver = dyn_cast<sim::DriverType>(result.getType()))
      element = driver.getElementType();
    else
      return result.getDefiningOp()->emitOpError(
          "expected a state handle type");
    uint64_t width = *simulationWidth(element);
    emit({MakeHandle, 0, reg(plan, result), kind, static_cast<uint32_t>(width),
          0, 0, found->second});
    return success();
  }

  LogicalResult encodeHandleOffsetRegister(FunctionPlan &plan, Value result,
                                           Value input, uint64_t offset,
                                           uint32_t dynamic) {
    Type element;
    if (auto reference = dyn_cast<sim::RefType>(result.getType()))
      element = reference.getElementType();
    else if (auto net = dyn_cast<sim::NetType>(result.getType()))
      element = net.getElementType();
    else if (auto driver = dyn_cast<sim::DriverType>(result.getType()))
      element = driver.getElementType();
    else
      return result.getDefiningOp()->emitOpError(
          "view result is not a reference, net, or driver");
    std::optional<uint32_t> width = simulationWidth(element);
    if (!width)
      return result.getDefiningOp()->emitOpError(
          "view element has no fixed packed width");
    emit({HandleOffset, 0, reg(plan, result), reg(plan, input), dynamic, 0,
          *width, offset});
    return success();
  }

  LogicalResult encodeHandleOffset(FunctionPlan &plan, Value result,
                                   Value input, uint64_t offset,
                                   Value dynamic) {
    return encodeHandleOffsetRegister(plan, result, input, offset,
                                      dynamic ? reg(plan, dynamic)
                                              : kInvalidRegister);
  }

  LogicalResult encodeSubelementView(FunctionPlan &plan, Value result,
                                     Value input, ArrayRef<int64_t> indices,
                                     Operation *anchor) {
    Type type;
    if (auto reference = dyn_cast<sim::RefType>(input.getType()))
      type = reference.getElementType();
    else if (auto driver = dyn_cast<sim::DriverType>(input.getType()))
      type = driver.getElementType();
    else
      return anchor->emitOpError("subelement input is not a state view");
    uint64_t offset = 0;
    for (int64_t rawIndex : indices) {
      if (rawIndex < 0 ||
          static_cast<uint64_t>(rawIndex) >= sim::getAggregateNumElements(type))
        return anchor->emitOpError("subelement index is out of range");
      unsigned index = static_cast<unsigned>(rawIndex);
      auto subelement = sim::getAggregateProvenanceSubelement(type, index);
      if (!subelement ||
          subelement->first > std::numeric_limits<uint64_t>::max() - offset)
        return anchor->emitOpError("subelement path overflows packed offset");
      offset += subelement->first;
      type = sim::getAggregateElementType(type, index);
    }
    return encodeHandleOffsetRegister(plan, result, input, offset,
                                      kInvalidRegister);
  }

  LogicalResult encodeArrayView(FunctionPlan &plan, Value result, Value input,
                                Value index, Operation *anchor) {
    Type array;
    if (auto reference = dyn_cast<sim::RefType>(input.getType()))
      array = reference.getElementType();
    else if (auto driver = dyn_cast<sim::DriverType>(input.getType()))
      array = driver.getElementType();
    else
      return anchor->emitOpError("array view input is not a state view");
    FailureOr<uint32_t> offset = encodeArrayOffset(plan, array, index, anchor);
    if (failed(offset))
      return failure();
    return encodeHandleOffsetRegister(plan, result, input, 0, *offset);
  }

  void emitFrameTransfer(FunctionPlan &plan, uint16_t opcode, Value value,
                         uint64_t offset, uint32_t transferSize = 0) {
    uint16_t kind = 0;
    uint32_t width = 0;
    Type type = value.getType();
    if (auto reference = dyn_cast<sim::RefType>(type)) {
      kind = 2;
      width = *simulationWidth(reference.getElementType());
    } else if (auto net = dyn_cast<sim::NetType>(type)) {
      kind = 3;
      width = *simulationWidth(net.getElementType());
    } else if (auto driver = dyn_cast<sim::DriverType>(type)) {
      kind = 4;
      width = *simulationWidth(driver.getElementType());
    } else if (isa<sim::EventType>(type)) {
      kind = 5;
    } else if (isa<sim::ProcessType>(type)) {
      kind = 6;
    }
    if (opcode == LoadFrame)
      emit({LoadFrame, kind, reg(plan, value), 0, 0, 0,
            kind == 0 ? transferSize : width, offset});
    else
      emit({StoreFrame, kind, 0, reg(plan, value), 0, 0,
            kind == 0 ? transferSize : width, offset});
  }

  LogicalResult encodeObserverWait(FunctionPlan &plan,
                                   sim::SimSuspendObserveOp operation) {
    if (!plan.frame)
      return operation.emitOpError("suspension has no canonical frame");
    const ProcessSuspension *suspension = plan.frame->getSuspension(operation);
    if (!suspension)
      return operation.emitOpError("suspension is missing frame analysis");
    ArrayRef<ProcessFrameValue> slots =
        plan.frame->getContinuationLayout(suspension->continuationID);
    if (slots.size() != operation.getContinuationOperands().size())
      return operation.emitOpError("continuation frame arity mismatch");
    for (auto [value, slot] :
         llvm::zip_equal(operation.getContinuationOperands(), slots)) {
      uint64_t transferSize =
          slot.storageSize * (slot.hasSecondaryStorage() ? 2 : 1);
      if (transferSize > UINT32_MAX)
        return operation.emitOpError(
            "canonical frame transfer exceeds the bytecode ABI limit");
      emitFrameTransfer(plan, StoreFrame, value, slot.valueOffset,
                        static_cast<uint32_t>(transferSize));
    }

    uint32_t primaryCount = operation.getEdges().size();
    uint32_t conditionCount = operation.getConditionCount();
    uint32_t observerCount = primaryCount + conditionCount;
    SmallVector<sim::SimObserverBindOp> bindings;
    for (Value value : operation.getPrimaries()) {
      auto binding = value.getDefiningOp<sim::SimObserverBindOp>();
      if (!binding)
        return operation.emitOpError(
            "primary token is not produced by observer.bind");
      bindings.push_back(binding);
    }
    for (Value value : operation.getConditions()) {
      auto binding = value.getDefiningOp<sim::SimObserverBindOp>();
      if (!binding)
        return operation.emitOpError(
            "condition token is not produced by observer.bind");
      bindings.push_back(binding);
    }
    if (bindings.size() != observerCount)
      return operation.emitOpError("observer inventory is truncated");
    uint32_t captureCount = 0;
    uint32_t dependencyCount = 0;
    uint32_t previousLimbs = 0;
    SmallVector<uint32_t> widths;
    for (auto [index, binding] : llvm::enumerate(bindings)) {
      captureCount += binding.getCaptures().size();
      dependencyCount += binding.getDependencies().size();
      auto observerType =
          cast<sim::ObserverType>(binding.getResult().getType());
      std::optional<uint32_t> width =
          simulationWidth(observerType.getResultType());
      if (!width)
        return binding.emitOpError("observer result has no packed width");
      widths.push_back(*width);
      if (index < primaryCount)
        previousLimbs += (*width + 63) / 64;
    }
    uint64_t observersOffset = sizeof(obelisk_rt_computed_wait_record_v1);
    uint64_t capturesOffset =
        observersOffset +
        uint64_t{observerCount} * sizeof(obelisk_rt_computed_observer_v1);
    uint64_t dependenciesOffset =
        capturesOffset +
        uint64_t{captureCount} * sizeof(obelisk_rt_computed_capture_v1);
    uint64_t clausesOffset =
        dependenciesOffset +
        uint64_t{dependencyCount} * sizeof(obelisk_rt_computed_dependency_v1);
    uint64_t previousOffset =
        clausesOffset +
        uint64_t{primaryCount} * sizeof(obelisk_rt_computed_clause_v1);
    uint64_t totalSize =
        previousOffset + uint64_t{previousLimbs} * sizeof(uint64_t) * 2;
    if (totalSize > suspension->waitSize)
      return operation.emitOpError(
          "computed wait exceeds its canonical frame field");

    SmallVector<uint8_t> bytes(suspension->waitSize, 0);
    write32(bytes, 0, OBELISK_RT_VERSION);
    write32(bytes, 4, OBELISK_RT_SUSPEND_OBSERVER);
    write32(bytes, 8, OBELISK_RT_COMPUTED_WAIT_INTERLEAVED);
    write32(bytes, 12, primaryCount);
    write32(bytes, 16, observerCount);
    write32(bytes, 20, captureCount);
    write32(bytes, 24, dependencyCount);
    write32(bytes, 28, previousLimbs);
    write64(bytes, 32, observersOffset);
    write64(bytes, 40, capturesOffset);
    write64(bytes, 48, dependenciesOffset);
    write64(bytes, 56, clausesOffset);
    write64(bytes, 64, previousOffset);
    write64(bytes, 72, 0);
    write64(bytes, 80, totalSize);

    uint32_t captureCursor = 0;
    uint32_t dependencyCursor = 0;
    uint32_t previousCursor = 0;
    for (auto [index, binding] : llvm::enumerate(bindings)) {
      auto found = indices.find(binding.getEvaluator());
      if (found == indices.end())
        return binding.emitOpError("observer evaluator has no bytecode body");
      FunctionPlan &evaluator = plans[found->second];
      uint64_t entry =
          observersOffset + index * sizeof(obelisk_rt_computed_observer_v1);
      write64(bytes, entry, evaluator.stableID);
      write32(bytes, entry + 8, captureCursor);
      write32(bytes, entry + 12, binding.getCaptures().size());
      write32(bytes, entry + 16, dependencyCursor);
      write32(bytes, entry + 20, binding.getDependencies().size());
      write32(bytes, entry + 24,
              index < primaryCount
                  ? static_cast<uint32_t>(previousOffset +
                                          uint64_t{previousCursor} * 16)
                  : UINT32_MAX);
      captureCursor += binding.getCaptures().size();
      for (Value dependency : binding.getDependencies()) {
        uint64_t entryOffset =
            dependenciesOffset + uint64_t{dependencyCursor} *
                                     sizeof(obelisk_rt_computed_dependency_v1);
        if (auto event = dyn_cast<sim::EventType>(dependency.getType())) {
          (void)event;
          write32(bytes, entryOffset + 8, OBELISK_RT_OBSERVER_DEPENDENCY_EVENT);
          write32(bytes, entryOffset + 12, 1);
        } else {
          Type element =
              isa<sim::RefType>(dependency.getType())
                  ? cast<sim::RefType>(dependency.getType()).getElementType()
                  : cast<sim::NetType>(dependency.getType()).getElementType();
          std::optional<uint32_t> width = simulationWidth(element);
          if (!width)
            return operation.emitOpError(
                "observer dependency has no packed width");
          write32(bytes, entryOffset + 8,
                  OBELISK_RT_OBSERVER_DEPENDENCY_SIGNAL);
          write32(bytes, entryOffset + 12, *width);
        }
        ++dependencyCursor;
      }
      if (index < primaryCount)
        previousCursor += (uint64_t{widths[index]} + 63) / 64;
    }
    for (uint32_t index = 0; index != primaryCount; ++index) {
      uint64_t clause = clausesOffset +
                        uint64_t{index} * sizeof(obelisk_rt_computed_clause_v1);
      int32_t condition = operation.getConditionIndices()[index];
      write32(bytes, clause, index);
      write32(bytes, clause + 4,
              condition < 0 ? OBELISK_RT_OBSERVER_CONDITION_NONE
                            : primaryCount + static_cast<uint32_t>(condition));
      write32(bytes, clause + 8, operation.getEdges()[index]);
      write32(bytes, clause + 12,
              bindings[index]->hasAttr("obelisk_sim.event_primary")
                  ? OBELISK_RT_COMPUTED_CLAUSE_EVENT_PRIMARY
                  : 0);
    }

    Layout record{Bits,
                  0,
                  static_cast<uint32_t>(suspension->waitSize * 8),
                  0,
                  suspension->waitSize,
                  0};
    uint32_t recordRegister = plan.layouts.size();
    record.offset = llvm::alignTo(plan.scratchSize, uint64_t{8});
    plan.scratchSize = record.offset + record.size;
    plan.layouts.push_back(record);
    uint64_t constantOffset = constants.size();
    llvm::append_range(constants, bytes);
    emit({Constant, 0, recordRegister, 0, 0, 0, 0, constantOffset});
    emit({StoreFrame, 0, 0, recordRegister, 0, 0, 0, suspension->waitOffset});
    // Dynamic captures and dependencies overwrite their zeroed record slots
    // after the constant header has been copied.
    // Re-emit them because the stores above intentionally describe the final
    // frame addresses but precede this constant in the instruction stream.
    captureCursor = 0;
    dependencyCursor = 0;
    for (sim::SimObserverBindOp binding : bindings) {
      for (Value capture : binding.getCaptures()) {
        emitFrameTransfer(plan, StoreFrame, capture,
                          suspension->waitOffset + capturesOffset +
                              uint64_t{captureCursor++} *
                                  sizeof(obelisk_rt_computed_capture_v1),
                          sizeof(obelisk_rt_computed_capture_v1));
      }
      for (Value dependency : binding.getDependencies()) {
        uint32_t stableID =
            temporary(plan, IntegerType::get(operation.getContext(), 64));
        if (stableID == kInvalidRegister)
          return failure();
        emit({HandleID, 0, stableID, reg(plan, dependency)});
        emit({StoreFrame, 0, 0, stableID, 0, 0, 0,
              suspension->waitOffset + dependenciesOffset +
                  uint64_t{dependencyCursor++} *
                      sizeof(obelisk_rt_computed_dependency_v1)});
      }
    }
    previousCursor = 0;
    for (auto [index, initial] :
         llvm::enumerate(operation.getInitialValues())) {
      uint64_t offset = suspension->waitOffset + previousOffset +
                        uint64_t{previousCursor} * sizeof(uint64_t) * 2;
      Layout layout = plan.layouts[reg(plan, initial)];
      if (layout.size > UINT32_MAX)
        return operation.emitOpError("observer initial value is too large");
      emitFrameTransfer(plan, StoreFrame, initial, offset,
                        static_cast<uint32_t>(layout.size));
      previousCursor += (uint64_t{widths[index]} + 63) / 64;
    }
    Layout offsetLayout{Bits, 0, 64, 0, 8, 0};
    uint32_t offsetRegister = plan.layouts.size();
    offsetLayout.offset = llvm::alignTo(plan.scratchSize, uint64_t{8});
    plan.scratchSize = offsetLayout.offset + offsetLayout.size;
    plan.layouts.push_back(offsetLayout);
    emit({Constant, 0, offsetRegister, 0, 0, 0, 0,
          addConstant(offsetLayout, APInt(64, suspension->waitOffset))});
    uint32_t actionFlags = resumeActionFlags(operation);
    if (actionFlags == UINT32_MAX)
      return operation.emitOpError("has no executable resume region");
    emit({Suspend, OBELISK_RT_SUSPEND_OBSERVER, 0, offsetRegister, 0, 0,
          actionFlags, suspension->continuationID});
    return success();
  }

  LogicalResult encodeWait(FunctionPlan &plan, Operation *operation,
                           ValueRange continuationOperands, uint32_t kind,
                           uint32_t flags, ArrayRef<uint32_t> edges,
                           ArrayRef<Value> watched, Value delay = {}) {
    if (!plan.frame)
      return operation->emitOpError("suspension has no canonical frame");
    const ProcessSuspension *suspension = plan.frame->getSuspension(operation);
    if (!suspension)
      return operation->emitOpError("suspension is missing frame analysis");
    ArrayRef<ProcessFrameValue> slots =
        plan.frame->getContinuationLayout(suspension->continuationID);
    if (slots.size() != continuationOperands.size())
      return operation->emitOpError("continuation frame arity mismatch");
    for (auto [value, slot] : llvm::zip_equal(continuationOperands, slots))
      if (slot.storageSize > UINT32_MAX ||
          (slot.hasSecondaryStorage() && slot.storageSize > UINT32_MAX / 2))
        return operation->emitOpError(
            "canonical frame transfer exceeds the bytecode ABI limit");
      else {
        uint64_t transferSize =
            slot.storageSize * (slot.hasSecondaryStorage() ? 2 : 1);
        emitFrameTransfer(plan, StoreFrame, value, slot.valueOffset,
                          static_cast<uint32_t>(transferSize));
      }
    if (suspension->waitSize < 32 || (suspension->waitSize - 32) % 16 != 0)
      return operation->emitOpError("wait record size does not match operands");
    uint64_t entryCapacity = (suspension->waitSize - 32) / 16;
    if (edges.size() > entryCapacity || watched.size() != edges.size())
      return operation->emitOpError("wait record size does not match operands");
    Layout record{Bits,
                  0,
                  static_cast<uint32_t>(suspension->waitSize * 8),
                  0,
                  suspension->waitSize,
                  0};
    uint32_t recordRegister = plan.layouts.size();
    record.offset = llvm::alignTo(plan.scratchSize, uint64_t{8});
    plan.scratchSize = record.offset + record.size;
    plan.layouts.push_back(record);
    SmallVector<uint8_t> bytes(suspension->waitSize, 0);
    bool signalWait = kind == 2 || kind == 3;
    write32(bytes, 0, OBELISK_RT_VERSION);
    write32(bytes, 4, kind);
    write32(bytes, 8, flags);
    write32(bytes, 12, edges.size());
    for (auto [index, edge] : llvm::enumerate(edges)) {
      write32(bytes, 32 + index * 16 + 8, edge);
      if (signalWait) {
        Type type = watched[index].getType();
        Type element;
        if (auto reference = dyn_cast<sim::RefType>(type))
          element = reference.getElementType();
        else if (auto net = dyn_cast<sim::NetType>(type))
          element = net.getElementType();
        else if (auto driver = dyn_cast<sim::DriverType>(type))
          element = driver.getElementType();
        std::optional<uint32_t> width = simulationWidth(element);
        if (!width)
          return operation->emitOpError(
              "signal wait handle has no fixed-width element");
        if (edge != static_cast<uint32_t>(sim::EdgeKind::Change) &&
            edge != OBELISK_RT_WAIT_EDGE_NONE)
          *width = 1;
        write32(bytes, 32 + index * 16 + 12, *width);
      }
    }
    uint64_t constantOffset = constants.size();
    llvm::append_range(constants, bytes);
    emit({Constant, 0, recordRegister, 0, 0, 0, 0, constantOffset});
    emit({StoreFrame, 0, 0, recordRegister, 0, 0, 0, suspension->waitOffset});
    for (auto [index, handle] : llvm::enumerate(watched)) {
      uint32_t stableID =
          temporary(plan, IntegerType::get(operation->getContext(), 64));
      if (stableID == kInvalidRegister)
        return failure();
      emit({HandleID, 0, stableID, reg(plan, handle)});
      emit({StoreFrame, 0, 0, stableID, 0, 0, 0,
            suspension->waitOffset + 32 + index * 16});
    }
    if (delay)
      emit({StoreFrame, 0, 0, reg(plan, delay), 0, 0, 0,
            suspension->waitOffset + 16});
    Layout offsetLayout{Bits, 0, 64, 0, 8, 0};
    uint32_t offsetRegister = plan.layouts.size();
    offsetLayout.offset = llvm::alignTo(plan.scratchSize, uint64_t{8});
    plan.scratchSize = offsetLayout.offset + offsetLayout.size;
    plan.layouts.push_back(offsetLayout);
    APInt waitOffset(64, suspension->waitOffset);
    emit({Constant, 0, offsetRegister, 0, 0, 0, 0,
          addConstant(offsetLayout, waitOffset)});
    uint32_t actionFlags = resumeActionFlags(operation);
    if (actionFlags == UINT32_MAX)
      return operation->emitOpError("has no executable resume region");
    emit({Suspend, static_cast<uint16_t>(kind), 0, offsetRegister, 0, 0,
          actionFlags, suspension->continuationID});
    return success();
  }

  uint32_t getVPIProfile() {
    StringRef requested = options.vpi;
    if (requested == "auto") {
      if (auto graph = design.getComputeGraphAttr()) {
        switch (graph.getVpi()) {
        case sim::ComputeVPIMode::Off:
          requested = "off";
          break;
        case sim::ComputeVPIMode::Read:
          requested = "read";
          break;
        case sim::ComputeVPIMode::Full:
          requested = "full";
          break;
        }
      } else {
        requested = "off";
      }
    }
    if (requested == "off")
      return 0;
    if (requested == "read")
      return kDatabaseProfileRead;
    if (requested == "full")
      return kDatabaseProfileRead | kDatabaseProfileWrite;
    design.emitOpError("bytecode VPI profile must be auto, off, read, or full");
    return UINT32_MAX;
  }

  SmallVector<uint8_t> serializeBytecode() {
    SmallVector<uint8_t> output(OBELISK_RT_DESIGN_BYTECODE_HEADER_SIZE, 0);
    static constexpr char magic[8] = {'O', 'B', 'B', 'C', 'D', 'S', '1', '\0'};
    std::copy(std::begin(magic), std::end(magic), output.begin());
    write32(output, 8, OBELISK_RT_VERSION);
    write32(output, 12, 0);
    write32(output, 16, OBELISK_RT_DESIGN_BYTECODE_HEADER_SIZE);

    alignTo(output, 8);
    uint64_t functionOffset = output.size();
    uint64_t layoutCursor = 0;
    uint64_t continuationCursor = 0;
    for (FunctionPlan &plan : plans) {
      append64(output, plan.stableID);
      append64(output, plan.initialScheduleRank);
      append64(output, plan.firstInstruction);
      append64(output, plan.instructionCount);
      append64(output, layoutCursor);
      append64(output, plan.layouts.size());
      append32(output, plan.function.getFunctionType().getNumInputs());
      append32(output, plan.function.getFunctionType().getNumResults());
      append64(output, plan.scratchSize);
      append64(output, plan.scratchAlignment);
      append64(output, continuationCursor);
      append64(output, plan.continuations.size());
      uint64_t functionFlags =
          (plan.function.getEntryKind() == sim::EntryKind::Function ||
           plan.function.getEntryKind() == sim::EntryKind::Observer)
              ? 0
              : (plan.frame->getFrameSize() << 1) |
                    OBELISK_RT_DESIGN_FUNCTION_PROCESS;
      if (plan.function.getEntryKind() == sim::EntryKind::Final)
        functionFlags |= OBELISK_RT_DESIGN_FUNCTION_FINAL;
      if ((functionFlags & OBELISK_RT_DESIGN_FUNCTION_PROCESS) != 0) {
        uint32_t homeRegion = executableRegion(plan.function.getHomeRegion());
        if (homeRegion == UINT32_MAX) {
          plan.function.emitOpError("has no executable runtime home region");
          return {};
        }
        functionFlags |= OBELISK_RT_DESIGN_FUNCTION_HOME(homeRegion);
      }
      append64(output, functionFlags);
      layoutCursor += plan.layouts.size();
      continuationCursor += plan.continuations.size();
    }
    alignTo(output, 8);
    uint64_t layoutOffset = output.size();
    for (const FunctionPlan &plan : plans)
      for (const Layout &layout : plan.layouts) {
        output.push_back(layout.kind);
        output.push_back(layout.flags);
        append16(output, 0);
        append32(output, layout.width);
        append64(output, layout.offset);
        append64(output, layout.size);
        append64(output, layout.auxiliary);
        append64(output, 0);
      }
    alignTo(output, 8);
    uint64_t codeOffset = output.size();
    for (const Instruction &instruction : instructions) {
      append16(output, instruction.opcode);
      append16(output, instruction.flags);
      append32(output, instruction.destination);
      append32(output, instruction.source0);
      append32(output, instruction.source1);
      append32(output, instruction.source2);
      append32(output, instruction.auxiliary);
      append64(output, instruction.immediate);
    }
    alignTo(output, 8);
    uint64_t operandOffset = output.size();
    for (const OperandMap &operand : operandMaps) {
      append32(output, operand.destination);
      append32(output, operand.source);
    }
    alignTo(output, 8);
    uint64_t constantOffset = output.size();
    llvm::append_range(output, constants);
    alignTo(output, 8);
    uint64_t continuationOffset = output.size();
    for (const FunctionPlan &plan : plans)
      for (const Continuation &continuation : plan.continuations) {
        append32(output, continuation.function);
        append32(output, continuation.id);
        append64(output, continuation.instruction);
        append32(output, continuation.scheduleRank);
        append32(output, 0);
      }
    alignTo(output, 8);
    uint64_t intrinsicOffset = output.size();
    for (const IntrinsicSignature &signature : intrinsicSignatures) {
      append32(output, signature.id);
      append32(output, signature.inputCount);
      append32(output, signature.outputCount);
      append32(output, signature.flags);
    }
    alignTo(output, 8);
    uint64_t siteOffset = output.size();
    for (const IntrinsicSite &site : intrinsicSites) {
      append32(output, site.intrinsic);
      append32(output, site.firstOperand);
      append32(output, site.inputCount);
      append32(output, site.outputCount);
    }
    alignTo(output, 8);
    uint64_t stateOffset = output.size();
    for (const CaptureRecord &capture : captureRecords) {
      append32(output, capture.function);
      append32(output, capture.argument);
      append64(output, capture.valueOffset);
      append64(output, capture.unknownOffset);
      append64(output, capture.planeSize);
    }
    // Static net descriptors precede drivers. They let a design-bound context
    // reproduce the native initial Z state even when a net has no drivers.
    for (const StateLayout::Net &net : state.netLayouts) {
      append32(output, UINT32_MAX - 1);
      append32(output, (net.fourState ? 1u : 0u) |
                           (static_cast<uint32_t>(net.resolution) << 1));
      append64(output, net.offset);
      append64(output, UINT64_MAX);
      append64(output, net.width);
    }
    for (const StateLayout::Driver &driver : state.driverLayouts) {
      append32(output, UINT32_MAX);
      // Driver planes remain four-state even when the logical destination is
      // two-state, so Z release and contention are never inferred from a
      // previously published net value.
      append32(output, 1u | (static_cast<uint32_t>(driver.resolution) << 1));
      append64(output, driver.offset + driver.drivenLow);
      append64(output, driver.netOffset + driver.drivenLow);
      append64(output, driver.drivenWidth);
    }
    alignTo(output, 8);
    uint64_t connectivityOffset = output.size();
    for (const StateLayout::Connection &connection : state.connections) {
      append64(output, connection.lhsOffset);
      append64(output, connection.rhsOffset);
      append64(output, connection.width);
      output.push_back(static_cast<uint8_t>(connection.lhsResolution));
      output.push_back(static_cast<uint8_t>(connection.rhsResolution));
      output.push_back(connection.rhsReversed ? 1 : 0);
      output.push_back(0);
      append32(output, 0);
    }

    write64(output, 24, output.size());
    write64(output, 40, functionOffset);
    write64(output, 48, plans.size());
    write64(output, 56, layoutOffset);
    write64(output, 64, layoutCursor);
    write64(output, 72, codeOffset);
    write64(output, 80, instructions.size());
    write64(output, 88, operandOffset);
    write64(output, 96, operandMaps.size());
    write64(output, 104, constantOffset);
    write64(output, 112, constants.size());
    write64(output, 120, continuationOffset);
    write64(output, 128, continuationCursor);
    write64(output, 136, intrinsicOffset);
    write64(output, 144, intrinsicSignatures.size());
    write64(output, 152, siteOffset);
    write64(output, 160, intrinsicSites.size());
    write64(output, 168, stateOffset);
    write64(output, 176,
            captureRecords.size() + state.netLayouts.size() +
                state.driverLayouts.size());
    write64(output, 184, connectivityOffset);
    write64(output, 192, state.connections.size());
    write64(output, 200, 0);
    write64(output, 32, checksum(output, 32));
    return output;
  }

  SmallVector<uint8_t> serializeDatabase(uint32_t profile) {
    struct Source {
      std::string file;
      uint64_t lineColumn = 0;
    };
    auto sourceFor = [](Operation *operation) {
      Source source;
      if (auto location =
              operation->getLoc()->findInstanceOf<FileLineColLoc>()) {
        source.file = location.getFilename().getValue().str();
        source.lineColumn =
            uint64_t{location.getLine()} << 32 | uint64_t{location.getColumn()};
      }
      return source;
    };
    struct Record {
      uint32_t kind;
      uint32_t caps;
      uint64_t id;
      uint64_t scope;
      std::string name;
      Type type;
      uint64_t stateOffset;
      Source source;
    };
    SmallVector<sim::SimScopeDeclOp> scopes;
    SmallVector<Record> objects;
    auto fallbackName = [](StringRef kind, uint64_t id) {
      return (kind + "." + Twine(id)).str();
    };
    for (Operation &operation : design.getBody().front()) {
      if (auto scope = dyn_cast<sim::SimScopeDeclOp>(operation))
        scopes.push_back(scope);
      else if (auto storage = dyn_cast<sim::SimStorageDeclOp>(operation))
        objects.push_back({2, profile & kDatabaseProfileWrite ? 3u : 1u,
                           storage.getId(), storage.getScopeId(),
                           storage.getHierarchicalName()
                               .value_or(storage.getDebugName().value_or(
                                   fallbackName("storage", storage.getId())))
                               .str(),
                           storage.getType(),
                           state.storage.lookup(storage.getId()),
                           sourceFor(storage)});
      else if (auto net = dyn_cast<sim::SimNetDeclOp>(operation))
        objects.push_back({3, profile & kDatabaseProfileWrite ? 3u : 1u,
                           net.getId(), net.getScopeId(),
                           net.getHierarchicalName()
                               .value_or(net.getDebugName().value_or(
                                   fallbackName("net", net.getId())))
                               .str(),
                           net.getType(), state.nets.lookup(net.getId()),
                           sourceFor(net)});
      else if (auto driver = dyn_cast<sim::SimDriverDeclOp>(operation))
        objects.push_back({4, profile & kDatabaseProfileWrite ? 3u : 1u,
                           driver.getId(), driver.getScopeId(),
                           (driver.getHierarchicalName().value_or(
                                driver.getDebugName().value_or(
                                    fallbackName("driver", driver.getId()))) +
                            ".$driver." + Twine(driver.getId()))
                               .str(),
                           driver.getType(),
                           state.drivers.lookup(driver.getId()),
                           sourceFor(driver)});
      else if (auto codeUnit = dyn_cast<sim::SimCodeUnitDeclOp>(operation))
        objects.push_back(
            {(codeUnit.getCodeUnitKind() == sim::EntryKind::Function ||
              codeUnit.getCodeUnitKind() == sim::EntryKind::Observer)
                 ? 7u
                 : 5u,
             0, codeUnit.getId(), codeUnit.getScopeId(),
             codeUnit.getHierarchicalName().str(), Type{}, 0,
             sourceFor(codeUnit)});
    }
    if (scopes.empty())
      return {};
    llvm::sort(scopes, [](auto left, auto right) {
      return left.getId() < right.getId();
    });
    llvm::sort(objects, [](const Record &left, const Record &right) {
      return std::tie(left.scope, left.name, left.kind, left.id) <
             std::tie(right.scope, right.name, right.kind, right.id);
    });
    DenseSet<uint64_t> scopeIDs;
    sim::SimScopeDeclOp root;
    for (auto scope : scopes) {
      if (!scopeIDs.insert(scope.getId()).second) {
        scope.emitOpError("duplicate scope ID in reflection database");
        return {};
      }
      if (!scope.getParent()) {
        if (root) {
          scope.emitOpError(
              "reflection database requires exactly one root scope");
          return {};
        }
        root = scope;
      }
    }
    if (!root) {
      design.emitOpError("reflection database requires a root scope");
      return {};
    }
    llvm::sort(objects, [](const Record &left, const Record &right) {
      return std::tie(left.scope, left.name, left.kind, left.id) <
             std::tie(right.scope, right.name, right.kind, right.id);
    });
    for (auto scope : scopes)
      if (scope.getParent() && !scopeIDs.contains(*scope.getParent())) {
        scope.emitOpError("reflection parent scope does not exist");
        return {};
      }
    for (const Record &object : objects)
      if (!scopeIDs.contains(object.scope)) {
        design.emitOpError("reflection object references an unknown scope");
        return {};
      }
    struct TypeRecord {
      uint32_t kind = 0;
      uint32_t flags = 0;
      uint64_t width = 0;
      int64_t left = 0;
      int64_t right = 0;
      uint32_t element = UINT32_MAX;
      uint32_t firstChild = UINT32_MAX;
      uint64_t childCount = 0;
      uint64_t ordinal = 0;
      uint64_t packedOffset = 0;
      std::string name;
    };
    SmallVector<TypeRecord> types;
    DenseMap<Type, uint32_t> typeIndices;
    bool typeError = false;
    std::function<std::optional<uint32_t>(Type)> addType =
        [&](Type type) -> std::optional<uint32_t> {
      if (auto found = typeIndices.find(type); found != typeIndices.end())
        return found->second;
      std::optional<uint32_t> width = simulationWidth(type);
      if (!width) {
        typeError = true;
        return std::nullopt;
      }
      uint32_t index = types.size();
      typeIndices[type] = index;
      types.emplace_back();
      TypeRecord &record = types[index];
      record.width = *width;
      record.left = static_cast<int64_t>(*width - 1);
      record.right = 0;
      if (containsLogic(type))
        record.flags |= 1;
      if (auto integer = dyn_cast<IntegerType>(type)) {
        record.kind = 1;
        record.flags |= integer.isSigned() ? 2 : 0;
        record.flags |= 4;
        record.name = "bits";
      } else if (type.isF64()) {
        record.kind = 1;
        record.name = "real";
      } else if (isa<sim::LogicType>(type)) {
        record.kind = 1;
        record.flags |= 4;
        record.name = "logic";
      } else if (auto packed = dyn_cast<sim::PackedArrayType>(type)) {
        record.kind = 2;
        record.flags |= 4;
        record.left = packed.getLeft();
        record.right = packed.getRight();
        record.name = "packed_array";
        auto element = addType(packed.getElementType());
        if (!element)
          return std::nullopt;
        types[index].element = *element;
      } else if (auto unpacked = dyn_cast<sim::UnpackedArrayType>(type)) {
        record.kind = 2;
        record.left = unpacked.getLeft();
        record.right = unpacked.getRight();
        record.name = "unpacked_array";
        auto element = addType(unpacked.getElementType());
        if (!element)
          return std::nullopt;
        types[index].element = *element;
      } else {
        ArrayAttr fields;
        bool packed = false;
        bool tagged = false;
        uint64_t tagBits = 0;
        if (auto value = dyn_cast<sim::PackedStructType>(type)) {
          record.kind = 3;
          packed = true;
          fields = value.getFields();
          record.name = "packed_struct";
        } else if (auto value = dyn_cast<sim::UnpackedStructType>(type)) {
          record.kind = 3;
          fields = value.getFields();
          record.name = "unpacked_struct";
        } else if (auto value = dyn_cast<sim::PackedUnionType>(type)) {
          record.kind = 4;
          packed = true;
          tagged = value.getIsTagged();
          tagBits = value.getTagBits();
          fields = value.getFields();
          record.name = "packed_union";
        } else if (auto value = dyn_cast<sim::UnpackedUnionType>(type)) {
          record.kind = 4;
          tagged = value.getIsTagged();
          fields = value.getFields();
          if (tagged)
            tagBits =
                llvm::Log2_64_Ceil(static_cast<uint64_t>(fields.size()) + 1);
          record.name = "unpacked_union";
        } else {
          typeError = true;
          return std::nullopt;
        }
        if (packed)
          types[index].flags |= 4;
        if (tagged)
          types[index].flags |= 8;
        types[index].ordinal = tagBits;
        types[index].firstChild = types.size();
        types[index].childCount = fields.size();
        for (size_t field = 0; field != fields.size(); ++field)
          types.emplace_back();
        for (auto [ordinal, attribute] : llvm::enumerate(fields)) {
          auto field = dyn_cast<sim::FieldAttr>(attribute);
          if (!field) {
            typeError = true;
            return std::nullopt;
          }
          auto element = addType(field.getType());
          if (!element)
            return std::nullopt;
          TypeRecord &child = types[types[index].firstChild + ordinal];
          child.kind = 5;
          child.flags = containsLogic(field.getType()) ? 1 : 0;
          child.width = *simulationWidth(field.getType());
          child.left = static_cast<int64_t>(child.width - 1);
          child.right = 0;
          child.element = *element;
          child.ordinal = ordinal;
          if (auto subelement = sim::getAggregateProvenanceSubelement(
                  type, static_cast<unsigned>(ordinal)))
            child.packedOffset = subelement->first;
          child.name = field.getName().getValue().str();
        }
      }
      return index;
    };
    for (const Record &object : objects)
      if (object.type && !addType(object.type))
        return {};
    if (typeError)
      return {};

    SmallVector<uint8_t> strings(1, 0);
    llvm::StringMap<uint64_t> stringOffsets;
    auto intern = [&](StringRef value) {
      auto found = stringOffsets.find(value);
      if (found != stringOffsets.end())
        return found->second;
      uint64_t offset = strings.size();
      llvm::append_range(strings, value.bytes());
      strings.push_back(0);
      stringOffsets[value] = offset;
      return offset;
    };
    for (auto scope : scopes) {
      intern(scope.getHierarchicalName().value_or(
          scope.getDebugName().value_or(fallbackName("scope", scope.getId()))));
      Source source = sourceFor(scope);
      if (!source.file.empty())
        intern(source.file);
    }
    for (const Record &object : objects) {
      intern(object.name);
      if (!object.source.file.empty())
        intern(object.source.file);
    }
    for (const TypeRecord &type : types)
      intern(type.name);

    SmallVector<uint8_t> output(128, 0);
    uint64_t scopeOffset = output.size();
    DenseMap<uint64_t, uint64_t> scopeOffsets;
    for (auto [index, scope] : llvm::enumerate(scopes))
      scopeOffsets[scope.getId()] = scopeOffset + index * 64;
    uint64_t objectOffset = scopeOffset + scopes.size() * 64;
    uint64_t typeOffset = objectOffset + objects.size() * 96;
    uint64_t stringOffset = typeOffset + types.size() * 80;
    uint64_t indexOffset = 0;

    DenseMap<uint64_t, SmallVector<uint64_t>> children;
    for (auto scope : scopes)
      if (auto parent = scope.getParent())
        children[*parent].push_back(scopeOffsets.lookup(scope.getId()));
    for (auto [index, object] : llvm::enumerate(objects))
      children[object.scope].push_back(objectOffset + index * 96);

    for (auto scope : scopes) {
      uint64_t self = scopeOffsets.lookup(scope.getId());
      append32(output, 1);
      append32(output, 4);
      append64(output, scope.getId());
      append64(output,
               scope.getParent() ? scopeOffsets.lookup(*scope.getParent()) : 0);
      append64(output, children[scope.getId()].empty()
                           ? 0
                           : children[scope.getId()][0]);
      uint64_t sibling = 0;
      if (scope.getParent()) {
        ArrayRef<uint64_t> peers = children[*scope.getParent()];
        auto found = llvm::find(peers, self);
        if (found != peers.end() && std::next(found) != peers.end())
          sibling = *std::next(found);
      }
      append64(output, sibling);
      std::string generatedName = fallbackName("scope", scope.getId());
      StringRef name = scope.getHierarchicalName().value_or(
          scope.getDebugName().value_or(generatedName));
      append64(output, stringOffset + intern(name));
      Source source = sourceFor(scope);
      append64(output,
               source.file.empty() ? 0 : stringOffset + intern(source.file));
      append64(output, source.lineColumn);
    }
    for (auto [index, object] : llvm::enumerate(objects)) {
      append32(output, object.kind);
      append32(output, object.caps);
      append64(output, object.id);
      append64(output, scopeOffsets.lookup(object.scope));
      ArrayRef<uint64_t> peers = children[object.scope];
      uint64_t self = objectOffset + index * 96;
      auto found = llvm::find(peers, self);
      append64(output, found != peers.end() && std::next(found) != peers.end()
                           ? *std::next(found)
                           : 0);
      append64(output, object.source.file.empty()
                           ? 0
                           : stringOffset + intern(object.source.file));
      append64(output, stringOffset + intern(object.name));
      uint64_t width = 0;
      if (object.type) {
        uint32_t typeIndex = typeIndices.lookup(object.type);
        width = *simulationWidth(object.type);
        append64(output, typeOffset + uint64_t{typeIndex} * 80);
      } else {
        append64(output, 0);
      }
      append64(output, width);
      int64_t left = width == 0 ? 0 : static_cast<int64_t>(width - 1);
      int64_t right = 0;
      if (auto array = dyn_cast_if_present<sim::PackedArrayType>(object.type)) {
        left = array.getLeft();
        right = array.getRight();
      } else if (auto array =
                     dyn_cast_if_present<sim::UnpackedArrayType>(object.type)) {
        left = array.getLeft();
        right = array.getRight();
      }
      append64(output, static_cast<uint64_t>(left));
      append64(output, static_cast<uint64_t>(right));
      append64(output, object.stateOffset);
      append64(output, object.source.lineColumn);
    }
    for (const TypeRecord &entry : types) {
      append32(output, 6);
      append32(output, entry.kind | (entry.flags << 8));
      append64(output, entry.width);
      append64(output, static_cast<uint64_t>(entry.left));
      append64(output, static_cast<uint64_t>(entry.right));
      append64(output, entry.element == UINT32_MAX
                           ? 0
                           : typeOffset + uint64_t{entry.element} * 80);
      append64(output, entry.firstChild == UINT32_MAX
                           ? 0
                           : typeOffset + uint64_t{entry.firstChild} * 80);
      append64(output, entry.childCount);
      append64(output, entry.ordinal);
      append64(output, entry.packedOffset);
      append64(output, stringOffset + intern(entry.name));
    }
    llvm::append_range(output, strings);
    alignTo(output, 8);
    indexOffset = output.size();
    struct Index {
      uint64_t hash, name, record;
      std::string text;
    };
    SmallVector<Index> names;
    for (auto scope : scopes) {
      std::string generatedName = fallbackName("scope", scope.getId());
      StringRef name = scope.getHierarchicalName().value_or(
          scope.getDebugName().value_or(generatedName));
      names.push_back({stableHash(name), stringOffset + intern(name),
                       scopeOffsets.lookup(scope.getId()), name.str()});
    }
    for (auto [index, object] : llvm::enumerate(objects))
      names.push_back({stableHash(object.name),
                       stringOffset + intern(object.name),
                       objectOffset + index * 96, object.name});
    llvm::sort(names, [](const Index &left, const Index &right) {
      return std::tie(left.hash, left.text) < std::tie(right.hash, right.text);
    });
    for (size_t index = 1; index < names.size(); ++index)
      if (names[index - 1].text == names[index].text) {
        design.emitOpError() << "duplicate hierarchical reflection name '"
                             << names[index].text << "'";
        return {};
      }
    for (const Index &entry : names) {
      append64(output, entry.hash);
      append64(output, entry.name);
      append64(output, entry.record);
    }
    static constexpr char magic[8] = {'O', 'B', 'D', 'S', 'G', 'N', '1', '\0'};
    std::copy(std::begin(magic), std::end(magic), output.begin());
    write32(output, 8, OBELISK_RT_VERSION);
    write32(output, 12, 0);
    write32(output, 16, profile);
    write32(output, 20, 128);
    write64(output, 24, output.size());
    write64(output, 40, scopeOffsets.lookup(root.getId()));
    write64(output, 48, scopeOffset);
    write64(output, 56, scopes.size());
    write64(output, 64, objectOffset);
    write64(output, 72, objects.size());
    write64(output, 80, typeOffset);
    write64(output, 88, types.size());
    write64(output, 96, stringOffset);
    write64(output, 104, strings.size());
    write64(output, 112, indexOffset);
    write64(output, 120, names.size());
    write64(output, 32, checksum(output, 32));
    return output;
  }

  sim::SimDesignOp design;
  SimulationBytecodeOptions options;
  const llvm::DataLayout &dataLayout;
  StateLayout state;
  DenseSet<Value> twoStateLogicRegisters;
  SmallVector<FunctionPlan, 0> plans;
  llvm::StringMap<uint32_t> indices;
  llvm::StringMap<uint64_t> classIDs;
  llvm::StringMap<sim::SimFuncOp> externalFunctions;
  DenseMap<uint32_t, std::string> importSymbols;
  SmallVector<Instruction> instructions;
  SmallVector<OperandMap> operandMaps;
  SmallVector<uint8_t> constants;
  DenseMap<uint64_t, uint64_t> zeroConstants;
  SmallVector<IntrinsicSignature> intrinsicSignatures;
  SmallVector<IntrinsicSite> intrinsicSites;
  SmallVector<CaptureRecord> captureRecords;
};

class EncodeObeliskSimToBytecodePass final
    : public impl::EncodeObeliskSimToBytecodePassBase<
          EncodeObeliskSimToBytecodePass> {
public:
  using Base =
      impl::EncodeObeliskSimToBytecodePassBase<EncodeObeliskSimToBytecodePass>;
  using Base::Base;

  void runOnOperation() override {
    ModuleOp module = getOperation();
    auto layoutAttr = module->getAttrOfType<StringAttr>("llvm.data_layout");
    if (!layoutAttr) {
      module.emitError(
          "bytecode encoding requires an explicit llvm.data_layout");
      return signalPassFailure();
    }
    llvm::Expected<llvm::DataLayout> parsed =
        llvm::DataLayout::parse(layoutAttr.getValue());
    if (!parsed) {
      module.emitError() << "invalid LLVM data layout: "
                         << llvm::toString(parsed.takeError());
      return signalPassFailure();
    }
    if (!parsed->isLittleEndian() || parsed->getPointerSizeInBits() != 64) {
      module.emitError(
          "bytecode encoding requires a 64-bit little-endian target");
      return signalPassFailure();
    }
    SmallVector<sim::SimDesignOp> designs;
    module.walk([&](sim::SimDesignOp design) { designs.push_back(design); });
    if (designs.size() != 1) {
      module.emitError(
          "bytecode encoding requires exactly one simulation design");
      return signalPassFailure();
    }
    SimulationBytecodeOptions options;
    options.vpi = vpi;
    options.requireBytecode = requireBytecode;
    FailureOr<EncodedSimulationDesign> encoded =
        encodeSimulationDesign(designs.front(), options);
    if (failed(encoded))
      return signalPassFailure();
    OpBuilder builder(module.getContext());
    auto asI8 = [](ArrayRef<uint8_t> bytes) {
      return ArrayRef<int8_t>(reinterpret_cast<const int8_t *>(bytes.data()),
                              bytes.size());
    };
    module->setAttr("obelisk.bytecode.image",
                    builder.getDenseI8ArrayAttr(asI8(encoded->bytecode)));
    module->setAttr("obelisk.execution.flags",
                    builder.getI32IntegerAttr(encoded->executionFlags));
    module->setAttr("obelisk.execution.state_bits",
                    builder.getI64IntegerAttr(encoded->stateBitCount));
    if (!encoded->designDatabase.empty())
      module->setAttr(
          "obelisk.design.database",
          builder.getDenseI8ArrayAttr(asI8(encoded->designDatabase)));
    else
      module->removeAttr("obelisk.design.database");
    llvm::StringMap<sim::SimFuncOp> functions;
    designs.front().walk([&](sim::SimFuncOp function) {
      functions[function.getSymName()] = function;
    });
    for (const SimulationBytecodeFunction &function : encoded->functions) {
      sim::SimFuncOp source = functions.lookup(function.symbol);
      source->setAttr("obelisk.bytecode.function",
                      builder.getI32IntegerAttr(function.index));
      source->setAttr("obelisk.bytecode.scratch_size",
                      builder.getI64IntegerAttr(function.scratchSize));
      source->setAttr("obelisk.bytecode.scratch_alignment",
                      builder.getI64IntegerAttr(function.scratchAlignment));
      source->setAttr(
          "obelisk.bytecode.two_state_logic_registers",
          builder.getI32IntegerAttr(function.twoStateLogicRegisters));
    }
  }
};

} // namespace

FailureOr<EncodedSimulationDesign>
encodeSimulationDesign(sim::SimDesignOp design,
                       const SimulationBytecodeOptions &options) {
  ModuleOp module = design->getParentOfType<ModuleOp>();
  if (!module)
    return design.emitOpError("requires a containing module");
  auto layoutAttr = module->getAttrOfType<StringAttr>("llvm.data_layout");
  if (!layoutAttr)
    return design.emitOpError("requires an explicit llvm.data_layout");
  llvm::Expected<llvm::DataLayout> parsed =
      llvm::DataLayout::parse(layoutAttr.getValue());
  if (!parsed) {
    design.emitOpError() << "invalid LLVM data layout: "
                         << llvm::toString(parsed.takeError());
    return failure();
  }
  if (!parsed->isLittleEndian() || parsed->getPointerSizeInBits() != 64)
    return design.emitOpError(
        "bytecode encoding requires a 64-bit little-endian target");
  Encoder encoder(design, options, *parsed);
  return encoder.encode();
}

} // namespace obelisk
