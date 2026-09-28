//===- SimulationCopyKernelMaterialization.cpp - Copy kernels -----------===//

#include "SimulationProcessCoroutineLowering.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/IR/IRMapping.h"
#include "llvm/ADT/Hashing.h"

using namespace mlir;

namespace obelisk::detail {
namespace {

// Only integer literals become row fields. Everything else, including runtime
// calls, types, flags, symbols, CFG edges and SSA wiring, must match exactly.
IntegerAttr getRowLiteral(Operation &op) {
  if (!isa<LLVM::ConstantOp, arith::ConstantOp>(op))
    return {};
  auto value = dyn_cast_or_null<IntegerAttr>(op.getAttr("value"));
  if (!value || (value.getType().getIntOrFloatBitWidth() != 32 &&
                 value.getType().getIntOrFloatBitWidth() != 64))
    return {};
  return value;
}

struct CopyShape {
  SmallVector<uintptr_t> signature;
  SmallVector<Operation *> literals;
};

// Pointer identities here are uniqued MLIR names/types/attributes, never SSA
// values or block addresses. Exact signature comparison follows hash lookup.
std::optional<CopyShape> getShape(LLVM::LLVMFuncOp function) {
  CopyShape shape;
  auto &key = shape.signature;
  DenseMap<Value, unsigned> values;
  DenseMap<Block *, unsigned> blocks;
  for (Block &block : function.getBody()) {
    blocks[&block] = blocks.size();
    for (Value argument : block.getArguments())
      values[argument] = values.size();
    for (Operation &op : block) {
      if (op.getNumRegions())
        return std::nullopt;
      for (Value result : op.getResults())
        values[result] = values.size();
    }
  }
  key.push_back(blocks.size());
  for (Block &block : function.getBody()) {
    key.push_back(block.getNumArguments());
    for (Value argument : block.getArguments())
      key.push_back(
          reinterpret_cast<uintptr_t>(argument.getType().getAsOpaquePointer()));
    key.push_back(block.getOperations().size());
    for (Operation &op : block) {
      key.push_back(
          reinterpret_cast<uintptr_t>(op.getName().getAsOpaquePointer()));
      NamedAttrList attrs(op.getAttrDictionary());
      if (getRowLiteral(op)) {
        attrs.erase("value");
        shape.literals.push_back(&op);
      }
      key.push_back(reinterpret_cast<uintptr_t>(
          attrs.getDictionary(function.getContext()).getAsOpaquePointer()));
      key.push_back(op.getNumResults());
      for (Value result : op.getResults())
        key.push_back(
            reinterpret_cast<uintptr_t>(result.getType().getAsOpaquePointer()));
      key.push_back(op.getNumOperands());
      for (Value operand : op.getOperands()) {
        auto found = values.find(operand);
        if (found == values.end())
          return std::nullopt;
        key.push_back(found->second);
      }
      key.push_back(op.getNumSuccessors());
      for (Block *successor : op.getSuccessors()) {
        auto found = blocks.find(successor);
        if (found == blocks.end())
          return std::nullopt;
        key.push_back(found->second);
      }
    }
  }
  return shape;
}

struct CopyGroup {
  SmallVector<uintptr_t> signature;
  SmallVector<PreparedSuspendableProcess *> processes;
  SmallVector<SmallVector<Operation *>> literals;
};

void materializeGroup(ModuleOp module, CopyGroup &group, unsigned ordinal) {
  OpBuilder builder(module.getContext());
  Location location = group.processes.front()->location;
  auto pointer = LLVM::LLVMPointerType::get(module.getContext());
  Type i64 = builder.getI64Type();

  // Uniform constants stay immediate. Identical varying columns share a load,
  // e.g. repeated source offsets or continuation IDs in the same activation.
  SmallVector<SmallVector<uint64_t>> columns;
  SmallVector<std::pair<unsigned, unsigned>> replacements;
  for (unsigned literal = 0; literal < group.literals.front().size();
       ++literal) {
    SmallVector<uint64_t> column;
    for (auto &row : group.literals)
      column.push_back(getRowLiteral(*row[literal]).getValue().getZExtValue());
    if (llvm::all_equal(column))
      continue;
    auto found = llvm::find(columns, column);
    unsigned index = std::distance(columns.begin(), found);
    if (found == columns.end())
      columns.push_back(std::move(column));
    replacements.emplace_back(literal, index);
  }

  std::string name = "__obelisk_copy_kernel_" + std::to_string(ordinal);
  LLVM::GlobalOp table;
  if (!columns.empty()) {
    builder.setInsertionPointToEnd(module.getBody());
    auto array =
        LLVM::LLVMArrayType::get(i64, group.processes.size() * columns.size());
    table = LLVM::GlobalOp::create(builder, location, array, true,
                                   LLVM::Linkage::Internal, name + ".rows",
                                   Attribute{}, 8);
    table.getInitializerRegion().push_back(new Block);
    builder.setInsertionPointToStart(&table.getInitializerRegion().front());
    Value data = LLVM::ZeroOp::create(builder, location, array);
    for (unsigned row = 0; row < group.processes.size(); ++row)
      for (auto [index, column] : llvm::enumerate(columns)) {
        Value value = llvmConstant(builder, location, i64, column[row]);
        data = LLVM::InsertValueOp::create(
            builder, location, data, value,
            ArrayRef<int64_t>{int64_t(row * columns.size() + index)});
      }
    LLVM::ReturnOp::create(builder, location, data);
  }

  builder.setInsertionPointToEnd(module.getBody());
  auto prototype = group.processes.front()->ramp;
  auto kernel = LLVM::LLVMFuncOp::create(
      builder, location, name,
      LLVM::LLVMFunctionType::get(LLVM::LLVMVoidType::get(module.getContext()),
                                  {pointer, pointer}, false));
  // Keep one body across native partitions and prevent per-actor inlining
  // during LLVM optimization. Ownership defaults to primary.
  kernel->setAttr("passthrough",
                  builder.getArrayAttr({builder.getStringAttr("noinline")}));
  IRMapping mapping;
  prototype.getBody().cloneInto(&kernel.getBody(), mapping);
  Block &entry = kernel.getBody().front();
  builder.setInsertionPointToStart(&entry);
  // Requirements already use the common zero-scratch callback. The shared
  // kernel is only entered for execution, with the selected instance and row.
  entry.getArgument(1).replaceAllUsesWith(
      llvmConstant(builder, location, builder.getI32Type(), 1));
  Value null = LLVM::ZeroOp::create(builder, location, pointer);
  entry.getArgument(2).replaceAllUsesWith(null);
  entry.getArgument(3).replaceAllUsesWith(null);
  entry.eraseArguments(1, 3);
  Value row = entry.addArgument(pointer, location);
  builder.setInsertionPointToStart(&entry);
  SmallVector<Value> loaded;
  for (unsigned column = 0; column < columns.size(); ++column) {
    Value offset = llvmConstant(builder, location, i64, column);
    Value address = LLVM::GEPOp::create(builder, location, pointer, i64, row,
                                        ValueRange{offset});
    loaded.push_back(LLVM::LoadOp::create(builder, location, i64, address, 8));
  }
  for (auto [literal, column] : replacements) {
    Operation *old = mapping.lookup(group.literals.front()[literal]);
    Value value = loaded[column];
    Type type = old->getResult(0).getType();
    if (type != i64)
      value = LLVM::TruncOp::create(builder, location, type, value);
    old->getResult(0).replaceAllUsesWith(value);
    old->erase();
  }
  for (auto [rowIndex, process] : llvm::enumerate(group.processes))
    process->copyKernel = {kernel, table, unsigned(rowIndex),
                           unsigned(columns.size())};
}
} // namespace

void materializeCopyKernels(
    ModuleOp module, MutableArrayRef<PreparedSuspendableProcess> processes) {
  SmallVector<CopyGroup> groups;
  DenseMap<size_t, SmallVector<unsigned>> buckets;
  for (PreparedSuspendableProcess &process : processes) {
    if (!process.copyActivation || !process.directActivation)
      continue;
    auto shape = getShape(process.ramp);
    if (!shape)
      continue;
    size_t hash = llvm::hash_combine_range(shape->signature.begin(),
                                           shape->signature.end());
    auto &bucket = buckets[hash];
    auto found = llvm::find_if(bucket, [&](unsigned index) {
      return groups[index].signature == shape->signature;
    });
    unsigned index;
    if (found == bucket.end()) {
      index = groups.size();
      bucket.push_back(index);
      groups.push_back({std::move(shape->signature), {}, {}});
    } else {
      index = *found;
    }
    groups[index].processes.push_back(&process);
    groups[index].literals.push_back(std::move(shape->literals));
  }
  unsigned ordinal = 0;
  for (CopyGroup &group : groups)
    if (group.processes.size() > 1)
      materializeGroup(module, group, ordinal++);
}
} // namespace obelisk::detail
