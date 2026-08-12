//===- RuntimeDialect.cpp - Typed runtime ABI dialect --------------------===//

#include "obelisk/Dialect/Runtime/RuntimeABI.h"
#include "obelisk/Dialect/Runtime/RuntimeOps.h"

#include "obelisk/Runtime/Runtime.h"

#include "mlir/IR/Diagnostics.h"
#include "llvm/ADT/TypeSwitch.h"
#include "llvm/Support/ErrorHandling.h"

using namespace mlir;

#include "obelisk/Dialect/Runtime/RuntimeDialect.cpp.inc"
#include "obelisk/Dialect/Runtime/RuntimeEnums.cpp.inc"

#define GET_TYPEDEF_CLASSES
#include "obelisk/Dialect/Runtime/RuntimeTypes.cpp.inc"

#define GET_OP_CLASSES
#include "obelisk/Dialect/Runtime/RuntimeOps.cpp.inc"

namespace obelisk::runtime {

Operation *ObeliskRuntimeDialect::materializeConstant(OpBuilder &builder,
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
#define OBELISK_RUNTIME_CALL(Name, Op, Symbol, Signature)                      \
  case RuntimeCall::Name:                                                      \
    return #Symbol;
#include "obelisk/Dialect/Runtime/RuntimeABI.def"
#undef OBELISK_RUNTIME_CALL
  }
  llvm_unreachable("unknown runtime call");
}

RuntimeSignature getRuntimeSignature(RuntimeCall call) {
  switch (call) {
#define OBELISK_RUNTIME_CALL(Name, Op, Symbol, Signature)                      \
  case RuntimeCall::Name:                                                      \
    return RuntimeSignature::Signature;
#include "obelisk/Dialect/Runtime/RuntimeABI.def"
#undef OBELISK_RUNTIME_CALL
  }
  llvm_unreachable("unknown runtime call");
}

std::optional<RuntimeCall> getRuntimeCall(Operation *operation) {
#define OBELISK_RUNTIME_CALL(Name, Op, Symbol, Signature)                      \
  if (isa<Op>(operation))                                                      \
    return RuntimeCall::Name;
#include "obelisk/Dialect/Runtime/RuntimeABI.def"
#undef OBELISK_RUNTIME_CALL
  return std::nullopt;
}

static_assert(static_cast<uint32_t>(DescriptorKind::Invalid) ==
              OBELISK_RT_DESCRIPTOR_INVALID);
static_assert(static_cast<uint32_t>(DescriptorKind::Scope) ==
              OBELISK_RT_DESCRIPTOR_SCOPE);
static_assert(static_cast<uint32_t>(DescriptorKind::Storage) ==
              OBELISK_RT_DESCRIPTOR_STORAGE);
static_assert(static_cast<uint32_t>(DescriptorKind::Net) ==
              OBELISK_RT_DESCRIPTOR_NET);
static_assert(static_cast<uint32_t>(DescriptorKind::Driver) ==
              OBELISK_RT_DESCRIPTOR_DRIVER);
static_assert(static_cast<uint32_t>(DescriptorKind::Event) ==
              OBELISK_RT_DESCRIPTOR_EVENT);
static_assert(static_cast<uint32_t>(DescriptorKind::Process) ==
              OBELISK_RT_DESCRIPTOR_PROCESS);
static_assert(static_cast<uint32_t>(DescriptorKind::Fragment) ==
              OBELISK_RT_DESCRIPTOR_FRAGMENT);
static_assert(static_cast<uint32_t>(DescriptorKind::Function) ==
              OBELISK_RT_DESCRIPTOR_FUNCTION);

static_assert(static_cast<uint32_t>(ActionKind::Continue) ==
              OBELISK_RT_FRAGMENT_CONTINUE);
static_assert(static_cast<uint32_t>(ActionKind::Suspend) ==
              OBELISK_RT_FRAGMENT_SUSPEND);
static_assert(static_cast<uint32_t>(ActionKind::Terminate) ==
              OBELISK_RT_FRAGMENT_TERMINATE);

static_assert(static_cast<uint32_t>(SuspensionKind::None) ==
              OBELISK_RT_SUSPEND_NONE);
static_assert(static_cast<uint32_t>(SuspensionKind::Delay) ==
              OBELISK_RT_SUSPEND_DELAY);
static_assert(static_cast<uint32_t>(SuspensionKind::Change) ==
              OBELISK_RT_SUSPEND_CHANGE);
static_assert(static_cast<uint32_t>(SuspensionKind::Edge) ==
              OBELISK_RT_SUSPEND_EDGE);
