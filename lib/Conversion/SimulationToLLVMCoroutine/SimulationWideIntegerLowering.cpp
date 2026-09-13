//===- SimulationWideIntegerLowering.cpp - Word-vector bitwise planes ----===//

#include "SimulationPackedLowering.h"

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/IR/DataLayout.h"

using namespace mlir;

namespace obelisk::detail {

// Very wide LLVM integers cause expensive recursive SelectionDAG integer
// legalization, even for loads, stores and bitwise operations. These closed
// SSA components have no carries or cross-word operations: represent each
// value/unknown plane by words instead. LLVM can select native vector chunks
// directly. The storage footprint and publication boundaries are unchanged.
// This preserves the independent per-bit truth tables of IEEE 1800-2023
// 11.4.8 and full-width equality of 11.4.5, after sizing and X/Z lowering.
void lowerWideNativeBitwiseIntegers(ModuleOp module,
                                    const llvm::DataLayout &dataLayout) {
  if (!dataLayout.isLittleEndian())
    return;
  auto wideType = [](Type type) -> IntegerType {
    auto integer = dyn_cast<IntegerType>(type);
    return integer && integer.getWidth() >= 4096 && integer.getWidth() % 64 == 0
               ? integer
               : IntegerType{};
  };
  auto supported = [](Operation *op) {
    if (isa<LLVM::AndOp, LLVM::OrOp, LLVM::XOrOp, LLVM::SelectOp>(op))
      return true;
    if (auto constant = dyn_cast<LLVM::ConstantOp>(op))
      return isa<IntegerAttr>(constant.getValue());
    if (auto load = dyn_cast<LLVM::LoadOp>(op))
      return !load.getVolatile_() &&
             load.getOrdering() == LLVM::AtomicOrdering::not_atomic;
    if (auto store = dyn_cast<LLVM::StoreOp>(op))
      return !store.getVolatile_() &&
             store.getOrdering() == LLVM::AtomicOrdering::not_atomic;
    if (auto compare = dyn_cast<LLVM::ICmpOp>(op))
      return compare.getPredicate() == LLVM::ICmpPredicate::eq ||
             compare.getPredicate() == LLVM::ICmpPredicate::ne;
    return false;
  };

  SmallVector<Value> seeds;
  module.walk([&](LLVM::LLVMFuncOp function) {
    function.walk([&](Operation *op) {
      for (Value result : op->getResults())
        if (wideType(result.getType()))
          seeds.push_back(result);
    });
  });
  DenseSet<Value> visited;
  for (Value seed : seeds) {
    IntegerType integer = wideType(seed.getType());
    if (!integer || visited.contains(seed))
      continue;
    llvm::SetVector<Value> values;
    llvm::SetVector<Operation *> operations;
    values.insert(seed);
    bool eligible = true;
    for (size_t index = 0; index != values.size(); ++index) {
      Value value = values[index];
      visited.insert(value);
      auto visit = [&](Operation *op) {
        if (!op || !supported(op)) {
          eligible = false;
          return;
        }
        operations.insert(op);
        for (Value operand : op->getOperands())
          if (operand.getType() == integer)
            values.insert(operand);
        for (Value result : op->getResults())
          if (result.getType() == integer)
            values.insert(result);
      };
      visit(value.getDefiningOp());
      for (Operation *user : value.getUsers())
        visit(user);
    }
    // Do not introduce integer/vector bridges for calls, block arguments,
    // arithmetic, shifts, or volatile/atomic accesses. Leave those components
    // intact; in particular this transformation cannot change a function ABI.
    if (!eligible)
      continue;
    int64_t words = integer.getWidth() / 64;
    auto word = IntegerType::get(module.getContext(), 64);
    auto vector = VectorType::get({words}, word);
    for (Operation *op : operations) {
      // An omitted alignment means the integer's ABI alignment, not the
      // (potentially much greater) vector ABI alignment. Materialize it before
      // changing types; never strengthen the original memory contract.
      uint64_t alignment =
          dataLayout.getABIIntegerTypeAlignment(integer.getWidth()).value();
      if (auto load = dyn_cast<LLVM::LoadOp>(op))
        if (!load.getAlignment().value_or(0))
          load.setAlignment(alignment);
      if (auto store = dyn_cast<LLVM::StoreOp>(op))
        if (!store.getAlignment().value_or(0))
          store.setAlignment(alignment);
      if (auto constant = dyn_cast<LLVM::ConstantOp>(op)) {
        const APInt &bits = cast<IntegerAttr>(constant.getValue()).getValue();
        SmallVector<APInt> limbs;
        limbs.reserve(words);
        for (int64_t index = 0; index != words; ++index)
          limbs.push_back(bits.extractBits(64, index * 64));
        constant.setValueAttr(DenseIntElementsAttr::get(vector, limbs));
      }
    }
    for (Value value : values)
      value.setType(vector);
    for (Operation *op : operations) {
      auto compare = dyn_cast<LLVM::ICmpOp>(op);
      if (!compare)
        continue;
      OpBuilder builder(compare);
      auto lanes = LLVM::ICmpOp::create(builder, compare.getLoc(),
                                        compare.getPredicate(),
                                        compare.getLhs(), compare.getRhs());
      Value reduced =
          compare.getPredicate() == LLVM::ICmpPredicate::eq
              ? LLVM::vector_reduce_and::create(builder, compare.getLoc(),
                                                builder.getI1Type(), lanes)
                    .getResult()
              : LLVM::vector_reduce_or::create(builder, compare.getLoc(),
                                               builder.getI1Type(), lanes)
                    .getResult();
      compare.replaceAllUsesWith(reduced);
      compare.erase();
    }
  }
}

} // namespace obelisk::detail
