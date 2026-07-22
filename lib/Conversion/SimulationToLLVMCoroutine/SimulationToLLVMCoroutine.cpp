//===- SimulationToLLVMCoroutine.cpp - Native process coroutines ---------===//

#include "obelisk/Conversion/SimulationToLLVMCoroutine.h"

#include "obelisk/Conversion/RuntimeToLLVM.h"
#include "obelisk/Conversion/SimulationToRuntime.h"
#include "obelisk/Conversion/SimulationToStandard.h"
#include "obelisk/Dialect/Runtime/RuntimeOps.h"

#include "mlir/Conversion/ArithToLLVM/ArithToLLVM.h"
#include "mlir/Conversion/ControlFlowToLLVM/ControlFlowToLLVM.h"
#include "mlir/Conversion/FuncToLLVM/ConvertFuncToLLVM.h"
#include "mlir/Conversion/LLVMCommon/LoweringOptions.h"
#include "mlir/Conversion/LLVMCommon/TypeConverter.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/Verifier.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/DialectConversion.h"

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/TargetParser/Triple.h"

#include <algorithm>
#include <limits>
#include <set>

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_CONVERTOBELISKSIMPROCESSESTOLLVMCOROUTINESPASS
#include "obelisk/Conversion/Passes.h.inc"

namespace {

constexpr uint64_t kNoOffset = std::numeric_limits<uint64_t>::max();
constexpr uint64_t kWaitHeaderSize = 32;
constexpr uint64_t kWaitEntrySize = 16;
constexpr uint32_t kWaitEdgeNone = std::numeric_limits<uint32_t>::max();

constexpr uint64_t kInstanceAllocationOffset = 8;
constexpr uint64_t kInstanceFrameOffset = 16;
constexpr uint64_t kInstanceScratchOffset = 32;
constexpr uint64_t kInstanceNativeHandleOffset = 48;
constexpr uint64_t kInstanceContinuationOffset = 56;
constexpr uint64_t kInstanceStatusOffset = 68;
constexpr uint64_t kInstanceContextOffset = 72;
constexpr uint64_t kInstanceActionOffset = 80;

bool alignUp(uint64_t value, uint64_t alignment, uint64_t &result) {
  if (value > std::numeric_limits<uint64_t>::max() - (alignment - 1))
    return false;
  result = llvm::alignTo(value, alignment);
  return true;
}

uint64_t appendHash(uint64_t hash, uint64_t value, unsigned bytes) {
  for (unsigned index = 0; index != bytes; ++index) {
    hash ^= static_cast<uint8_t>(value >> (index * 8));
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

bool isSuspension(Operation *operation) {
  return isa<sim::SimSuspendDelayOp, sim::SimSuspendChangeOp,
             sim::SimSuspendEdgeOp, sim::SimSuspendAnyOp,
             sim::SimSuspendEventOp, sim::SimSuspendAwaitOp,
             sim::SimSuspendJoinOp>(operation);
}

uint32_t suspensionKind(Operation *operation) {
  return TypeSwitch<Operation *, uint32_t>(operation)
      .Case<sim::SimSuspendDelayOp>([](auto) { return 1; })
      .Case<sim::SimSuspendChangeOp>([](auto) { return 2; })
      .Case<sim::SimSuspendEdgeOp>([](auto) { return 3; })
      .Case<sim::SimSuspendAnyOp>([](auto) { return 3; })
      .Case<sim::SimSuspendEventOp>([](auto) { return 4; })
      .Case<sim::SimSuspendAwaitOp>([](auto) { return 5; })
      .Case<sim::SimSuspendJoinOp>([](auto) { return 6; })
      .Default([](Operation *) { return 0; });
}

uint32_t waitEntryCount(Operation *operation) {
  return TypeSwitch<Operation *, uint32_t>(operation)
      .Case<sim::SimSuspendChangeOp, sim::SimSuspendEdgeOp,
            sim::SimSuspendEventOp, sim::SimSuspendAwaitOp>(
          [](auto) { return 1; })
      .Case<sim::SimSuspendAnyOp>(
          [](auto op) { return static_cast<uint32_t>(op.getWatched().size()); })
      .Case<sim::SimSuspendJoinOp>([](auto op) {
        return static_cast<uint32_t>(op.getProcesses().size());
      })
      .Default([](Operation *) { return 0; });
}

struct StorageProperties {
  uint64_t size;
  uint32_t alignment;
  bool fourState;
};

LogicalResult validateProcessABI(ModuleOp module,
                                 const llvm::DataLayout &layout) {
  // DataLayout caches StructLayout objects by LLVM type identity. Keep those
  // entries local to the LLVMContext that owns the validation-only types.
  llvm::DataLayout validationLayout(layout.getStringRepresentation());
  llvm::LLVMContext context;
  llvm::Type *pointer = llvm::PointerType::get(context, 0);
  llvm::Type *i32 = llvm::Type::getInt32Ty(context);
  llvm::Type *i64 = llvm::Type::getInt64Ty(context);
  auto checkType = [&](llvm::Type *type, uint64_t size, uint64_t alignment) {
    llvm::TypeSize actualSize = validationLayout.getTypeAllocSize(type);
    return !actualSize.isScalable() && actualSize.getFixedValue() == size &&
           validationLayout.getABITypeAlign(type).value() == alignment;
  };
  auto checkStruct = [&](llvm::ArrayRef<llvm::Type *> elements,
                         llvm::ArrayRef<uint64_t> offsets, uint64_t size,
                         uint64_t alignment) {
    auto *type = llvm::StructType::get(context, elements);
    if (!checkType(type, size, alignment))
      return false;
    const llvm::StructLayout *structLayout =
        validationLayout.getStructLayout(type);
    return llvm::all_of(llvm::enumerate(offsets), [&](auto indexedOffset) {
      return structLayout->getElementOffset(indexedOffset.index()) ==
             indexedOffset.value();
    });
  };
  auto *handle = llvm::StructType::get(context, {i32, i32, i64});
  bool compatible =
      checkType(pointer, 8, 8) && checkType(i32, 4, 4) &&
      checkType(i64, 8, 8) &&
      checkStruct({i32, i32, i32, i32, i64, i64}, {0, 4, 8, 12, 16, 24}, 32,
                  8) &&
      checkStruct({i64, i32, i32}, {0, 8, 12}, 16, 8) &&
      checkStruct({i32, i32, i64, i64, i32, i32}, {0, 4, 8, 16, 24, 28}, 32,
                  8) &&
      checkStruct({i32, i32, i64, i64, pointer, i32, i32, pointer, i64},
                  {0, 4, 8, 16, 24, 32, 36, 40, 48}, 56, 8) &&
      checkStruct({handle, i32, i32, i32, i32, pointer, pointer, pointer,
                   pointer, pointer},
                  {0, 16, 20, 24, 28, 32, 40, 48, 56, 64}, 72, 8) &&
      checkStruct({pointer, pointer, pointer, i64, i64, i64, pointer, i32, i32,
                   i32, i32, pointer, pointer},
                  {0, 8, 16, 24, 32, 40, 48, 56, 60, 64, 68, 72, 80}, 88, 8);
  if (!compatible)
    return module.emitError(
        "LLVM data layout is incompatible with the Obelisk process ABI");
  if (auto tripleAttr =
          module->getAttrOfType<StringAttr>("llvm.target_triple")) {
    llvm::Triple triple(tripleAttr.getValue());
    if (!triple.isArch64Bit() || !triple.isLittleEndian())
      return module.emitError(
          "llvm.target_triple is inconsistent with the Obelisk process ABI");
  }
  return success();
}

FailureOr<StorageProperties> storageProperties(Type type,
                                               const llvm::DataLayout &layout,
                                               llvm::LLVMContext &context) {
  llvm::Type *llvmType = nullptr;
  bool fourState = false;
  if (auto logic = dyn_cast<sim::LogicType>(type)) {
    llvmType = llvm::IntegerType::get(context, logic.getWidth());
    fourState = true;
  } else if (auto integer = dyn_cast<IntegerType>(type)) {
    llvmType = llvm::IntegerType::get(context, integer.getWidth());
  } else if (isa<sim::TimeType>(type)) {
    llvmType = llvm::Type::getInt64Ty(context);
  } else if (isa<sim::RefType, sim::NetType, sim::DriverType, sim::EventType,
                 sim::ProcessType>(type)) {
    // Simulation handles remain frame-relative stable IDs. They must never
    // become host pointers in the canonical frame shared with bytecode.
    llvmType = llvm::Type::getInt64Ty(context);
  } else {
    return failure();
  }
  llvm::TypeSize typeSize = layout.getTypeAllocSize(llvmType);
  if (typeSize.isScalable() || typeSize.getFixedValue() == 0)
    return failure();
  return StorageProperties{
      typeSize.getFixedValue(),
      static_cast<uint32_t>(layout.getABITypeAlign(llvmType).value()),
      fourState};
}

bool containsLogic(Type type) {
  bool result = false;
  type.walk([&](sim::LogicType) { result = true; });
  return result;
}

bool hasNoLogic(Operation *operation) {
  for (Type type : operation->getOperandTypes())
    if (containsLogic(type))
      return false;
  for (Type type : operation->getResultTypes())
    if (containsLogic(type))
      return false;
  for (Region &region : operation->getRegions())
    for (Block &block : region)
      for (BlockArgument argument : block.getArguments())
        if (containsLogic(argument.getType()))
          return false;
  return true;
}

SmallVector<Value> flatten(ArrayRef<ValueRange> ranges) {
  SmallVector<Value> values;
  for (ValueRange range : ranges)
    llvm::append_range(values, range);
  return values;
}

class SimContextRuntimeLowering final : public ConversionPattern {
public:
  SimContextRuntimeLowering(const TypeConverter &converter,
                            MLIRContext *context)
      : ConversionPattern(converter,
                          sim::SimContextRuntimeOp::getOperationName(), 1,
                          context) {}

  LogicalResult
  matchAndRewrite(Operation *operation, ArrayRef<Value> operands,
                  ConversionPatternRewriter &rewriter) const override {
    if (operands.size() != 1)
      return rewriter.notifyMatchFailure(operation,
                                         "context projection must convert 1:1");
    rewriter.replaceOp(operation, operands.front());
    return success();
  }
};

class SimFuncSignatureConversion final
    : public OpConversionPattern<sim::SimFuncOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(sim::SimFuncOp function, OneToNOpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    FunctionType type = function.getFunctionType();
    TypeConverter::SignatureConversion entry(type.getNumInputs());
    SmallVector<Type> results;
    if (failed(getTypeConverter()->convertSignatureArgs(type.getInputs(),
                                                        entry)) ||
        failed(getTypeConverter()->convertTypes(type.getResults(), results)) ||
        (!function.getBody().empty() &&
         failed(rewriter.convertRegionTypes(&function.getBody(),
                                            *getTypeConverter(), &entry))))
      return failure();
    SmallVector<Attribute> argAttrs(entry.getConvertedTypes().size(),
                                    rewriter.getDictionaryAttr({}));
    SmallVector<Attribute> resultAttrs(results.size(),
                                       rewriter.getDictionaryAttr({}));
    rewriter.modifyOpInPlace(function, [&] {
      function.setType(FunctionType::get(rewriter.getContext(),
                                         entry.getConvertedTypes(), results));
      function.setArgAttrsAttr(rewriter.getArrayAttr(argAttrs));
      if (!resultAttrs.empty())
        function.setResAttrsAttr(rewriter.getArrayAttr(resultAttrs));
    });
    return success();
  }
};

class SimReturnTypeConversion final
    : public OpConversionPattern<sim::SimReturnOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimReturnOp operation, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    OperationState state(operation.getLoc(), operation->getName());
    state.addOperands(flatten(adaptor.getOperands()));
    state.addAttributes(operation->getAttrs());
    rewriter.replaceOp(operation, rewriter.create(state));
    return success();
  }
};

