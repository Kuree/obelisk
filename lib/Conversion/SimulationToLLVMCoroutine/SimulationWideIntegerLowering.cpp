//===- SimulationWideIntegerLowering.cpp - Word-vector bitwise planes ----===//

#include "SimulationPackedLowering.h"
#include "SimulationToLLVMCoroutinePrivate.h"
#include "talos/Conversion/Passes.h"
#include "talos/Dialect/Native/NativeDialect.h"
#include "talos/Dialect/Schedule/ScheduleFields.h"

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/Matchers.h"
#include "mlir/Pass/AnalysisManager.h"
#include "mlir/Pass/Pass.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Support/Error.h"

using namespace mlir;

namespace talos::detail {
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
    auto optimization = native::get<native::OptimizationLevel>(module);
    if (optimization && optimization.getInt() >= 2) {
      auto limit = schedule::get<schedule::Field::MaxInlineOps>(module);
      inlineOperationLimit = limit ? limit.getUInt() : UINT64_C(5000);
    }
  }
  llvm::DataLayout dataLayout;
  uint64_t inlineOperationLimit = 0;
  bool valid = true;
};

/// Gives temporaries whose every use lies between one lifetime start and end
/// in a single block a shared stack slot per element type and alignment, by
/// a linear scan of each block. Runtime-call buffers otherwise each own a
/// frame slot: a large generated body then has a frame far beyond AArch64
/// immediate offsets, and codegen keeps frame base registers live across
/// every call. Coroutines keep their allocations, which lay out their frames.
static bool shareScopedTemporaries(LLVM::LLVMFuncOp function) {
  if (ArrayAttr passthrough = function.getPassthroughAttr())
    if (llvm::any_of(passthrough, [](Attribute attribute) {
          auto name = dyn_cast<StringAttr>(attribute);
          return name && name.getValue() == "presplitcoroutine";
        }))
      return false;
  DenseMap<Operation *, LLVM::AllocaOp> starts, ends;
  function.walk([&](LLVM::AllocaOp allocation) {
    APInt count;
    if (!matchPattern(allocation.getArraySize(), m_ConstantInt(&count)) ||
        count != 1)
      return;
    Operation *start = nullptr, *end = nullptr;
    bool scoped = true;
    for (Operation *user : allocation->getUsers()) {
      if (isa<LLVM::LifetimeStartOp>(user)) {
        scoped &= !start;
        start = user;
      } else if (isa<LLVM::LifetimeEndOp>(user)) {
        scoped &= !end;
        end = user;
      }
    }
    if (!scoped || !start || !end || start->getBlock() != end->getBlock() ||
        !start->isBeforeInBlock(end))
      return;
    if (llvm::any_of(allocation->getUsers(), [&](Operation *user) {
          return user != start && user != end &&
                 (user->getBlock() != start->getBlock() ||
                  !start->isBeforeInBlock(user) || !user->isBeforeInBlock(end));
        }))
      return;
    starts[start] = allocation;
    ends[end] = allocation;
  });
  if (starts.size() < 2)
    return false;
  using Key = std::pair<Type, uint64_t>;
  auto keyOf = [](LLVM::AllocaOp allocation) {
    return Key(allocation.getElemType(), allocation.getAlignment().value_or(0));
  };
  // Slot indices per key; every scope closes in the block that opened it.
  SmallVector<std::pair<LLVM::AllocaOp, unsigned>> order;
  DenseMap<Operation *, unsigned> assigned;
  for (Block &block : function.getBody()) {
    DenseMap<Key, SmallVector<unsigned>> available;
    DenseMap<Key, unsigned> opened;
    for (Operation &operation : block) {
      if (LLVM::AllocaOp allocation = starts.lookup(&operation)) {
        Key key = keyOf(allocation);
        auto &free = available[key];
        unsigned slot;
        if (!free.empty())
          slot = free.pop_back_val();
        else
          slot = opened[key]++;
        assigned[allocation] = slot;
        order.emplace_back(allocation, slot);
      } else if (LLVM::AllocaOp allocation = ends.lookup(&operation)) {
        available[keyOf(allocation)].push_back(assigned.lookup(allocation));
      }
    }
  }
  // The function entry dominates every scope.
  OpBuilder builder(&function.getBody().front(),
                    function.getBody().front().begin());
  Location location = function.getLoc();
  Type pointer = LLVM::LLVMPointerType::get(function.getContext());
  Value one = LLVM::ConstantOp::create(builder, location, builder.getI64Type(),
                                       builder.getI64IntegerAttr(1));
  DenseMap<std::pair<Key, unsigned>, Value> slots;
  SmallVector<LLVM::AllocaOp> merged;
  // Visit scopes in program order so slot creation is deterministic.
  for (auto [allocation, slot] : order) {
    Key key = keyOf(allocation);
    Value &shared = slots[{key, slot}];
    if (!shared)
      shared = LLVM::AllocaOp::create(builder, location, pointer, key.first,
                                      one, key.second);
    allocation.getResult().replaceAllUsesWith(shared);
    merged.push_back(allocation);
  }
  for (LLVM::AllocaOp allocation : merged)
    allocation.erase();
  return true;
}

