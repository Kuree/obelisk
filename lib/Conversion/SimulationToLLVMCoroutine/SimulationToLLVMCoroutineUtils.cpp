//===- SimulationToLLVMCoroutineUtils.cpp - Shared lowering support -----===//

#include "SimulationToLLVMCoroutinePrivate.h"

#include "obelisk/Analysis/SimulationAnalysis.h"
#include "obelisk/Dialect/Runtime/RuntimeOps.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Runtime/StableHandle.h"

#include "mlir/Analysis/DataFlowFramework.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/IR/Matchers.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/LLVMContext.h"
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
  SmallVector<Type> fields{i32,     i64,     pointer, i64,     i32,     i32,
                           pointer, pointer, i64,     pointer, pointer, pointer,
                           pointer, i32,     i32,     pointer, i64,     pointer,
                           i64,     pointer, i64,     pointer, pointer, pointer,
                           i32,     i32,     pointer, i32,     i32,     pointer,
                           i32,     i32,     pointer, i64,     pointer, pointer,
                           pointer, pointer, pointer, pointer, pointer};
  assert(fields.size() == static_cast<size_t>(NativeSchedulePlanField::Count));
  return LLVM::LLVMStructType::getLiteral(context, fields);
}

uint64_t getNativeSchedulePlanSize(const llvm::DataLayout &dataLayout) {
  llvm::LLVMContext context;
  llvm::Type *pointer = llvm::PointerType::get(context, 0);
  llvm::Type *i32 = llvm::Type::getInt32Ty(context);
  llvm::Type *i64 = llvm::Type::getInt64Ty(context);
  SmallVector<llvm::Type *> fields{
      i32,     i64,     pointer, i64,     i32,     i32,     pointer,
      pointer, i64,     pointer, pointer, pointer, pointer, i32,
      i32,     pointer, i64,     pointer, i64,     pointer, i64,
      pointer, pointer, pointer, i32,     i32,     pointer, i32,
      i32,     pointer, i32,     i32,     pointer, i64,     pointer,
      pointer, pointer, pointer, pointer, pointer, pointer};
  auto *type = llvm::StructType::get(context, fields);
  return dataLayout.getTypeAllocSize(type).getFixedValue();
}