template <typename Op>
class SimSuspendTypeConversion final : public OpConversionPattern<Op> {
public:
  using OpConversionPattern<Op>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(Op operation,
                  typename OpConversionPattern<Op>::OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    OperationState state(operation.getLoc(), operation->getName());
    state.addOperands(flatten(adaptor.getOperands()));
    state.addSuccessors(operation->getSuccessors());
    state.addAttributes(operation->getAttrs());
    rewriter.replaceOp(operation, rewriter.create(state));
    return success();
  }
};

class SimCallTypeConversion final : public OpConversionPattern<sim::SimCallOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimCallOp operation, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    SmallVector<Type> results;
    SmallVector<size_t> resultSizes;
    for (Type type : operation.getResultTypes()) {
      size_t start = results.size();
      if (failed(getTypeConverter()->convertType(type, results)))
        return failure();
      resultSizes.push_back(results.size() - start);
    }
    OperationState state(operation.getLoc(), operation->getName());
    state.addOperands(flatten(adaptor.getOperands()));
    state.addTypes(results);
    state.addAttributes(operation->getAttrs());
    Operation *replacement = rewriter.create(state);
    SmallVector<ValueRange> replacements;
    size_t offset = 0;
    for (size_t size : resultSizes) {
      replacements.push_back(replacement->getResults().slice(offset, size));
      offset += size;
    }
    rewriter.replaceOpWithMultiple(operation, replacements);
    return success();
  }
};

Type convertProcessType(Type type, MLIRContext *context) {
  if (isa<sim::ContextType, runtime::ContextType,
          runtime::ProcessDescriptorType, runtime::ProcessInstanceType>(type))
    return LLVM::LLVMPointerType::get(context);
  if (isa<sim::RefType, sim::NetType, sim::DriverType, sim::EventType,
          sim::ProcessType>(type))
    return IntegerType::get(context, 64);
  if (isa<sim::TimeType>(type))
    return IntegerType::get(context, 64);
  return type;
}

Value llvmConstant(OpBuilder &builder, Location location, Type type,
                   uint64_t value) {
  return LLVM::ConstantOp::create(builder, location, type,
                                  builder.getIntegerAttr(type, value));
}

Value byteGEP(OpBuilder &builder, Location location, Value base,
              uint64_t offset) {
  Type pointer = LLVM::LLVMPointerType::get(builder.getContext());
  Type i8 = builder.getI8Type();
  constexpr uint64_t maxConstantIndex =
      (uint64_t{1} << (LLVM::kGEPConstantBitWidth - 1)) - 1;
  SmallVector<LLVM::GEPArg> indices;
  if (offset <= maxConstantIndex)
    indices.emplace_back(static_cast<int32_t>(offset));
  else
    indices.emplace_back(
        llvmConstant(builder, location, builder.getI64Type(), offset));
  return LLVM::GEPOp::create(builder, location, pointer, i8, base, indices);
}

Value loadAt(OpBuilder &builder, Location location, Value base, uint64_t offset,
             Type type, unsigned alignment) {
  return LLVM::LoadOp::create(builder, location, type,
                              byteGEP(builder, location, base, offset),
                              alignment);
}

void storeAt(OpBuilder &builder, Location location, Value base, uint64_t offset,
             Value value, unsigned alignment) {
  LLVM::StoreOp::create(builder, location, value,
                        byteGEP(builder, location, base, offset), alignment);
}

Value asI64(OpBuilder &builder, Location location, Value value) {
  Type i64 = builder.getI64Type();
  if (isa<LLVM::LLVMPointerType>(value.getType()))
    return LLVM::PtrToIntOp::create(builder, location, i64, value);
  auto integer = cast<IntegerType>(value.getType());
  if (integer.getWidth() == 64)
    return value;
  if (integer.getWidth() < 64)
    return arith::ExtUIOp::create(builder, location, i64, value);
  return arith::TruncIOp::create(builder, location, i64, value);
}

void publishAction(OpBuilder &builder, Location location, Value instance,
                   uint32_t actionKind, uint32_t suspendKind,
                   uint32_t continuation, uint32_t flags, Value payload,
                   uint64_t auxiliary) {
  Type pointer = LLVM::LLVMPointerType::get(builder.getContext());
  Type i32 = builder.getI32Type();
  Type i64 = builder.getI64Type();
  Value action =
      loadAt(builder, location, instance, kInstanceActionOffset, pointer, 8);
  storeAt(builder, location, action, 0,
          llvmConstant(builder, location, i32, actionKind), 4);
  storeAt(builder, location, action, 4,
          llvmConstant(builder, location, i32, suspendKind), 4);
  storeAt(builder, location, action, 8,
          llvmConstant(builder, location, i32, continuation), 4);
  storeAt(builder, location, action, 12,
          llvmConstant(builder, location, i32, flags), 4);
  storeAt(builder, location, action, 16, payload, 8);
  storeAt(builder, location, action, 24,
          llvmConstant(builder, location, i64, auxiliary), 8);
}

void addFrameAttributes(LLVM::LLVMFuncOp ramp,
                        const SimulationProcessFrameAnalysis &analysis,
                        OpBuilder &builder) {
  ramp->setAttr("obelisk.frame.size",
                builder.getI64IntegerAttr(analysis.getFrameSize()));
  ramp->setAttr("obelisk.frame.alignment",
                builder.getI64IntegerAttr(analysis.getFrameAlignment()));
  ramp->setAttr("obelisk.frame.checksum",
                builder.getI64IntegerAttr(analysis.getChecksum()));
  SmallVector<int32_t> continuationIDs;
  for (uint32_t continuation : analysis.getContinuations())
    continuationIDs.push_back(static_cast<int32_t>(continuation));
  ramp->setAttr("obelisk.frame.continuations",
                builder.getDenseI32ArrayAttr(continuationIDs));
  SmallVector<Attribute> fields;
  for (const ProcessFrameField &field : analysis.getFields()) {
    NamedAttrList attributes;
    attributes.set(
        "kind", builder.getI32IntegerAttr(static_cast<uint32_t>(field.kind)));
    attributes.set(
        "flags", builder.getI32IntegerAttr(static_cast<uint32_t>(field.flags)));
    attributes.set("offset", builder.getI64IntegerAttr(field.offset));
    attributes.set("size", builder.getI64IntegerAttr(field.size));
    attributes.set("alignment", builder.getI32IntegerAttr(field.alignment));
    fields.push_back(attributes.getDictionary(builder.getContext()));
  }
  ramp->setAttr("obelisk.frame.fields", builder.getArrayAttr(fields));
}

LogicalResult lowerTimeOperations(sim::SimFuncOp function) {
  IRRewriter rewriter(function.getContext());
  SmallVector<Operation *> operations;
  function.walk([&](Operation *operation) {
    if (isa<sim::SimTimeConstantOp, sim::SimTimeAddOp, sim::SimTimeScaleOp>(
            operation))
      operations.push_back(operation);
  });
  for (Operation *operation : operations) {
    rewriter.setInsertionPoint(operation);
    Location location = operation->getLoc();
    if (auto constant = dyn_cast<sim::SimTimeConstantOp>(operation)) {
      Value value = arith::ConstantOp::create(
          rewriter, location, rewriter.getI64Type(),
          rewriter.getI64IntegerAttr(constant.getValue()));
      rewriter.replaceOp(operation, value);
      continue;
    }
    if (auto add = dyn_cast<sim::SimTimeAddOp>(operation)) {
      Value value =
          arith::AddIOp::create(rewriter, location, add.getLhs(), add.getRhs());
      rewriter.replaceOp(operation, value);
      continue;
    }
    auto scale = cast<sim::SimTimeScaleOp>(operation);
    Value input = scale.getInput();
    auto inputType = cast<IntegerType>(input.getType());
    Value extended = input;
    if (inputType.getWidth() < 64) {
      if (scale.getIsSigned())
        extended = arith::ExtSIOp::create(rewriter, location,
                                          rewriter.getI64Type(), input);
      else
        extended = arith::ExtUIOp::create(rewriter, location,
                                          rewriter.getI64Type(), input);
    } else if (inputType.getWidth() > 64)
      extended = arith::TruncIOp::create(rewriter, location,
                                         rewriter.getI64Type(), input);
    Value multiplier =
        arith::ConstantOp::create(rewriter, location, rewriter.getI64Type(),
                                  rewriter.getI64IntegerAttr(scale.getScale()));
    rewriter.replaceOp(operation, arith::MulIOp::create(rewriter, location,
                                                        extended, multiplier));
  }
  return success();
}

struct RampBlocks {
  Block *suspendReturn;
  Block *terminate;
  Block *cleanup;
  DenseMap<Block *, Block *> shims;
};

Block *makeCoroutineReturnBlock(Region &region, Location location,
                                Value handle) {
  Block *block = new Block;
  region.push_back(block);
  OpBuilder builder(block, block->begin());
  Value unwind = llvmConstant(builder, location, builder.getI1Type(), 0);
  Value none = LLVM::NoneTokenOp::create(builder, location);
  LLVM::CoroEndOp::create(builder, location, builder.getI1Type(), handle,
                          unwind, none);
  LLVM::ReturnOp::create(builder, location, ValueRange{});
  return block;
}

LogicalResult storeFrameValue(OpBuilder &builder, Location location,
                              Value frame, Value value, uint64_t offset,
                              uint32_t alignment) {
  if (!isa<IntegerType, LLVM::LLVMPointerType>(value.getType()))
    return failure();
  storeAt(builder, location, frame, offset, value, alignment);
  return success();
}

