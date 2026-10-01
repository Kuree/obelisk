//===- SimulationNativeGroupMaterialization.cpp - Native group bodies ----===//

#include "NativeSymbolUses.h"
#include "SimulationAOTPlanning.h"
#include "SimulationEvalReadySet.h"
#include "SimulationToLLVMCoroutinePrivate.h"
#include "obelisk/Dialect/Schedule/ScheduleEnums.h"
#include "obelisk/Dialect/Schedule/ScheduleFields.h"
#include "obelisk/Dialect/Schedule/ScheduleMetadata.h"

#include "obelisk/Conversion/Passes.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"

#include "mlir/Analysis/CallGraph.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/LLVMIR/Transforms/InlinerInterfaceImpl.h"
#include "mlir/IR/Dominance.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/Matchers.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/IR/Threading.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Transforms/InliningUtils.h"
#include "mlir/Transforms/Mem2Reg.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/raw_ostream.h"

#include <map>

using namespace mlir;

namespace obelisk::detail {
namespace {

std::optional<uint64_t> staticByteOffset(Value pointer) {
  uint64_t bytes = 0;
  while (auto gep = pointer.getDefiningOp<LLVM::GEPOp>()) {
    if (!gep.getElemType().isInteger(8) || gep.getIndices().size() != 1)
      return std::nullopt;
    APInt constant;
    auto index = gep.getIndices()[0];
    if (auto integer = dyn_cast<IntegerAttr>(index))
      constant = integer.getValue();
    else if (!matchPattern(cast<Value>(index), m_ConstantInt(&constant)))
      return std::nullopt;
    if (constant.isNegative() || constant.getActiveBits() > 64 ||
        constant.getZExtValue() > UINT64_MAX - bytes)
      return std::nullopt;
    bytes += constant.getZExtValue();
    pointer = gep.getBase();
  }
  return bytes;
}

/// Keep the ready words in SSA between executor boundaries. This is a local
/// representation of the original pending activations, not a second queue.
/// Retained calls may publish, observe, or reenter: materialize before them
/// and acquire their result afterward. Returns publish even on rejection or
/// error, so a handoff never restarts completed work or loses pending work.
LogicalResult promoteGroupReadyWords(LLVM::LLVMFuncOp function,
                                     const llvm::StringMap<Type> &globalTypes,
                                     uint64_t &budget) {
  auto reference =
      ::obelisk::schedule::get<::obelisk::schedule::Field::EvalGroupIngress>(
          function);
  auto array = reference ? dyn_cast_or_null<LLVM::LLVMArrayType>(
                               globalTypes.lookup(reference.getValue()))
                         : LLVM::LLVMArrayType{};
  if (!array || !array.getElementType().isInteger(64))
    return success();

  std::map<uint64_t, SmallVector<Operation *>> accesses;
  SmallVector<Operation *> boundaries;
  SmallVector<Value> pointers;
  llvm::DenseSet<Value> visited;
  bool supported = true;
  function.walk([&](Operation *operation) {
    if (auto address = dyn_cast<LLVM::AddressOfOp>(operation);
        address && address.getGlobalName() == reference.getValue())
      pointers.push_back(address.getResult());
    if (isa<LLVM::CallOp, LLVM::ReturnOp>(operation))
      boundaries.push_back(operation);
    if (isa<LLVM::InvokeOp, LLVM::InlineAsmOp>(operation))
      supported = false;
  });
  while (!pointers.empty() && supported) {
    Value pointer = pointers.pop_back_val();
    if (!visited.insert(pointer).second)
      continue;
    for (OpOperand &use : pointer.getUses()) {
      Operation *operation = use.getOwner();
      if (auto gep = dyn_cast<LLVM::GEPOp>(operation)) {
        pointers.push_back(gep.getResult());
        continue;
      }
      auto load = dyn_cast<LLVM::LoadOp>(operation);
      auto store = dyn_cast<LLVM::StoreOp>(operation);
      bool plainLoad = load && load.getType().isInteger(64) &&
                       !load.getVolatile_() &&
                       load.getOrdering() == LLVM::AtomicOrdering::not_atomic;
      bool plainStore = store && store.getAddr() == pointer &&
                        store.getValue().getType().isInteger(64) &&
                        !store.getVolatile_() &&
                        store.getOrdering() == LLVM::AtomicOrdering::not_atomic;
      if (!plainLoad && !plainStore) {
        supported = false;
        break;
      }
      auto bytes = staticByteOffset(pointer);
      if (!bytes || *bytes % 8) {
        // Hierarchical ready lookup can select a word dynamically. Keep that
        // exact access on authoritative storage and treat it as a boundary.
        boundaries.push_back(operation);
      } else if (*bytes / 8 >= array.getNumElements()) {
        supported = false;
        break;
      } else {
        accesses[*bytes / 8].push_back(operation);
      }
    }
  }
  if (!supported || accesses.empty())
    return success();
  // Flush/acquire code is also charged to the existing group growth budget.
  // Avoid multiplying a large ready set by an unresolved helper call graph.
  uint64_t perWordCost = 4 + 6 * uint64_t(boundaries.size()) +
                         function.getBody().getBlocks().size();
  if (accesses.size() > budget / perWordCost)
    return success();
  budget -= accesses.size() * perWordCost;

  OpBuilder builder(function.getContext());
  builder.setInsertionPointToStart(&function.getBody().front());
  Location location = function.getLoc();
  Type i64 = builder.getI64Type();
  Value base = LLVM::AddressOfOp::create(
      builder, location, LLVM::LLVMPointerType::get(function.getContext()),
      reference.getValue());
  struct Word {
    Value address;
    Value slot;
  };
  SmallVector<Word> words;
  SmallVector<PromotableAllocationOpInterface> allocations;
  for (auto &[index, operations] : accesses) {
    Value address = byteGEP(builder, location, base, index * 8);
    Value slot = entryAlloca(builder, location, i64, 1, 8);
    allocations.push_back(
        cast<PromotableAllocationOpInterface>(slot.getDefiningOp()));
    Value initial = LLVM::LoadOp::create(builder, location, i64, address, 8);
    LLVM::StoreOp::create(builder, location, initial, slot, 8);
    words.push_back({address, slot});
    for (Operation *operation : operations) {
      if (auto load = dyn_cast<LLVM::LoadOp>(operation))
        load.getAddrMutable().assign(slot);
      else
        cast<LLVM::StoreOp>(operation).getAddrMutable().assign(slot);
    }
  }
  for (Operation *boundary : boundaries) {
    builder.setInsertionPoint(boundary);
    for (const Word &word : words)
      LLVM::StoreOp::create(
          builder, boundary->getLoc(),
          LLVM::LoadOp::create(builder, boundary->getLoc(), i64, word.slot, 8),
          word.address, 8);
    if (isa<LLVM::ReturnOp>(boundary))
      continue;
    builder.setInsertionPointAfter(boundary);
    for (const Word &word : words)
      LLVM::StoreOp::create(builder, boundary->getLoc(),
                            LLVM::LoadOp::create(builder, boundary->getLoc(),
                                                 i64, word.address, 8),
                            word.slot, 8);
  }
  DominanceInfo dominance(function);
  if (failed(tryToPromoteMemorySlots(allocations, builder,
                                     DataLayout::closest(function), dominance)))
    return function.emitError("could not materialize group ready words in SSA");
  ::obelisk::schedule::set<::obelisk::schedule::Field::EvalSsaReadyWords>(
      function, builder.getI64IntegerAttr(words.size()));
  return success();
}

/// Forward exact physical state ranges between certified actors. Canonical
/// state is acquired at entry, including its X/Z plane; no initialization or
/// value-domain proof is inferred here. Original transition computations and
/// invalidation hooks still execute at each logical store. Only its physical
/// publication is deferred until the next boundary (LRM 4.3, 4.6, 4.10).
LogicalResult promoteGroupState(LLVM::LLVMFuncOp function,
                                const llvm::StringMap<Type> &globalTypes,
                                StringRef name, uint64_t &budget) {
  auto array = dyn_cast_or_null<LLVM::LLVMArrayType>(globalTypes.lookup(name));
  if (!array || !array.getElementType().isInteger(8))
    return success();
  struct Range {
    Type type;
    SmallVector<Operation *> operations;
    llvm::SmallDenseSet<uint32_t, 2> owners;
    bool read = false;
    bool written = false;
    bool overlaps = false;
  };
  std::map<std::pair<uint64_t, uint64_t>, Range> ranges;
  llvm::SetVector<Operation *> boundaries;
  bool supported = true;
  function.walk([&](Operation *operation) {
    if (operation == function.getOperation())
      return;
    if (isa<LLVM::CallOp, LLVM::ReturnOp>(operation)) {
      boundaries.insert(operation);
      return;
    }
    if (isa<LLVM::InvokeOp, LLVM::InlineAsmOp>(operation))
      supported = false;
    auto load = dyn_cast<LLVM::LoadOp>(operation);
    auto store = dyn_cast<LLVM::StoreOp>(operation);
    if (!load && !store) {
      // Intrinsics and future memory operations must not silently bypass
      // publication. Unknown control effects cannot be resumed locally.
      if (!isa<LLVM::AllocaOp>(operation) && !isMemoryEffectFree(operation)) {
        if (operation->getNumRegions() ||
            operation->hasTrait<OpTrait::IsTerminator>())
          supported = false;
        else
          boundaries.insert(operation);
      }
      return;
    }
    Value pointer = load ? load.getAddr() : store.getAddr();
    Value root = pointer;
    while (auto gep = root.getDefiningOp<LLVM::GEPOp>())
      root = gep.getBase();
    auto address = root.getDefiningOp<LLVM::AddressOfOp>();
    if (!address || !globalTypes.contains(address.getGlobalName())) {
      // A returned pointer or an indirect context access may alias canonical
      // state. Local allocation addresses are the only non-global exception.
      if (!root.getDefiningOp<LLVM::AllocaOp>())
        boundaries.insert(operation);
      return;
    }
    if (address.getGlobalName() != name)
      return;
    auto owner =
        ::obelisk::schedule::get<::obelisk::schedule::Field::EvalGroupOwner>(
            operation);
    auto type = dyn_cast<IntegerType>(load ? load.getType()
                                           : store.getValue().getType());
    auto offset = staticByteOffset(pointer);
    bool plain =
        load ? !load.getVolatile_() &&
                   load.getOrdering() == LLVM::AtomicOrdering::not_atomic
             : !store.getVolatile_() &&
                   store.getOrdering() == LLVM::AtomicOrdering::not_atomic;
    if (!owner || !plain || !type || type.getWidth() % 8 || !offset) {
      boundaries.insert(operation);
      return;
    }
    uint64_t width = type.getWidth() / 8;
    if (*offset >= array.getNumElements() ||
        width > array.getNumElements() - *offset) {
      supported = false;
      return;
    }
    Range &range = ranges[{*offset, width}];
    range.type = type;
    range.operations.push_back(operation);
    range.owners.insert(owner.getUInt());
    range.read |= bool(load);
    range.written |= bool(store);
  });
  // The global address must never escape into an opaque memory operation.
  // Calls are handled at boundaries, but storing the pointer or passing it
  // through control flow would hide later aliases from the access inventory.
  SmallVector<Value> pending;
  llvm::DenseSet<Value> seen;
  function.walk([&](LLVM::AddressOfOp address) {
    if (address.getGlobalName() == name)
      pending.push_back(address.getResult());
  });
  while (!pending.empty() && supported) {
    Value pointer = pending.pop_back_val();
    if (!seen.insert(pointer).second)
      continue;
    for (OpOperand &use : pointer.getUses()) {
      Operation *operation = use.getOwner();
      if (auto gep = dyn_cast<LLVM::GEPOp>(operation))
        pending.push_back(gep.getResult());
      else if (auto store = dyn_cast<LLVM::StoreOp>(operation))
        supported &= store.getAddr() == pointer;
      else
        supported &= isa<LLVM::LoadOp, LLVM::CallOp>(operation);
    }
  }
  if (!supported || ranges.empty())
    return success();
  // Reject differently typed or partial overlaps with a sorted interval
  // sweep. No all-pairs alias matrix and no union of copy-connected ports.
  Range *longest = nullptr;
  uint64_t end = 0;
  for (auto &[key, range] : ranges) {
    if (longest && key.first < end)
      longest->overlaps = range.overlaps = true;
    if (!longest || key.first + key.second > end) {
      end = key.first + key.second;
      longest = &range;
    }
  }
  // Overlapping candidates are all rejected; noncandidate target accesses
  // were recorded as boundaries above. Disjoint rejected ranges need no
  // synchronization.
  SmallVector<std::pair<uint64_t, Range *>> selected;
  uint64_t perRangeCost = 4 + 6 * uint64_t(boundaries.size()) +
                          function.getBody().getBlocks().size();
  for (auto &[key, range] : ranges)
    if (!range.overlaps && range.read && range.written &&
        range.owners.size() > 1 && budget >= perRangeCost) {
      selected.push_back({key.first, &range});
      budget -= perRangeCost;
    }
  if (selected.empty())
    return success();
  OpBuilder builder(function.getContext());
  builder.setInsertionPointToStart(&function.getBody().front());
  Location location = function.getLoc();
  Value base = LLVM::AddressOfOp::create(
      builder, location, LLVM::LLVMPointerType::get(function.getContext()),
      name);
  struct Slot {
    Value address;
    Value local;
    Type type;
  };
  SmallVector<Slot> slots;
  SmallVector<PromotableAllocationOpInterface> allocations;
  for (auto [offset, range] : selected) {
    Value address = byteGEP(builder, location, base, offset);
    Value local = entryAlloca(builder, location, range->type, 1, 1);
    allocations.push_back(
        cast<PromotableAllocationOpInterface>(local.getDefiningOp()));
    LLVM::StoreOp::create(
        builder, location,
        LLVM::LoadOp::create(builder, location, range->type, address, 1), local,
        1);
    slots.push_back({address, local, range->type});
    for (Operation *operation : range->operations)
      if (auto load = dyn_cast<LLVM::LoadOp>(operation)) {
        load.getAddrMutable().assign(local);
        load.setAlignment(1);
      } else {
        auto store = cast<LLVM::StoreOp>(operation);
        store.getAddrMutable().assign(local);
        store.setAlignment(1);
      }
  }
  for (Operation *boundary : boundaries) {
    builder.setInsertionPoint(boundary);
    for (const Slot &slot : slots)
      LLVM::StoreOp::create(builder, boundary->getLoc(),
                            LLVM::LoadOp::create(builder, boundary->getLoc(),
                                                 slot.type, slot.local, 1),
                            slot.address, 1);
    if (isa<LLVM::ReturnOp>(boundary))
      continue;
    builder.setInsertionPointAfter(boundary);
    for (const Slot &slot : slots)
      LLVM::StoreOp::create(builder, boundary->getLoc(),
                            LLVM::LoadOp::create(builder, boundary->getLoc(),
                                                 slot.type, slot.address, 1),
                            slot.local, 1);
  }
  DominanceInfo dominance(function);
  if (failed(tryToPromoteMemorySlots(allocations, builder,
                                     DataLayout::closest(function), dominance)))
    return function.emitError("could not materialize group state in SSA");
  auto attribute = name == "__obelisk_state_value"
                       ? ::obelisk::schedule::Field::EvalSsaValueRanges
                       : ::obelisk::schedule::Field::EvalSsaUnknownRanges;
  schedule::set(function, attribute, builder.getI64IntegerAttr(slots.size()));
  return success();
}

struct NativeGroupPromotionInputs {
  explicit NativeGroupPromotionInputs(Operation *operation) {
    for (auto global : cast<ModuleOp>(operation).getOps<LLVM::GlobalOp>())
      globalTypes.try_emplace(global.getSymName(), global.getGlobalType());
    debugTiming = operation->hasAttr("obelisk.debug.native_timing");
  }
  llvm::StringMap<Type> globalTypes;
  DenseMap<Operation *, std::pair<uint64_t, size_t>> residuals;
  std::shared_ptr<NativeGroupPromotionReport> report;
  bool debugTiming = false;
};

class PromoteNativeGroupFunctionPass final
    : public PassWrapper<PromoteNativeGroupFunctionPass,
                         OperationPass<LLVM::LLVMFuncOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(PromoteNativeGroupFunctionPass)
  StringRef getArgument() const final {
    return "promote-native-group-function";
  }
  void runOnOperation() override {
    auto function = getOperation();
    auto cached = getCachedParentAnalysis<NativeGroupPromotionInputs>(
        function->getParentOfType<ModuleOp>());
    if (!cached) {
      function.emitError("native group promotion requires cached global types");
      return signalPassFailure();
    }
    const auto &inputs = cached->get();
    auto found = inputs.residuals.find(function);
    if (found == inputs.residuals.end()) {
      markAllAnalysesPreserved();
      return;
    }
    auto [budget, index] = found->second;
    uint64_t remaining = budget;
    if (failed(
            promoteGroupReadyWords(function, inputs.globalTypes, remaining)) ||
        failed(promoteGroupState(function, inputs.globalTypes,
                                 "__obelisk_state_value", remaining)) ||
        failed(promoteGroupState(function, inputs.globalTypes,
                                 "__obelisk_state_unknown", remaining)))
      return signalPassFailure();
    if (remaining == budget)
      markAllAnalysesPreserved();
    if (!inputs.debugTiming)
      return;
    llvm::raw_string_ostream diagnostic(inputs.report->diagnostics[index]);
    auto count = [&](schedule::Field name) -> uint64_t {
      auto value = schedule::get<IntegerAttr>(function, name);
      return value ? value.getUInt() : 0;
    };
    diagnostic << "obelisk native group: " << function.getSymName()
               << " expanded_calls="
               << count(schedule::Field::EvalMaterializedGroupCalls)
               << " ready_words=" << count(schedule::Field::EvalSsaReadyWords)
               << " value_ranges=" << count(schedule::Field::EvalSsaValueRanges)
               << " unknown_ranges="
               << count(schedule::Field::EvalSsaUnknownRanges)
               << " remaining_budget=" << remaining << '\n';
  }
};

struct InlinedGroupBody {
  uint64_t remaining;
  uint64_t materialized;
};

InlinedGroupBody
inlineGroupBody(LLVM::LLVMFuncOp function, const SymbolTable &symbols,
                const DenseMap<Operation *, uint64_t> &stableCosts,
                DenseMap<Operation *, uint64_t> &costs,
                uint64_t operationLimit) {
  InlinerInterface interface(function.getContext());
  SmallVector<LLVM::CallOp> pending;
  function.walk([&](LLVM::CallOp call) {
    if (::obelisk::schedule::has<::obelisk::schedule::Field::EvalGroupMember>(
            call))
      pending.push_back(call);
  });
  std::reverse(pending.begin(), pending.end());
  // The existing native helper-size budget also caps total additional IR
  // per coordinator. Oversized computation stays in direct helpers; helper
  // calls do not acquire time/region ownership or become scheduler exits.
  uint64_t remaining = operationLimit;
  uint64_t materialized = 0;
  llvm::DenseSet<Operation *> expanded;
  while (!pending.empty()) {
    LLVM::CallOp call = pending.pop_back_val();
    if (!call.getCallee())
      continue;
    auto callee = symbols.lookup<LLVM::LLVMFuncOp>(*call.getCallee());
    if (!callee || callee == function || callee.isExternal() ||
        expanded.contains(callee) ||
        !(::obelisk::schedule::has<
              ::obelisk::schedule::Field::EvalGroupDomainSelected>(call) ||
          ::obelisk::schedule::has<schedule::metadata::evalInfallible>(
              callee) ||
          ::obelisk::schedule::has<
              ::obelisk::schedule::Field::EvalFourStateSource>(callee) ||
          ::obelisk::schedule::has<
              ::obelisk::schedule::Field::EvalConditionallyTwoState>(callee)))
      continue;
    uint64_t cost;
    if (auto frozen = stableCosts.find(callee); frozen != stableCosts.end()) {
      cost = frozen->second;
    } else {
      auto [found, inserted] = costs.try_emplace(callee, 0);
      if (inserted)
        callee.walk([&](Operation *operation) {
          found->second += operation != callee.getOperation();
        });
      cost = found->second;
    }
    if (!cost || cost > remaining)
      continue;
    SmallVector<LLVM::CallOp> nested;
    auto owner =
        ::obelisk::schedule::get<::obelisk::schedule::Field::EvalGroupMember>(
            call);
    auto clone = [&](OpBuilder &, Region *source, Block *inlineBlock,
                     Block *postInsertBlock, IRMapping &mapping,
                     bool shouldClone) {
      assert(shouldClone && "canonical executor bodies must remain intact");
      source->cloneInto(inlineBlock->getParent(),
                        postInsertBlock->getIterator(), mapping);
      for (Block &block : *source)
        for (Operation &operation : *mapping.lookup(&block)) {
          if (owner && isa<LLVM::LoadOp, LLVM::StoreOp>(operation))
            schedule::set<schedule::Field::EvalGroupOwner>(&operation, owner);
          if (auto child = dyn_cast<LLVM::CallOp>(operation)) {
            if (owner)
              ::obelisk::schedule::set<
                  ::obelisk::schedule::Field::EvalGroupMember>(child, owner);
            nested.push_back(child);
          }
        }
    };
    if (failed(inlineCall(interface, clone, cast<CallOpInterface>(*call),
                          cast<CallableOpInterface>(*callee),
                          &callee.getBody())))
      continue;
    call.erase();
    // Shared computation remains a direct helper after one expansion.
    // Besides avoiding Cartesian cloning, this cuts recursive call edges
    // even when the user disables the operation-size budget.
    expanded.insert(callee);
    remaining -= cost;
    ++materialized;
    std::reverse(nested.begin(), nested.end());
    llvm::append_range(pending, nested);
  }
  if (materialized)
    ::obelisk::schedule::set<
        ::obelisk::schedule::Field::EvalMaterializedGroupCalls>(
        function, IntegerAttr::get(IntegerType::get(function.getContext(), 64),
                                   materialized));
  return {remaining, materialized};
}

} // namespace