namespace {
// A sparse lattice on the queried SSA/CFG slice: bottom -> constant -> unknown.
// Dependencies are registered with MLIR's solver. Diamonds and cycles share
// their lattice state instead of recursively enumerating predecessor paths.
class HandleConstantState final : public AnalysisState {
public:
  using AnalysisState::AnalysisState;
  bool initialized = false;
  std::optional<uint64_t> constant;
  ChangeResult join(std::optional<uint64_t> incoming) {
    if (!initialized) {
      initialized = true;
      constant = incoming;
      return ChangeResult::Change;
    }
    if (constant && constant != incoming) {
      constant.reset();
      return ChangeResult::Change;
    }
    return ChangeResult::NoChange;
  }
  void print(raw_ostream &os) const override {
    if (constant)
      os << *constant;
    else
      os << "unknown";
  }
};
class HandleConstantDataflow final : public DataFlowAnalysis {
public:
  HandleConstantDataflow(DataFlowSolver &solver, Value query)
      : DataFlowAnalysis(solver), query(query) {}
  LogicalResult initialize(Operation *) override {
    SmallVector<Value> worklist{query};
    DenseSet<Value> seen;
    while (!worklist.empty()) {
      Value value = worklist.pop_back_val();
      if (!seen.insert(value).second)
        continue;
      auto &node = nodes[value];
      APInt literal;
      if (matchPattern(value, m_ConstantInt(&literal)) &&
          literal.getBitWidth() <= 64) {
        node.local = literal.getZExtValue();
        node.leaf = true;
      } else if (auto argument = dyn_cast<BlockArgument>(value)) {
        Block *block = argument.getOwner();
        node.leaf = block->hasNoPredecessors();
        for (Block *predecessor : block->getPredecessors()) {
          Operation *terminator = predecessor->getTerminator();
          auto branch = dyn_cast<BranchOpInterface>(terminator);
          if (!branch) {
            node.leaf = true;
            break;
          }
          for (unsigned successor = 0;
               successor != terminator->getNumSuccessors(); ++successor) {
            if (terminator->getSuccessor(successor) != block)
              continue;
            auto operands = branch.getSuccessorOperands(successor);
            unsigned index = argument.getArgNumber();
            if (index >= operands.size() || operands.isOperandProduced(index)) {
              node.leaf = true;
              break;
            }
            node.incoming.push_back(operands[index]);
          }
          if (node.leaf)
            break;
        }
        if (node.leaf)
          node.incoming.clear();
        else
          llvm::append_range(worklist, node.incoming);
      } else {
        node.leaf = true;
      }
      ProgramPoint *point =
          value.getDefiningOp()
              ? getProgramPointAfter(value.getDefiningOp())
              : getProgramPointBefore(cast<BlockArgument>(value).getOwner());
      points[point].push_back(value);
    }
    for (auto &entry : points)
      if (failed(visit(entry.first)))
        return failure();
    return success();
  }
  LogicalResult visit(ProgramPoint *point) override {
    for (Value value : points.lookup(point)) {
      const auto &node = nodes.find(value)->second;
      bool initialized = node.leaf;
      auto constant = node.local;
      for (Value incoming : node.incoming) {
        auto *state = getOrCreateFor<HandleConstantState>(point, incoming);
        if (!state->initialized)
          continue;
        if (!initialized) {
          initialized = true;
          constant = state->constant;
        } else if (constant != state->constant)
          constant.reset();
      }
      if (initialized) {
        auto *state = getOrCreate<HandleConstantState>(value);
        propagateIfChanged(state, state->join(constant));
      }
    }
    return success();
  }

private:
  struct Node {
    bool leaf = false;
    std::optional<uint64_t> local;
    SmallVector<Value> incoming;
  };
  Value query;
  DenseMap<Value, Node> nodes;
  DenseMap<ProgramPoint *, SmallVector<Value>> points;
};
} // namespace
std::optional<uint64_t> resolveCFGConstantInteger(Value value) {
  DataFlowSolver solver;
  solver.load<HandleConstantDataflow>(value);
  Operation *owner = value.getDefiningOp();
  if (!owner)
    owner = cast<BlockArgument>(value).getOwner()->getParentOp();
  if (failed(solver.initializeAndRun(owner)))
    return std::nullopt;
  auto *state = solver.lookupState<HandleConstantState>(value);
  return state && state->initialized ? state->constant : std::nullopt;
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

Value elementGEP(OpBuilder &builder, Location location, Value base,
                 Type elementType, uint64_t index) {
  Type pointer = LLVM::LLVMPointerType::get(builder.getContext());
  SmallVector<LLVM::GEPArg> indices;
  if (index <= INT32_MAX)
    indices.emplace_back(static_cast<int32_t>(index));
  else
    indices.emplace_back(
        llvmConstant(builder, location, builder.getI64Type(), index));
  return LLVM::GEPOp::create(builder, location, pointer, elementType, base,
                             indices);
}

Value fieldGEP(OpBuilder &builder, Location location, Value base,
               Type structure, uint32_t field) {
  Type pointer = LLVM::LLVMPointerType::get(builder.getContext());
  SmallVector<LLVM::GEPArg> indices{LLVM::GEPArg(0),
                                    LLVM::GEPArg(static_cast<int32_t>(field))};
  return LLVM::GEPOp::create(builder, location, pointer, structure, base,
                             indices);
}

namespace {

Type processInstanceType(MLIRContext *context) {
  Type pointer = LLVM::LLVMPointerType::get(context);
  Type i32 = IntegerType::get(context, 32);
  Type i64 = IntegerType::get(context, 64);
  return LLVM::LLVMStructType::getLiteral(
      context, {pointer, pointer, pointer, i64, i64, i64, pointer, i32, i32,
                i32, i32, pointer, pointer, pointer, i32, i32});
}

Type fragmentActionType(MLIRContext *context) {
  Type i32 = IntegerType::get(context, 32);
  Type i64 = IntegerType::get(context, 64);
  return LLVM::LLVMStructType::getLiteral(context,
                                          {i32, i32, i32, i32, i64, i64});
}

} // namespace

Value loadAt(OpBuilder &builder, Location location, Value base,
             ProcessInstanceField field, Type type, unsigned alignment) {
  Value address = fieldGEP(builder, location, base,
                           processInstanceType(builder.getContext()),
                           static_cast<uint32_t>(field));
  return LLVM::LoadOp::create(builder, location, type, address, alignment);
}

void storeAt(OpBuilder &builder, Location location, Value base,
             ProcessInstanceField field, Value value, unsigned alignment) {
  LLVM::StoreOp::create(builder, location, value,
                        fieldGEP(builder, location, base,
                                 processInstanceType(builder.getContext()),
                                 static_cast<uint32_t>(field)),
                        alignment);
}

Value loadAt(OpBuilder &builder, Location location, Value base,
             FragmentActionField field, Type type, unsigned alignment) {
  Value address = fieldGEP(builder, location, base,
                           fragmentActionType(builder.getContext()),
                           static_cast<uint32_t>(field));
  return LLVM::LoadOp::create(builder, location, type, address, alignment);
}

void storeAt(OpBuilder &builder, Location location, Value base,
             FragmentActionField field, Value value, unsigned alignment) {
  LLVM::StoreOp::create(builder, location, value,
                        fieldGEP(builder, location, base,
                                 fragmentActionType(builder.getContext()),
                                 static_cast<uint32_t>(field)),
                        alignment);
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
  // Native entries run with a live context. For a constant global/static
  // handle, retain only validates its encoding and returns OK: these objects
  // live for the design lifetime and have no automatic reference count.
  // Use the runtime decoder, preserving checks for malformed encodings and
  // all automatic or unresolved handles. Do not infer lifetime from knownness.
  APInt constant;
  if (matchPattern(handle, m_ConstantInt(&constant)) &&
      constant.getBitWidth() == 64) {
    obelisk_rt_stable_handle_v1 decoded;
    if (obelisk_rt_stable_handle_decode(constant.getZExtValue(), &decoded) &&
        (decoded.kind == OBELISK_RT_STABLE_HANDLE_GLOBAL ||
         decoded.kind == OBELISK_RT_STABLE_HANDLE_STATIC))
      return;
  }
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