LogicalResult
lowerSuspendTerminator(Operation *operation, Value instance, Value handle,
                       const SimulationProcessFrameAnalysis &analysis,
                       const RampBlocks &blocks) {
  IRRewriter builder(operation->getContext());
  builder.setInsertionPoint(operation);
  Location location = operation->getLoc();
  auto branch = cast<BranchOpInterface>(operation);
  auto continuationAttr =
      operation->getAttrOfType<IntegerAttr>("obelisk.coro.continuation");
  auto waitOffsetAttr =
      operation->getAttrOfType<IntegerAttr>("obelisk.coro.wait_offset");
  auto waitSizeAttr =
      operation->getAttrOfType<IntegerAttr>("obelisk.coro.wait_size");
  if (!continuationAttr || !waitOffsetAttr || !waitSizeAttr)
    return operation->emitError("missing coroutine frame analysis metadata");
  uint32_t continuationID = continuationAttr.getInt();
  Block *continuation = operation->getSuccessor(0);
  ArrayRef<ProcessFrameValue> layout =
      analysis.getContinuationLayout(continuationID);
  SmallVector<Value> operands(
      branch.getSuccessorOperands(0).getForwardedOperands().begin(),
      branch.getSuccessorOperands(0).getForwardedOperands().end());
  Value frame = loadAt(builder, location, instance, kInstanceFrameOffset,
                       LLVM::LLVMPointerType::get(builder.getContext()), 8);
  size_t physical = 0;
  for (const ProcessFrameValue &slot : layout) {
    if (physical >= operands.size() ||
        failed(storeFrameValue(builder, location, frame, operands[physical++],
                               slot.valueOffset, slot.alignment)))
      return operation->emitError("cannot store continuation value in frame");
    if (slot.isFourState()) {
      if (physical >= operands.size() ||
          failed(storeFrameValue(builder, location, frame, operands[physical++],
                                 slot.unknownOffset, slot.alignment)))
        return operation->emitError(
            "cannot store four-state unknown plane in frame");
    }
  }
  if (physical != operands.size())
    return operation->emitError(
        "converted continuation arity disagrees with frame analysis");

  uint64_t waitOffset = waitOffsetAttr.getInt();
  uint64_t waitSize = waitSizeAttr.getInt();
  uint32_t kind = suspensionKind(operation);
  uint32_t count = waitEntryCount(operation);
  Value wait = byteGEP(builder, location, frame, waitOffset);
  Type i32 = builder.getI32Type();
  Type i64 = builder.getI64Type();
  storeAt(builder, location, wait, 0, llvmConstant(builder, location, i32, 1),
          4);
  storeAt(builder, location, wait, 4,
          llvmConstant(builder, location, i32, kind), 4);
  uint32_t waitFlags = 0;
  if (auto join = dyn_cast<sim::SimSuspendJoinOp>(operation))
    waitFlags = static_cast<uint32_t>(join.getKind());
  storeAt(builder, location, wait, 8,
          llvmConstant(builder, location, i32, waitFlags), 4);
  storeAt(builder, location, wait, 12,
          llvmConstant(builder, location, i32, count), 4);
  Value payload = llvmConstant(builder, location, i64, 0);
  if (auto delay = dyn_cast<sim::SimSuspendDelayOp>(operation))
    payload = asI64(builder, location, delay.getDelay());
  storeAt(builder, location, wait, 16, payload, 8);
  storeAt(builder, location, wait, 24, llvmConstant(builder, location, i64, 0),
          8);

  SmallVector<Value> watched;
  SmallVector<uint32_t> watchedEdges;
  TypeSwitch<Operation *>(operation)
      .Case<sim::SimSuspendChangeOp>([&](auto op) {
        watched.push_back(op.getWatched());
        watchedEdges.push_back(static_cast<uint32_t>(sim::EdgeKind::Change));
      })
      .Case<sim::SimSuspendEdgeOp>([&](auto op) {
        watched.push_back(op.getWatched());
        watchedEdges.push_back(static_cast<uint32_t>(op.getEdge()));
      })
      .Case<sim::SimSuspendAnyOp>([&](auto op) {
        llvm::append_range(watched, op.getWatched());
        for (int32_t edge : op.getEdges())
          watchedEdges.push_back(static_cast<uint32_t>(edge));
      })
      .Case<sim::SimSuspendEventOp>([&](auto op) {
        watched.push_back(op.getEvent());
        watchedEdges.push_back(kWaitEdgeNone);
      })
      .Case<sim::SimSuspendAwaitOp>([&](auto op) {
        watched.push_back(op.getProcess());
        watchedEdges.push_back(kWaitEdgeNone);
      })
      .Case<sim::SimSuspendJoinOp>([&](auto op) {
        llvm::append_range(watched, op.getProcesses());
        watchedEdges.append(op.getProcesses().size(), kWaitEdgeNone);
      });
  if (watched.size() != watchedEdges.size())
    return operation->emitError("wait handle and edge inventories disagree");
  for (auto [index, value] : llvm::enumerate(watched)) {
    uint64_t entryOffset = kWaitHeaderSize + index * kWaitEntrySize;
    storeAt(builder, location, wait, entryOffset,
            asI64(builder, location, value), 8);
    storeAt(builder, location, wait, entryOffset + 8,
            llvmConstant(builder, location, i32, watchedEdges[index]), 4);
    storeAt(builder, location, wait, entryOffset + 12,
            llvmConstant(builder, location, i32, 0), 4);
  }

  storeAt(builder, location, instance, kInstanceContinuationOffset,
          llvmConstant(builder, location, i32, continuationID), 4);
  publishAction(builder, location, instance, 1, kind, continuationID, 1,
                llvmConstant(builder, location, i64, waitOffset), waitSize);

  Value final = llvmConstant(builder, location, builder.getI1Type(), 0);
  Value save = LLVM::CoroSaveOp::create(
      builder, location, LLVM::LLVMTokenType::get(builder.getContext()),
      handle);
  Value state = LLVM::CoroSuspendOp::create(builder, location,
                                            builder.getI8Type(), save, final);
  SmallVector<Block *> destinations{blocks.shims.lookup(continuation),
                                    blocks.cleanup};
  SmallVector<ValueRange> destinationOperands(2);
  SmallVector<APInt> caseValues{APInt(8, 0), APInt(8, 1)};
  LLVM::SwitchOp::create(builder, location, state, blocks.suspendReturn,
                         ValueRange{}, caseValues, destinations,
                         destinationOperands, ArrayRef<int32_t>{});
  builder.eraseOp(operation);
  return success();
}

LogicalResult lowerFinalReturn(sim::SimReturnOp operation,
                               const RampBlocks &blocks) {
  if (!operation.getOperands().empty())
    return operation.emitError("suspendable process cannot return values");
  IRRewriter builder(operation->getContext());
  builder.setInsertionPoint(operation);
  cf::BranchOp::create(builder, operation.getLoc(), blocks.terminate);
  builder.eraseOp(operation);
  return success();
}

LogicalResult makeNativeWrappers(ModuleOp module, LLVM::LLVMFuncOp ramp,
                                 StringRef baseName) {
  OpBuilder builder(ramp);
  builder.setInsertionPointAfter(ramp);
  Location location = ramp.getLoc();
  MLIRContext *context = module.getContext();
  Type pointer = LLVM::LLVMPointerType::get(context);
  Type i32 = builder.getI32Type();
  Type voidType = LLVM::LLVMVoidType::get(context);
  auto makeName = [&](StringRef suffix) { return (baseName + suffix).str(); };

  auto requirements = LLVM::LLVMFuncOp::create(
      builder, location, makeName(".__obelisk_native_requirements"),
      LLVM::LLVMFunctionType::get(i32, {pointer, pointer}, false));
  Block *requirementsEntry = requirements.addEntryBlock(builder);
  builder.setInsertionPointToStart(requirementsEntry);
  Value null = LLVM::ZeroOp::create(builder, location, pointer);
  Value mode = llvmConstant(builder, location, i32, 0);
  auto requirementsCall = LLVM::CallOp::create(
      builder, location, TypeRange{}, SymbolRefAttr::get(ramp),
      ValueRange{null, mode, requirementsEntry->getArgument(0),
                 requirementsEntry->getArgument(1)});
  (void)requirementsCall;
  LLVM::ReturnOp::create(builder, location,
                         llvmConstant(builder, location, i32, 0));

  builder.setInsertionPointAfter(requirements);
  auto execute = LLVM::LLVMFuncOp::create(
      builder, location, makeName(".__obelisk_native_execute"),
      LLVM::LLVMFunctionType::get(i32, {pointer}, false));
  Block *executeEntry = execute.addEntryBlock(builder);
  Block *start = new Block;
  Block *resume = new Block;
  Block *done = new Block;
  execute.getBody().push_back(start);
  execute.getBody().push_back(resume);
  execute.getBody().push_back(done);
  builder.setInsertionPointToStart(executeEntry);
  Value instance = executeEntry->getArgument(0);
  Value handle = loadAt(builder, location, instance,
                        kInstanceNativeHandleOffset, pointer, 8);
  Value bits =
      LLVM::PtrToIntOp::create(builder, location, builder.getI64Type(), handle);
  Value isNull = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::eq, bits,
      llvmConstant(builder, location, builder.getI64Type(), 0));
  cf::CondBranchOp::create(builder, location, isNull, start, ValueRange{},
                           resume, ValueRange{});
  builder.setInsertionPointToStart(start);
  Value modeExecute = llvmConstant(builder, location, i32, 1);
  Value nullOut = LLVM::ZeroOp::create(builder, location, pointer);
  LLVM::CallOp::create(builder, location, TypeRange{}, SymbolRefAttr::get(ramp),
                       ValueRange{instance, modeExecute, nullOut, nullOut});
  cf::BranchOp::create(builder, location, done);
  builder.setInsertionPointToStart(resume);
  LLVM::CoroResumeOp::create(builder, location, handle);
  cf::BranchOp::create(builder, location, done);
  builder.setInsertionPointToStart(done);
  Value status =
      loadAt(builder, location, instance, kInstanceStatusOffset, i32, 4);
  LLVM::ReturnOp::create(builder, location, status);

  builder.setInsertionPointAfter(execute);
  auto destroy = LLVM::LLVMFuncOp::create(
      builder, location, makeName(".__obelisk_native_destroy"),
      LLVM::LLVMFunctionType::get(voidType, {pointer}, false));
  Block *destroyEntry = destroy.addEntryBlock(builder);
  Block *destroyCall = new Block;
  Block *destroyDone = new Block;
  destroy.getBody().push_back(destroyCall);
  destroy.getBody().push_back(destroyDone);
  builder.setInsertionPointToStart(destroyEntry);
  Value destroyInstance = destroyEntry->getArgument(0);
  Value destroyHandle = loadAt(builder, location, destroyInstance,
                               kInstanceNativeHandleOffset, pointer, 8);
  Value destroyBits = LLVM::PtrToIntOp::create(
      builder, location, builder.getI64Type(), destroyHandle);
  Value destroyIsNull = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::eq, destroyBits,
      llvmConstant(builder, location, builder.getI64Type(), 0));
  cf::CondBranchOp::create(builder, location, destroyIsNull, destroyDone,
                           ValueRange{}, destroyCall, ValueRange{});
  builder.setInsertionPointToStart(destroyCall);
  LLVM::CallIntrinsicOp::create(builder, location,
                                builder.getStringAttr("llvm.coro.destroy"),
                                destroyHandle);
  storeAt(builder, location, destroyInstance, kInstanceNativeHandleOffset,
          LLVM::ZeroOp::create(builder, location, pointer), 8);
  cf::BranchOp::create(builder, location, destroyDone);
  builder.setInsertionPointToStart(destroyDone);
  LLVM::ReturnOp::create(builder, location, ValueRange{});
  return success();
}

template <typename Initializer>
LLVM::GlobalOp makeConstantGlobal(ModuleOp module, Location location, Type type,
                                  StringRef name, LLVM::Linkage linkage,
                                  uint64_t alignment,
                                  Initializer &&initializer) {
  OpBuilder builder(module.getContext());
  builder.setInsertionPointToStart(module.getBody());
  auto global = LLVM::GlobalOp::create(builder, location, type, true, linkage,
                                       name, Attribute{}, alignment);
  Block *block = new Block;
  global.getInitializerRegion().push_back(block);
  builder.setInsertionPointToStart(block);
  LLVM::ReturnOp::create(builder, location, initializer(builder));
  return global;
}

Value insertValue(OpBuilder &builder, Location location, Value aggregate,
                  Value element, int64_t index) {
  return LLVM::InsertValueOp::create(builder, location, aggregate, element,
                                     ArrayRef<int64_t>{index});
}