/// Materialize certified activation bodies before native partitioning. The
/// original pending bits, source predicates, and boundary exits remain the
/// schedule: neither a value snapshot nor an unconditional call sequence can
/// replace that activation evidence (IEEE 1800-2023 4.3--4.7).
///
/// Route specialization must run first. A durable two-state certificate then
/// exposes a direct body; an unresolved route remains an executor boundary.
/// In particular, do not select a two-state callee just because it appears in
/// an indirect call's allowed-callee inventory.
FailureOr<std::shared_ptr<NativeGroupPromotionReport>>
materializeNativeEvalGroupBodies(ModuleOp module, AnalysisManager manager) {
  auto report = std::make_shared<NativeGroupPromotionReport>();
  SmallVector<LLVM::LLVMFuncOp> groups;
  for (auto function : module.getOps<LLVM::LLVMFuncOp>())
    if (::obelisk::schedule::has<::obelisk::schedule::Field::EvalRankedMembers>(
            function))
      groups.push_back(function);
  if (groups.empty())
    return report;
  auto &inputs = manager.getAnalysis<NativeGroupPromotionInputs>();
  inputs.report = report;
  DialectRegistry registry;
  LLVM::registerInlinerInterface(registry);
  module.getContext()->appendDialectRegistry(registry);
  SymbolTable symbols(module);
  // Candidates are transient compiler-owned bodies. Preserve user-authored
  // candidates with existing callers rather than deleting or retargeting
  // their callable interface. Inventory uses once, not once per group.
  DenseSet<Operation *> externallyUsedCandidates;
  if (llvm::any_of(groups, [](auto function) {
        return ::obelisk::schedule::has<
            ::obelisk::schedule::Field::EvalDataflowCandidate>(function);
      })) {
    SmallVector<Region *> scopes;
    module.walk([&](Operation *operation) {
      if (operation->hasTrait<OpTrait::SymbolTable>())
        for (Region &region : operation->getRegions())
          scopes.push_back(&region);
    });
    auto uses = collectNativeSymbolUses(module.getContext(), scopes);
    if (!uses) {
      // An unknown symbol scope cannot prove a transient candidate unused.
      for (auto function : groups)
        if (::obelisk::schedule::has<
                ::obelisk::schedule::Field::EvalDataflowCandidate>(function))
          externallyUsedCandidates.insert(function);
    } else {
      SymbolTableCollection tables;
      // Resolve in the scope containing the user, as SymbolUserMap does.
      // A symbol-table operation's own attributes still use its outer scope.
      for (const auto &use : *uses)
        if (auto function = tables.lookupNearestSymbolFrom<LLVM::LLVMFuncOp>(
                use.getUser()->getParentOp(), use.getSymbolRef());
            function &&
            ::obelisk::schedule::has<
                ::obelisk::schedule::Field::EvalDataflowCandidate>(function))
          externallyUsedCandidates.insert(function);
    }
  }

  auto limit =
      ::obelisk::schedule::get<::obelisk::schedule::Field::MaxInlineOps>(
          module);
  uint64_t operationLimit = limit ? limit.getValue().getZExtValue() : 5000;
  if (!operationLimit)
    operationLimit = UINT64_MAX;

  // Measure each needed definition once. Charging its own operations bounds
  // both cloning and subsequent optimization; following shared callees here
  // would repeatedly charge the same graph, just as following global
  // initializers did in native partition cost estimation.
  llvm::DenseMap<Operation *, uint64_t> costs;
  SmallVector<std::pair<LLVM::LLVMFuncOp, uint64_t>> residuals;
  DenseMap<Operation *, uint64_t> residualBudgets;
  DenseSet<Operation *> finalized, discarded, refinedChildren;
  struct Refinement {
    LLVM::LLVMFuncOp parent;
    SmallVector<LLVM::LLVMFuncOp> children;
  };
  SmallVector<Refinement> refinements;
  DenseMap<Operation *, unsigned> refinementRoots;
  SmallVector<uint64_t> refinementBudgets;
  // Establish body read/write dependencies with MLIR's call graph before
  // mutation. A worker must neither read a group another worker can change,
  // nor change a definition read by the ordered refinement phase. Shared leaf
  // executors remain immutable; groups with dependencies retain source order.
  CallGraph callGraph(module);
  DenseSet<CallGraphNode *> groupNodes, reachesGroup, referencedGroups;
  DenseMap<CallGraphNode *, SmallVector<CallGraphNode *>> callers;
  for (CallGraphNode *node : callGraph)
    for (const auto &edge : *node)
      callers[edge.getTarget()].push_back(node);
  SmallVector<CallGraphNode *> pending;
  for (auto function : groups)
    if (auto *node = callGraph.lookupNode(&function.getBody())) {
      groupNodes.insert(node);
      reachesGroup.insert(node);
      pending.push_back(node);
    }
  while (!pending.empty()) {
    auto *node = pending.pop_back_val();
    for (auto *caller : callers.lookup(node))
      if (reachesGroup.insert(caller).second)
        pending.push_back(caller);
  }
  DenseSet<CallGraphNode *> reachable;
  llvm::append_range(pending, groupNodes);
  while (!pending.empty()) {
    auto *node = pending.pop_back_val();
    if (!reachable.insert(node).second)
      continue;
    for (const auto &edge : *node) {
      if (groupNodes.contains(edge.getTarget()))
        referencedGroups.insert(edge.getTarget());
      if (!edge.getTarget()->isExternal())
        pending.push_back(edge.getTarget());
    }
  }
  SmallVector<LLVM::LLVMFuncOp> stableDefinitions;
  for (auto *node : reachable) {
    if (node->isExternal() || groupNodes.contains(node))
      continue;
    if (auto function = dyn_cast<LLVM::LLVMFuncOp>(
            node->getCallableRegion()->getParentOp()))
      stableDefinitions.push_back(function);
  }
  SmallVector<uint64_t> stableCounts(stableDefinitions.size());
  parallelFor(module.getContext(), 0, stableDefinitions.size(), [&](size_t i) {
    stableDefinitions[i].walk([&](Operation *operation) {
      stableCounts[i] += operation != stableDefinitions[i].getOperation();
    });
  });
  DenseMap<Operation *, uint64_t> stableCosts;
  for (auto [i, function] : llvm::enumerate(stableDefinitions))
    stableCosts.try_emplace(function, stableCounts[i]);

  // Refinement can replace the original named by candidate metadata without
  // a call edge. Preserve that write-before-inline dependency as well. An
  // original preceding its candidate may be prepared early: its ordered
  // inlining already runs before the candidate's body transaction.
  DenseMap<Operation *, size_t> groupOrder;
  for (auto [i, function] : llvm::enumerate(groups))
    groupOrder.try_emplace(function, i);
  DenseSet<Operation *> earlyRefinementTargets;
  for (auto [i, function] : llvm::enumerate(groups))
    if (auto candidate = ::obelisk::schedule::get<
            ::obelisk::schedule::Field::EvalDataflowCandidate>(function)) {
      auto original = symbols.lookup<LLVM::LLVMFuncOp>(candidate.getValue());
      auto position = groupOrder.find(original);
      if (position != groupOrder.end() && position->second > i)
        earlyRefinementTargets.insert(original);
    }
  SmallVector<LLVM::LLVMFuncOp> independent;
  for (auto function : groups) {
    auto *node = callGraph.lookupNode(&function.getBody());
    if (!node || referencedGroups.contains(node) ||
        earlyRefinementTargets.contains(function) ||
        externallyUsedCandidates.contains(function) ||
        llvm::any_of(*node, [&](const auto &edge) {
          return reachesGroup.contains(edge.getTarget());
        }))
      continue;
    independent.push_back(function);
  }
  SmallVector<InlinedGroupBody> inlinedBodies(independent.size());
  parallelFor(module.getContext(), 0, independent.size(), [&](size_t i) {
    DenseMap<Operation *, uint64_t> localCosts;
    inlinedBodies[i] = inlineGroupBody(independent[i], symbols, stableCosts,
                                       localCosts, operationLimit);
  });
  DenseMap<Operation *, InlinedGroupBody> preInlined;
  for (auto [i, function] : llvm::enumerate(independent))
    preInlined.try_emplace(function, inlinedBodies[i]);
  if (module->hasAttr("obelisk.debug.native_timing"))
    llvm::errs() << "obelisk native group inlining: independent="
                 << independent.size()
                 << " ordered=" << groups.size() - independent.size() << '\n';

  // Refinement appends children. Keep original activation CFGs intact until
  // all candidates have been considered; memory SSA would erase the entry
  // evidence needed to prove a partial split.
  for (size_t index = 0; index < groups.size(); ++index) {
    auto function = groups[index];
    if (externallyUsedCandidates.contains(function)) {
      ::obelisk::schedule::remove<
          ::obelisk::schedule::Field::EvalDataflowCandidate>(function);
      continue;
    }
    auto inlined = preInlined.find(function);
    InlinedGroupBody body =
        inlined != preInlined.end()
            ? inlined->second
            : inlineGroupBody(function, symbols, stableCosts, costs,
                              operationLimit);
    // Refinement may erase a candidate and allocate new child functions.
    // Do not retain a consumed operation pointer across that allocation.
    if (inlined != preInlined.end())
      preInlined.erase(inlined);
    uint64_t remaining = body.remaining;
    uint64_t materialized = body.materialized;
    if (auto candidate = ::obelisk::schedule::get<
            ::obelisk::schedule::Field::EvalDataflowCandidate>(function)) {
      auto original = symbols.lookup<LLVM::LLVMFuncOp>(candidate.getValue());
      auto members = ::obelisk::schedule::get<
          ::obelisk::schedule::Field::EvalRankedMembers>(function);
      auto pendingGlobal = symbols.lookup<LLVM::GlobalOp>(
          "__obelisk_eval_promotion_pending_mask_v1");
      auto pendingType =
          pendingGlobal
              ? dyn_cast<LLVM::LLVMArrayType>(pendingGlobal.getGlobalType())
              : LLVM::LLVMArrayType{};
      bool identitiesValid =
          original && original != function && !original.empty() &&
          !::obelisk::schedule::has<
              ::obelisk::schedule::Field::EvalDataflowCandidate>(original) &&
          !::obelisk::schedule::has<
              ::obelisk::schedule::Field::EvalDataflowExecutor>(original) &&
          members &&
          !::obelisk::schedule::has<
              ::obelisk::schedule::Field::EvalDataflowFallback>(original) &&
          !members.empty() &&
          original.getFunctionType() == function.getFunctionType() &&
          ::obelisk::schedule::get<
              ::obelisk::schedule::Field::EvalRankedMembers>(original) ==
              members &&
          ::obelisk::schedule::get<
              ::obelisk::schedule::Field::EvalGroupIngress>(original) ==
              ::obelisk::schedule::get<
                  ::obelisk::schedule::Field::EvalGroupIngress>(function) &&
          pendingType && pendingType.getElementType().isInteger(64);
      llvm::SmallDenseSet<uint32_t> identities;
      if (identitiesValid)
        for (int32_t owner : members.asArrayRef())
          identitiesValid &=
              owner >= 0 &&
              uint32_t(owner) / 64 < pendingType.getNumElements() &&
              identities.insert(owner).second;
      bool accepted = identitiesValid && materializeNativeGroupDataflow(
                                             function, symbols, operationLimit);
      if (module->hasAttr("obelisk.debug.native_timing"))
        llvm::errs() << "obelisk group dataflow: " << function.getSymName()
                     << " accepted=" << accepted << '\n';
      if (!accepted) {
        if (identitiesValid) {
          auto [root, inserted] =
              refinementRoots.try_emplace(function, refinementBudgets.size());
          if (inserted)
            refinementBudgets.push_back(operationLimit);
          unsigned rootID = root->second;
          auto children = splitNativeEvalGroup(original, function, symbols,
                                               refinementBudgets[rootID]);
          llvm::append_range(groups, children);
          if (!children.empty()) {
            refinements.push_back({original, {children[0], children[2]}});
            refinedChildren.insert(children[0]);
            refinedChildren.insert(children[2]);
            for (unsigned i : {1u, 3u})
              refinementRoots[children[i]] = rootID;
          }
        }
        symbols.erase(function);
        continue;
      }
      // One value-domain check for the whole predicated computation. A
      // pending proof retains the ordinary per-member path, allowing the
      // unaffected members to remain two-state. Activation predicates are
      // still evaluated inside the dataflow body; no cached coherence fact
      // can be invalidated by a known VPI deposit.
      // Promote the slow body before adding its fast guard, so slow-path
      // materialization never introduces loads on a collapsed child's entry.
      if (auto remaining = residualBudgets.find(original);
          remaining != residualBudgets.end()) {
        if (failed(promoteGroupReadyWords(original, inputs.globalTypes,
                                          remaining->second)) ||
            failed(promoteGroupState(original, inputs.globalTypes,
                                     "__obelisk_state_value",
                                     remaining->second)) ||
            failed(promoteGroupState(original, inputs.globalTypes,
                                     "__obelisk_state_unknown",
                                     remaining->second)))
          return failure();
        finalized.insert(original);
      }
      if (refinedChildren.contains(original)) {
        // The parent owns this child's selection point. Keeping another
        // guarded wrapper on the fast path would add a call boundary which
        // physical native partitioning cannot reliably inline away.
        ::obelisk::schedule::set<
            ::obelisk::schedule::Field::EvalDataflowExecutor>(
            original, FlatSymbolRefAttr::get(function));
        ::obelisk::schedule::remove<
            ::obelisk::schedule::Field::EvalDataflowCandidate>(function);
        continue;
      }
      // Make computation the actual group entry before physical partitioning.
      // Keep exactly one copy of each body: the original guarded executor
      // becomes a cold helper, while the finite SSA body moves into the entry.
      // Native partitioning can then split helpers without reintroducing a
      // wrapper call on every known activation.
      Region fallbackBody;
      fallbackBody.takeBody(original.getBody());
      original.getBody().takeBody(function.getBody());
      function.getBody().takeBody(fallbackBody);
      for (auto name : {::obelisk::schedule::Field::EvalMaterializedGroupCalls,
                        ::obelisk::schedule::Field::EvalSsaReadyWords,
                        ::obelisk::schedule::Field::EvalSsaValueRanges,
                        ::obelisk::schedule::Field::EvalSsaUnknownRanges,
                        ::obelisk::schedule::Field::EvalPredicatedDataflow,
                        ::obelisk::schedule::Field::EvalDataflowSlots,
                        ::obelisk::schedule::Field::EvalCoalescedSlots,
                        ::obelisk::schedule::Field::EvalCacheHintWords}) {
        Attribute slow = schedule::get<Attribute>(original, name),
                  fast = schedule::get<Attribute>(function, name);
        schedule::remove(original, name);
        schedule::remove(function, name);
        if (fast)
          schedule::set(original, name, fast);
        if (slow)
          schedule::set(function, name, slow);
      }
      symbols.remove(function);
      function.setSymName(original.getSymName().str() + ".fallback");
      symbols.insert(function);
      ::obelisk::schedule::remove<
          ::obelisk::schedule::Field::EvalDataflowCandidate>(function);
      OpBuilder builder(module.getContext());
      Location loc = function.getLoc();
      Block *oldEntry = &original.getBody().front();
      auto *guard = new Block, *fallback = new Block;
      original.getBody().push_front(guard);
      original.getBody().push_back(fallback);
      for (BlockArgument argument : oldEntry->getArguments())
        guard->addArgument(argument.getType(), argument.getLoc());
      builder.setInsertionPointToStart(guard);
      unsigned width = 64;
      for (int32_t owner : members.asArrayRef())
        width = std::max(width, unsigned(owner) + 1);
      APInt mask(width, 0);
      for (int32_t owner : members.asArrayRef())
        mask.setBit(owner);
      Value pending = LLVM::AddressOfOp::create(
          builder, loc, LLVM::LLVMPointerType::get(module.getContext()),
          "__obelisk_eval_promotion_pending_mask_v1");
      Value needed = llvmConstant(builder, loc, builder.getI64Type(), 0);
      for (unsigned word = 0; word < mask.getNumWords(); ++word) {
        uint64_t bits = ownerMaskWord(mask, word);
        if (!bits)
          continue;
        Value selected = LLVM::AndOp::create(
            builder, loc, loadOwnerWord(builder, loc, pending, word),
            llvmConstant(builder, loc, builder.getI64Type(), bits));
        needed = LLVM::OrOp::create(builder, loc, needed, selected);
      }
      Value known = LLVM::ICmpOp::create(
          builder, loc, LLVM::ICmpPredicate::eq, needed,
          llvmConstant(builder, loc, builder.getI64Type(), 0));
      LLVM::CondBrOp::create(builder, loc, known, oldEntry,
                             guard->getArguments(), fallback, ValueRange{});
      builder.setInsertionPointToStart(fallback);
      LLVM::CallOp::create(builder, loc, TypeRange{}, function.getSymName(),
                           guard->getArguments());
      LLVM::ReturnOp::create(builder, loc, ValueRange{});
      ::obelisk::schedule::set<
          ::obelisk::schedule::Field::EvalDataflowFallback>(
          original, FlatSymbolRefAttr::get(function));
      continue;
    }
    if (materialized || refinedChildren.contains(function)) {
      residuals.emplace_back(function, remaining);
      residualBudgets[function] = remaining;
    }
  }
  for (auto &refinement : llvm::reverse(refinements)) {
    bool useful = llvm::any_of(refinement.children, [](auto child) {
      return ::obelisk::schedule::has<
                 ::obelisk::schedule::Field::EvalDataflowExecutor>(child) ||
             ::obelisk::schedule::has<
                 ::obelisk::schedule::Field::EvalGroupChildren>(child);
    });
    if (!useful) {
      for (auto child : refinement.children) {
        discarded.insert(child);
        symbols.erase(child);
      }
      continue;
    }
    // Children partition the original ordered leaf identities. A parent adds
    // no readiness bits, domain guard, or event queue. Backward publications
    // survive child return for the shared loop (IEEE 1800-2023 4.4--4.7).
    auto parent = refinement.parent;
    parent.getBody().dropAllReferences();
    parent.getBody().getBlocks().clear();
    OpBuilder builder(module.getContext());
    Block *entry = parent.addEntryBlock(builder);
    builder.setInsertionPointToStart(entry);
    auto ingress =
        ::obelisk::schedule::get<::obelisk::schedule::Field::EvalGroupIngress>(
            parent);
    Value ready = LLVM::AddressOfOp::create(
        builder, parent.getLoc(),
        LLVM::LLVMPointerType::get(module.getContext()), ingress.getValue());
    SmallVector<Attribute> references;
    for (auto child : refinement.children) {
      references.push_back(FlatSymbolRefAttr::get(child));
      // Predication removes internal branches but may still compute inactive
      // expressions. Pay for a collapsed child only when it owns pending work.
      // Reacquire after each child: an earlier child can publish a later one.
      std::map<unsigned, uint64_t> masks;
      for (int32_t owner :
           ::obelisk::schedule::get<
               ::obelisk::schedule::Field::EvalRankedMembers>(child)
               .asArrayRef())
        masks[unsigned(owner) / 64] |= uint64_t{1} << (unsigned(owner) % 64);
      Value pending =
          llvmConstant(builder, parent.getLoc(), builder.getI64Type(), 0);
      for (auto [word, mask] : masks) {
        Value bits = LLVM::AndOp::create(
            builder, parent.getLoc(),
            loadOwnerWord(builder, parent.getLoc(), ready, word),
            llvmConstant(builder, parent.getLoc(), builder.getI64Type(), mask));
        pending = LLVM::OrOp::create(builder, parent.getLoc(), pending, bits);
      }
      Value active = LLVM::ICmpOp::create(
          builder, parent.getLoc(), LLVM::ICmpPredicate::ne, pending,
          llvmConstant(builder, parent.getLoc(), builder.getI64Type(), 0));
      auto *execute = new Block, *next = new Block;
      parent.getBody().push_back(execute);
      parent.getBody().push_back(next);
      LLVM::CondBrOp::create(builder, parent.getLoc(), active, execute,
                             ValueRange{}, next, ValueRange{});
      builder.setInsertionPointToStart(execute);
      if (auto fast = ::obelisk::schedule::get<
              ::obelisk::schedule::Field::EvalDataflowExecutor>(child)) {
        Value proof = LLVM::AddressOfOp::create(
            builder, parent.getLoc(),
            LLVM::LLVMPointerType::get(module.getContext()),
            "__obelisk_eval_promotion_pending_mask_v1");
        Value needed =
            llvmConstant(builder, parent.getLoc(), builder.getI64Type(), 0);
        for (auto [word, mask] : masks) {
          Value bits = LLVM::AndOp::create(
              builder, parent.getLoc(),
              loadOwnerWord(builder, parent.getLoc(), proof, word),
              llvmConstant(builder, parent.getLoc(), builder.getI64Type(),
                           mask));
          needed = LLVM::OrOp::create(builder, parent.getLoc(), needed, bits);
        }
        Value known = LLVM::ICmpOp::create(
            builder, parent.getLoc(), LLVM::ICmpPredicate::eq, needed,
            llvmConstant(builder, parent.getLoc(), builder.getI64Type(), 0));
        auto *collapsed = new Block, *fallback = new Block;
        parent.getBody().push_back(collapsed);
        parent.getBody().push_back(fallback);
        LLVM::CondBrOp::create(builder, parent.getLoc(), known, collapsed,
                               ValueRange{}, fallback, ValueRange{});
        builder.setInsertionPointToStart(collapsed);
        LLVM::CallOp::create(builder, parent.getLoc(), TypeRange{},
                             fast.getValue(), entry->getArguments());
        LLVM::BrOp::create(builder, parent.getLoc(), ValueRange{}, next);
        builder.setInsertionPointToStart(fallback);
      }
      LLVM::CallOp::create(builder, parent.getLoc(), TypeRange{},
                           child.getSymName(), entry->getArguments());
      LLVM::BrOp::create(builder, parent.getLoc(), ValueRange{}, next);
      builder.setInsertionPointToStart(next);
    }
    LLVM::ReturnOp::create(builder, parent.getLoc(), ValueRange{});
    ::obelisk::schedule::set<::obelisk::schedule::Field::EvalGroupChildren>(
        parent, builder.getArrayAttr(references));
    if (module->hasAttr("obelisk.debug.native_timing"))
      llvm::errs() << "obelisk group hierarchy: " << parent.getSymName()
                   << " children=" << refinement.children.size() << '\n';
  }
  // Group refinement and symbol creation are complete. Residual promotion
  // only rewrites its own function and reads globals through a frozen table.
  llvm::erase_if(residuals, [&](const auto &residual) {
    auto function = residual.first;
    return discarded.contains(function) || finalized.contains(function) ||
           ::obelisk::schedule::has<
               ::obelisk::schedule::Field::EvalGroupChildren>(function);
  });
  report->diagnostics.resize(residuals.size());
  for (auto [index, residual] : llvm::enumerate(residuals))
    inputs.residuals.try_emplace(residual.first,
                                 std::make_pair(residual.second, index));
  return report;
}

