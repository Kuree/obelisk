//===- ExtractPeriodicClocks.cpp - Normalize uniform delay processes
//-------===//

#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Dialect/Simulation/Transforms/Passes.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"

using namespace mlir;

namespace obelisk {
#define GEN_PASS_DEF_OBELISKSIMEXTRACTPERIODICCLOCKSPASS
#include "obelisk/Dialect/Simulation/Transforms/Passes.h.inc"

namespace {
class ObeliskSimExtractPeriodicClocksPass final
    : public impl::ObeliskSimExtractPeriodicClocksPassBase<
          ObeliskSimExtractPeriodicClocksPass> {
public:
  using Base::Base;
  void runOnOperation() override;
};

// IEEE 1800-2023 9.4.1-9.4.2: replace a positive constant delay only when
// each private clock edge resumes the source at that same simulation time.
// Cutting every delay edge must leave an acyclic CFG with no terminal paths.
// Thus every activation reaches its next delay, and the cadence cannot outlive
// the source process or run ahead of a source stuck in a zero-time loop.
bool hasUniformCadence(sim::SimFuncOp function,
                       SmallVectorImpl<sim::SimSuspendDelayOp> &delays) {
  uint64_t period = 0;
  unsigned stores = 0, loads = 0, inversions = 0;
  bool hasClockWrite = false;
  DenseMap<Block *, unsigned> predecessors;
  for (Block &block : function.getBody()) {
    predecessors.try_emplace(&block, 0);
    for (Operation &operation : block) {
      if (operation.getNumRegions())
        return false;
      if (auto delay = dyn_cast<sim::SimSuspendDelayOp>(operation)) {
        auto constant =
            delay.getDelay().getDefiningOp<sim::SimTimeConstantOp>();
        if (!constant || !constant.getValue() ||
            (period && period != constant.getValue()) ||
            delay.getResumeRegionAttr() ||
            !delay.getContinuationOperands().empty() ||
            !delay.getContinuation()->hasOneUse())
          return false;
        period = constant.getValue();
        delays.push_back(delay);
        continue;
      }
      if (sim::isSuspensionOp(&operation))
        return false;
      if (isa<cf::BranchOp, cf::CondBranchOp>(operation))
        continue;
      Value written;
      if (auto store = dyn_cast<sim::SimRefStoreOp>(operation)) {
        written = store.getValue();
        ++stores;
      } else if (auto nba = dyn_cast<sim::SimNBAEnqueueOp>(operation)) {
        if (nba.getDelay() || nba.getClockingOutputAttr())
          return false;
        written = nba.getValue();
      } else if (isa<sim::SimRefLoadOp>(operation)) {
        ++loads;
      } else if (!isMemoryEffectFree(&operation) ||
                 operation.hasTrait<OpTrait::IsTerminator>()) {
        return false;
      }
      if (written) {
        auto logic = dyn_cast<sim::LogicType>(written.getType());
        hasClockWrite |=
            written.getType().isInteger(1) || (logic && logic.getWidth() == 1);
      }
      inversions += isa<sim::SimLogicUnaryOp, arith::XOrIOp>(operation);
    }
    if (!isa<sim::SimSuspendDelayOp>(block.getTerminator()))
      for (Block *successor : block.getSuccessors())
        ++predecessors[successor];
  }
  if (delays.empty() || !hasClockWrite)
    return false;
  // Leave the already-recognized blocking toggle alone.
  if (delays.size() == 1 && stores == 1 && loads == 1 && inversions == 1)
    return false;
  SmallVector<Block *> ready;
  for (auto [block, count] : predecessors)
    if (!count)
      ready.push_back(block);
  unsigned visited = 0;
  while (!ready.empty()) {
    Block *block = ready.pop_back_val();
    ++visited;
    if (!isa<sim::SimSuspendDelayOp>(block->getTerminator()))
      for (Block *successor : block->getSuccessors())
        if (!--predecessors[successor])
          ready.push_back(successor);
  }
  return visited == predecessors.size();
}

void ObeliskSimExtractPeriodicClocksPass::runOnOperation() {
  sim::SimDesignOp design = getOperation();
  if (vpi != "off" || design.getComputeGraphAttr())
    return;
  // IEEE 1800-2023 9.6.2, 9.7: an extra timer must not survive a source
  // killed/suspended by another process. Exclude process control and disable.
  bool controlled = false;
  DenseMap<StringAttr, SmallVector<sim::SimSpawnOp>> spawns;
  design.walk([&](Operation *operation) {
    controlled |= isa<sim::SimProcessControlOp, sim::SimControlDisableOp,
                      sim::SimDisableChildrenOp>(operation);
    if (auto spawn = dyn_cast<sim::SimSpawnOp>(operation))
      spawns[spawn.getCalleeAttr().getAttr()].push_back(spawn);
  });
  if (controlled)
    return;
  uint64_t nextStorage = 0, nextCodeUnit = 1;
  DenseMap<uint64_t, uint64_t> scopes;
  for (auto declaration :
       design.getBody().front().getOps<sim::SimStorageDeclOp>())
    nextStorage = std::max(nextStorage, declaration.getId() + 1);
  for (auto declaration :
       design.getBody().front().getOps<sim::SimCodeUnitDeclOp>())
    scopes[declaration.getId()] = declaration.getScopeId();
  SymbolTable symbols(design);
  SmallVector<sim::SimFuncOp> functions(
      design.getBody().front().getOps<sim::SimFuncOp>());
  for (sim::SimFuncOp function : functions) {
    if (function.isExternal() ||
        (function.getEntryKind() != sim::EntryKind::Initial &&
         function.getEntryKind() != sim::EntryKind::Always) ||
        function.getDomain() != sim::ExecutionDomain::Design ||
        function.getHomeRegion() != sim::EventRegion::Active)
      continue;
    auto found = spawns.find(function.getSymNameAttr());
    if (found == spawns.end() || found->second.size() != 1)
      continue;
    sim::SimSpawnOp spawn = found->second.front();
    auto parent = spawn->getParentOfType<sim::SimFuncOp>();
    if (!parent || parent.getEntryKind() != sim::EntryKind::RootInitializer ||
        spawn->getBlock() != &parent.getBody().front() ||
        !spawn.getResult().use_empty())
      continue;
    Block &entry = function.getBody().front();
    if (!entry.getNumArguments() ||
        !isa<sim::ContextType>(entry.getArgument(0).getType()))
      continue;
    auto scope = scopes.find(function.getCodeUnitId().value_or(0));
    if (scope == scopes.end())
      continue;
    SmallVector<sim::SimSuspendDelayOp> delays;
    if (!hasUniformCadence(function, delays))
      continue;
    auto uses = SymbolTable::getSymbolUses(function, &design.getBody());
    if (!uses || llvm::any_of(*uses, [&](const SymbolTable::SymbolUse &use) {
          return use.getUser() != spawn.getOperation() ||
                 use.getSymbolRef() != spawn.getCalleeAttr();
        }))
      continue;

    Location loc = function.getLoc();
    OpBuilder builder = OpBuilder::atBlockEnd(&design.getBody().front());
    Type bit = builder.getI1Type();
    Type referenceType = sim::RefType::get(design.getContext(), bit);
    uint64_t storage = nextStorage++;
    sim::SimStorageDeclOp::create(
        builder, loc, storage, scope->second, bit, sim::Lifetime::Design,
        StringAttr{}, StringAttr{},
        sim::ComputeObservabilityKindAttr::get(
            design.getContext(), sim::ComputeObservabilityKind::Invisible),
        sim::VPITypeSemanticsAttr{});
    while (scopes.contains(nextCodeUnit))
      ++nextCodeUnit;
    uint64_t codeUnit = nextCodeUnit++;
    std::string name = "__obelisk_periodic_tick_" + std::to_string(codeUnit);
    SmallVector<NamedAttribute> attrs{builder.getNamedAttr(
        "code_unit_id", builder.getI64IntegerAttr(codeUnit))};
    DictionaryAttr contextAttrs =
        builder.getDictionaryAttr({builder.getNamedAttr(
            sim::metadata::captureKind, builder.getI32IntegerAttr(0))});
    DictionaryAttr refAttrs = builder.getDictionaryAttr(
        {builder.getNamedAttr(sim::metadata::captureKind,
                              builder.getI32IntegerAttr(3)),
         builder.getNamedAttr(sim::metadata::descriptorId,
                              builder.getI64IntegerAttr(storage))});
    auto tick = sim::SimFuncOp::create(
        builder, loc, name,
        builder.getFunctionType({entry.getArgument(0).getType(), referenceType},
                                {}),
        sim::EntryKind::Always, attrs,
        ArrayRef<DictionaryAttr>{contextAttrs, refAttrs});
    symbols.insert(tick);
    SymbolTable::setSymbolVisibility(tick, SymbolTable::Visibility::Private);
    sim::SimCodeUnitDeclOp::create(
        builder, loc, codeUnit, scope->second, sim::EntryKind::Always,
        tick.getSymNameAttr(), StringAttr{}, builder.getUnitAttr());
    Block *tickEntry = &tick.getBody().front();
    auto *wait = new Block;
    auto *toggle = new Block;
    tick.getBody().push_back(wait);
    tick.getBody().push_back(toggle);
    builder.setInsertionPointToEnd(tickEntry);
    cf::BranchOp::create(builder, loc, wait);
    builder.setInsertionPointToEnd(wait);
    Value period = sim::SimTimeConstantOp::create(
        builder, loc,
        delays.front()
            .getDelay()
            .getDefiningOp<sim::SimTimeConstantOp>()
            .getValue());
    sim::SimSuspendDelayOp::create(
        builder, loc, period, delays.front().getTimingAttr(), ValueRange{},
        sim::ContinuationSiteAttr{}, sim::EventRegionAttr{}, toggle);
    builder.setInsertionPointToEnd(toggle);
    Value old =
        sim::SimRefLoadOp::create(builder, loc, bit, tickEntry->getArgument(1));
    Value one = arith::ConstantIntOp::create(builder, loc, 1, 1);
    Value next = arith::XOrIOp::create(builder, loc, old, one);
    sim::SimRefStoreOp::create(builder, loc, next, tickEntry->getArgument(1));
    cf::BranchOp::create(builder, loc, wait);

    // IEEE 1800-2023 4.5, 9.4.1: root initialization starts both actors at
    // the same simulation time. Source statements retain their order (4.6(a));
    // in particular, public clock NBAs still update in the NBA region (10.4.2).
    builder.setInsertionPoint(spawn);
    Value ref = sim::SimContextStorageOp::create(
        builder, loc, referenceType, spawn.getOperands().front(), storage);
    Value zero = arith::ConstantIntOp::create(builder, loc, 0, 1);
    sim::SimRefStoreOp::create(builder, loc, zero, ref);
    sim::SimSpawnOp::create(builder, loc, tick.getSymNameAttr(),
                            ValueRange{spawn.getOperands().front(), ref},
                            ArrayAttr{}, ArrayAttr{});
    if (failed(function.insertArgument(entry.getNumArguments(), referenceType,
                                       refAttrs, loc))) {
      signalPassFailure();
      return;
    }
    Value watched = entry.getArguments().back();
    spawn.getOperandsMutable().append(ref);
    if (auto argAttrs = spawn.getArgAttrsAttr()) {
      SmallVector<Attribute> expanded(argAttrs.getValue());
      expanded.push_back(refAttrs);
      spawn.setArgAttrsAttr(builder.getArrayAttr(expanded));
    }
    for (auto delay : delays) {
      builder.setInsertionPoint(delay);
      auto wait = sim::SimSuspendEdgeOp::create(
          builder, delay.getLoc(), sim::EdgeKind::Both, watched, ValueRange{},
          delay.getSiteAttr(), sim::EventRegionAttr{}, delay.getContinuation());
      wait->setAttr(sim::metadata::proceduralEventWait, builder.getUnitAttr());
      delay.erase();
    }
    function->setAttr(sim::metadata::periodicControl, builder.getUnitAttr());
    // The effect summary predates the new private subscription.
    function->removeAttr("effect_summary");
  }
}
} // namespace
} // namespace obelisk