uint64_t stableProcessID(StringRef name) {
  uint64_t hash = UINT64_C(14695981039346656037);
  for (unsigned char byte : name.bytes()) {
    hash ^= byte;
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

LogicalResult
makeProcessDescriptor(ModuleOp module, Location location, StringRef baseName,
                      const SimulationProcessFrameAnalysis &analysis) {
  MLIRContext *context = module.getContext();
  Type pointer = LLVM::LLVMPointerType::get(context);
  Type i32 = IntegerType::get(context, 32);
  Type i64 = IntegerType::get(context, 64);
  auto fieldType =
      LLVM::LLVMStructType::getLiteral(context, {i32, i32, i64, i64, i32, i32});
  auto fieldsType =
      LLVM::LLVMArrayType::get(fieldType, analysis.getFields().size());
  auto continuationsType =
      LLVM::LLVMArrayType::get(i32, analysis.getContinuations().size());
  auto layoutType = LLVM::LLVMStructType::getLiteral(
      context, {i32, i32, i64, i64, pointer, i32, i32, pointer, i64});
  auto handleType = LLVM::LLVMStructType::getLiteral(context, {i32, i32, i64});
  auto descriptorType = LLVM::LLVMStructType::getLiteral(
      context, {handleType, i32, i32, i32, i32, pointer, pointer, pointer,
                pointer, pointer});

  std::string fieldsName = (baseName + ".__obelisk_frame_fields").str();
  std::string continuationsName = (baseName + ".__obelisk_continuations").str();
  std::string layoutName = (baseName + ".__obelisk_frame_layout").str();
  std::string descriptorName =
      (baseName + ".__obelisk_process_descriptor").str();

  makeConstantGlobal(
      module, location, fieldsType, fieldsName, LLVM::Linkage::Internal, 8,
      [&](OpBuilder &builder) {
        Value array = LLVM::ZeroOp::create(builder, location, fieldsType);
        for (auto [index, field] : llvm::enumerate(analysis.getFields())) {
          Value value = LLVM::ZeroOp::create(builder, location, fieldType);
          value = insertValue(builder, location, value,
                              llvmConstant(builder, location, i32,
                                           static_cast<uint32_t>(field.kind)),
                              0);
          value = insertValue(builder, location, value,
                              llvmConstant(builder, location, i32,
                                           static_cast<uint32_t>(field.flags)),
                              1);
          value = insertValue(
              builder, location, value,
              llvmConstant(builder, location, i64, field.offset), 2);
          value =
              insertValue(builder, location, value,
                          llvmConstant(builder, location, i64, field.size), 3);
          value = insertValue(
              builder, location, value,
              llvmConstant(builder, location, i32, field.alignment), 4);
          array = LLVM::InsertValueOp::create(
              builder, location, array, value,
              ArrayRef<int64_t>{static_cast<int64_t>(index)});
        }
        return array;
      });
  makeConstantGlobal(module, location, continuationsType, continuationsName,
                     LLVM::Linkage::Internal, 4, [&](OpBuilder &builder) {
                       Value array = LLVM::ZeroOp::create(builder, location,
                                                          continuationsType);
                       for (auto [index, continuation] :
                            llvm::enumerate(analysis.getContinuations()))
                         array = LLVM::InsertValueOp::create(
                             builder, location, array,
                             llvmConstant(builder, location, i32, continuation),
                             ArrayRef<int64_t>{static_cast<int64_t>(index)});
                       return array;
                     });
  makeConstantGlobal(
      module, location, layoutType, layoutName, LLVM::Linkage::Internal, 8,
      [&](OpBuilder &builder) {
        Value layout = LLVM::ZeroOp::create(builder, location, layoutType);
        layout = insertValue(builder, location, layout,
                             llvmConstant(builder, location, i32, 1), 0);
        layout = insertValue(
            builder, location, layout,
            llvmConstant(builder, location, i64, analysis.getFrameSize()), 2);
        layout = insertValue(
            builder, location, layout,
            llvmConstant(builder, location, i64, analysis.getFrameAlignment()),
            3);
        layout = insertValue(
            builder, location, layout,
            LLVM::AddressOfOp::create(builder, location, pointer, fieldsName),
            4);
        layout = insertValue(
            builder, location, layout,
            llvmConstant(builder, location, i32, analysis.getFields().size()),
            5);
        layout = insertValue(builder, location, layout,
                             llvmConstant(builder, location, i32,
                                          analysis.getContinuations().size()),
                             6);
        layout = insertValue(builder, location, layout,
                             LLVM::AddressOfOp::create(
                                 builder, location, pointer, continuationsName),
                             7);
        return insertValue(
            builder, location, layout,
            llvmConstant(builder, location, i64, analysis.getChecksum()), 8);
      });
  makeConstantGlobal(
      module, location, descriptorType, descriptorName, LLVM::Linkage::External,
      8, [&](OpBuilder &builder) {
        Value handle = LLVM::ZeroOp::create(builder, location, handleType);
        handle = insertValue(builder, location, handle,
                             llvmConstant(builder, location, i32, 6), 0);
        handle = insertValue(
            builder, location, handle,
            llvmConstant(builder, location, i64, stableProcessID(baseName)), 2);
        Value descriptor =
            LLVM::ZeroOp::create(builder, location, descriptorType);
        descriptor = insertValue(builder, location, descriptor, handle, 0);
        descriptor = insertValue(builder, location, descriptor,
                                 llvmConstant(builder, location, i32, 1), 1);
        descriptor = insertValue(builder, location, descriptor,
                                 llvmConstant(builder, location, i32, 1), 3);
        descriptor = insertValue(
            builder, location, descriptor,
            LLVM::AddressOfOp::create(builder, location, pointer, layoutName),
            5);
        descriptor = insertValue(
            builder, location, descriptor,
            LLVM::AddressOfOp::create(
                builder, location, pointer,
                (baseName + ".__obelisk_native_requirements").str()),
            6);
        descriptor =
            insertValue(builder, location, descriptor,
                        LLVM::AddressOfOp::create(
                            builder, location, pointer,
                            (baseName + ".__obelisk_native_execute").str()),
                        7);
        return insertValue(builder, location, descriptor,
                           LLVM::AddressOfOp::create(
                               builder, location, pointer,
                               (baseName + ".__obelisk_native_destroy").str()),
                           8);
      });
  return success();
}

LogicalResult threadRuntimeStatuses(ModuleOp module) {
  MLIRContext *context = module.getContext();
  llvm::StringMap<sim::SimFuncOp> functions;
  SmallVector<sim::SimFuncOp> orderedFunctions;
  module.walk([&](sim::SimFuncOp function) {
    functions[function.getSymName()] = function;
    orderedFunctions.push_back(function);
  });

  llvm::DenseSet<Operation *> mayFail;
  for (sim::SimFuncOp function : orderedFunctions)
    function.walk([&](sim::SimStatusCheckOp) {
      mayFail.insert(function.getOperation());
    });

  bool changed;
  do {
    changed = false;
    for (sim::SimFuncOp function : orderedFunctions) {
      if (mayFail.contains(function.getOperation()))
        continue;
      bool callsFailing = false;
      function.walk([&](sim::SimCallOp call) {
        auto callee = functions.find(call.getCallee());
        callsFailing |= callee != functions.end() &&
                        mayFail.contains(callee->second.getOperation());
      });
      if (callsFailing)
        changed |= mayFail.insert(function.getOperation()).second;
    }
  } while (changed);

  llvm::DenseSet<Operation *> statusReturning;
  OpBuilder builder(context);
  Type statusType = runtime::StatusType::get(context);
  for (sim::SimFuncOp function : orderedFunctions) {
    if (!mayFail.contains(function.getOperation()) ||
        function.getEntryKind() != sim::EntryKind::Function)
      continue;
    statusReturning.insert(function.getOperation());
    SmallVector<Type> results(function.getResultTypes());
    results.push_back(statusType);
    function.setType(
        FunctionType::get(context, function.getArgumentTypes(), results));
    SmallVector<Attribute> resultAttrs;
    if (auto attrs = function.getResAttrs())
      llvm::append_range(resultAttrs, *attrs);
    while (resultAttrs.size() != results.size())
      resultAttrs.push_back(builder.getDictionaryAttr({}));
    function.setResAttrsAttr(builder.getArrayAttr(resultAttrs));
  }

  SmallVector<sim::SimCallOp> calls;
  module.walk([&](sim::SimCallOp call) { calls.push_back(call); });
  IRRewriter rewriter(context);
  for (sim::SimCallOp call : calls) {
    auto callee = functions.find(call.getCallee());
    if (callee == functions.end() ||
        !statusReturning.contains(callee->second.getOperation()))
      continue;
    SmallVector<Type> results(call.getResultTypes());
    results.push_back(statusType);
    SmallVector<Attribute> resultAttrs;
    if (auto attrs = call.getResAttrs())
      llvm::append_range(resultAttrs, *attrs);
    while (resultAttrs.size() != results.size())
      resultAttrs.push_back(rewriter.getDictionaryAttr({}));
    rewriter.setInsertionPoint(call);
    auto replacement = sim::SimCallOp::create(
        rewriter, call.getLoc(), results, call.getCalleeAttr(),
        call.getOperands(), call.getArgAttrsAttr(),
        rewriter.getArrayAttr(resultAttrs));
    for (auto [oldResult, newResult] : llvm::zip_equal(
             call.getResults(), replacement.getResults().drop_back()))
      oldResult.replaceAllUsesWith(newResult);
    rewriter.setInsertionPointAfter(replacement);
    sim::SimStatusCheckOp::create(rewriter, call.getLoc(),
                                  replacement.getResults().back());
    rewriter.eraseOp(call);
  }

  for (sim::SimFuncOp function : orderedFunctions) {
    if (!statusReturning.contains(function.getOperation()))
      continue;
    SmallVector<sim::SimReturnOp> returns;
    function.walk(
        [&](sim::SimReturnOp operation) { returns.push_back(operation); });
    for (sim::SimReturnOp operation : returns) {
      rewriter.setInsertionPoint(operation);
      Value zero = arith::ConstantOp::create(rewriter, operation.getLoc(),
                                             rewriter.getI32Type(),
                                             rewriter.getI32IntegerAttr(0));
      Value ok = runtime::RTStatusFromBitsOp::create(
          rewriter, operation.getLoc(), statusType, zero);
      SmallVector<Value> operands(operation.getOperands());
      operands.push_back(ok);
      sim::SimReturnOp::create(rewriter, operation.getLoc(), operands);
      rewriter.eraseOp(operation);
    }

    SmallVector<sim::SimStatusCheckOp> checks;
    function.walk(
        [&](sim::SimStatusCheckOp check) { checks.push_back(check); });
    for (sim::SimStatusCheckOp check : checks) {
      Block *source = check->getBlock();
      Block *continuation = source->splitBlock(std::next(check->getIterator()));
      Block *failure = new Block;
      function.getBody().push_back(failure);
      rewriter.setInsertionPoint(check);
      Value ok = runtime::RTStatusIsOp::create(
          rewriter, check.getLoc(), rewriter.getI1Type(), check.getStatus(), 0);
      cf::CondBranchOp::create(rewriter, check.getLoc(), ok, continuation,
                               ValueRange{}, failure, ValueRange{});
      Value status = check.getStatus();
      rewriter.eraseOp(check);

      rewriter.setInsertionPointToStart(failure);
      SmallVector<Value> values;
      for (Type type : function.getResultTypes().drop_back()) {
        auto integer = dyn_cast<IntegerType>(type);
        if (!integer)
          return function.emitError()
                 << "cannot materialize a failure result for " << type;
        values.push_back(
            arith::ConstantOp::create(rewriter, function.getLoc(), integer,
                                      rewriter.getIntegerAttr(integer, 0)));
      }
      values.push_back(status);
      sim::SimReturnOp::create(rewriter, function.getLoc(), values);
    }
  }
  return success();
}

LogicalResult
makePlainNativeWrappers(ModuleOp module, func::FuncOp body, StringRef baseName,
                        const SimulationProcessFrameAnalysis &analysis) {
  OpBuilder builder(body);
  builder.setInsertionPointAfter(body);
  Location location = body.getLoc();
  MLIRContext *context = module.getContext();
  Type pointer = LLVM::LLVMPointerType::get(context);
  Type i32 = builder.getI32Type();
  Type i64 = builder.getI64Type();
  Type voidType = LLVM::LLVMVoidType::get(context);

  auto requirements = LLVM::LLVMFuncOp::create(
      builder, location, (baseName + ".__obelisk_native_requirements").str(),
      LLVM::LLVMFunctionType::get(i32, {pointer, pointer}, false));
  Block *requirementsEntry = requirements.addEntryBlock(builder);
  builder.setInsertionPointToStart(requirementsEntry);
  LLVM::StoreOp::create(builder, location,
                        llvmConstant(builder, location, i64, 0),
                        requirementsEntry->getArgument(0), 8);
  LLVM::StoreOp::create(builder, location,
                        llvmConstant(builder, location, i64, 1),
                        requirementsEntry->getArgument(1), 8);
  LLVM::ReturnOp::create(builder, location,
                         llvmConstant(builder, location, i32, 0));

  builder.setInsertionPointAfter(requirements);
  auto execute = LLVM::LLVMFuncOp::create(
      builder, location, (baseName + ".__obelisk_native_execute").str(),
      LLVM::LLVMFunctionType::get(i32, {pointer}, false));
  Block *executeEntry = execute.addEntryBlock(builder);
  builder.setInsertionPointToStart(executeEntry);
  Value instance = executeEntry->getArgument(0);
  Value frame =
      loadAt(builder, location, instance, kInstanceFrameOffset, pointer, 8);
  SmallVector<Value> arguments;
  size_t physicalArgument = 0;
  Block &bodyEntry = body.getBody().front();
  for (const ProcessFrameValue &slot : analysis.getEntryCaptureLayout()) {
    if (slot.valueOffset == kNoOffset) {
      arguments.push_back(loadAt(builder, location, instance,
                                 kInstanceContextOffset, pointer, 8));
      ++physicalArgument;
      continue;
    }
    Type valueType = bodyEntry.getArgument(physicalArgument++).getType();
    arguments.push_back(loadAt(builder, location, frame, slot.valueOffset,
                               valueType, slot.alignment));
    if (slot.isFourState()) {
      Type unknownType = bodyEntry.getArgument(physicalArgument++).getType();
      arguments.push_back(loadAt(builder, location, frame, slot.unknownOffset,
                                 unknownType, slot.alignment));
    }
  }
  if (arguments.size() != bodyEntry.getNumArguments())
    return body.emitError(
        "converted entry arity disagrees with canonical capture layout");
  auto call = func::CallOp::create(builder, location, body.getSymName(),
                                   TypeRange{i32}, arguments);
  storeAt(builder, location, instance, kInstanceContinuationOffset,
          llvmConstant(builder, location, i32, 0), 4);
  publishAction(builder, location, instance, 2, 0, 0, 0,
                llvmConstant(builder, location, i64, 0), 0);
  LLVM::ReturnOp::create(builder, location, call.getResult(0));

  builder.setInsertionPointAfter(execute);
  auto destroy = LLVM::LLVMFuncOp::create(
      builder, location, (baseName + ".__obelisk_native_destroy").str(),
      LLVM::LLVMFunctionType::get(voidType, {pointer}, false));
  Block *destroyEntry = destroy.addEntryBlock(builder);
  builder.setInsertionPointToStart(destroyEntry);
  LLVM::ReturnOp::create(builder, location, ValueRange{});
  return success();
}

LogicalResult
lowerPlainNativeProcess(sim::SimFuncOp function,
                        const SimulationProcessFrameAnalysis &analysis) {
  if (!function.getResultTypes().empty())
    return function.emitError("simulation process cannot return values");
  if (failed(lowerTimeOperations(function)))
    return failure();
  ModuleOp module = function->getParentOfType<ModuleOp>();
  Location location = function.getLoc();
  MLIRContext *context = function.getContext();
  std::string baseName = function.getSymName().str();
  context->getOrLoadDialect<func::FuncDialect>();
  SmallVector<Type> inputTypes;
  for (BlockArgument argument : function.getBody().front().getArguments())
    inputTypes.push_back(convertProcessType(argument.getType(), context));
  OpBuilder builder(function);
  auto body = func::FuncOp::create(
      builder, location, baseName,
      FunctionType::get(context, inputTypes, TypeRange{builder.getI32Type()}));
  body->setAttr("obelisk.entry_kind",
                builder.getI32IntegerAttr(
                    static_cast<uint32_t>(function.getEntryKind())));
  body->setAttr("obelisk.native_scratch_size", builder.getI64IntegerAttr(0));
  body.getBody().takeBody(function.getBody());
  function.erase();
  for (Block &block : body.getBody())
    for (BlockArgument argument : block.getArguments())
      argument.setType(convertProcessType(argument.getType(), context));

  IRRewriter rewriter(context);
  SmallVector<Operation *> operations;
  body.walk([&](Operation *operation) {
    if (isa<sim::SimReturnOp, sim::SimCallOp>(operation))
      operations.push_back(operation);
  });
  for (Operation *operation : operations) {
    rewriter.setInsertionPoint(operation);
    if (auto returnOp = dyn_cast<sim::SimReturnOp>(operation)) {
      Value zero = arith::ConstantOp::create(rewriter, returnOp.getLoc(),
                                             rewriter.getI32Type(),
                                             rewriter.getI32IntegerAttr(0));
      func::ReturnOp::create(rewriter, returnOp.getLoc(), zero);
      rewriter.eraseOp(returnOp);
      continue;
    }
    auto call = cast<sim::SimCallOp>(operation);
    auto converted =
        func::CallOp::create(rewriter, call.getLoc(), call.getCallee(),
                             call.getResultTypes(), call.getOperands());
    rewriter.replaceOp(call, converted.getResults());
  }

  SmallVector<sim::SimStatusCheckOp> checks;
  body.walk([&](sim::SimStatusCheckOp check) { checks.push_back(check); });
  for (sim::SimStatusCheckOp check : checks) {
    Block *source = check->getBlock();
    Block *continuation = source->splitBlock(std::next(check->getIterator()));
    Block *failure = new Block;
    body.getBody().push_back(failure);
    rewriter.setInsertionPoint(check);
    Value ok = runtime::RTStatusIsOp::create(
        rewriter, check.getLoc(), rewriter.getI1Type(), check.getStatus(), 0);
    cf::CondBranchOp::create(rewriter, check.getLoc(), ok, continuation,
                             ValueRange{}, failure, ValueRange{});
    Value status = check.getStatus();
    rewriter.eraseOp(check);
    rewriter.setInsertionPointToStart(failure);
    Value bits = runtime::RTStatusToBitsOp::create(
        rewriter, body.getLoc(), rewriter.getI32Type(), status);
    func::ReturnOp::create(rewriter, body.getLoc(), bits);
  }

  if (failed(makePlainNativeWrappers(module, body, baseName, analysis)))
    return failure();
  return makeProcessDescriptor(module, location, baseName, analysis);
}

LogicalResult
lowerSuspendableProcess(sim::SimFuncOp function,
                        const SimulationProcessFrameAnalysis &analysis) {
  if (failed(lowerTimeOperations(function)))
    return failure();
  ModuleOp module = function->getParentOfType<ModuleOp>();
  OpBuilder builder(function);
  Location location = function.getLoc();
  MLIRContext *context = function.getContext();
  Type pointer = LLVM::LLVMPointerType::get(context);
  Type i32 = builder.getI32Type();
  Type i64 = builder.getI64Type();
  std::string baseName = function.getSymName().str();
  std::string rampName = baseName + ".__obelisk_coro_ramp";
  auto ramp = LLVM::LLVMFuncOp::create(
      builder, location, rampName,
      LLVM::LLVMFunctionType::get(LLVM::LLVMVoidType::get(context),
                                  {pointer, i32, pointer, pointer}, false));
  ramp->setAttr(
      "passthrough",
      builder.getArrayAttr({builder.getStringAttr("presplitcoroutine")}));
  addFrameAttributes(ramp, analysis, builder);
  ramp.getBody().takeBody(function.getBody());
  function.erase();

  for (Block &block : ramp.getBody())
    for (BlockArgument argument : block.getArguments())
      argument.setType(convertProcessType(argument.getType(), context));

  IRRewriter callRewriter(context);
  SmallVector<sim::SimCallOp> calls;
  ramp.walk([&](sim::SimCallOp call) { calls.push_back(call); });
  for (sim::SimCallOp call : calls) {
    callRewriter.setInsertionPoint(call);
    auto converted =
        func::CallOp::create(callRewriter, call.getLoc(), call.getCallee(),
                             call.getResultTypes(), call.getOperands());
    callRewriter.replaceOp(call, converted.getResults());
  }

  Block *oldEntry = &ramp.getBody().front();
  Block *entry = new Block;
  for (Type type : {pointer, i32, pointer, pointer})
    entry->addArgument(type, location);
  ramp.getBody().getBlocks().insert(ramp.getBody().begin(), entry);
  Block *requirements = new Block;
  Block *execute = new Block;
  ramp.getBody().push_back(requirements);
  ramp.getBody().push_back(execute);
  builder.setInsertionPointToStart(entry);
  Value zero32 = llvmConstant(builder, location, i32, 0);
  Value null = LLVM::ZeroOp::create(builder, location, pointer);
  Value id = LLVM::CoroIdOp::create(builder, location,
                                    LLVM::LLVMTokenType::get(context), zero32,
                                    null, null, null);
  Value requirementsMode =
      arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                            entry->getArgument(1), zero32);
  cf::CondBranchOp::create(builder, location, requirementsMode, requirements,
                           ValueRange{}, execute, ValueRange{});

  builder.setInsertionPointToStart(requirements);
  Value size = LLVM::CoroSizeOp::create(builder, location, i64);
  Value alignment = LLVM::CoroAlignOp::create(builder, location, i64);
  LLVM::StoreOp::create(builder, location, size, entry->getArgument(2), 8);
  LLVM::StoreOp::create(builder, location, alignment, entry->getArgument(3), 8);
  LLVM::ReturnOp::create(builder, location, ValueRange{});

  builder.setInsertionPointToStart(execute);
  Value instance = entry->getArgument(0);
  Value allocation = loadAt(builder, location, instance,
                            kInstanceAllocationOffset, pointer, 8);
  Value scratchOffset =
      loadAt(builder, location, instance, kInstanceScratchOffset, i64, 8);
  Value scratch =
      LLVM::GEPOp::create(builder, location, pointer, builder.getI8Type(),
                          allocation, ValueRange{scratchOffset});
  Value handle =
      LLVM::CoroBeginOp::create(builder, location, pointer, id, scratch);
  storeAt(builder, location, instance, kInstanceNativeHandleOffset, handle, 8);

  RampBlocks blocks;
  blocks.suspendReturn =
      makeCoroutineReturnBlock(ramp.getBody(), location, handle);
  blocks.cleanup = new Block;
  bool canTerminate = false;
  ramp.walk([&](Operation *operation) {
    canTerminate |= isa<sim::SimReturnOp, sim::SimStatusCheckOp>(operation);
  });
  blocks.terminate = canTerminate ? new Block : nullptr;
  ramp.getBody().push_back(blocks.cleanup);
  if (blocks.terminate)
    ramp.getBody().push_back(blocks.terminate);

  llvm::SetVector<Block *> continuationBlocks;
  for (Block &block : ramp.getBody())
    if (!block.empty() && isSuspension(block.getTerminator()))
      continuationBlocks.insert(block.getTerminator()->getSuccessor(0));
  for (Block *continuation : continuationBlocks) {
    Block *shim = new Block;
    ramp.getBody().push_back(shim);
    blocks.shims[continuation] = shim;
    builder.setInsertionPointToStart(shim);
    Value frame =
        loadAt(builder, location, instance, kInstanceFrameOffset, pointer, 8);
    SmallVector<Value> loaded;
    size_t argumentIndex = 0;
    uint32_t continuationID = 0;
    for (Block *predecessor : continuation->getPredecessors()) {
      if (auto id = predecessor->getTerminator()->getAttrOfType<IntegerAttr>(
              "obelisk.coro.continuation")) {
        continuationID = id.getInt();
        break;
      }
    }
    if (continuationID == 0)
      return continuation->getParentOp()->emitError(
          "continuation block is missing its stable continuation ID");
    for (const ProcessFrameValue &slot :
         analysis.getContinuationLayout(continuationID)) {
      Type valueType = continuation->getArgument(argumentIndex++).getType();
      loaded.push_back(loadAt(builder, location, frame, slot.valueOffset,
                              valueType, slot.alignment));
      if (slot.isFourState()) {
        Type unknownType = continuation->getArgument(argumentIndex++).getType();
        loaded.push_back(loadAt(builder, location, frame, slot.unknownOffset,
                                unknownType, slot.alignment));
      }
    }
    cf::BranchOp::create(builder, location, continuation, loaded);
  }

  builder.setInsertionPointToEnd(execute);
  SmallVector<Value> entryArguments;
  Value frame =
      loadAt(builder, location, instance, kInstanceFrameOffset, pointer, 8);
  ArrayRef<ProcessFrameValue> captureLayout = analysis.getEntryCaptureLayout();
  size_t physicalArgument = 0;
  auto refreshFrameArgument = [&](BlockArgument argument, Type type,
                                  uint64_t offset, uint32_t alignment) {
    SmallVector<OpOperand *> uses;
    for (OpOperand &use : argument.getUses())
      uses.push_back(&use);
    OpBuilder refreshBuilder(context);
    for (OpOperand *use : uses) {
      Location useLocation = use->getOwner()->getLoc();
      refreshBuilder.setInsertionPoint(use->getOwner());
      Value currentFrame = loadAt(refreshBuilder, useLocation, instance,
                                  kInstanceFrameOffset, pointer, 8);
      use->set(loadAt(refreshBuilder, useLocation, currentFrame, offset, type,
                      alignment));
    }
  };
  for (const ProcessFrameValue &slot : captureLayout) {
    if (slot.valueOffset == kNoOffset) {
      entryArguments.push_back(loadAt(builder, location, instance,
                                      kInstanceContextOffset, pointer, 8));
      // The scheduler may supply a different transient context on every
      // invocation.  Do not let LLVM preserve the first context in the
      // coroutine frame: reload it through the runtime-owned instance at each
      // actual use, including uses reached after a resume.
      BlockArgument contextArgument = oldEntry->getArgument(physicalArgument);
      SmallVector<OpOperand *> contextUses;
      for (OpOperand &use : contextArgument.getUses())
        contextUses.push_back(&use);
      OpBuilder refreshBuilder(context);
      for (OpOperand *use : contextUses) {
        refreshBuilder.setInsertionPoint(use->getOwner());
        Value currentContext =
            loadAt(refreshBuilder, use->getOwner()->getLoc(), instance,
                   kInstanceContextOffset, pointer, 8);
        use->set(currentContext);
      }
      ++physicalArgument;
      continue;
    }
    BlockArgument valueArgument = oldEntry->getArgument(physicalArgument++);
    Type valueType = valueArgument.getType();
    entryArguments.push_back(loadAt(builder, location, frame, slot.valueOffset,
                                    valueType, slot.alignment));
    // Captures are canonical-frame state, not coroutine-frame state. Reload
    // them through the instance at every use so LLVM only needs to preserve
    // the instance/control pointer across suspension.
    refreshFrameArgument(valueArgument, valueType, slot.valueOffset,
                         slot.alignment);
    if (slot.isFourState()) {
      BlockArgument unknownArgument = oldEntry->getArgument(physicalArgument++);
      Type unknownType = unknownArgument.getType();
      entryArguments.push_back(loadAt(builder, location, frame,
                                      slot.unknownOffset, unknownType,
                                      slot.alignment));
      refreshFrameArgument(unknownArgument, unknownType, slot.unknownOffset,
                           slot.alignment);
    }
  }
  if (entryArguments.size() != oldEntry->getNumArguments())
    return ramp.emitError(
        "converted entry arity disagrees with canonical capture layout");
  for (auto [argument, loaded] :
       llvm::zip_equal(oldEntry->getArguments(), entryArguments))
    argument.replaceAllUsesWith(loaded);

  // Dynamic entry dispatch supports bytecode -> native reconstruction at any
  // stable semantic continuation. Ordinary CFG edges remain direct branches.
  Block *dispatch = new Block;
  ramp.getBody().push_back(dispatch);
  cf::BranchOp::create(builder, location, dispatch);
  builder.setInsertionPointToStart(dispatch);
  Value continuationID =
      loadAt(builder, location, instance, kInstanceContinuationOffset, i32, 4);
  SmallVector<std::pair<uint32_t, Block *>> targets;
  targets.emplace_back(0, oldEntry);
  for (uint32_t idValue : analysis.getContinuations()) {
    if (idValue == 0)
      continue;
    Block *target = nullptr;
    for (Block *candidate : continuationBlocks) {
      for (Block *predecessor : candidate->getPredecessors()) {
        Operation *terminator = predecessor->getTerminator();
        auto idAttr =
            terminator->getAttrOfType<IntegerAttr>("obelisk.coro.continuation");
        if (idAttr && idAttr.getInt() == idValue)
          target = blocks.shims.lookup(candidate);
      }
    }
    if (target)
      targets.emplace_back(idValue, target);
  }
  Block *invalid = new Block;
  ramp.getBody().push_back(invalid);
  builder.setInsertionPointToStart(invalid);
  storeAt(builder, location, instance, kInstanceStatusOffset,
          llvmConstant(builder, location, i32, 12), 4);
  cf::BranchOp::create(builder, location, blocks.cleanup);
  Block *test = dispatch;
  for (auto [index, target] : llvm::enumerate(targets)) {
    builder.setInsertionPointToEnd(test);
    Value expected = llvmConstant(builder, location, i32, target.first);
    Value equal = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, continuationID, expected);
    Block *next = index + 1 == targets.size() ? invalid : new Block;
    if (next != invalid)
      ramp.getBody().push_back(next);
    ValueRange arguments =
        target.first == 0 ? ValueRange(entryArguments) : ValueRange{};
    cf::CondBranchOp::create(builder, location, equal, target.second, arguments,
                             next, ValueRange{});
    test = next;
  }

  if (blocks.terminate) {
    // LLVM permits exactly one final suspend per switched-resume coroutine.
    // Funnel every semantic process return through this shared block.
    builder.setInsertionPointToStart(blocks.terminate);
    publishAction(builder, location, instance, 2, 0, 0, 0,
                  llvmConstant(builder, location, i64, 0), 0);
    Value final = llvmConstant(builder, location, builder.getI1Type(), 1);
    Value save = LLVM::CoroSaveOp::create(
        builder, location, LLVM::LLVMTokenType::get(context), handle);
    Value finalState = LLVM::CoroSuspendOp::create(
        builder, location, builder.getI8Type(), save, final);
    Block *invalidFinalResume = new Block;
    ramp.getBody().push_back(invalidFinalResume);
    builder.setInsertionPointToStart(invalidFinalResume);
    LLVM::UnreachableOp::create(builder, location);
    builder.setInsertionPointToEnd(blocks.terminate);
    SmallVector<Block *> destinations{invalidFinalResume, blocks.cleanup};
    SmallVector<ValueRange> destinationOperands(2);
    SmallVector<APInt> caseValues{APInt(8, 0), APInt(8, 1)};
    LLVM::SwitchOp::create(builder, location, finalState, blocks.suspendReturn,
                           ValueRange{}, caseValues, destinations,
                           destinationOperands, ArrayRef<int32_t>{});
  }

  builder.setInsertionPointToStart(blocks.cleanup);
  storeAt(builder, location, instance, kInstanceNativeHandleOffset,
          LLVM::ZeroOp::create(builder, location, pointer), 8);
  cf::BranchOp::create(builder, location, blocks.suspendReturn);

  SmallVector<Operation *> terminators;
  ramp.walk([&](Operation *operation) {
    if (isSuspension(operation) || isa<sim::SimReturnOp>(operation))
      terminators.push_back(operation);
  });
  for (Operation *operation : terminators) {
    if (isSuspension(operation)) {
      if (failed(lowerSuspendTerminator(operation, instance, handle, analysis,
                                        blocks)))
        return failure();
    } else if (failed(lowerFinalReturn(cast<sim::SimReturnOp>(operation),
                                       blocks))) {
      return failure();
    }
  }

  SmallVector<sim::SimStatusCheckOp> checks;
  ramp.walk([&](sim::SimStatusCheckOp check) { checks.push_back(check); });
  for (sim::SimStatusCheckOp check : checks) {
    Block *source = check->getBlock();
    Block *continuation = source->splitBlock(std::next(check->getIterator()));
    Block *failure = new Block;
    ramp.getBody().push_back(failure);
    builder.setInsertionPoint(check);
    Value ok = runtime::RTStatusIsOp::create(
        builder, check.getLoc(), builder.getI1Type(), check.getStatus(), 0);
    cf::CondBranchOp::create(builder, check.getLoc(), ok, continuation,
                             ValueRange{}, failure, ValueRange{});
    Value status = check.getStatus();
    check.erase();
    builder.setInsertionPointToStart(failure);
    Value bits = runtime::RTStatusToBitsOp::create(
        builder, location, builder.getI32Type(), status);
    storeAt(builder, location, instance, kInstanceStatusOffset, bits, 4);
    cf::BranchOp::create(builder, location, blocks.terminate);
  }
  if (failed(makeNativeWrappers(module, ramp, baseName)))
    return failure();
  return makeProcessDescriptor(module, location, baseName, analysis);
}

