//===- SimulationToLLVMCoroutineUtils.cpp - Shared lowering support -----===//

#include "SimulationToLLVMCoroutinePrivate.h"

#include "obelisk/Analysis/SimulationAnalysis.h"
#include "obelisk/Dialect/Runtime/RuntimeOps.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/MathExtras.h"

#include <cassert>
#include <limits>

using namespace mlir;

namespace obelisk::detail {

LLVM::LLVMStructType getNativeSchedulePlanLLVMType(MLIRContext *context) {
  OpBuilder builder(context);
  Type pointer = LLVM::LLVMPointerType::get(context);
  Type i32 = builder.getI32Type();
  Type i64 = builder.getI64Type();
  SmallVector<Type> fields{
      i32,     i64,     pointer, i64,     i32,     i32,     pointer, pointer,
      i64,     pointer, pointer, pointer, pointer, i32,     i32,     pointer,
      i64,     pointer, i64,     pointer, i64,     pointer, pointer, pointer,
      i32,     i32,     pointer, i32,     i32,     pointer, i32,     i32,
      pointer, i64,     pointer, pointer, pointer};
  assert(fields.size() == static_cast<size_t>(NativeSchedulePlanField::Count));
  return LLVM::LLVMStructType::getLiteral(context, fields);
}

std::optional<uint64_t> resolveCFGConstantInteger(Value value,
                                                  DenseSet<Value> &active) {
  if (auto constant = value.getDefiningOp<arith::ConstantOp>())
    if (auto integer = dyn_cast<IntegerAttr>(constant.getValue()))
      return integer.getValue().getZExtValue();
  auto argument = dyn_cast<BlockArgument>(value);
  if (!argument || !active.insert(value).second)
    return std::nullopt;
  std::optional<uint64_t> resolved;
  Block *block = argument.getOwner();
  for (Block *predecessor : block->getPredecessors()) {
    Operation *terminator = predecessor->getTerminator();
    auto branch = dyn_cast<BranchOpInterface>(terminator);
    if (!branch) {
      active.erase(value);
      return std::nullopt;
    }
    for (unsigned successor = 0; successor != terminator->getNumSuccessors();
         ++successor) {
      if (terminator->getSuccessor(successor) != block)
        continue;
      SuccessorOperands operands = branch.getSuccessorOperands(successor);
      unsigned index = argument.getArgNumber();
      if (index >= operands.size() || operands.isOperandProduced(index)) {
        active.erase(value);
        return std::nullopt;
      }
      std::optional<uint64_t> incoming =
          resolveCFGConstantInteger(operands[index], active);
      if (!incoming || (resolved && *resolved != *incoming)) {
        active.erase(value);
        return std::nullopt;
      }
      resolved = incoming;
    }
  }
  active.erase(value);
  return resolved;
}

std::optional<uint64_t> resolveCFGConstantInteger(Value value) {
  DenseSet<Value> active;
  return resolveCFGConstantInteger(value, active);
}
bool alignUp(uint64_t value, uint64_t alignment, uint64_t &result) {
  if (value > std::numeric_limits<uint64_t>::max() - (alignment - 1))
    return false;
  result = llvm::alignTo(value, alignment);
  return true;
}

bool containsLogic(Type type) { return analysis::containsFourStateLogic(type); }

std::optional<unsigned> nativeStateWidth(Type type) {
  return analysis::getSimulationStorageBitWidth(type);
}

Type convertProcessType(Type type, MLIRContext *context) {
  if (isa<sim::ContextType, runtime::ContextType,
          runtime::ProcessDescriptorType, runtime::ProcessInstanceType>(type))
    return LLVM::LLVMPointerType::get(context);
  if (sim::isSimulationHandleType(type) || sim::isManagedHandleType(type))
    return IntegerType::get(context, sim::simulationHandleBitWidth);
  if (isa<sim::ArgumentRefType>(type))
    return IntegerType::get(context, 192);
  if (isa<sim::TimeType>(type))
    return IntegerType::get(context, 64);
  return type;
}

SmallVector<Value> flatten(ArrayRef<ValueRange> ranges) {
  SmallVector<Value> values;
  for (ValueRange range : ranges)
    llvm::append_range(values, range);
  return values;
}

Value llvmConstant(OpBuilder &builder, Location location, Type type,
                   uint64_t value) {
  return LLVM::ConstantOp::create(builder, location, type,
                                  builder.getIntegerAttr(type, value));
}

Value entryAlloca(OpBuilder &builder, Location location, Type elementType,
                  uint64_t count, unsigned alignment) {
  Block *insertionBlock = builder.getInsertionBlock();
  assert(insertionBlock && insertionBlock->getParent() &&
         !insertionBlock->getParent()->empty() &&
         "entry allocation requires a function body");
  OpBuilder::InsertionGuard guard(builder);
  builder.setInsertionPointToStart(&insertionBlock->getParent()->front());
  Type pointer = LLVM::LLVMPointerType::get(builder.getContext());
  Value countValue =
      llvmConstant(builder, location, builder.getI64Type(), count);
  return LLVM::AllocaOp::create(builder, location, pointer, elementType,
                                countValue, alignment);
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

Value castIntegerWidth(OpBuilder &builder, Location location, Value value,
                       Type target) {
  auto source = cast<IntegerType>(value.getType());
  auto destination = cast<IntegerType>(target);
  if (source.getWidth() == destination.getWidth())
    return value;
  if (source.getWidth() < destination.getWidth())
    return arith::ExtUIOp::create(builder, location, target, value);
  return arith::TruncIOp::create(builder, location, target, value);
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

Value resizeNativeInteger(OpBuilder &builder, Location location, Value value,
                          IntegerType result, bool isSigned) {
  auto input = cast<IntegerType>(value.getType());
  if (input == result)
    return value;
  if (input.getWidth() < result.getWidth()) {
    if (isSigned)
      return arith::ExtSIOp::create(builder, location, result, value);
    return arith::ExtUIOp::create(builder, location, result, value);
  }
  return arith::TruncIOp::create(builder, location, result, value);
}

SignedI64Index resizeSignedIndexToI64(OpBuilder &builder, Location location,
                                      Value source) {
  IntegerType i64 = builder.getI64Type();
  auto sourceType = cast<IntegerType>(source.getType());
  Value value = resizeNativeInteger(builder, location, source, i64, true);
  Value representable = arith::ConstantOp::create(
      builder, location, builder.getI1Type(), builder.getBoolAttr(true));
  if (sourceType.getWidth() > i64.getWidth()) {
    Value roundTripped =
        resizeNativeInteger(builder, location, value, sourceType, true);
    representable = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, source, roundTripped);
  }
  return {value, representable};
}

Value insertValue(OpBuilder &builder, Location location, Value aggregate,
                  Value element, int64_t index) {
  return LLVM::InsertValueOp::create(builder, location, aggregate, element,
                                     ArrayRef<int64_t>{index});
}

Value insertValue(OpBuilder &builder, Location location, Value aggregate,
                  Value element, NativeSchedulePlanField field) {
  return insertValue(builder, location, aggregate, element,
                     static_cast<int64_t>(field));
}

void emitNativeStateRetain(OpBuilder &builder, Location location,
                           Value handle) {
  Type pointer = LLVM::LLVMPointerType::get(builder.getContext());
  Type i32 = builder.getI32Type();
  Value contextAddress = LLVM::AddressOfOp::create(builder, location, pointer,
                                                   "__obelisk_current_context");
  Value context =
      LLVM::LoadOp::create(builder, location, pointer, contextAddress, 8);
  Value status = LLVM::CallOp::create(
                     builder, location, TypeRange{i32},
                     SymbolRefAttr::get(builder.getContext(),
                                        "obelisk_rt_v1_native_state_retain"),
                     ValueRange{context, handle})
                     .getResult();
  LLVM::CallOp::create(
      builder, location, TypeRange{},
      SymbolRefAttr::get(builder.getContext(), "obelisk_rt_v1_scheduler_fail"),
      ValueRange{context, status});
}

std::string managedClassDescriptorName(SymbolRefAttr className) {
  return (className.getRootReference().getValue() +
          ".__obelisk_class_descriptor")
      .str();
}

std::string managedMethodThunkName(StringRef methodName) {
  return (methodName + ".__obelisk_native_thunk").str();
}

LLVM::GlobalOp makeByteArrayGlobal(ModuleOp module, Location location,
                                   StringRef name, StringRef bytes) {
  MLIRContext *context = module.getContext();
  Type i8 = IntegerType::get(context, 8);
  Type type = LLVM::LLVMArrayType::get(i8, bytes.size());
  OpBuilder builder(context);
  builder.setInsertionPointToStart(module.getBody());
  // LLVM globals accept a StringAttr as the direct initializer for an i8
  // array. Emitting one insertvalue per byte makes construction proportional
  // to every character in every class debug name and creates IR which the
  // translator immediately folds back into the same byte string.
  return LLVM::GlobalOp::create(builder, location, type, true,
                                LLVM::Linkage::Internal, name,
                                builder.getStringAttr(bytes), 1);
}

LLVM::GlobalOp
makeConstantGlobal(ModuleOp module, Location location, Type type,
                   StringRef name, LLVM::Linkage linkage, uint64_t alignment,
                   llvm::function_ref<Value(OpBuilder &)> initializer) {
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

LLVM::LLVMFuncOp getOrDeclareLLVMFunction(ModuleOp module, StringRef name,
                                          Type result,
                                          ArrayRef<Type> arguments) {
  if (auto existing = module.lookupSymbol<LLVM::LLVMFuncOp>(name))
    return existing;
  OpBuilder builder(module.getContext());
  builder.setInsertionPointToStart(module.getBody());
  return LLVM::LLVMFuncOp::create(
      builder, module.getLoc(), name,
      LLVM::LLVMFunctionType::get(result, arguments, false));
}

void copyNativePartition(Operation *source, Operation *target) {
  if (Attribute partition = source->getAttr(sim::metadata::nativePartition))
    target->setAttr(sim::metadata::nativePartition, partition);
}

} // namespace obelisk::detail
