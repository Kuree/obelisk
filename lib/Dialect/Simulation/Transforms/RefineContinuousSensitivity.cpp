//===- RefineContinuousSensitivity.cpp - Exact RHS wakeup ranges --------===//
//
// IEEE 1800-2023 10.3.2 updates a continuous assignment when its RHS changes.
// Sparse handle provenance bounds every load, including dynamic selections.
// A pure, stateless activation with one unconditional, exclusively owned store
// cannot publish a change when none of those ranges changed. Procedural source
// sensitivity (9.2.2.2.1, 11.5.3) is deliberately a separate proof obligation.
// Analysis and view planning finish before any executable IR is rewritten.
//===----------------------------------------------------------------------===//

#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Dominance.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "obelisk/Analysis/HandleDataflowAnalysis.h"
#include "obelisk/Analysis/StorageWriteAnalysis.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Dialect/Simulation/Transforms/Passes.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SCCIterator.h"

using namespace mlir;
namespace obelisk {
#define GEN_PASS_DEF_OBELISKSIMREFINECONTINUOUSSENSITIVITYPASS
#include "obelisk/Dialect/Simulation/Transforms/Passes.h.inc"
namespace {
using analysis::HandleFact;
using Range = std::pair<uint64_t, uint64_t>;

bool overlaps(const HandleFact &a, const HandleFact &b) {
  return a.descriptor == b.descriptor && a.width && b.width &&
         (a.low <= b.low ? b.low - a.low < a.width : a.low - b.low < b.width);
}

// Plan a typed view before mutating IR. Unpacked provenance can contain
// padding; descend through its declared children, never invent a packed cast of
// it.
struct View {
  SmallVector<int64_t> path;
  Type type;
  uint64_t low, width;
};
bool planViews(Type type, uint64_t low, uint64_t width,
               SmallVector<int64_t> &path, SmallVector<View> &views) {
  auto span = sim::getProvenanceSpan(type);
  if (!span || !width || low >= *span || width > *span - low ||
      views.size() >= 64)
    return false;
  if (width == *span || sim::getPackedWidth(type)) {
    views.push_back({path, type, low, width});
    return true;
  }
  if (!sim::isAggregateType(type))
    return false;
  unsigned first = 0, endIndex = sim::getAggregateNumElements(type);
  // Array provenance has a fixed stride. Visit only intersecting children,
  // including when a small RHS slice comes from a very large array.
  if (isa<sim::UnpackedArrayType>(type) && endIndex > 1) {
    auto second = sim::getAggregateProvenanceSubelement(type, 1);
    if (!second || !second->first)
      return false;
    first = low / second->first;
    endIndex =
        std::min<uint64_t>(endIndex, (low + width - 1) / second->first + 1);
  }
  for (unsigned i = first; i != endIndex; ++i) {
    auto child = sim::getAggregateProvenanceSubelement(type, i);
    if (!child)
      return false;
    uint64_t begin = std::max(low, child->first);
    uint64_t end = std::min(low + width, child->first + child->second);
    if (begin >= end)
      continue;
    path.push_back(i);
    bool valid = planViews(sim::getAggregateElementType(type, i),
                           begin - child->first, end - begin, path, views);
    path.pop_back();
    if (!valid)
      return false;
  }
  return true;
}

struct ObeliskSimRefineContinuousSensitivityPass
    : impl::ObeliskSimRefineContinuousSensitivityPassBase<
          ObeliskSimRefineContinuousSensitivityPass> {
  using Base::Base;
  void runOnOperation() override {
    auto design = getOperation();
    if (vpi != "off" && vpi != "read" && vpi != "full") {
      design.emitError("invalid VPI sensitivity contract");
      return signalPassFailure();
    }
    if (vpi != "off")
      return;
    // External mutation can restore a destination independently of its RHS.
    // Overrides also invalidate the ordinary continuous-assignment contract.
    if (design
            .walk([](Operation *op) {
              return isa<sim::SimOverrideOp, sim::SimDynamicOverrideOp,
                         sim::SimReleaseOverrideOp, sim::SimDPICallOp>(op)
                         ? WalkResult::interrupt()
                         : WalkResult::advance();
            })
            .wasInterrupted())
      return;
    DenseMap<uint64_t, sim::SimStorageDeclOp> declarations;
    for (auto storage :
         design.getBody().front().getOps<sim::SimStorageDeclOp>())
      declarations.try_emplace(storage.getId(), storage);
    sim::SimFuncOp root;
    for (auto function : design.getBody().front().getOps<sim::SimFuncOp>())
      if (function.getEntryKind() == sim::EntryKind::RootInitializer) {
        if (root)
          return;
        root = function;
      }
    if (!root || root.isExternal())
      return;
    for (auto it = llvm::scc_begin(&root.getBody().front()); !it.isAtEnd();
         ++it)
      if (it.hasCycle())
        return;
    DominanceInfo dominance(root);
    analysis::NoBarrierAnalysis initialization(root, [](Operation *op) {
      return sim::isSuspensionOp(op) ||
             isa<sim::SimCallOp, sim::SimTaskCallOp>(op);
    });
    SmallVector<Operation *> exits;
    for (Block &block : root.getBody())
      if (!block.getNumSuccessors())
        exits.push_back(block.getTerminator());
    SymbolTable symbols(design);
    DenseMap<Operation *, unsigned> uses;
    DenseSet<Operation *> openBindings;
    auto allUses = SymbolTable::getSymbolUses(&design.getBody());
    if (!allUses)
      return;
    for (const auto &use : *allUses)
      if (auto *target =
              symbols.lookup(use.getSymbolRef().getRootReference())) {
        ++uses[target];
        if (!isa<sim::SimCallOp, sim::SimTaskCallOp, sim::SimSpawnOp>(
                use.getUser()))
          openBindings.insert(target);
      }
    DenseMap<Operation *, sim::SimSpawnOp> instances;
    root.walk([&](sim::SimSpawnOp spawn) {
      auto function = symbols.lookup<sim::SimFuncOp>(spawn.getCallee());
      if (function && function.isPrivate() && uses.lookup(function) == 1 &&
          spawn->getParentRegion() == &root.getBody() &&
          llvm::all_of(exits, [&](Operation *exit) {
            return dominance.dominates(spawn.getOperation(), exit);
          }))
        instances.try_emplace(function, spawn);
    });
    analysis::HandleDataflowAnalysis handles(design);
    DenseMap<Operation *, analysis::HandleFacts> facts;
    DenseMap<uint64_t, SmallVector<std::pair<Operation *, HandleFact>>> writers;
    // Freeze intraprocedural sparse facts before inspecting any call binding.
    for (auto function : design.getBody().front().getOps<sim::SimFuncOp>())
      facts[function] = handles.derive(function);
    bool unknownWrite = false;
    for (auto function : design.getBody().front().getOps<sim::SimFuncOp>()) {
      auto &derived = facts[function];
      function.walk([&](Operation *op) {
        if (op == function ||
            isa<sim::SimReturnOp, cf::BranchOp, cf::CondBranchOp, cf::SwitchOp>(
                op))
          return;
        bool call = isa<sim::SimCallOp, sim::SimTaskCallOp>(op);
        auto spawn = dyn_cast<sim::SimSpawnOp>(op);
        if (!call && !spawn &&
            (isMemoryEffectFree(op) || isa<sim::SimRefLoadOp>(op) ||
             sim::isSuspensionOp(op)))
          return;
        if (call || spawn) {
          StringRef name = spawn ? spawn.getCallee()
                           : isa<sim::SimCallOp>(op)
                               ? cast<sim::SimCallOp>(op).getCallee()
                               : cast<sim::SimTaskCallOp>(op).getCallee();
          auto callee = symbols.lookup<sim::SimFuncOp>(name);
          unknownWrite |= !callee || callee.isExternal();
        } else if (auto effects = dyn_cast<MemoryEffectOpInterface>(op)) {
          SmallVector<MemoryEffects::EffectInstance> instances;
          effects.getEffects(instances);
          for (const auto &effect : instances) {
            if (!isa<MemoryEffects::Write, MemoryEffects::Free>(
                    effect.getEffect()))
              continue;
            // Erased argument references, class dispatch and opaque effects
            // may reach canonical storage without an ordinary ref operand.
            if (isa<SideEffects::DefaultResource>(effect.getResource()))
              unknownWrite = true;
            if (!isa<sim::StorageResource>(effect.getResource()) ||
                isa<sim::SimRefStoreOp, sim::SimRefCopyOp,
                    sim::SimRefStoreInertialPathOp>(op))
              continue;
            bool localFree = isa<sim::SimRefReleaseOwnerOp>(op) &&
                             llvm::all_of(op->getOperands(), [&](Value value) {
                               auto fact = derived.find(value);
                               return fact != derived.end() &&
                                      fact->second.resource ==
                                          schedule::ComputeResourceKind::Local;
                             });
            unknownWrite |= !localFree;
          }
        } else {
          unknownWrite = true;
        }
        // Pin the entire actual reference at synchronous call boundaries.
        // Private formal-only writes then add no new roots: every ingress to
        // that closed call graph is covered by a concrete actual or a local
        // allocation. Unknown joins and externally callable formals stay open.
        for (auto [index, operand] : llvm::enumerate(op->getOperands())) {
          if (!isa<sim::RefType>(operand.getType()))
            continue;
          auto found = derived.find(operand);
          if (found != derived.end() &&
              (found->second.resource == schedule::ComputeResourceKind::Local ||
               (!found->second.descriptor && found->second.formal &&
                function.isPrivate() && uses.lookup(function) &&
                !openBindings.contains(function))))
            continue;
          if (found == derived.end() || !found->second.descriptor) {
            unknownWrite = true;
            continue;
          }
          if (spawn) {
            auto callee = symbols.lookup<sim::SimFuncOp>(spawn.getCallee());
            if (callee && index < callee.getNumArguments()) {
              auto formal = facts[callee].find(callee.getArgument(index));
              if (formal != facts[callee].end() &&
                  formal->second == found->second)
                continue;
            }
          }
          writers[*found->second.descriptor].emplace_back(op, found->second);
        }
      });
    }
    if (unknownWrite)
      return;
    unsigned refined = 0;
    for (auto function : design.getBody().front().getOps<sim::SimFuncOp>()) {
      // Port-copy cohorts currently rely on shared whole-root watches for
      // body fusion. Narrowing them fragments native dispatch, so keep that
      // representation until the cohort planner can preserve those groups.
      if (function.getEntryKind() != sim::EntryKind::Continuous)
        continue;
      auto instance = instances.lookup(function);
      if (!instance || function.getHomeRegion() != sim::EventRegion::Active)
        continue;
      auto &derived = facts[function];
      bool bindings = instance.getNumOperands() == function.getNumArguments();
      if (bindings)
        for (auto [actual, formal] :
             llvm::zip(instance.getOperands(), function.getArguments()))
          if (isa<sim::RefType>(formal.getType())) {
            auto a = facts[root].find(actual), f = derived.find(formal);
            bindings &= a != facts[root].end() && f != derived.end() &&
                        a->second == f->second;
          }
      if (!bindings)
        continue;
      SmallVector<sim::SimRefStoreOp> stores;
      SmallVector<sim::SimRefLoadOp> loads;
      SmallVector<Operation *> waits;
      bool safe = true;
      function.walk([&](Operation *op) {
        if (op == function)
          return;
        if (auto store = dyn_cast<sim::SimRefStoreOp>(op))
          stores.push_back(store);
        else if (auto load = dyn_cast<sim::SimRefLoadOp>(op))
          loads.push_back(load);
        else if (isa<sim::SimSuspendChangeOp, sim::SimSuspendAnyOp>(op))
          waits.push_back(op);
        else if (!isMemoryEffectFree(op) && !isa<cf::BranchOp>(op))
          safe = false;
      });
      // One unconditional RHS evaluation per activation. General procedural
      // bodies need a separate idempotence and source-sensitivity proof.
      if (!safe || stores.size() != 1 || waits.size() != 1 || loads.empty())
        continue;
      Operation *wait = waits.front();
      auto store = stores.front();
      if (store->getBlock() != wait->getBlock() ||
          wait->getSuccessor(0) != store->getBlock() ||
          wait->getSuccessor(0)->getNumArguments() != 0)
        continue;
      auto output = derived.find(store.getReference());
      if (output == derived.end() || !output->second.descriptor ||
          output->second.dynamic)
        continue;
      auto destination = declarations.lookup(*output->second.descriptor);
      if (!destination || destination.getLifetime() != sim::Lifetime::Design ||
          destination.getObservability() ==
              schedule::ComputeObservabilityKind::ExternallyWritable)
        continue;
      for (auto &[writer, range] : writers[*output->second.descriptor])
        if (writer != store && overlaps(output->second, range) &&
            !(isa<sim::SimRefStoreOp>(writer) &&
              writer->getParentOfType<sim::SimFuncOp>() == root &&
              initialization.isSafeBefore(writer)))
          safe = false;
      analysis::MustDefinitionAnalysis must(
          function, {store},
          [](Operation *op) { return sim::isSuspensionOp(op); });
      if (!safe || !must.containsBefore(store, wait))
        continue;
      llvm::MapVector<uint64_t, SmallVector<Range>> reads;
      for (auto load : loads) {
        auto found = derived.find(load.getReference());
        if (found == derived.end() || !found->second.descriptor ||
            !found->second.width || overlaps(output->second, found->second)) {
          safe = false;
          break;
        }
        const auto &range = found->second;
        // Sparse handle provenance retains the static parent of a dynamic
        // selection. Never watch just its currently selected element.
        reads[*range.descriptor].emplace_back(range.low,
                                              range.low + range.width);
      }
      SmallVector<Value> oldWatches;
      if (auto change = dyn_cast<sim::SimSuspendChangeOp>(wait))
        oldWatches.push_back(change.getWatched());
      else {
        auto any = cast<sim::SimSuspendAnyOp>(wait);
        llvm::append_range(oldWatches, any.getWatched());
        safe &= llvm::all_of(any.getEdges(), [](int32_t edge) {
          return edge == static_cast<int32_t>(sim::EdgeKind::Change);
        });
      }
      uint64_t oldBits = 0, newBits = 0;
      for (Value watched : oldWatches) {
        auto found = derived.find(watched);
        if (!isa<sim::RefType>(watched.getType()) || found == derived.end() ||
            !found->second.descriptor || found->second.dynamic) {
          safe = false;
          break;
        }
        if (found->second.width > UINT64_MAX - oldBits) {
          safe = false;
          break;
        }
        oldBits += found->second.width;
      }
      llvm::MapVector<uint64_t, SmallVector<View>> planned;
      for (auto &[id, ranges] : reads) {
        auto declaration = declarations.lookup(id);
        if (!declaration ||
            declaration.getLifetime() != sim::Lifetime::Design) {
          safe = false;
          break;
        }
        llvm::sort(ranges);
        SmallVector<Range> merged;
        for (auto range : ranges) {
          if (!merged.empty() && range.first <= merged.back().second)
            merged.back().second = std::max(merged.back().second, range.second);
          else
            merged.push_back(range);
        }
        for (auto [low, high] : merged) {
          bool covered =
              llvm::any_of(oldWatches, [&, descriptor = id, begin = low,
                                        end = high](Value watched) {
                const auto &old = derived.lookup(watched);
                return old.descriptor == descriptor && begin >= old.low &&
                       end <= old.low + old.width;
              });
          SmallVector<int64_t> path;
          safe &= covered && planViews(declaration.getType(), low, high - low,
                                       path, planned[id]);
          if (high - low > UINT64_MAX - newBits)
            safe = false;
          else
            newBits += high - low;
        }
      }
      Value context;
      for (Value argument : function.getBody().front().getArguments())
        if (isa<sim::ContextType>(argument.getType()))
          context = argument;
      if (!safe || !context || newBits >= oldBits || planned.empty() ||
          llvm::any_of(planned,
                       [](const auto &entry) { return entry.second.empty(); }))
        continue;
      OpBuilder builder(wait);
      SmallVector<Value> watched;
      for (auto &[id, views] : planned) {
        auto root = sim::SimContextStorageOp::create(
            builder, wait->getLoc(),
            sim::RefType::get(&getContext(), declarations.lookup(id).getType()),
            context, id);
        for (const auto &view : views) {
          Value reference = root;
          if (!view.path.empty())
            reference = sim::SimRefSubelementOp::create(
                builder, wait->getLoc(),
                sim::RefType::get(&getContext(), view.type), reference,
                builder.getDenseI64ArrayAttr(view.path));
          if (view.width != *sim::getProvenanceSpan(view.type)) {
            Type scalar = sim::getPackedScalarType(view.type);
            Type selected =
                isa<sim::LogicType>(scalar)
                    ? Type(sim::LogicType::get(&getContext(), view.width))
                    : Type(builder.getIntegerType(view.width));
            reference = sim::SimRefExtractOp::create(
                builder, wait->getLoc(),
                sim::RefType::get(&getContext(), selected), reference,
                view.low);
          }
          watched.push_back(reference);
        }
      }
      if (auto any = dyn_cast<sim::SimSuspendAnyOp>(wait)) {
        unsigned count = watched.size();
        llvm::append_range(watched, any.getContinuationOperands());
        any.getValuesMutable().assign(watched);
        any.setEdgesAttr(
            builder.getDenseI32ArrayAttr(SmallVector<int32_t>(count, 0)));
      } else {
        auto change = cast<sim::SimSuspendChangeOp>(wait);
        SmallVector<Value> values(watched);
        llvm::append_range(values, change.getContinuationOperands());
        sim::SimSuspendAnyOp::create(
            builder, wait->getLoc(), values,
            builder.getDenseI32ArrayAttr(
                SmallVector<int32_t>(watched.size(), 0)),
            change.getSiteAttr(), change.getResumeRegionAttr(),
            change.getContinuation());
        change.erase();
      }
      ++refined;
    }
    numRefined += refined;
    if (design->getParentOfType<ModuleOp>()->hasAttr(
            "obelisk.debug.native_timing"))
      emitRemark(design.getLoc())
          << "obelisk continuous sensitivity: refined=" << uint64_t(refined);
  }
};
} // namespace
} // namespace obelisk