LogicalResult lowerOrdinaryFunction(sim::SimFuncOp function) {
  Location location = function.getLoc();
  std::string symbolName = function.getSymName().str();
  FunctionType functionType = function.getFunctionType();
  uint32_t entryKind = static_cast<uint32_t>(function.getEntryKind());
  function.getContext()->getOrLoadDialect<func::FuncDialect>();
  OpBuilder builder(function.getContext());
  builder.setInsertionPoint(function);
  auto replacement =
      func::FuncOp::create(builder, location, builder.getStringAttr(symbolName),
                           TypeAttr::get(functionType), StringAttr{},
                           ArrayAttr{}, ArrayAttr{}, UnitAttr{});
  replacement->setAttr("obelisk.entry_kind",
                       builder.getI32IntegerAttr(entryKind));
  replacement->setAttr("obelisk.native_scratch_size",
                       builder.getI64IntegerAttr(0));
  replacement.getBody().takeBody(function.getBody());
  function.erase();
  IRRewriter rewriter(replacement.getContext());
  SmallVector<Operation *> operations;
  replacement.walk([&](Operation *operation) {
    if (isa<sim::SimReturnOp, sim::SimCallOp>(operation))
      operations.push_back(operation);
  });
  for (Operation *operation : operations) {
    rewriter.setInsertionPoint(operation);
    if (auto returnOp = dyn_cast<sim::SimReturnOp>(operation)) {
      func::ReturnOp::create(rewriter, returnOp.getLoc(),
                             returnOp.getOperands());
      rewriter.eraseOp(returnOp);
    } else {
      auto call = cast<sim::SimCallOp>(operation);
      auto converted =
          func::CallOp::create(rewriter, call.getLoc(), call.getCallee(),
                               call.getResultTypes(), call.getOperands());
      rewriter.replaceOp(call, converted.getResults());
    }
  }
  return success();
}