static_assert(static_cast<uint32_t>(SuspensionKind::Event) ==
              OBELISK_RT_SUSPEND_EVENT);
static_assert(static_cast<uint32_t>(SuspensionKind::Await) ==
              OBELISK_RT_SUSPEND_AWAIT);
static_assert(static_cast<uint32_t>(SuspensionKind::Join) ==
              OBELISK_RT_SUSPEND_JOIN);
static_assert(static_cast<uint32_t>(SuspensionKind::Forever) ==
              OBELISK_RT_SUSPEND_FOREVER);
static_assert(static_cast<uint32_t>(SuspensionKind::Frontier) ==
              OBELISK_RT_SUSPEND_FRONTIER);

static_assert(static_cast<uint32_t>(Radix::Binary) == OBELISK_RT_RADIX_BINARY);
static_assert(static_cast<uint32_t>(Radix::Octal) == OBELISK_RT_RADIX_OCTAL);
static_assert(static_cast<uint32_t>(Radix::Decimal) ==
              OBELISK_RT_RADIX_DECIMAL);
static_assert(static_cast<uint32_t>(Radix::Hex) == OBELISK_RT_RADIX_HEX);

static_assert(static_cast<uint32_t>(SeekOrigin::Set) == OBELISK_RT_SEEK_SET);
static_assert(static_cast<uint32_t>(SeekOrigin::Current) ==
              OBELISK_RT_SEEK_CUR);
static_assert(static_cast<uint32_t>(SeekOrigin::End) == OBELISK_RT_SEEK_END);
static_assert(static_cast<uint32_t>(CodeKind::Native) ==
              OBELISK_RT_FRAGMENT_NATIVE);
static_assert(static_cast<uint32_t>(CodeKind::Bytecode) ==
              OBELISK_RT_FRAGMENT_BYTECODE);

static_assert(static_cast<uint32_t>(BytecodeValueKind::None) ==
              OBELISK_RT_BC_TYPE_NONE);
static_assert(static_cast<uint32_t>(BytecodeValueKind::U64) ==
              OBELISK_RT_BC_TYPE_U64);
static_assert(static_cast<uint32_t>(BytecodeValueKind::I64) ==
              OBELISK_RT_BC_TYPE_I64);
static_assert(static_cast<uint32_t>(BytecodeValueKind::Bool) ==
              OBELISK_RT_BC_TYPE_BOOL);
static_assert(static_cast<uint32_t>(BytecodeValueKind::Status) ==
              OBELISK_RT_BC_TYPE_STATUS);
static_assert(static_cast<uint32_t>(BytecodeValueKind::Resource) ==
              OBELISK_RT_BC_TYPE_RESOURCE);
static_assert(static_cast<uint32_t>(BytecodeOpcode::Nop) == OBELISK_RT_BC_NOP);
static_assert(static_cast<uint32_t>(BytecodeOpcode::Const) ==
              OBELISK_RT_BC_CONST);
static_assert(static_cast<uint32_t>(BytecodeOpcode::Move) ==
              OBELISK_RT_BC_MOVE);
static_assert(static_cast<uint32_t>(BytecodeOpcode::Add) == OBELISK_RT_BC_ADD);
static_assert(static_cast<uint32_t>(BytecodeOpcode::Sub) == OBELISK_RT_BC_SUB);
static_assert(static_cast<uint32_t>(BytecodeOpcode::Mul) == OBELISK_RT_BC_MUL);
static_assert(static_cast<uint32_t>(BytecodeOpcode::And) == OBELISK_RT_BC_AND);
static_assert(static_cast<uint32_t>(BytecodeOpcode::Or) == OBELISK_RT_BC_OR);
static_assert(static_cast<uint32_t>(BytecodeOpcode::Xor) == OBELISK_RT_BC_XOR);
static_assert(static_cast<uint32_t>(BytecodeOpcode::Not) == OBELISK_RT_BC_NOT);
static_assert(static_cast<uint32_t>(BytecodeOpcode::Eq) == OBELISK_RT_BC_EQ);
static_assert(static_cast<uint32_t>(BytecodeOpcode::Ult) == OBELISK_RT_BC_ULT);
static_assert(static_cast<uint32_t>(BytecodeOpcode::Slt) == OBELISK_RT_BC_SLT);
static_assert(static_cast<uint32_t>(BytecodeOpcode::LoadFrame) ==
              OBELISK_RT_BC_LOAD_FRAME);
static_assert(static_cast<uint32_t>(BytecodeOpcode::StoreFrame) ==
              OBELISK_RT_BC_STORE_FRAME);