std::unique_ptr<Pass> createPromoteNativeGroupFunctionPass() {
  return std::make_unique<PromoteNativeGroupFunctionPass>();
}

} // namespace obelisk::detail

namespace obelisk {
#define GEN_PASS_DEF_MATERIALIZEOBELISKNATIVEEVALGROUPSPASS
#include "obelisk/Conversion/Passes.h.inc"

namespace {
struct MaterializeObeliskNativeEvalGroupsPass
    : impl::MaterializeObeliskNativeEvalGroupsPassBase<
          MaterializeObeliskNativeEvalGroupsPass> {
  using MaterializeObeliskNativeEvalGroupsPassBase::
      MaterializeObeliskNativeEvalGroupsPassBase;

  void runOnOperation() override {
    auto report = detail::materializeNativeEvalGroupBodies(
        getOperation(), getAnalysisManager());
    if (failed(report))
      return signalPassFailure();
    if (!(*report)->diagnostics.empty()) {
      OpPassManager functions(ModuleOp::getOperationName());
      functions.nest<LLVM::LLVMFuncOp>().addPass(
          detail::createPromoteNativeGroupFunctionPass());
      if (failed(runPipeline(functions, getOperation())))
        return signalPassFailure();
    }
    for (const auto &diagnostic : (*report)->diagnostics)
      llvm::errs() << diagnostic;
  }
};
} // namespace
} // namespace obelisk