/// Make every non-capture value crossing a block boundary explicit in block
/// arguments. In particular, this turns values live across a semantic
/// suspension into successor operands, so the canonical continuation lanes
/// own them instead of LLVM's coroutine frame. The fixed point also threads a
/// value through ordinary CFG blocks before or after a suspension.
LogicalResult threadProcessStateThroughCFG(sim::SimFuncOp function) {
  Block *entry = &function.getBody().front();
  DenseMap<Block *, DenseMap<Value, BlockArgument>> threadedValues;
  bool changed;
  do {
    changed = false;
    for (Block &block : function.getBody()) {
      if (&block == entry)
        continue;
      llvm::SetVector<Value> externalValues;
      for (Operation &operation : block)
        for (Value value : operation.getOperands()) {
          if (value.getParentBlock() == &block)
            continue;
          if (auto argument = dyn_cast<BlockArgument>(value);
              argument && argument.getOwner() == entry)
            continue;
          externalValues.insert(value);
        }
      for (Value value : externalValues) {
        auto &threaded = threadedValues[&block];
        auto existing = threaded.find(value);
        if (existing != threaded.end()) {
          for (Operation &operation : block)
            for (OpOperand &operand : operation.getOpOperands())
              if (operand.get() == value)
                operand.set(existing->second);
          continue;
        }
        BlockArgument argument =
            block.addArgument(value.getType(), value.getLoc());
        threaded.insert({value, argument});
        SmallVector<OpOperand *> uses;
        for (Operation &operation : block)
          for (OpOperand &operand : operation.getOpOperands())
            if (operand.get() == value)
              uses.push_back(&operand);
        for (OpOperand *use : uses)
          use->set(argument);

        for (Block *predecessor : block.getPredecessors()) {
          auto branch =
              dyn_cast<BranchOpInterface>(predecessor->getTerminator());
          if (!branch)
            return predecessor->getTerminator()->emitError(
                "cannot thread suspension-live state through a non-branch "
                "terminator");
          bool found = false;
          for (auto [index, successor] :
               llvm::enumerate(predecessor->getSuccessors())) {
            if (successor != &block)
              continue;
            branch.getSuccessorOperands(index).append(value);
            found = true;
          }
          if (!found)
            return predecessor->getTerminator()->emitError(
                "predecessor is missing its CFG successor");
        }
        changed = true;
      }
    }
  } while (changed);
  return success();
}

