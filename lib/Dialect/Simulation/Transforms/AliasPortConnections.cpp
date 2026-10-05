//===- AliasPortConnections.cpp - Share port-connection storage ----------===//
//
// IEEE 1800-2023 23.3.3 and 4.9.6 define every port connection as an implied
// continuous assignment from source to sink. 4.9.1 schedules its update as an
// active event, and 4.7 permits suspending the writer after any statement and
// processing active events in any order, so updating the sink immediately
// after an Active source write is a legal execution. Sharing the storage
// selects that ordering, provided initialization and region boundaries are
// preserved. 23.3.3.7 states the same permission for
// net-to-net connections. The proof needs the sink's single driver (23.3.3.2,
// 10.3.2), no force/release on either side (10.6.2), and no observer of the
// intermediate copy: VPI off, no waveform dump.
//
//===----------------------------------------------------------------------===//

#include "obelisk/Analysis/HandleDataflowAnalysis.h"
#include "obelisk/Dialect/Schedule/ScheduleEnums.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Dialect/Simulation/Transforms/Passes.h"

#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/Dominance.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/ADT/SmallVector.h"

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_OBELISKSIMALIASPORTCONNECTIONSPASS
#include "obelisk/Dialect/Simulation/Transforms/Passes.h.inc"

namespace {
namespace sim = ::obelisk::sim;

/// One accepted equation `eliminated == target[path]` from a copy process.
struct Candidate {
  sim::SimSpawnOp spawn;
  sim::SimFuncOp copy;
  uint64_t source = 0, destination = 0;
  uint64_t eliminated = 0, target = 0;
  SmallVector<int64_t> path;
  /// The eliminated root is the copy destination (input or plain copy). An
  /// output into an array element eliminates the child's source storage.
  bool sinkElimination = true;
  bool rejected = false;
};

struct Access {
  enum class Kind { Read, Write, Override };
  uint64_t descriptor = 0, low = 0, width = 0;
  Kind kind = Kind::Read;
  bool nonblocking = false;
};

/// A resolved constant-index view of a surviving storage root.
struct ResolvedView {
  uint64_t root = 0;
  SmallVector<int64_t> path;
};

bool hasViewAttributes(sim::SimFuncOp function, unsigned index) {
  return function.getArgAttr(index, sim::metadata::descriptorRootType) ||
         function.getArgAttr(index, sim::metadata::descriptorLow) ||
         function.getArgAttr(index, sim::metadata::descriptorIndices) ||
         function.getArgAttr(index, sim::metadata::descriptorAggregateType) ||
         function.getArgAttr(index, sim::metadata::descriptorPackedLow);
}

/// Descriptor of a plain whole-storage capture, or nullopt.
std::optional<uint64_t> plainStorageCapture(sim::SimFuncOp function,
                                            unsigned index) {
  auto kind =
      function.getArgAttrOfType<IntegerAttr>(index, sim::metadata::captureKind);
  auto descriptor = function.getArgAttrOfType<IntegerAttr>(
      index, sim::metadata::descriptorId);
  if (!kind || kind.getInt() != int32_t(sim::CaptureKind::Storage) ||
      !descriptor || descriptor.getValue().isNegative() ||
      descriptor.getValue().getActiveBits() > 64 ||
      hasViewAttributes(function, index))
    return std::nullopt;
  return descriptor.getUInt();
}

/// Provenance offset and element type of `path` below `type`.
std::optional<std::pair<uint64_t, Type>> projectPath(Type type,
                                                     ArrayRef<int64_t> path) {
  uint64_t low = 0;
  for (int64_t index : path) {
    if (index < 0 || uint64_t(index) > UINT_MAX)
      return std::nullopt;
    auto child = sim::getAggregateProvenanceSubelement(type, unsigned(index));
    if (!child || child->first > UINT64_MAX - low)
      return std::nullopt;
    low += child->first;
    type = sim::getAggregateElementType(type, unsigned(index));
    if (!type)
      return std::nullopt;
  }
  return std::make_pair(low, type);
}

struct CopyShape {
  unsigned sourceArgument, destinationArgument;
  SmallVector<int64_t> sourcePath, destinationPath;
};

/// Match `cf.br ^b; ^b: [subelement] load [subelement] store; suspend.change
/// <source> to ^b` where both refs are plain storage captures and at most one
/// side selects a constant subelement.
std::optional<CopyShape> matchCopy(sim::SimFuncOp function) {
  if (function.isExternal() || !function.isPrivate() ||
      function.getHomeRegion() != sim::EventRegion::Active ||
      (function.getEntryKind() != sim::EntryKind::PortInput &&
       function.getEntryKind() != sim::EntryKind::PortOutput &&
       function.getEntryKind() != sim::EntryKind::Continuous) ||
      function.getBody().getBlocks().size() != 2)
    return std::nullopt;
  Block &entry = function.getBody().front();
  Block &body = function.getBody().back();
  auto branch = dyn_cast<cf::BranchOp>(entry.getTerminator());
  if (!llvm::hasSingleElement(entry) || !branch || branch.getDest() != &body ||
      !branch.getDestOperands().empty() || body.getNumArguments() != 0)
    return std::nullopt;
  auto wait = dyn_cast<sim::SimSuspendChangeOp>(body.getTerminator());
  if (!wait || wait.getContinuation() != &body ||
      !wait.getContinuationOperands().empty())
    return std::nullopt;
  sim::SimRefLoadOp load;
  sim::SimRefStoreOp store;
  SmallVector<sim::SimRefSubelementOp> projections;
  for (Operation &operation : body.without_terminator()) {
    if (auto found = dyn_cast<sim::SimRefLoadOp>(operation)) {
      if (load)
        return std::nullopt;
      load = found;
    } else if (auto found = dyn_cast<sim::SimRefStoreOp>(operation)) {
      if (store)
        return std::nullopt;
      store = found;
    } else if (auto found = dyn_cast<sim::SimRefSubelementOp>(operation)) {
      projections.push_back(found);
    } else {
      return std::nullopt;
    }
  }
  if (!load || !store || store.getValue() != load.getResult() ||
      !load.getResult().hasOneUse() || projections.size() > 1)
    return std::nullopt;
  auto split = [&](Value reference,
                   SmallVector<int64_t> &path) -> std::optional<unsigned> {
    if (auto projection = reference.getDefiningOp<sim::SimRefSubelementOp>()) {
      if (!projection.getResult().hasOneUse())
        return std::nullopt;
      llvm::append_range(path, projection.getIndices());
      reference = projection.getInput();
    }
    auto argument = dyn_cast<BlockArgument>(reference);
    if (!argument || argument.getOwner() != &entry)
      return std::nullopt;
    return argument.getArgNumber();
  };
  CopyShape shape;
  std::optional<unsigned> source = split(load.getReference(), shape.sourcePath);
  std::optional<unsigned> destination =
      split(store.getReference(), shape.destinationPath);
  if (!source || !destination || *source == *destination ||
      (!shape.sourcePath.empty() && !shape.destinationPath.empty()) ||
      wait.getWatched() != entry.getArgument(*source))
    return std::nullopt;
  // The single projection must be the one consumed by the load or store.
  if (projections.size() == 1 && shape.sourcePath.empty() &&
      shape.destinationPath.empty())
    return std::nullopt;
  shape.sourceArgument = *source;
  shape.destinationArgument = *destination;
  return shape;
}

class ObeliskSimAliasPortConnectionsPass
    : public impl::ObeliskSimAliasPortConnectionsPassBase<
          ObeliskSimAliasPortConnectionsPass> {
public:
  using Base::Base;

private:
  void runOnOperation() override;
};

void ObeliskSimAliasPortConnectionsPass::runOnOperation() {
  sim::SimDesignOp design = getOperation();
  if (vpi != "off") {
    if (vpi != "read" && vpi != "full") {
      design.emitOpError("VPI mode must be off, read, or full");
      return signalPassFailure();
    }
    return;
  }
  if (design.getBody().empty())
    return;
  Block &designBlock = design.getBody().front();

  // Waveform dumps observe the sink storage itself, not just its value at
  // assignment boundaries.
  bool dumps = false;
  design.walk([&](Operation *operation) {
    if (operation->getName().getStringRef().starts_with("simulation.dump."))
      dumps = true;
  });
  if (dumps)
    return;

  DenseMap<uint64_t, sim::SimStorageDeclOp> storages;
  for (auto storage : designBlock.getOps<sim::SimStorageDeclOp>())
    storages[storage.getId()] = storage;

  sim::SimFuncOp root;
  for (auto function : designBlock.getOps<sim::SimFuncOp>())
    if (function.getEntryKind() == sim::EntryKind::RootInitializer) {
      if (root)
        return;
      root = function;
    }
  if (!root || root.isExternal())
    return;

  SymbolTable symbols(design);
  // Count every symbol use, so a copy body is removable once its spawn is.
  DenseMap<Operation *, unsigned> uses;
  std::optional<SymbolTable::UseRange> all =
      SymbolTable::getSymbolUses(&design.getBody());
  if (!all)
    return;
  for (const SymbolTable::SymbolUse &use : *all)
    if (Operation *target =
            symbols.lookup(use.getSymbolRef().getRootReference().getValue()))
      ++uses[target];

  // Only unconditional, once-executed startup spawns establish an equation
  // for the entire design lifetime. A single symbol use can still be in a
  // conditional or loop. Require an acyclic root CFG and dominance of every
  // exit; do not infer execution count from symbol-use count alone.
  for (auto it = llvm::scc_begin(&root.getBody().front()); !it.isAtEnd(); ++it)
    if (it.hasCycle())
      return;
  DominanceInfo dominance(root);
  SmallVector<Operation *> exits;
  for (Block &block : root.getBody())
    if (block.getNumSuccessors() == 0)
      exits.push_back(block.getTerminator());

  // Function calls run during static initialization too (LRM 6.8). Their
  // reads must see the sink's initial value, before any port process runs.
  DenseMap<Operation *, SmallVector<Operation *>> callees;
  for (auto function : designBlock.getOps<sim::SimFuncOp>())
    function.walk([&](Operation *operation) {
      StringRef name;
      if (auto call = dyn_cast<sim::SimCallOp>(operation))
        name = call.getCallee();
      else if (auto call = dyn_cast<sim::SimTaskCallOp>(operation))
        name = call.getCallee();
      else
        return;
      if (auto callee = symbols.lookup<sim::SimFuncOp>(name))
        callees[function].push_back(callee);
    });
  auto closure = [&](SmallVector<Operation *> worklist) {
    DenseSet<Operation *> reached;
    while (!worklist.empty()) {
      Operation *function = worklist.pop_back_val();
      if (reached.insert(function).second)
        llvm::append_range(worklist, callees.lookup(function));
    }
    return reached;
  };
  DenseSet<Operation *> initializers = closure({root});
  SmallVector<Operation *> nonActive;
  for (auto function : designBlock.getOps<sim::SimFuncOp>())
    if (function.getHomeRegion() != sim::EventRegion::Active)
      nonActive.push_back(function);
  DenseSet<Operation *> nonActiveClosure = closure(std::move(nonActive));

  // 1. Candidate copy processes spawned by the root initializer.
  SmallVector<Candidate> candidates;
  root.walk([&](sim::SimSpawnOp spawn) {
    if (spawn->getParentRegion() != &root.getBody() ||
        !llvm::all_of(exits, [&](Operation *exit) {
          return dominance.dominates(spawn.getOperation(), exit);
        }))
      return;
    auto copy = symbols.lookup<sim::SimFuncOp>(spawn.getCallee());
    if (!copy || uses.lookup(copy) != 1 || !spawn.getProcess().use_empty())
      return;
    std::optional<CopyShape> shape = matchCopy(copy);
    if (!shape)
      return;
    std::optional<uint64_t> source =
        plainStorageCapture(copy, shape->sourceArgument);
    std::optional<uint64_t> destination =
        plainStorageCapture(copy, shape->destinationArgument);
    if (!source || !destination || *source == *destination ||
        spawn.getOperands().size() != copy.getNumArguments())
      return;
    auto bound = [&](unsigned index, uint64_t descriptor) {
      auto storage =
          spawn.getOperands()[index].getDefiningOp<sim::SimContextStorageOp>();
      return storage && storage.getId() == descriptor;
    };
    if (!bound(shape->sourceArgument, *source) ||
        !bound(shape->destinationArgument, *destination))
      return;
    Candidate candidate;
    candidate.spawn = spawn;
    candidate.copy = copy;
    candidate.source = *source;
    candidate.destination = *destination;
    // Element connections change the consumer's capture shape; generated
    // eval does not certify every such body yet (see the pass option).
    if (!elements &&
        (!shape->sourcePath.empty() || !shape->destinationPath.empty()))
      return;
    if (shape->destinationPath.empty()) {
      candidate.sinkElimination = true;
      candidate.eliminated = *destination;
      candidate.target = *source;
      candidate.path = shape->sourcePath;
    } else {
      candidate.sinkElimination = false;
      candidate.eliminated = *source;
      candidate.target = *destination;
      candidate.path = shape->destinationPath;
    }
    candidates.push_back(std::move(candidate));
  });
  if (candidates.empty())
    return;
  numCandidates += candidates.size();

  // A root may be eliminated by at most one equation.
  DenseMap<uint64_t, unsigned> eliminations;
  for (const Candidate &candidate : candidates)
    ++eliminations[candidate.eliminated];
  for (Candidate &candidate : candidates)
    candidate.rejected |= eliminations[candidate.eliminated] != 1;

  // Port reflection represents packed offsets, not unpacked element paths.
  // Propagate this constraint through whole-root chains before rewriting so
  // a reflected alias never points at an unrepresentable view.
  DenseSet<uint64_t> reflected;
  for (auto port : designBlock.getOps<sim::SimPortDeclOp>())
    if (!port.getSourceIsNet())
      reflected.insert(port.getSourceId());
  bool reflectionChanged = true;
  while (reflectionChanged) {
    reflectionChanged = false;
    for (const Candidate &candidate : candidates)
      if (!candidate.rejected && reflected.contains(candidate.eliminated))
        reflectionChanged |= reflected.insert(candidate.target).second;
  }

  // Static storage checks and types.
  for (Candidate &candidate : candidates) {
    auto eliminated = storages.find(candidate.eliminated);
    auto target = storages.find(candidate.target);
    if (eliminated == storages.end() || target == storages.end()) {
      candidate.rejected = true;
      continue;
    }
    sim::SimStorageDeclOp x = eliminated->second, r = target->second;
    auto projected = projectPath(r.getType(), candidate.path);
    std::optional<schedule::ComputeObservabilityKind> xObserved =
        x.getObservability();
    std::optional<schedule::ComputeObservabilityKind> rObserved =
        r.getObservability();
    candidate.rejected |=
        x.getLifetime() != sim::Lifetime::Design ||
        r.getLifetime() != sim::Lifetime::Design ||
        (xObserved &&
         *xObserved != schedule::ComputeObservabilityKind::Invisible) ||
        (rObserved &&
         *rObserved ==
             schedule::ComputeObservabilityKind::ExternallyWritable) ||
        (reflected.contains(candidate.eliminated) && !candidate.path.empty() &&
         !sim::getPackedWidth(r.getType())) ||
        !projected || projected->second != x.getType();
  }

  // Every capture of an eliminated root must be a plain whole capture; a
  // pre-existing view of it would need view composition.
  DenseSet<uint64_t> viewed;
  for (auto function : designBlock.getOps<sim::SimFuncOp>())
    for (unsigned index = 0; index < function.getNumArguments(); ++index) {
      auto kind = function.getArgAttrOfType<IntegerAttr>(
          index, sim::metadata::captureKind);
      auto descriptor = function.getArgAttrOfType<IntegerAttr>(
          index, sim::metadata::descriptorId);
      if (kind && kind.getInt() == int32_t(sim::CaptureKind::Storage) &&
          descriptor &&
          (hasViewAttributes(function, index) || function.isExternal()))
        viewed.insert(descriptor.getUInt());
    }
  for (Candidate &candidate : candidates)
    candidate.rejected |= viewed.contains(candidate.eliminated);

  DenseSet<uint64_t> interesting;
  for (const Candidate &candidate : candidates)
    if (!candidate.rejected) {
      interesting.insert(candidate.eliminated);
      interesting.insert(candidate.target);
    }
  if (interesting.empty())
    return;

  // 2. Whole-design access inventory, per function. Unknown writes abandon
  // the transformation: they may target any eliminated sink.
  analysis::HandleDataflowAnalysis provenanceAnalysis(design);
  DenseMap<Operation *, SmallVector<Access>> accesses;
  DenseMap<Operation *, DenseSet<uint64_t>> readRoots, writtenRoots;
  bool unknownWrite = false;
  for (auto function : designBlock.getOps<sim::SimFuncOp>()) {
    if (function.isExternal())
      continue;
    analysis::HandleFacts provenance = provenanceAnalysis.derive(function);
    SmallVector<Access> &found = accesses[function];
    function.walk([&](Operation *operation) {
      if (operation == function.getOperation())
        return;
      if (auto effects = dyn_cast<MemoryEffectOpInterface>(operation)) {
        SmallVector<MemoryEffects::EffectInstance> instances;
        effects.getEffects(instances);
        for (const auto &effect : instances) {
          if (!isa<MemoryEffects::Write>(effect.getEffect()) ||
              !isa<sim::StorageResource>(effect.getResource()))
            continue;
          if (isa<sim::SimRefStoreOp, sim::SimRefCopyOp, sim::SimOverrideOp,
                  sim::SimDynamicOverrideOp, sim::SimReleaseOverrideOp,
                  sim::SimRefStoreInertialPathOp>(operation))
            continue;
          if (auto call = dyn_cast<sim::SimCallOp>(operation)) {
            auto callee = symbols.lookup<sim::SimFuncOp>(call.getCallee());
            unknownWrite |= !callee || callee.isExternal();
            continue;
          }
          if (auto call = dyn_cast<sim::SimTaskCallOp>(operation)) {
            auto callee = symbols.lookup<sim::SimFuncOp>(call.getCallee());
            unknownWrite |= !callee || callee.isExternal();
            continue;
          }
          unknownWrite = true;
        }
      }
      for (OpOperand &use : operation->getOpOperands()) {
        Value operand = use.get();
        if (!isa<sim::RefType>(operand.getType()))
          continue;
        auto fact = provenance.find(operand);
        if (fact != provenance.end() &&
            fact->second.resource != schedule::ComputeResourceKind::Storage &&
            fact->second.resource != schedule::ComputeResourceKind::Unknown)
          continue;
        bool reads = isa<sim::SimRefLoadOp, sim::SimSuspendChangeOp,
                         sim::SimSuspendEdgeOp, sim::SimSuspendAnyOp,
                         sim::SimSuspendObserveOp, sim::SimRefSubelementOp,
                         sim::SimRefExtractOp, sim::SimRefDynExtractOp,
                         sim::SimRefArrayElementOp>(operation);
        if (fact == provenance.end() || !fact->second.descriptor ||
            fact->second.resource != schedule::ComputeResourceKind::Storage) {
          // A formal reference is charged at its call site below. Any other
          // unresolved writer could alias an eliminated sink.
          if (fact != provenance.end() && fact->second.formal)
            continue;
          if (!reads)
            unknownWrite = true;
          continue;
        }
        const analysis::HandleFact &span = fact->second;
        if (!isa<sim::SimRefSubelementOp, sim::SimRefExtractOp,
                 sim::SimRefDynExtractOp, sim::SimRefArrayElementOp,
                 sim::SimSpawnOp, sim::SimCallOp, sim::SimTaskCallOp>(
                operation)) {
          if (isa<sim::SimRefLoadOp, sim::SimSuspendChangeOp,
                  sim::SimSuspendEdgeOp, sim::SimSuspendAnyOp,
                  sim::SimSuspendObserveOp>(operation))
            readRoots[function].insert(*span.descriptor);
          else
            writtenRoots[function].insert(*span.descriptor);
        }
        if (!interesting.contains(*span.descriptor))
          continue;
        // Views are transparent; their consumers are visited separately.
        if (isa<sim::SimRefSubelementOp, sim::SimRefExtractOp,
                sim::SimRefDynExtractOp, sim::SimRefArrayElementOp>(operation))
          continue;
        Access access{*span.descriptor, span.low, span.width,
                      Access::Kind::Write};
        if (isa<sim::SimRefLoadOp, sim::SimSuspendChangeOp,
                sim::SimSuspendEdgeOp, sim::SimSuspendAnyOp,
                sim::SimSuspendObserveOp>(operation)) {
          access.kind = Access::Kind::Read;
        } else if (auto copy = dyn_cast<sim::SimRefCopyOp>(operation)) {
          if (copy.getSource() == operand && copy.getDestination() != operand)
            access.kind = Access::Kind::Read;
        } else if (isa<sim::SimOverrideOp, sim::SimDynamicOverrideOp,
                       sim::SimReleaseOverrideOp>(operation)) {
          access.kind = Access::Kind::Override;
        } else if (isa<sim::SimNBAEnqueueOp>(operation)) {
          access.nonblocking = true;
        } else if (isa<sim::SimSpawnOp, sim::SimCallOp, sim::SimTaskCallOp>(
                       operation)) {
          // A storage capture of the same descriptor is analyzed inside the
          // callee. Other reference arguments can be read, written or forced
          // through a formal; conservatively pin either side of the alias.
          StringRef calleeName;
          if (auto spawn = dyn_cast<sim::SimSpawnOp>(operation))
            calleeName = spawn.getCallee();
          else if (auto call = dyn_cast<sim::SimCallOp>(operation))
            calleeName = call.getCallee();
          else
            calleeName = cast<sim::SimTaskCallOp>(operation).getCallee();
          auto callee = symbols.lookup<sim::SimFuncOp>(calleeName);
          unsigned index = use.getOperandNumber();
          if (callee && !callee.isExternal() &&
              index < callee.getNumArguments()) {
            auto descriptor = callee.getArgAttrOfType<IntegerAttr>(
                index, sim::metadata::descriptorId);
            auto kind = callee.getArgAttrOfType<IntegerAttr>(
                index, sim::metadata::captureKind);
            if (kind && kind.getInt() == int32_t(sim::CaptureKind::Storage) &&
                descriptor && descriptor.getUInt() == *span.descriptor)
              continue;
          }
          access.kind = Access::Kind::Override;
        }
        found.push_back(access);
      }
    });
  }
  if (unknownWrite)
    return;

  auto overlaps = [](uint64_t lowA, uint64_t widthA, uint64_t lowB,
                     uint64_t widthB) {
    return widthA && widthB &&
           (lowA <= lowB ? lowB - lowA < widthA : lowA - lowB < widthB);
  };

  // A process that reads storage it also writes settles against itself.
  // Generated eval owns such a settling body only while its inputs arrive
  // through private copies; keep the copies feeding it.
  DenseSet<Operation *> selfSettling;
  for (auto &[function, reads] : readRoots) {
    auto writes = writtenRoots.find(function);
    if (writes != writtenRoots.end() &&
        llvm::any_of(reads, [&](uint64_t descriptor) {
          return writes->second.contains(descriptor);
        }))
      selfSettling.insert(function);
  }
  // A root written by delay-driven code (a testbench clock or stimulus) is
  // published by the runtime calendar, which walks every subscriber of that
  // root. Keep the first copy after it: downstream sinks alias that copy's
  // storage, whose fanout generated code publishes as ready-bit words.
  DenseSet<uint64_t> calendarWritten;
  for (auto &[function, written] : writtenRoots) {
    bool delayed = false;
    function->walk([&](sim::SimSuspendDelayOp) { delayed = true; });
    if (delayed)
      calendarWritten.insert(written.begin(), written.end());
  }
  // Periodic-clock extraction later publishes the first copy's sink from the
  // runtime too, so keep one more copy below a calendar-driven root.
  DenseSet<uint64_t> calendarPublished(calendarWritten.begin(),
                                       calendarWritten.end());
  for (const Candidate &candidate : candidates)
    if (calendarWritten.contains(candidate.source))
      calendarPublished.insert(candidate.destination);
  for (Candidate &candidate : candidates)
    if (candidate.sinkElimination &&
        calendarPublished.contains(candidate.target))
      candidate.rejected = true;

  for (Candidate &candidate : candidates) {
    if (candidate.rejected)
      continue;
    for (auto &[function, list] : accesses)
      if (selfSettling.contains(function) &&
          llvm::any_of(list, [&](const Access &access) {
            return access.descriptor == candidate.eliminated &&
                   access.kind == Access::Kind::Read;
          }))
        candidate.rejected = true;
  }

  // 3. Validate until the accepted set is stable: a rejected copy's own store
  // is an additional writer of its sink.
  bool changed = true;
  while (changed) {
    changed = false;
    DenseSet<Operation *> accepted;
    DenseSet<uint64_t> sinkEliminated, sourceTargets;
    for (const Candidate &candidate : candidates)
      if (!candidate.rejected) {
        accepted.insert(candidate.copy);
        if (candidate.sinkElimination)
          sinkEliminated.insert(candidate.eliminated);
        else
          sourceTargets.insert(candidate.target);
      }
    // Element spans of accepted source eliminations per target root.
    DenseMap<uint64_t, SmallVector<std::pair<uint64_t, uint64_t>>> claims;
    for (const Candidate &candidate : candidates) {
      if (candidate.rejected || candidate.sinkElimination)
        continue;
      auto projected =
          projectPath(storages[candidate.target].getType(), candidate.path);
      auto width = sim::getProvenanceSpan(projected->second);
      claims[candidate.target].push_back({projected->first, width.value_or(0)});
    }
    for (Candidate &candidate : candidates) {
      if (candidate.rejected)
        continue;
      bool reject = false;
      uint64_t x = candidate.eliminated, r = candidate.target;
      auto projected = projectPath(storages[r].getType(), candidate.path);
      uint64_t low = projected->first;
      uint64_t width =
          sim::getProvenanceSpan(projected->second).value_or(UINT64_MAX);
      // A root cannot both lose its storage as a sink and absorb a child's
      // output storage.
      reject |= sinkEliminated.contains(x) && sourceTargets.contains(x);
      reject |= sinkEliminated.contains(r) && !candidate.sinkElimination;
      if (!candidate.sinkElimination) {
        unsigned overlapping = 0;
        for (auto [claimLow, claimWidth] : claims[r])
          overlapping += overlaps(low, width, claimLow, claimWidth);
        reject |= overlapping != 1;
      }
      for (auto &[function, list] : accesses) {
        bool isRoot = initializers.contains(function);
        if (accepted.contains(function))
          continue;
        for (const Access &access : list) {
          if (access.descriptor != x && access.descriptor != r)
            continue;
          // LRM 4.5 drains the Reactive region set before returning to
          // Active. An Active port copy cannot run immediately after a
          // Reactive writer, including a helper called by that writer.
          if (access.kind != Access::Kind::Read &&
              nonActiveClosure.contains(function)) {
            reject = true;
            break;
          }
          if (access.kind == Access::Kind::Override) {
            reject = true;
            break;
          }
          if (access.descriptor == x) {
            // The eliminated sink has the copy as its only driver. A root
            // read during initialization precedes every process (6.8).
            if (candidate.sinkElimination && access.kind == Access::Kind::Write)
              reject = true;
            // Generated NBA commit needs one semantic root per site. Sibling
            // instances writing elements of one parent array would share it,
            // so keep nonblocking outputs on their own storage.
            if (!candidate.sinkElimination && access.nonblocking)
              reject = true;
            // A process that reads storage it also writes would read and
            // write the shared root after aliasing. Its old self-dependence
            // was private to one root; keep such outputs separate rather
            // than create a settling cycle on the shared root.
            if (!candidate.sinkElimination &&
                access.kind == Access::Kind::Read &&
                llvm::any_of(list, [&](const Access &other) {
                  return other.descriptor == x &&
                         other.kind == Access::Kind::Write;
                }))
              reject = true;
            // Symmetrically, a writer of the source that reads the sink would
            // read its own writes once they share storage.
            if (candidate.sinkElimination &&
                access.kind == Access::Kind::Read &&
                llvm::any_of(list, [&](const Access &other) {
                  return other.descriptor == r &&
                         other.kind == Access::Kind::Write;
                }))
              reject = true;
            if (isRoot && access.kind == Access::Kind::Read)
              reject = true;
          } else if (!candidate.sinkElimination &&
                     overlaps(access.low, access.width, low, width)) {
            // The target element is driven only by this output connection.
            if (access.kind == Access::Kind::Write ||
                (isRoot && access.kind == Access::Kind::Read))
              reject = true;
          }
          if (reject)
            break;
        }
        if (reject)
          break;
      }
      if (reject) {
        candidate.rejected = true;
        changed = true;
      }
    }
  }

  // 4. Resolve chains `x := r[p]`, `r := r'[p']` into surviving roots.
  DenseMap<uint64_t, const Candidate *> byEliminated;
  for (const Candidate &candidate : candidates)
    if (!candidate.rejected)
      byEliminated[candidate.eliminated] = &candidate;
  if (byEliminated.empty())
    return;
  llvm::MapVector<uint64_t, ResolvedView> resolved;
  for (auto &[eliminated, candidate] : byEliminated) {
    (void)candidate;
    ResolvedView view{eliminated, {}};
    DenseSet<uint64_t> visiting;
    bool cycle = false;
    while (auto found = byEliminated.lookup(view.root)) {
      if (!visiting.insert(view.root).second) {
        cycle = true;
        break;
      }
      SmallVector<int64_t> path(found->path);
      llvm::append_range(path, view.path);
      view.path = std::move(path);
      view.root = found->target;
    }
    if (!cycle)
      resolved[eliminated] = std::move(view);
  }

  // 5. Rewrite every reference to an eliminated root. Captures are lifted to
  // whole captures of the surviving root; each body selects its element with
  // a constant `ref.subelement`, the same form a source-level constant select
  // of a captured array has. Spawn/call operands that bind another lifted
  // capture keep passing the whole root.
  MLIRContext *context = design.getContext();
  for (Candidate &candidate : candidates)
    if (!candidate.rejected && resolved.count(candidate.eliminated)) {
      candidate.spawn.erase();
      ++numAliased;
    }
  DenseMap<Operation *, SmallVector<std::pair<unsigned, uint64_t>>> lifted;
  for (auto function : designBlock.getOps<sim::SimFuncOp>())
    for (unsigned index = 0; index < function.getNumArguments(); ++index)
      if (std::optional<uint64_t> descriptor =
              plainStorageCapture(function, index);
          descriptor && resolved.count(*descriptor))
        lifted[function].push_back({index, *descriptor});
  auto bindsLiftedCapture = [&](OpOperand &use) {
    StringRef calleeName;
    Operation *owner = use.getOwner();
    if (auto spawn = dyn_cast<sim::SimSpawnOp>(owner))
      calleeName = spawn.getCallee();
    else if (auto call = dyn_cast<sim::SimCallOp>(owner))
      calleeName = call.getCallee();
    else if (auto call = dyn_cast<sim::SimTaskCallOp>(owner))
      calleeName = call.getCallee();
    else
      return false;
    Operation *callee = symbols.lookup(calleeName);
    auto found = lifted.find(callee);
    return found != lifted.end() &&
           llvm::any_of(found->second, [&](auto entry) {
             return entry.first == use.getOperandNumber();
           });
  };
  // Replace `value` (a ref to eliminated storage) by `whole` (a ref to the
  // surviving root), projecting it for every use other than a lifted binding.
  auto redirect = [&](Value value, Value whole, const ResolvedView &view,
                      OpBuilder &builder, Location loc) {
    if (view.path.empty()) {
      value.replaceAllUsesWith(whole);
      return;
    }
    Value element = sim::SimRefSubelementOp::create(
        builder, loc, value.getType(), whole,
        builder.getDenseI64ArrayAttr(view.path));
    value.replaceUsesWithIf(element, [&](OpOperand &use) {
      return use.getOwner() != element.getDefiningOp() &&
             !bindsLiftedCapture(use);
    });
    value.replaceUsesWithIf(whole, [&](OpOperand &use) {
      return use.getOwner() != element.getDefiningOp();
    });
  };
  SmallVector<sim::SimContextStorageOp> storageOps;
  design.walk([&](sim::SimContextStorageOp storage) {
    if (resolved.count(storage.getId()))
      storageOps.push_back(storage);
  });
  for (sim::SimContextStorageOp storage : storageOps) {
    const ResolvedView &view = resolved.find(storage.getId())->second;
    OpBuilder builder(storage);
    Type rootType = storages[view.root].getType();
    Value whole = sim::SimContextStorageOp::create(
        builder, storage.getLoc(), sim::RefType::get(context, rootType),
        storage->getOperand(0), builder.getI64IntegerAttr(view.root));
    redirect(storage.getResult(), whole, view, builder, storage.getLoc());
    storage.erase();
  }
  for (auto &[operation, arguments] : lifted) {
    auto function = cast<sim::SimFuncOp>(operation);
    SmallVector<Type> inputs(function.getArgumentTypes());
    Block &entry = function.getBody().front();
    OpBuilder builder = OpBuilder::atBlockBegin(&entry);
    for (auto [index, descriptor] : arguments) {
      const ResolvedView &view = resolved.find(descriptor)->second;
      function.setArgAttr(index, sim::metadata::descriptorId,
                          builder.getI64IntegerAttr(view.root));
      if (view.path.empty())
        continue;
      Type rootRef = sim::RefType::get(context, storages[view.root].getType());
      BlockArgument argument = entry.getArgument(index);
      Type original = argument.getType();
      argument.setType(rootRef);
      inputs[index] = rootRef;
      // The projection keeps the element type for every original use.
      Value element = sim::SimRefSubelementOp::create(
          builder, function.getLoc(), original, argument,
          builder.getDenseI64ArrayAttr(view.path));
      argument.replaceUsesWithIf(element, [&](OpOperand &use) {
        return use.getOwner() != element.getDefiningOp() &&
               !bindsLiftedCapture(use);
      });
    }
    function.setFunctionType(FunctionType::get(
        context, inputs, function.getFunctionType().getResults()));
  }
  // Reflection must keep pointing at the live storage, including its offset.
  for (auto port : designBlock.getOps<sim::SimPortDeclOp>()) {
    if (port.getSourceIsNet())
      continue;
    auto found = resolved.find(port.getSourceId());
    if (found == resolved.end())
      continue;
    auto projected =
        projectPath(storages[found->second.root].getType(), found->second.path);
    port.setSourceLow(port.getSourceLow() + projected->first);
    port.setSourceId(found->second.root);
  }
}

} // namespace
} // namespace obelisk