struct NativeFunctionFinalizationPass
    : PassWrapper<NativeFunctionFinalizationPass,
                  OperationPass<LLVM::LLVMFuncOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(NativeFunctionFinalizationPass)

  StringRef getArgument() const final {
    return "talos-native-finalize-function";
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
    changed |= shareScopedTemporaries(function);
    SmallVector<LLVM::CallOp> provenOffsets;
    function.walk([&](LLVM::CallOp call) {
      if (native::has<native::InBoundsHandleOffset>(call) && call.getCallee() &&
          *call.getCallee() == "talos_rt_v1_native_handle_offset" &&
          call.getArgOperands().size() == 2)
        provenOffsets.push_back(call);
    });
    for (auto call : provenOffsets) {
      OpBuilder builder(call);
      Value result =
          LLVM::AddOp::create(builder, call.getLoc(), call.getArgOperands()[0],
                              call.getArgOperands()[1]);
      call.getResult().replaceAllUsesWith(result);
      call.erase();
      changed = true;
    }
    // Source ownership has been consumed by scheduling and publication
    // proofs. Drop this dialect attribute from remaining runtime calls before
    // exporting a standalone LLVM module to the upstream LLVM translator.
    function.walk([&](Operation *operation) {
      if (schedule::has<schedule::Field::EvalSourceOwner>(operation)) {
        schedule::remove<schedule::Field::EvalSourceOwner>(operation);
        changed = true;
      }
    });
    // Keep the generated eval loop's hottest call boundaries on an I-cache
    // line regardless of unrelated runtime/string table growth. These bodies
    // are deliberately retained as calls by the large-function policy below;
    // leaving their placement at the target's minimum function alignment
    // makes steady-state throughput depend on incidental section size.
    Builder alignmentBuilder(function.getContext());
    {
      StringRef name = function.getSymName();
      uint64_t alignment = 0;
      if (name.starts_with("__talos_aot_static_nba_commit_two_state"))
        alignment = 128;
      else if (name.starts_with("__talos_fused_") &&
               name.contains("__talos_eval_body_"))
        alignment = 64;
      else if (::talos::schedule::has<
                   ::talos::schedule::Field::EvalCallClosureRoot>(function))
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

} // namespace talos::detail

namespace talos {
#define GEN_PASS_DEF_SHARENATIVESCOPEDTEMPORARIESPASS
#include "talos/Conversion/Passes.h.inc"
namespace {
struct ShareNativeScopedTemporariesPass
    : impl::ShareNativeScopedTemporariesPassBase<
          ShareNativeScopedTemporariesPass> {
  void runOnOperation() override {
    LLVM::LLVMFuncOp function = getOperation();
    if (function.isExternal() || !detail::shareScopedTemporaries(function))
      markAllAnalysesPreserved();
  }
};
} // namespace
} // namespace talos