LogicalResult prepareSimulationProcessesForLLVMCoroutinesImpl(
    ModuleOp module, const llvm::DataLayout &dataLayout) {
  MLIRContext *context = module.getContext();
  llvm::MapVector<Operation *, std::unique_ptr<SimulationProcessFrameAnalysis>>
      analyses;
  WalkResult analyzed = module.walk([&](sim::SimFuncOp function) {
    bool suspendable = false;
    function.walk(
        [&](Operation *operation) { suspendable |= isSuspension(operation); });
    bool process = function.getEntryKind() != sim::EntryKind::Function;
    if (!suspendable && !process)
      return WalkResult::advance();
    if (suspendable && failed(threadProcessStateThroughCFG(function)))
      return WalkResult::interrupt();
    auto analysis =
        SimulationProcessFrameAnalysis::create(function, dataLayout);
    if (failed(analysis))
      return WalkResult::interrupt();
    for (const ProcessSuspension &suspension : (*analysis)->getSuspensions()) {
      suspension.operation->setAttr(
          "obelisk.coro.continuation",
          IntegerAttr::get(IntegerType::get(context, 32),
                           suspension.continuationID));
      suspension.operation->setAttr(
          "obelisk.coro.wait_offset",
          IntegerAttr::get(IntegerType::get(context, 64),
                           suspension.waitOffset));
      suspension.operation->setAttr(
          "obelisk.coro.wait_size",
          IntegerAttr::get(IntegerType::get(context, 64), suspension.waitSize));
    }
    analyses.insert({function.getOperation(), std::move(*analysis)});
    return WalkResult::advance();
  });
  if (analyzed.wasInterrupted())
    return failure();

  SimulationToStandardTypeConverter packedConverter;
  addSimulationPackedAggregateTypeConversions(packedConverter);
  addSimulationToRuntimeTypeConversions(packedConverter);
  RewritePatternSet packedPatterns(context);
  populateSimulationToStandardPatterns(packedConverter, packedPatterns);
  populateSimulationPackedAggregateViewPatterns(packedConverter,
                                                packedPatterns);
  populateSimulationToRuntimePatterns(packedConverter, packedPatterns);
  packedPatterns.add<SimFuncSignatureConversion, SimReturnTypeConversion,
                     SimCallTypeConversion,
                     SimSuspendTypeConversion<sim::SimSuspendDelayOp>,
                     SimSuspendTypeConversion<sim::SimSuspendChangeOp>,
                     SimSuspendTypeConversion<sim::SimSuspendEdgeOp>,
                     SimSuspendTypeConversion<sim::SimSuspendAnyOp>,
                     SimSuspendTypeConversion<sim::SimSuspendEventOp>,
                     SimSuspendTypeConversion<sim::SimSuspendAwaitOp>,
                     SimSuspendTypeConversion<sim::SimSuspendJoinOp>>(
      packedConverter, context);
  ConversionTarget packedTarget(*context);
  packedTarget.addIllegalOp<
      sim::SimBytesConstantOp, sim::SimDisplayOp, sim::SimFileOpenMCDOp,
      sim::SimFileOpenOp, sim::SimFileCloseOp, sim::SimFileFlushOp,
      sim::SimFileGetcOp, sim::SimFileUngetcOp, sim::SimFileGetlineOp,
      sim::SimFileReadPackedOp, sim::SimFileEofOp, sim::SimFileSeekOp,
      sim::SimFileTellOp, sim::SimFileRewindOp>();
  packedTarget.addLegalDialect<runtime::ObeliskRuntimeDialect>();
  packedTarget.addLegalOp<sim::SimContextRuntimeOp, sim::SimStatusCheckOp>();
  packedTarget.addDynamicallyLegalDialect<
      sim::ObeliskSimulationDialect, arith::ArithDialect,
      cf::ControlFlowDialect, func::FuncDialect>(hasNoLogic);
  packedTarget.addDynamicallyLegalOp<ModuleOp>(hasNoLogic);
  packedTarget.markUnknownOpDynamicallyLegal(hasNoLogic);
  if (failed(applyFullConversion(module, packedTarget,
                                 std::move(packedPatterns))) ||
      failed(threadRuntimeStatuses(module)))
    return failure();
  if (failed(validateRuntimeToLLVMPreconditions(module, dataLayout)))
    return failure();

  for (auto &entry : analyses) {
    auto function = dyn_cast_if_present<sim::SimFuncOp>(entry.first);
    if (!function)
      return failure();
    LogicalResult lowered =
        entry.second->getSuspensions().empty()
            ? lowerPlainNativeProcess(function, *entry.second)
            : lowerSuspendableProcess(function, *entry.second);
    if (failed(lowered))
      return failure();
  }
  SmallVector<sim::SimFuncOp> ordinary;
  module.walk([&](sim::SimFuncOp function) { ordinary.push_back(function); });
  for (sim::SimFuncOp function : ordinary)
    if (failed(lowerOrdinaryFunction(function)))
      return failure();

  SmallVector<sim::SimDesignOp> designs;
  module.walk([&](sim::SimDesignOp design) { designs.push_back(design); });
  for (sim::SimDesignOp design : designs) {
    SmallVector<Operation *> nested;
    for (Operation &operation : design.getBody().front())
      nested.push_back(&operation);
    for (Operation *operation : nested) {
      if (isa<sim::SimScopeDeclOp, sim::SimStorageDeclOp, sim::SimNetDeclOp,
              sim::SimDriverDeclOp>(operation)) {
        operation->erase();
        continue;
      }
      operation->moveBefore(design);
    }
    design.erase();
  }
  return success();
}

class ConvertObeliskSimProcessesToLLVMCoroutinesPass final
    : public impl::ConvertObeliskSimProcessesToLLVMCoroutinesPassBase<
          ConvertObeliskSimProcessesToLLVMCoroutinesPass> {
public:
  void runOnOperation() override {
    ModuleOp module = getOperation();
    auto layoutAttr = module->getAttrOfType<StringAttr>("llvm.data_layout");
    if (!layoutAttr) {
      module.emitError(
          "coroutine lowering requires an explicit llvm.data_layout");
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
      module.emitError("coroutine lowering currently requires a 64-bit "
                       "little-endian target");
      return signalPassFailure();
    }
    if (failed(validateProcessABI(module, *parsed)))
      return signalPassFailure();
    if (failed(validateRuntimeToLLVMPreconditions(module, *parsed)))
      return signalPassFailure();

    if (failed(prepareSimulationProcessesToLLVMCoroutines(module, *parsed)))
      return signalPassFailure();

    LowerToLLVMOptions options(&getContext());
    options.dataLayout = *parsed;
    LLVMTypeConverter converter(&getContext(), options);
    converter.addConversion([&](Type type) -> std::optional<Type> {
      Type converted = convertProcessType(type, &getContext());
      if (converted != type)
        return converted;
      return std::nullopt;
    });
    addRuntimeToLLVMTypeConversions(converter);
    RewritePatternSet patterns(&getContext());
    populateSimulationCoroutineToLLVMPatterns(converter, patterns);
    if (failed(verify(module)))
      return signalPassFailure();
    ConversionTarget target(getContext());
    target.addLegalDialect<LLVM::LLVMDialect>();
    target.addLegalOp<ModuleOp>();
    target.markUnknownOpDynamicallyLegal(
        [](Operation *operation) { return isa<LLVM::LLVMFuncOp>(operation); });
    if (failed(applyFullConversion(module, target, std::move(patterns)))) {
      signalPassFailure();
      return;
    }
    if (failed(verify(module)))
      signalPassFailure();
  }
};

} // namespace

LogicalResult
prepareSimulationProcessesToLLVMCoroutines(ModuleOp module,
                                           const llvm::DataLayout &dataLayout) {
  return prepareSimulationProcessesForLLVMCoroutinesImpl(module, dataLayout);
}

