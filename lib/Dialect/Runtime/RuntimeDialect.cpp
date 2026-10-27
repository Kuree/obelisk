//===- RuntimeDialect.cpp - Typed runtime ABI dialect --------------------===//

#include "talos/Dialect/Runtime/RuntimeABI.h"
#include "talos/Dialect/Runtime/RuntimeOps.h"

#include "talos/Runtime/Runtime.h"

#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/DialectImplementation.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/TypeSwitch.h"
#include "llvm/Support/ErrorHandling.h"

using namespace mlir;

#include "talos/Dialect/Runtime/RuntimeDialect.cpp.inc"
#include "talos/Dialect/Runtime/RuntimeEnums.cpp.inc"

#define GET_ATTRDEF_CLASSES
#include "talos/Dialect/Runtime/RuntimeAttrs.cpp.inc"

#define GET_TYPEDEF_CLASSES
#include "talos/Dialect/Runtime/RuntimeTypes.cpp.inc"

#define GET_OP_CLASSES
#include "talos/Dialect/Runtime/RuntimeOps.cpp.inc"

namespace talos::runtime {

Operation *TalosRuntimeDialect::materializeConstant(OpBuilder &builder,
                                                      Attribute value,
                                                      Type type,
                                                      Location location) {
  if (!isa<ByteSpanType>(type))
    return nullptr;
  auto bytes = dyn_cast<StringAttr>(value);
  return bytes ? RTBytesConstantOp::create(builder, location, type, bytes)
               : nullptr;
}

OpFoldResult RTBytesConstantOp::fold(FoldAdaptor) { return getValueAttr(); }

llvm::StringRef getRuntimeSymbol(RuntimeCall call) {
  switch (call) {
#define TALOS_RUNTIME_CALL(Name, Op, Symbol, Signature)                      \
  case RuntimeCall::Name:                                                      \
    return #Symbol;
#include "talos/Dialect/Runtime/RuntimeABI.def"
#undef TALOS_RUNTIME_CALL
  }
  llvm_unreachable("unknown runtime call");
}

RuntimeSignature getRuntimeSignature(RuntimeCall call) {
  switch (call) {
#define TALOS_RUNTIME_CALL(Name, Op, Symbol, Signature)                      \
  case RuntimeCall::Name:                                                      \
    return RuntimeSignature::Signature;
#include "talos/Dialect/Runtime/RuntimeABI.def"
#undef TALOS_RUNTIME_CALL
  }
  llvm_unreachable("unknown runtime call");
}

std::optional<RuntimeCall> getRuntimeCall(Operation *operation) {
#define TALOS_RUNTIME_CALL(Name, Op, Symbol, Signature)                      \
  if (isa<Op>(operation))                                                      \
    return RuntimeCall::Name;
#include "talos/Dialect/Runtime/RuntimeABI.def"
#undef TALOS_RUNTIME_CALL
  return std::nullopt;
}

bool isColdRuntimeCall(RuntimeCall call) {
  switch (call) {
  case RuntimeCall::StatusString:
  case RuntimeCall::LastError:
  case RuntimeCall::Finish:
  case RuntimeCall::ProgramExit:
  case RuntimeCall::Stop:
  case RuntimeCall::Fatal:
  case RuntimeCall::Error:
  case RuntimeCall::Display:
  case RuntimeCall::FileError:
    return true;
  default:
    // Formatting and file services may sit on a design's ordinary path (a
    // trace written every cycle); only diagnostics and termination are cold.
    return false;
  }
}

static_assert(static_cast<uint32_t>(DescriptorKind::Invalid) ==
              TALOS_RT_DESCRIPTOR_INVALID);
static_assert(static_cast<uint32_t>(DescriptorKind::Scope) ==
              TALOS_RT_DESCRIPTOR_SCOPE);
static_assert(static_cast<uint32_t>(DescriptorKind::Storage) ==
              TALOS_RT_DESCRIPTOR_STORAGE);
static_assert(static_cast<uint32_t>(DescriptorKind::Net) ==
              TALOS_RT_DESCRIPTOR_NET);
static_assert(static_cast<uint32_t>(DescriptorKind::Driver) ==
              TALOS_RT_DESCRIPTOR_DRIVER);
static_assert(static_cast<uint32_t>(DescriptorKind::Event) ==
              TALOS_RT_DESCRIPTOR_EVENT);
static_assert(static_cast<uint32_t>(DescriptorKind::Process) ==
              TALOS_RT_DESCRIPTOR_PROCESS);
static_assert(static_cast<uint32_t>(DescriptorKind::Fragment) ==
              TALOS_RT_DESCRIPTOR_FRAGMENT);
static_assert(static_cast<uint32_t>(DescriptorKind::Function) ==
              TALOS_RT_DESCRIPTOR_FUNCTION);

static_assert(static_cast<uint32_t>(ActionKind::Continue) ==
              TALOS_RT_FRAGMENT_CONTINUE);
static_assert(static_cast<uint32_t>(ActionKind::Suspend) ==
              TALOS_RT_FRAGMENT_SUSPEND);
static_assert(static_cast<uint32_t>(ActionKind::Terminate) ==
              TALOS_RT_FRAGMENT_TERMINATE);

static_assert(static_cast<uint32_t>(SuspensionKind::None) ==
              TALOS_RT_SUSPEND_NONE);
static_assert(static_cast<uint32_t>(SuspensionKind::Delay) ==
              TALOS_RT_SUSPEND_DELAY);
static_assert(static_cast<uint32_t>(SuspensionKind::Change) ==
              TALOS_RT_SUSPEND_CHANGE);
static_assert(static_cast<uint32_t>(SuspensionKind::Edge) ==
              TALOS_RT_SUSPEND_EDGE);
static_assert(static_cast<uint32_t>(SuspensionKind::Event) ==
              TALOS_RT_SUSPEND_EVENT);
static_assert(static_cast<uint32_t>(SuspensionKind::Await) ==
              TALOS_RT_SUSPEND_AWAIT);
static_assert(static_cast<uint32_t>(SuspensionKind::Join) ==
              TALOS_RT_SUSPEND_JOIN);
static_assert(static_cast<uint32_t>(SuspensionKind::Forever) ==
              TALOS_RT_SUSPEND_FOREVER);
static_assert(static_cast<uint32_t>(SuspensionKind::Frontier) ==
              TALOS_RT_SUSPEND_FRONTIER);
static_assert(static_cast<uint32_t>(SuspensionKind::EventOrder) ==
              TALOS_RT_SUSPEND_EVENT_ORDER);

static_assert(static_cast<uint32_t>(ExecutionFlags::HasBytecode) ==
              TALOS_RT_EXECUTION_HAS_BYTECODE);
static_assert(static_cast<uint32_t>(ExecutionFlags::HasDesignDatabase) ==
              TALOS_RT_EXECUTION_HAS_DESIGN_DATABASE);
static_assert(static_cast<uint32_t>(ExecutionFlags::VPIRead) ==
              TALOS_RT_EXECUTION_VPI_READ);
static_assert(static_cast<uint32_t>(ExecutionFlags::VPIWrite) ==
              TALOS_RT_EXECUTION_VPI_WRITE);
static_assert(static_cast<uint32_t>(ExecutionFlags::RequireBytecode) ==
              TALOS_RT_EXECUTION_REQUIRE_BYTECODE);
static_assert(static_cast<uint32_t>(ExecutionFlags::PreponedSnapshot) ==
              TALOS_RT_EXECUTION_PREPONED_SNAPSHOT);
static_assert(static_cast<uint32_t>(ExecutionFlags::WaveformMetadata) ==
              TALOS_RT_EXECUTION_WAVEFORM_METADATA);
static_assert(static_cast<uint32_t>(ExecutionFlags::DPIExports) ==
              TALOS_RT_EXECUTION_DPI_EXPORTS);
static_assert(static_cast<uint32_t>(ExecutionFlags::ClassBitstream) ==
              TALOS_RT_EXECUTION_CLASS_BITSTREAM);
static_assert(static_cast<uint32_t>(ExecutionFlags::CoverageSchema) ==
              TALOS_RT_EXECUTION_COVERAGE_SCHEMA);
static_assert(static_cast<uint32_t>(ExecutionFlags::PolledCollection) ==
              TALOS_RT_EXECUTION_POLLED_COLLECTION);
static_assert(static_cast<uint32_t>(ExecutionFlags::TwoState) ==
              TALOS_RT_EXECUTION_TWO_STATE);

ExecutionFlags getExecutionFlags(Operation *module) {
  ExecutionFlagsAttr flags = get<ExecutionFlagSet>(module);
  return flags ? flags.getValue() : ExecutionFlags::None;
}

void setExecutionFlags(Operation *module, ExecutionFlags flags) {
  set<ExecutionFlagSet>(module,
                        ExecutionFlagsAttr::get(module->getContext(), flags));
}

Features getFeatures(Operation *module) {
  FeaturesAttr features = get<FeatureSet>(module);
  return features ? features.getValue() : Features::None;
}

void setFeatures(Operation *module, Features features, bool required) {
  Features current = getFeatures(module);
  current = required ? current | features : current & ~features;
  if (current == Features::None)
    remove<FeatureSet>(module);
  else
    set<FeatureSet>(module, FeaturesAttr::get(module->getContext(), current));
}

LogicalResult
TalosRuntimeDialect::verifyOperationAttribute(Operation *operation,
                                                NamedAttribute attribute) {
  if (failed(discardable::verify<
             BytecodeImage, DesignDatabase, ExecutionFlagSet, StateBits,
             SampledRanges, Materialized, ClassBitstreamBlob,
             CoverageSchemaBlob, FeatureSet, BytecodeFunction,
             BytecodeScratchSize, BytecodeScratchAlignment,
             BytecodeTwoStateLogicRegisters, LLVMPrepared, LLVMByteGlobals>(
          operation, attribute)))
    return failure();
  StringRef name = attribute.getName().strref();
  bool function = name == BytecodeFunction::getNameStr() ||
                  name == BytecodeScratchSize::getNameStr() ||
                  name == BytecodeScratchAlignment::getNameStr() ||
                  name == BytecodeTwoStateLogicRegisters::getNameStr();
  if (!function && name != LLVMByteGlobals::getNameStr() &&
      !isa<ModuleOp>(operation))
    return operation->emitOpError("attribute '")
           << name << "' belongs on a module";
  if (name == SampledRanges::getNameStr() &&
      cast<DenseI64ArrayAttr>(attribute.getValue()).size() % 2 != 0)
    return operation->emitOpError("attribute '")
           << name << "' must hold offset/width pairs";
  return success();
}

static_assert(static_cast<uint32_t>(Radix::Binary) == TALOS_RT_RADIX_BINARY);
static_assert(static_cast<uint32_t>(Radix::Octal) == TALOS_RT_RADIX_OCTAL);
static_assert(static_cast<uint32_t>(Radix::Decimal) ==
              TALOS_RT_RADIX_DECIMAL);
static_assert(static_cast<uint32_t>(Radix::Hex) == TALOS_RT_RADIX_HEX);

static_assert(static_cast<uint32_t>(SeekOrigin::Set) == TALOS_RT_SEEK_SET);
static_assert(static_cast<uint32_t>(SeekOrigin::Current) ==
              TALOS_RT_SEEK_CUR);
static_assert(static_cast<uint32_t>(SeekOrigin::End) == TALOS_RT_SEEK_END);
static_assert(static_cast<uint32_t>(CodeKind::Native) ==
              TALOS_RT_FRAGMENT_NATIVE);
static_assert(static_cast<uint32_t>(CodeKind::Bytecode) ==
              TALOS_RT_FRAGMENT_BYTECODE);

static_assert(static_cast<uint32_t>(BytecodeValueKind::None) ==
              TALOS_RT_BC_TYPE_NONE);
static_assert(static_cast<uint32_t>(BytecodeValueKind::U64) ==
              TALOS_RT_BC_TYPE_U64);
static_assert(static_cast<uint32_t>(BytecodeValueKind::I64) ==
              TALOS_RT_BC_TYPE_I64);
static_assert(static_cast<uint32_t>(BytecodeValueKind::Bool) ==
              TALOS_RT_BC_TYPE_BOOL);
static_assert(static_cast<uint32_t>(BytecodeValueKind::Status) ==
              TALOS_RT_BC_TYPE_STATUS);
static_assert(static_cast<uint32_t>(BytecodeValueKind::Resource) ==
              TALOS_RT_BC_TYPE_RESOURCE);
static_assert(static_cast<uint32_t>(BytecodeOpcode::Nop) == TALOS_RT_BC_NOP);
static_assert(static_cast<uint32_t>(BytecodeOpcode::Const) ==
              TALOS_RT_BC_CONST);
static_assert(static_cast<uint32_t>(BytecodeOpcode::Move) ==
              TALOS_RT_BC_MOVE);
static_assert(static_cast<uint32_t>(BytecodeOpcode::Add) == TALOS_RT_BC_ADD);
static_assert(static_cast<uint32_t>(BytecodeOpcode::Sub) == TALOS_RT_BC_SUB);
static_assert(static_cast<uint32_t>(BytecodeOpcode::Mul) == TALOS_RT_BC_MUL);
static_assert(static_cast<uint32_t>(BytecodeOpcode::And) == TALOS_RT_BC_AND);
static_assert(static_cast<uint32_t>(BytecodeOpcode::Or) == TALOS_RT_BC_OR);
static_assert(static_cast<uint32_t>(BytecodeOpcode::Xor) == TALOS_RT_BC_XOR);
static_assert(static_cast<uint32_t>(BytecodeOpcode::Not) == TALOS_RT_BC_NOT);
static_assert(static_cast<uint32_t>(BytecodeOpcode::Eq) == TALOS_RT_BC_EQ);
static_assert(static_cast<uint32_t>(BytecodeOpcode::Ult) == TALOS_RT_BC_ULT);
static_assert(static_cast<uint32_t>(BytecodeOpcode::Slt) == TALOS_RT_BC_SLT);
static_assert(static_cast<uint32_t>(BytecodeOpcode::LoadFrame) ==
              TALOS_RT_BC_LOAD_FRAME);
static_assert(static_cast<uint32_t>(BytecodeOpcode::StoreFrame) ==
              TALOS_RT_BC_STORE_FRAME);
static_assert(static_cast<uint32_t>(BytecodeOpcode::Jump) ==
              TALOS_RT_BC_JUMP);
static_assert(static_cast<uint32_t>(BytecodeOpcode::BranchZero) ==
              TALOS_RT_BC_BRANCH_ZERO);
static_assert(static_cast<uint32_t>(BytecodeOpcode::Continue) ==
              TALOS_RT_BC_CONTINUE);
static_assert(static_cast<uint32_t>(BytecodeOpcode::Suspend) ==
              TALOS_RT_BC_SUSPEND);
static_assert(static_cast<uint32_t>(BytecodeOpcode::Terminate) ==
              TALOS_RT_BC_TERMINATE);
static_assert(static_cast<uint32_t>(BytecodeOpcode::CallService) ==
              TALOS_RT_BC_CALL_SERVICE);
static_assert(static_cast<uint32_t>(BytecodeOpcode::Fail) ==
              TALOS_RT_BC_FAIL);

#define RT_ENUM_EQ(Enum, Case, Value)                                          \
  static_assert(static_cast<uint32_t>(Enum::Case) == Value)
RT_ENUM_EQ(BytecodeOperandKind, Immediate, TALOS_RT_BC_OPERAND_IMMEDIATE);
RT_ENUM_EQ(BytecodeOperandKind, Register, TALOS_RT_BC_OPERAND_REGISTER);
RT_ENUM_EQ(BytecodeOperandKind, Frame, TALOS_RT_BC_OPERAND_FRAME);
RT_ENUM_EQ(BytecodeOperandKind, Constant, TALOS_RT_BC_OPERAND_CONSTANT);
RT_ENUM_EQ(BytecodeOperandKind, Resource, TALOS_RT_BC_OPERAND_RESOURCE);
RT_ENUM_EQ(BytecodeOperandDirection, Input, TALOS_RT_BC_OPERAND_INPUT);
RT_ENUM_EQ(BytecodeOperandDirection, Output, TALOS_RT_BC_OPERAND_OUTPUT);
RT_ENUM_EQ(BytecodeOperandDirection, InOut, TALOS_RT_BC_OPERAND_INOUT);

RT_ENUM_EQ(BytecodeServiceValueKind, None, TALOS_RT_BC_VALUE_NONE);
RT_ENUM_EQ(BytecodeServiceValueKind, U8, TALOS_RT_BC_VALUE_U8);
RT_ENUM_EQ(BytecodeServiceValueKind, U32, TALOS_RT_BC_VALUE_U32);
RT_ENUM_EQ(BytecodeServiceValueKind, I32, TALOS_RT_BC_VALUE_I32);
RT_ENUM_EQ(BytecodeServiceValueKind, U64, TALOS_RT_BC_VALUE_U64);
RT_ENUM_EQ(BytecodeServiceValueKind, I64, TALOS_RT_BC_VALUE_I64);
RT_ENUM_EQ(BytecodeServiceValueKind, Bytes, TALOS_RT_BC_VALUE_BYTES);
RT_ENUM_EQ(BytecodeServiceValueKind, MutableBytes,
           TALOS_RT_BC_VALUE_MUTABLE_BYTES);
RT_ENUM_EQ(BytecodeServiceValueKind, ArgumentArray,
           TALOS_RT_BC_VALUE_ARGUMENT_ARRAY);
RT_ENUM_EQ(BytecodeServiceValueKind, FormatEnvironment,
           TALOS_RT_BC_VALUE_FORMAT_ENVIRONMENT);
RT_ENUM_EQ(BytecodeServiceValueKind, Buffer, TALOS_RT_BC_VALUE_BUFFER);
RT_ENUM_EQ(BytecodeServiceValueKind, ArgumentEmpty,
           TALOS_RT_BC_VALUE_ARGUMENT_EMPTY);
RT_ENUM_EQ(BytecodeServiceValueKind, ArgumentLogic,
           TALOS_RT_BC_VALUE_ARGUMENT_LOGIC);
RT_ENUM_EQ(BytecodeServiceValueKind, ArgumentString,
           TALOS_RT_BC_VALUE_ARGUMENT_STRING);
RT_ENUM_EQ(BytecodeServiceValueKind, ArgumentReal,
           TALOS_RT_BC_VALUE_ARGUMENT_REAL);
RT_ENUM_EQ(BytecodeServiceValueKind, ArgumentTime,
           TALOS_RT_BC_VALUE_ARGUMENT_TIME);

RT_ENUM_EQ(BytecodeService, Format, TALOS_RT_BC_SERVICE_FORMAT);
RT_ENUM_EQ(BytecodeService, Display, TALOS_RT_BC_SERVICE_DISPLAY);
RT_ENUM_EQ(BytecodeService, BufferRelease,
           TALOS_RT_BC_SERVICE_BUFFER_RELEASE);
RT_ENUM_EQ(BytecodeService, FileOpenMCD, TALOS_RT_BC_SERVICE_FILE_OPEN_MCD);
RT_ENUM_EQ(BytecodeService, FileOpen, TALOS_RT_BC_SERVICE_FILE_OPEN);
RT_ENUM_EQ(BytecodeService, FileClose, TALOS_RT_BC_SERVICE_FILE_CLOSE);
RT_ENUM_EQ(BytecodeService, FileFlush, TALOS_RT_BC_SERVICE_FILE_FLUSH);
RT_ENUM_EQ(BytecodeService, FileWrite, TALOS_RT_BC_SERVICE_FILE_WRITE);
RT_ENUM_EQ(BytecodeService, FileRead, TALOS_RT_BC_SERVICE_FILE_READ);
RT_ENUM_EQ(BytecodeService, FileGetc, TALOS_RT_BC_SERVICE_FILE_GETC);
RT_ENUM_EQ(BytecodeService, FileUngetc, TALOS_RT_BC_SERVICE_FILE_UNGETC);
RT_ENUM_EQ(BytecodeService, FileGetline, TALOS_RT_BC_SERVICE_FILE_GETLINE);
RT_ENUM_EQ(BytecodeService, FileEof, TALOS_RT_BC_SERVICE_FILE_EOF);
RT_ENUM_EQ(BytecodeService, FileError, TALOS_RT_BC_SERVICE_FILE_ERROR);
RT_ENUM_EQ(BytecodeService, FileSeek, TALOS_RT_BC_SERVICE_FILE_SEEK);
RT_ENUM_EQ(BytecodeService, FileTell, TALOS_RT_BC_SERVICE_FILE_TELL);
RT_ENUM_EQ(BytecodeService, FileRewind, TALOS_RT_BC_SERVICE_FILE_REWIND);
RT_ENUM_EQ(StatusCode, Ok, TALOS_RT_OK);
RT_ENUM_EQ(StatusCode, Eof, TALOS_RT_EOF);
RT_ENUM_EQ(StatusCode, InvalidArgument, TALOS_RT_INVALID_ARGUMENT);
RT_ENUM_EQ(StatusCode, InvalidHandle, TALOS_RT_INVALID_HANDLE);
RT_ENUM_EQ(StatusCode, IoError, TALOS_RT_IO_ERROR);
RT_ENUM_EQ(StatusCode, OutOfMemory, TALOS_RT_OUT_OF_MEMORY);
RT_ENUM_EQ(StatusCode, OutOfResources, TALOS_RT_OUT_OF_RESOURCES);
RT_ENUM_EQ(StatusCode, FormatError, TALOS_RT_FORMAT_ERROR);
RT_ENUM_EQ(StatusCode, ArgumentMismatch, TALOS_RT_ARGUMENT_MISMATCH);
RT_ENUM_EQ(StatusCode, InvalidBytecode, TALOS_RT_INVALID_BYTECODE);
RT_ENUM_EQ(StatusCode, StepLimit, TALOS_RT_STEP_LIMIT);
RT_ENUM_EQ(StatusCode, LayoutMismatch, TALOS_RT_LAYOUT_MISMATCH);
RT_ENUM_EQ(StatusCode, InvalidContinuation, TALOS_RT_INVALID_CONTINUATION);
RT_ENUM_EQ(StatusCode, TierUnavailable, TALOS_RT_TIER_UNAVAILABLE);
RT_ENUM_EQ(StatusCode, InvalidLifecycle, TALOS_RT_INVALID_LIFECYCLE);
RT_ENUM_EQ(StatusCode, InvalidFrame, TALOS_RT_INVALID_FRAME);
RT_ENUM_EQ(StatusCode, InvalidDesign, TALOS_RT_INVALID_DESIGN);
RT_ENUM_EQ(StatusCode, PermissionDenied, TALOS_RT_PERMISSION_DENIED);
RT_ENUM_EQ(StatusCode, DpiDisableUnsupported,
           TALOS_RT_DPI_DISABLE_UNSUPPORTED);
RT_ENUM_EQ(StatusCode, Fatal, TALOS_RT_FATAL);
RT_ENUM_EQ(StatusCode, AotCheckpoint, TALOS_RT_AOT_CHECKPOINT);
RT_ENUM_EQ(StatusCode, AotTimedCheckpoint, TALOS_RT_AOT_TIMED_CHECKPOINT);
RT_ENUM_EQ(StatusCode, AotGeneratedCheckpoint,
           TALOS_RT_AOT_GENERATED_CHECKPOINT);
RT_ENUM_EQ(ArgumentKind, Empty, TALOS_RT_ARG_EMPTY);
RT_ENUM_EQ(ArgumentKind, Logic, TALOS_RT_ARG_LOGIC);
RT_ENUM_EQ(ArgumentKind, String, TALOS_RT_ARG_STRING);
RT_ENUM_EQ(ArgumentKind, Real, TALOS_RT_ARG_REAL);
RT_ENUM_EQ(ArgumentKind, Time, TALOS_RT_ARG_TIME);
RT_ENUM_EQ(ArgumentKind, ManagedString, TALOS_RT_ARG_MANAGED_STRING);
RT_ENUM_EQ(ArgumentKind, ManagedContainer, TALOS_RT_ARG_MANAGED_CONTAINER);
RT_ENUM_EQ(ArgumentKind, ManagedObject, TALOS_RT_ARG_MANAGED_OBJECT);
RT_ENUM_EQ(ArgumentKind, VirtualInterface, TALOS_RT_ARG_VIRTUAL_INTERFACE);
RT_ENUM_EQ(ArgumentKind, Process, TALOS_RT_ARG_PROCESS);
RT_ENUM_EQ(ArgumentKind, Enum, TALOS_RT_ARG_ENUM);
RT_ENUM_EQ(ArgumentKind, Net, TALOS_RT_ARG_NET);
RT_ENUM_EQ(ArgumentKind, RawAggregate, TALOS_RT_ARG_RAW_AGGREGATE);
RT_ENUM_EQ(ArgumentFlags, Signed, TALOS_RT_ARG_SIGNED);
RT_ENUM_EQ(ArgumentFlags, FormatString, TALOS_RT_ARG_FORMAT_STRING);
RT_ENUM_EQ(ArgumentFlags, DesignatedFormat, TALOS_RT_ARG_DESIGNATED_FORMAT);
RT_ENUM_EQ(ArgumentFlags, RealTime, TALOS_RT_ARG_REAL_TIME);
#undef RT_ENUM_EQ

void TalosRuntimeDialect::initialize() {
  addAttributes<
#define GET_ATTRDEF_LIST
#include "talos/Dialect/Runtime/RuntimeAttrs.cpp.inc"
      >();
  addTypes<
#define GET_TYPEDEF_LIST
#include "talos/Dialect/Runtime/RuntimeTypes.cpp.inc"
      >();
  addOperations<
#define GET_OP_LIST
#include "talos/Dialect/Runtime/RuntimeOps.cpp.inc"
      >();
}

static LogicalResult verifyOwnedBuffer(Operation *producer, Value buffer) {
  unsigned releases = 0;
  unsigned sizes = 0;
  unsigned packedReads = 0;
  Operation *release = nullptr;
  for (Operation *consumer : buffer.getUsers()) {
    if (isa<RTBufferReleaseOp>(consumer)) {
      ++releases;
      release = consumer;
    } else if (isa<RTBytesSizeOp>(consumer))
      ++sizes;
    else if (isa<RTPackedFromBytesOp>(consumer))
      ++packedReads;
    else
      return producer->emitOpError()
             << "owned buffer has an unsupported consumer "
             << consumer->getName();
    if (consumer->getBlock() != producer->getBlock())
      return producer->emitOpError()
             << "owned buffer consumers must remain in the producer's block";
  }
  if (releases != 1 || sizes > 1 || packedReads > 1)
    return producer->emitOpError()
           << "owned buffer requires one release and at most one size and one "
              "packed read";
  for (Operation *consumer : buffer.getUsers())
    if (consumer != release && !consumer->isBeforeInBlock(release))
      return producer->emitOpError()
             << "owned buffer size and packed reads must precede its release";
  return success();
}

LogicalResult RTBufferReleaseOp::verify() {
  Operation *producer = getBuffer().getDefiningOp();
  if (!producer ||
      !isa<RTLastErrorOp, RTFormatOp, RTFileGetlineOp, RTFileErrorOp>(producer))
    return emitOpError()
           << "requires a buffer produced directly by an owned-buffer "
              "runtime operation";
  if (producer->getBlock() != getOperation()->getBlock())
    return emitOpError()
           << "must consume its owned buffer in the producer's block";
  return verifyOwnedBuffer(producer, getBuffer());
}

static bool isByteContainer(Type type) {
  return isa<ByteSpanType, MutableByteSpanType, BufferType>(type);
}

template <typename... AllowedConsumers>
static LogicalResult verifyConsumers(Operation *producer, Value value,
                                     StringRef description) {
  for (Operation *consumer : value.getUsers()) {
    if (!isa<AllowedConsumers...>(consumer))
      return producer->emitOpError()
             << description << " has an unsupported consumer "
             << consumer->getName();
  }
  return success();
}

template <typename... AllowedConsumers>
static LogicalResult verifyLocalConsumers(Operation *producer, Value value,
                                          StringRef description) {
  for (Operation *consumer : value.getUsers())
    if (consumer->getBlock() != producer->getBlock())
      return producer->emitOpError()
             << description << " consumers must remain in the producer's block";
  return verifyConsumers<AllowedConsumers...>(producer, value, description);
}

LogicalResult RTScratchOp::verify() {
  if (getSizeAttr().getValue().isNegative())
    return emitOpError("scratch byte count must be nonnegative");
  return verifyLocalConsumers<RTFileReadOp, RTFileReadMemTokenOp,
                              RTPackedFromBytesOp>(*this, getResult(),
                                                   "stack-backed scratch span");
}

LogicalResult RTBytesSizeOp::verify() {
  if (!isByteContainer(getBytes().getType()))
    return emitOpError("requires a byte span, mutable byte span, or buffer");
  return success();
}

LogicalResult RTPackedFromBytesOp::verify() {
  if (!isByteContainer(getBytes().getType()))
    return emitOpError("requires a byte span, mutable byte span, or buffer");
  if (getHighAlignment() && getLeastSignificantByteFirst())
    return emitOpError("cannot combine high alignment with least-significant-"
                       "byte-first ordering");
  return success();
}

LogicalResult RTArgumentEmptyOp::verify() {
  return verifyConsumers<RTArgumentArrayOp>(*this, getResult(),
                                            "format argument");
}

LogicalResult RTArgumentPackedOp::verify() {
  if (Value unknown = getUnknown())
    if (unknown.getType() != getValue().getType())
      return emitOpError("unknown plane must match the value plane type");
  return verifyLocalConsumers<RTArgumentArrayOp>(
      *this, getResult(), "stack-backed packed format argument");
}

LogicalResult RTArgumentEnumOp::verify() {
  if (Value unknown = getUnknown())
    if (unknown.getType() != getValue().getType())
      return emitOpError("unknown plane must match the value plane type");
  return verifyLocalConsumers<RTArgumentArrayOp>(
      *this, getResult(), "stack-backed enum format argument");
}

LogicalResult RTArgumentNetOp::verify() {
  if (Value unknown = getUnknown())
    if (unknown.getType() != getValue().getType())
      return emitOpError("unknown plane must match the value plane type");
  return verifyLocalConsumers<RTArgumentArrayOp>(
      *this, getResult(), "stack-backed net format argument");
}

LogicalResult RTArgumentRawAggregateOp::verify() {
  return verifyLocalConsumers<RTArgumentArrayOp>(
      *this, getResult(), "stack-backed raw aggregate format argument");
}

LogicalResult RTArgumentRealOp::verify() {
  return verifyLocalConsumers<RTArgumentArrayOp>(
      *this, getResult(), "stack-backed real format argument");
}

LogicalResult RTArgumentBytesOp::verify() {
  if (getDesignatedFormat() && !getIsFormatString())
    return emitOpError("designated format must also be a format string");
  return verifyConsumers<RTArgumentArrayOp>(*this, getResult(),
                                            "format argument");
}

LogicalResult RTArgumentManagedStringOp::verify() {
  if (getDesignatedFormat() && !getIsFormatString())
    return emitOpError("designated format must also be a format string");
  return verifyLocalConsumers<RTArgumentArrayOp>(
      *this, getResult(), "stack-backed managed string format argument");
}

LogicalResult RTArgumentArrayOp::verify() {
  return verifyLocalConsumers<RTFormatOp, RTStringOutputFormatOp, RTDisplayOp>(
      *this, getResult(), "stack-backed format argument array");
}

LogicalResult RTFormatEnvironmentOp::verify() {
  if (!getTimeMultiplierAttr().getValue().isStrictlyPositive())
    return emitOpError("time multiplier must be positive");
  return verifyLocalConsumers<RTFormatOp, RTStringOutputFormatOp, RTDisplayOp>(
      *this, getResult(), "stack-backed format environment");
}

LogicalResult RTLastErrorOp::verify() {
  return verifyOwnedBuffer(*this, getMessage());
}

LogicalResult RTFormatOp::verify() {
  return verifyOwnedBuffer(*this, getBuffer());
}

LogicalResult RTFileGetlineOp::verify() {
  return verifyOwnedBuffer(*this, getLine());
}

LogicalResult RTFileErrorOp::verify() {
  return verifyOwnedBuffer(*this, getMessage());
}

} // namespace talos::runtime
