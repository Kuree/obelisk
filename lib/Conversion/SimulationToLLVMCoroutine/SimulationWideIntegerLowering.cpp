//===- SimulationWideIntegerLowering.cpp - Word-vector bitwise planes ----===//

#include "SimulationPackedLowering.h"
#include "obelisk/Dialect/Schedule/ScheduleFields.h"

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Pass/AnalysisManager.h"
#include "mlir/Pass/Pass.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Support/Error.h"

using namespace mlir;

namespace obelisk::detail {
namespace {

// Very wide LLVM integers cause expensive recursive SelectionDAG integer
// legalization, even for loads, stores and bitwise operations. These closed
// SSA components have no carries or cross-word operations: represent each
// value/unknown plane by words instead. LLVM can select native vector chunks
// directly. The storage footprint and publication boundaries are unchanged.
// This preserves the independent per-bit truth tables of IEEE 1800-2023
// 11.4.8 and full-width equality of 11.4.5, after sizing and X/Z lowering.
bool lowerWideNativeBitwiseIntegers(LLVM::LLVMFuncOp function,
                                    const llvm::DataLayout &dataLayout) {
  if (!dataLayout.isLittleEndian())
    return false;
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
  function.walk([&](Operation *op) {
    for (Value result : op->getResults())
      if (wideType(result.getType()))
        seeds.push_back(result);
  });
  DenseSet<Value> visited;
  bool changed = false;
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
    changed = true;
    int64_t words = integer.getWidth() / 64;
    auto word = IntegerType::get(function.getContext(), 64);
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
  return changed;
}

struct NativeFunctionFinalizationInputs {
  explicit NativeFunctionFinalizationInputs(Operation *operation)
      : dataLayout("") {
    auto module = cast<ModuleOp>(operation);
    auto layout = module->getAttrOfType<StringAttr>("llvm.data_layout");
    auto parsed = llvm::DataLayout::parse(layout ? layout.getValue() : "");
    if (!parsed) {
      module.emitError() << "invalid native finalization data layout: "
                         << llvm::toString(parsed.takeError());
      valid = false;
      return;
    }
    dataLayout = std::move(*parsed);
    auto optimization =
        module->getAttrOfType<IntegerAttr>("obelisk.native.optimization_level");
    if (optimization && optimization.getInt() >= 2) {
      auto limit = schedule::get<schedule::Field::MaxInlineOps>(module);
      inlineOperationLimit = limit ? limit.getUInt() : UINT64_C(5000);
    }
  }
  llvm::DataLayout dataLayout;
  uint64_t inlineOperationLimit = 0;
  bool valid = true;
};

struct NativeFunctionFinalizationPass
    : PassWrapper<NativeFunctionFinalizationPass,
                  OperationPass<LLVM::LLVMFuncOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(NativeFunctionFinalizationPass)

  StringRef getArgument() const final {
    return "obelisk-native-finalize-function";
  }
  StringRef getDescription() const final {
    return "Legalize wide bitwise planes and finalize native function policy";
  }

  void runOnOperation() override {
    auto function = getOperation();
    if (function.isExternal()) {
      markAllAnalysesPreserved();
      return;
    }
    auto inputs = getCachedParentAnalysis<NativeFunctionFinalizationInputs>(
        function->getParentOfType<ModuleOp>());
    if (!inputs || !inputs->get().valid) {
      function.emitError("native finalization requires cached target inputs");
      return signalPassFailure();
    }
    const auto &configuration = inputs->get();
    uint64_t inlineOperationLimit = configuration.inlineOperationLimit;
    bool changed =
        lowerWideNativeBitwiseIntegers(function, configuration.dataLayout);
    // Keep the generated eval loop's hottest call boundaries on an I-cache
    // line regardless of unrelated runtime/string table growth. These bodies
    // are deliberately retained as calls by the large-function policy below;
    // leaving their placement at the target's minimum function alignment
    // makes steady-state throughput depend on incidental section size.
    Builder alignmentBuilder(function.getContext());
    {
      StringRef name = function.getSymName();
      uint64_t alignment = 0;
      if (name.starts_with("__obelisk_aot_static_nba_commit_two_state"))
        alignment = 128;
      else if (name.starts_with("__obelisk_fused_") &&
               name.contains("__obelisk_eval_body_"))
        alignment = 64;
      else if (::obelisk::schedule::has<
                   ::obelisk::schedule::Field::EvalCallClosureRoot>(function))
        alignment = 64;
      if (alignment) {
        auto desired = alignmentBuilder.getI64IntegerAttr(alignment);
        changed |= function.getAlignmentAttr() != desired;
        function.setAlignmentAttr(desired);
      }
    }

    // ThinLTO may otherwise import a mechanically expanded helper into many
    // shards and optimize the same large body repeatedly.  Keep full local
    // optimization enabled, but make sufficiently large definitions a hard
    // call boundary.  This avoids the old optnone tradeoff: every body still
    // receives the requested O2/O3 pipeline exactly once.
    if (inlineOperationLimit) {
      uint64_t operations = 0;
      function.walk([&](Operation *operation) {
        operations += operation != function.getOperation();
        return operations > inlineOperationLimit ? WalkResult::interrupt()
                                                 : WalkResult::advance();
      });
      if (operations <= inlineOperationLimit) {
        if (!changed)
          markAllAnalysesPreserved();
        return;
      }
      changed |= function.getAlwaysInline() || function.getInlineHint() ||
                 !function.getNoInline();
      function.setAlwaysInline(false);
      function.setInlineHint(false);
      function.setNoInline(true);
      SmallVector<Attribute> retained;
      if (ArrayAttr passthrough = function.getPassthroughAttr()) {
        for (Attribute attribute : passthrough) {
          StringAttr name = dyn_cast<StringAttr>(attribute);
          if (!name)
            if (auto pair = dyn_cast<ArrayAttr>(attribute);
                pair && !pair.empty())
              name = dyn_cast<StringAttr>(pair.getValue()[0]);
          if (name && name.getValue() == "alwaysinline")
            continue;
          retained.push_back(attribute);
        }
      }
      auto passthrough = retained.empty()
                             ? ArrayAttr{}
                             : ArrayAttr::get(function.getContext(), retained);
      changed |= function.getPassthroughAttr() != passthrough;
      if (!passthrough)
        function->removeAttr("passthrough");
      else
        function.setPassthroughAttr(passthrough);
    }
    if (!changed)
      markAllAnalysesPreserved();
  }
};
} // namespace

LogicalResult prepareNativeFunctionFinalizationInputs(ModuleOp module,
                                                      AnalysisManager manager) {
  return success(manager.getAnalysis<NativeFunctionFinalizationInputs>().valid);
}

std::unique_ptr<Pass> createNativeFunctionFinalizationPass() {
  return std::make_unique<NativeFunctionFinalizationPass>();
}

} // namespace obelisk::detail