FailureOr<std::unique_ptr<SimulationProcessFrameAnalysis>>
SimulationProcessFrameAnalysis::create(sim::SimFuncOp function,
                                       const llvm::DataLayout &dataLayout) {
  auto result = std::make_unique<SimulationProcessFrameAnalysis>();
  // Do not leave cached layouts for types owned by this temporary context in
  // a DataLayout that is subsequently reused by another analysis.
  llvm::DataLayout analysisLayout(dataLayout.getStringRepresentation());
  llvm::LLVMContext llvmContext;
  uint64_t cursor = 0;
  auto allocate =
      [&](Type type, ProcessFrameFieldKind kind,
          SmallVectorImpl<ProcessFrameValue> &values) -> LogicalResult {
    FailureOr<StorageProperties> storage =
        storageProperties(type, analysisLayout, llvmContext);
    if (failed(storage)) {
      function.emitError() << "cannot place type " << type
                           << " in the canonical process frame";
      return failure();
    }
    uint64_t valueOffset;
    if (!alignUp(cursor, storage->alignment, valueOffset))
      return function.emitError("canonical process frame offset overflow");
    uint64_t end;
    if (valueOffset > std::numeric_limits<uint64_t>::max() - storage->size)
      return function.emitError("canonical process frame size overflow");
    end = valueOffset + storage->size;
    uint64_t unknownOffset = kNoOffset;
    ProcessFrameFieldFlags flags = storage->fourState
                                       ? ProcessFrameFieldFlags::FourStateValue
                                       : ProcessFrameFieldFlags::None;
    result->fields.push_back(
        {kind, flags, valueOffset, storage->size, storage->alignment});
    if (storage->fourState) {
      unknownOffset = end;
      if (end > std::numeric_limits<uint64_t>::max() - storage->size)
        return function.emitError("canonical process frame size overflow");
      end += storage->size;
      result->fields.push_back({kind, ProcessFrameFieldFlags::FourStateUnknown,
                                unknownOffset, storage->size,
                                storage->alignment});
    }
    cursor = end;
    result->frameAlignment =
        std::max<uint64_t>(result->frameAlignment, storage->alignment);
    values.push_back(
        {valueOffset, unknownOffset, storage->size, storage->alignment});
    return success();
  };

  Block &entry = function.getBody().front();
  for (auto [index, argument] : llvm::enumerate(entry.getArguments())) {
    if (index == 0 && isa<sim::ContextType>(argument.getType())) {
      result->entryCaptureLayout.push_back({kNoOffset, kNoOffset, 0, 1});
      continue;
    }
    if (failed(allocate(argument.getType(), ProcessFrameFieldKind::Capture,
                        result->entryCaptureLayout)))
      return failure();
  }

  SmallVector<Operation *> suspensionOps;
  function.walk([&](Operation *operation) {
    if (isSuspension(operation))
      suspensionOps.push_back(operation);
  });
  llvm::SetVector<Block *> continuationBlocks;
  for (Operation *operation : suspensionOps)
    continuationBlocks.insert(operation->getSuccessor(0));

  // Exactly one semantic continuation is resident while a serial process is
  // suspended.  Treat each successor-argument position as a liveness lane and
  // size it for the largest value that can occupy that lane.  Distinct
  // continuation targets then share storage without copying on tier changes.
  struct ContinuationLane {
    uint64_t planeSize = 0;
    uint32_t alignment = 1;
    bool fourState = false;
    uint64_t valueOffset = 0;
    uint64_t unknownOffset = kNoOffset;
  };
  SmallVector<ContinuationLane> lanes;
  for (Block *block : continuationBlocks) {
    if (lanes.size() < block->getNumArguments())
      lanes.resize(block->getNumArguments());
    for (auto [index, argument] : llvm::enumerate(block->getArguments())) {
      FailureOr<StorageProperties> storage =
          storageProperties(argument.getType(), analysisLayout, llvmContext);
      if (failed(storage)) {
        function.emitError() << "cannot place type " << argument.getType()
                             << " in the canonical process frame";
        return failure();
      }
      ContinuationLane &lane = lanes[index];
      lane.planeSize = std::max(lane.planeSize, storage->size);
      lane.alignment = std::max(lane.alignment, storage->alignment);
      lane.fourState |= storage->fourState;
    }
  }
  for (ContinuationLane &lane : lanes) {
    if (!alignUp(lane.planeSize, lane.alignment, lane.planeSize) ||
        !alignUp(cursor, lane.alignment, lane.valueOffset) ||
        lane.valueOffset >
            std::numeric_limits<uint64_t>::max() - lane.planeSize)
      return function.emitError("canonical process frame size overflow");
    uint64_t end = lane.valueOffset + lane.planeSize;
    result->fields.push_back(
        {ProcessFrameFieldKind::Continuation,
         lane.fourState ? ProcessFrameFieldFlags::FourStateValue
                        : ProcessFrameFieldFlags::None,
         lane.valueOffset, lane.planeSize, lane.alignment});
    if (lane.fourState) {
      lane.unknownOffset = end;
      if (end > std::numeric_limits<uint64_t>::max() - lane.planeSize)
        return function.emitError("canonical process frame size overflow");
      end += lane.planeSize;
      result->fields.push_back({ProcessFrameFieldKind::Continuation,
                                ProcessFrameFieldFlags::FourStateUnknown,
                                lane.unknownOffset, lane.planeSize,
                                lane.alignment});
    }
    cursor = end;
    result->frameAlignment =
        std::max<uint64_t>(result->frameAlignment, lane.alignment);
  }
  for (Block *block : continuationBlocks) {
    auto &layout = result->continuationLayouts[block];
    for (auto [index, argument] : llvm::enumerate(block->getArguments())) {
      FailureOr<StorageProperties> storage =
          storageProperties(argument.getType(), analysisLayout, llvmContext);
      if (failed(storage))
        return failure();
      const ContinuationLane &lane = lanes[index];
      layout.push_back({lane.valueOffset,
                        storage->fourState ? lane.unknownOffset : kNoOffset,
                        storage->size, storage->alignment});
    }
  }

  uint64_t nextID = 1;
  // LLVM's DenseSet reserves the two largest unsigned values as sentinels,
  // but both are valid continuation IDs in the runtime ABI.
  std::set<uint32_t> usedIDs;
  uint64_t maxWaitSize = kWaitHeaderSize;
  for (Operation *operation : suspensionOps) {
    if (auto site =
            operation->getAttrOfType<sim::ContinuationSiteAttr>("site")) {
      if (site.getId() == 0 || !usedIDs.insert(site.getId()).second) {
        operation->emitError("requires a unique nonzero continuation ID");
        return failure();
      }
    }
    uint64_t entries = waitEntryCount(operation);
    if (entries > (std::numeric_limits<uint64_t>::max() - kWaitHeaderSize) /
                      kWaitEntrySize)
      return operation->emitError("wait record size overflow");
    maxWaitSize =
        std::max(maxWaitSize, kWaitHeaderSize + entries * kWaitEntrySize);
  }
  uint64_t waitOffset = kNoOffset;
  if (!suspensionOps.empty()) {
    if (!alignUp(cursor, 8, waitOffset) ||
        waitOffset > std::numeric_limits<uint64_t>::max() - maxWaitSize)
      return function.emitError("canonical wait record offset overflow");
    result->fields.push_back({ProcessFrameFieldKind::Wait,
                              ProcessFrameFieldFlags::None, waitOffset,
                              maxWaitSize, 8});
    result->frameAlignment = std::max<uint64_t>(result->frameAlignment, 8);
    cursor = waitOffset + maxWaitSize;
  }

  for (Operation *operation : suspensionOps) {
    uint32_t id;
    if (auto site = operation->getAttrOfType<sim::ContinuationSiteAttr>("site"))
      id = site.getId();
    else {
      while (nextID <= std::numeric_limits<uint32_t>::max() &&
             usedIDs.count(static_cast<uint32_t>(nextID)) != 0)
        ++nextID;
      if (nextID > std::numeric_limits<uint32_t>::max())
        return operation->emitError("continuation ID space is exhausted");
      id = static_cast<uint32_t>(nextID++);
      usedIDs.insert(id);
    }
    result->suspensions.push_back(
        {operation, operation->getSuccessor(0), id, waitOffset, maxWaitSize});
    result->continuationLayoutsByID[id] =
        result->continuationLayouts.lookup(operation->getSuccessor(0));
  }
  result->continuations.push_back(0);
  for (uint32_t id : usedIDs)
    result->continuations.push_back(id);
  llvm::sort(result->continuations);
  if (result->frameAlignment > 4096 ||
      result->fields.size() > std::numeric_limits<uint32_t>::max() ||
      result->continuations.size() > std::numeric_limits<uint32_t>::max())
    return function.emitError(
        "canonical process frame exceeds the runtime ABI limits");
  if (!alignUp(cursor, result->frameAlignment, result->frameSize))
    return function.emitError("canonical process frame size overflow");

  uint64_t hash = UINT64_C(14695981039346656037);
  hash = appendHash(hash, 1, 4);
  hash = appendHash(hash, 0, 4);
  hash = appendHash(hash, result->frameSize, 8);
  hash = appendHash(hash, result->frameAlignment, 8);
  hash = appendHash(hash, result->fields.size(), 4);
  hash = appendHash(hash, result->continuations.size(), 4);
  for (const ProcessFrameField &field : result->fields) {
    hash = appendHash(hash, static_cast<uint32_t>(field.kind), 4);
    hash = appendHash(hash, static_cast<uint32_t>(field.flags), 4);
    hash = appendHash(hash, field.offset, 8);
    hash = appendHash(hash, field.size, 8);
    hash = appendHash(hash, field.alignment, 4);
    hash = appendHash(hash, 0, 4);
  }
  for (uint32_t continuation : result->continuations)
    hash = appendHash(hash, continuation, 4);
  result->checksum = hash;
  return result;
}

ArrayRef<ProcessFrameValue>
SimulationProcessFrameAnalysis::getContinuationLayout(Block *block) const {
  auto found = continuationLayouts.find(block);
  return found == continuationLayouts.end()
             ? ArrayRef<ProcessFrameValue>{}
             : ArrayRef<ProcessFrameValue>(found->second);
}

ArrayRef<ProcessFrameValue>
SimulationProcessFrameAnalysis::getContinuationLayout(
    uint32_t continuationID) const {
  auto found = continuationLayoutsByID.find(continuationID);
  return found == continuationLayoutsByID.end()
             ? ArrayRef<ProcessFrameValue>{}
             : ArrayRef<ProcessFrameValue>(found->second);
}

const ProcessSuspension *
SimulationProcessFrameAnalysis::getSuspension(Operation *operation) const {
  for (const ProcessSuspension &suspension : suspensions)
    if (suspension.operation == operation)
      return &suspension;
  return nullptr;
}

void populateSimulationCoroutineToLLVMPatterns(
    const LLVMTypeConverter &converter, RewritePatternSet &patterns) {
  populateRuntimeToLLVMPatterns(converter, patterns);
  patterns.add<SimContextRuntimeLowering>(converter, patterns.getContext());
  arith::populateArithToLLVMConversionPatterns(converter, patterns);
  cf::populateControlFlowToLLVMConversionPatterns(converter, patterns);
  populateFuncToLLVMConversionPatterns(converter, patterns);
}

} // namespace obelisk