static_assert(static_cast<uint32_t>(BytecodeOpcode::Jump) ==
              OBELISK_RT_BC_JUMP);
static_assert(static_cast<uint32_t>(BytecodeOpcode::BranchZero) ==
              OBELISK_RT_BC_BRANCH_ZERO);
static_assert(static_cast<uint32_t>(BytecodeOpcode::Continue) ==
              OBELISK_RT_BC_CONTINUE);
static_assert(static_cast<uint32_t>(BytecodeOpcode::Suspend) ==
              OBELISK_RT_BC_SUSPEND);
static_assert(static_cast<uint32_t>(BytecodeOpcode::Terminate) ==
              OBELISK_RT_BC_TERMINATE);
static_assert(static_cast<uint32_t>(BytecodeOpcode::CallService) ==
              OBELISK_RT_BC_CALL_SERVICE);
static_assert(static_cast<uint32_t>(BytecodeOpcode::Fail) ==
              OBELISK_RT_BC_FAIL);

#define RT_ENUM_EQ(Enum, Case, Value)                                          \
  static_assert(static_cast<uint32_t>(Enum::Case) == Value)
RT_ENUM_EQ(BytecodeOperandKind, Immediate, OBELISK_RT_BC_OPERAND_IMMEDIATE);
RT_ENUM_EQ(BytecodeOperandKind, Register, OBELISK_RT_BC_OPERAND_REGISTER);
RT_ENUM_EQ(BytecodeOperandKind, Frame, OBELISK_RT_BC_OPERAND_FRAME);
RT_ENUM_EQ(BytecodeOperandKind, Constant, OBELISK_RT_BC_OPERAND_CONSTANT);
RT_ENUM_EQ(BytecodeOperandKind, Resource, OBELISK_RT_BC_OPERAND_RESOURCE);
RT_ENUM_EQ(BytecodeOperandDirection, Input, OBELISK_RT_BC_OPERAND_INPUT);
RT_ENUM_EQ(BytecodeOperandDirection, Output, OBELISK_RT_BC_OPERAND_OUTPUT);
RT_ENUM_EQ(BytecodeOperandDirection, InOut, OBELISK_RT_BC_OPERAND_INOUT);

RT_ENUM_EQ(BytecodeServiceValueKind, None, OBELISK_RT_BC_VALUE_NONE);
RT_ENUM_EQ(BytecodeServiceValueKind, U8, OBELISK_RT_BC_VALUE_U8);
RT_ENUM_EQ(BytecodeServiceValueKind, U32, OBELISK_RT_BC_VALUE_U32);
RT_ENUM_EQ(BytecodeServiceValueKind, I32, OBELISK_RT_BC_VALUE_I32);
RT_ENUM_EQ(BytecodeServiceValueKind, U64, OBELISK_RT_BC_VALUE_U64);
RT_ENUM_EQ(BytecodeServiceValueKind, I64, OBELISK_RT_BC_VALUE_I64);
RT_ENUM_EQ(BytecodeServiceValueKind, Bytes, OBELISK_RT_BC_VALUE_BYTES);
RT_ENUM_EQ(BytecodeServiceValueKind, MutableBytes,
           OBELISK_RT_BC_VALUE_MUTABLE_BYTES);
RT_ENUM_EQ(BytecodeServiceValueKind, ArgumentArray,
           OBELISK_RT_BC_VALUE_ARGUMENT_ARRAY);
RT_ENUM_EQ(BytecodeServiceValueKind, FormatEnvironment,
           OBELISK_RT_BC_VALUE_FORMAT_ENVIRONMENT);
RT_ENUM_EQ(BytecodeServiceValueKind, Buffer, OBELISK_RT_BC_VALUE_BUFFER);
RT_ENUM_EQ(BytecodeServiceValueKind, ArgumentEmpty,
           OBELISK_RT_BC_VALUE_ARGUMENT_EMPTY);
RT_ENUM_EQ(BytecodeServiceValueKind, ArgumentLogic,
           OBELISK_RT_BC_VALUE_ARGUMENT_LOGIC);
RT_ENUM_EQ(BytecodeServiceValueKind, ArgumentString,
           OBELISK_RT_BC_VALUE_ARGUMENT_STRING);
RT_ENUM_EQ(BytecodeServiceValueKind, ArgumentReal,
           OBELISK_RT_BC_VALUE_ARGUMENT_REAL);
RT_ENUM_EQ(BytecodeServiceValueKind, ArgumentTime,
           OBELISK_RT_BC_VALUE_ARGUMENT_TIME);

RT_ENUM_EQ(BytecodeService, Format, OBELISK_RT_BC_SERVICE_FORMAT);
RT_ENUM_EQ(BytecodeService, Display, OBELISK_RT_BC_SERVICE_DISPLAY);
RT_ENUM_EQ(BytecodeService, BufferRelease,
           OBELISK_RT_BC_SERVICE_BUFFER_RELEASE);
RT_ENUM_EQ(BytecodeService, FileOpenMCD, OBELISK_RT_BC_SERVICE_FILE_OPEN_MCD);
RT_ENUM_EQ(BytecodeService, FileOpen, OBELISK_RT_BC_SERVICE_FILE_OPEN);
RT_ENUM_EQ(BytecodeService, FileClose, OBELISK_RT_BC_SERVICE_FILE_CLOSE);
RT_ENUM_EQ(BytecodeService, FileFlush, OBELISK_RT_BC_SERVICE_FILE_FLUSH);
RT_ENUM_EQ(BytecodeService, FileWrite, OBELISK_RT_BC_SERVICE_FILE_WRITE);
RT_ENUM_EQ(BytecodeService, FileRead, OBELISK_RT_BC_SERVICE_FILE_READ);
RT_ENUM_EQ(BytecodeService, FileGetc, OBELISK_RT_BC_SERVICE_FILE_GETC);
RT_ENUM_EQ(BytecodeService, FileUngetc, OBELISK_RT_BC_SERVICE_FILE_UNGETC);
RT_ENUM_EQ(BytecodeService, FileGetline, OBELISK_RT_BC_SERVICE_FILE_GETLINE);
RT_ENUM_EQ(BytecodeService, FileEof, OBELISK_RT_BC_SERVICE_FILE_EOF);
RT_ENUM_EQ(BytecodeService, FileError, OBELISK_RT_BC_SERVICE_FILE_ERROR);
RT_ENUM_EQ(BytecodeService, FileSeek, OBELISK_RT_BC_SERVICE_FILE_SEEK);
RT_ENUM_EQ(BytecodeService, FileTell, OBELISK_RT_BC_SERVICE_FILE_TELL);
RT_ENUM_EQ(BytecodeService, FileRewind, OBELISK_RT_BC_SERVICE_FILE_REWIND);
#undef RT_ENUM_EQ

void ObeliskRuntimeDialect::initialize() {
  addTypes<
#define GET_TYPEDEF_LIST
#include "obelisk/Dialect/Runtime/RuntimeTypes.cpp.inc"
      >();
  addOperations<
#define GET_OP_LIST
#include "obelisk/Dialect/Runtime/RuntimeOps.cpp.inc"
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
                              RTPackedFromBytesOp>(
      *this, getResult(), "stack-backed scratch span");
}

LogicalResult RTBytesSizeOp::verify() {
  if (!isByteContainer(getBytes().getType()))
    return emitOpError("requires a byte span, mutable byte span, or buffer");
  return success();
}

LogicalResult RTPackedFromBytesOp::verify() {
  if (!isByteContainer(getBytes().getType()))
    return emitOpError("requires a byte span, mutable byte span, or buffer");
  return success();
}

LogicalResult RTArgumentEmptyOp::verify() {
  return verifyConsumers<RTArgumentArrayOp>(
      *this, getResult(), "format argument");
}

LogicalResult RTArgumentPackedOp::verify() {
  if (Value unknown = getUnknown())
    if (unknown.getType() != getValue().getType())
      return emitOpError("unknown plane must match the value plane type");
  return verifyLocalConsumers<RTArgumentArrayOp>(
      *this, getResult(), "stack-backed packed format argument");
}

LogicalResult RTArgumentRealOp::verify() {
  return verifyLocalConsumers<RTArgumentArrayOp>(
      *this, getResult(), "stack-backed real format argument");
}

LogicalResult RTArgumentBytesOp::verify() {
  return verifyConsumers<RTArgumentArrayOp>(
      *this, getResult(), "format argument");
}

LogicalResult RTArgumentArrayOp::verify() {
  return verifyLocalConsumers<RTFormatOp, RTDisplayOp>(
      *this, getResult(), "stack-backed format argument array");
}

LogicalResult RTFormatEnvironmentOp::verify() {
  if (!getTimeMultiplierAttr().getValue().isStrictlyPositive())
    return emitOpError("time multiplier must be positive");
  return verifyLocalConsumers<RTFormatOp, RTDisplayOp>(
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

} // namespace obelisk::runtime
