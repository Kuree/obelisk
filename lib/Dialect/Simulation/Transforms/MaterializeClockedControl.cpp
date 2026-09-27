//===- MaterializeClockedControl.cpp - Explicit clock-control state --------===//

#include "obelisk/Dialect/Simulation/Transforms/Passes.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"

using namespace mlir;

namespace obelisk {
#define GEN_PASS_DEF_OBELISKSIMMATERIALIZECLOCKEDCONTROLPASS
#include "obelisk/Dialect/Simulation/Transforms/Passes.h.inc"

namespace {
class ObeliskSimMaterializeClockedControlPass final
    : public impl::ObeliskSimMaterializeClockedControlPassBase<
          ObeliskSimMaterializeClockedControlPass> {
  void runOnOperation() override;
};

void ObeliskSimMaterializeClockedControlPass::runOnOperation() {
  sim::SimDesignOp design = getOperation();
  // This executable rewrite must precede persistent graph construction.
  if (design->hasAttr("compute_graph"))
    return;
  DenseMap<StringAttr, SmallVector<sim::SimSpawnOp>> spawns;
  design.walk([&](sim::SimSpawnOp spawn) {
    spawns[spawn.getCalleeAttr().getAttr()].push_back(spawn);
  });
  uint64_t nextStorage = 0;
  for (auto declaration :
       design.getBody().front().getOps<sim::SimStorageDeclOp>())
    nextStorage = std::max(nextStorage, declaration.getId() + 1);

  for (auto function : design.getBody().front().getOps<sim::SimFuncOp>()) {
    if (function.isExternal() ||
        (function.getEntryKind() != sim::EntryKind::Initial &&
         !function->hasAttr(sim::metadata::periodicControl)) ||
        function->hasAttr(sim::metadata::clockedControl))
      continue;
    auto found = spawns.find(function.getSymNameAttr());
    if (found == spawns.end() || found->second.size() != 1)
      continue;
    sim::SimSpawnOp spawn = found->second.front();
    auto parent = spawn->getParentOfType<sim::SimFuncOp>();
    // One unconditional root spawn, with no externally observable process
    // handle. A shared slot is not valid for reentrant/dynamically spawned
    // code.
    if (!parent || parent.getEntryKind() != sim::EntryKind::RootInitializer ||
        spawn->getBlock() != &parent.getBody().front() ||
        !spawn.getResult().use_empty())
      continue;
    Block &entry = function.getBody().front();
    if (entry.getNumArguments() == 0 ||
        !isa<sim::ContextType>(entry.getArgument(0).getType()))
      continue;
    SmallVector<sim::SimSuspendEdgeOp> waits;
    bool supported = true;
    function.walk([&](Operation *operation) {
      if (!sim::isSuspensionOp(operation))
        return;
      auto wait = dyn_cast<sim::SimSuspendEdgeOp>(operation);
      if (!wait || operation->getParentRegion() != &function.getBody() ||
          wait.getResumeRegionAttr()) {
        supported = false;
        return;
      }
      waits.push_back(wait);
    });
    if (!supported || waits.empty())
      continue;
    auto watched = dyn_cast<BlockArgument>(waits.front().getWatched());
    if (!watched || watched.getOwner() != &entry)
      continue;
    llvm::DenseSet<Block *> destinations;
    bool hasState = waits.size() > 1;
    for (auto wait : waits) {
      Block *resume = wait.getContinuation();
      if (wait.getWatched() != watched ||
          wait.getEdge() != waits.front().getEdge() || !resume->hasOneUse() ||
          !destinations.insert(resume).second)
        supported = false;
      for (Value operand : wait.getContinuationOperands()) {
        auto type = dyn_cast<IntegerType>(operand.getType());
        if (!type || type.getWidth() > 64)
          supported = false;
        hasState = true;
      }
    }
    if (!supported || !hasState)
      continue;
    std::optional<uint64_t> scope;
    for (auto declaration :
         design.getBody().front().getOps<sim::SimCodeUnitDeclOp>())
      if (function.getCodeUnitId() == declaration.getId())
        scope = declaration.getScopeId();
    if (!scope)
      continue;

    OpBuilder declarationBuilder(function);
    auto makeSlot = [&](Type type) -> uint64_t {
      uint64_t id = nextStorage++;
      sim::SimStorageDeclOp::create(
          declarationBuilder, function.getLoc(), id, *scope, type,
          sim::Lifetime::Design, StringAttr{}, StringAttr{},
          sim::ComputeObservabilityKindAttr::get(
              design.getContext(), sim::ComputeObservabilityKind::Invisible),
          sim::VPITypeSemanticsAttr{});
      return id;
    };
    // Rematerialize immutable handles at each access rather than threading
    // them through the wait as new coroutine-frame arguments.
    auto reference = [&](OpBuilder &builder, Location loc, uint64_t id,
                         Type type) -> Value {
      return sim::SimContextStorageOp::create(
          builder, loc, sim::RefType::get(design.getContext(), type),
          entry.getArgument(0), id);
    };
    Type phaseType = declarationBuilder.getI32Type();
    uint64_t phase = makeSlot(phaseType);
    // Zero is retired. Preserve termination on fallback, and make later native
    // clock activations inert without replaying terminal side effects.
    SmallVector<sim::SimReturnOp> returns;
    function.walk([&](sim::SimReturnOp op) { returns.push_back(op); });
    for (auto op : returns) {
      OpBuilder builder(op);
      Value zero = arith::ConstantIntOp::create(builder, op.getLoc(), 0, 32);
      sim::SimRefStoreOp::create(
          builder, op.getLoc(), zero,
          reference(builder, op.getLoc(), phase, phaseType));
    }

    auto *commonWait = new Block;
    auto *dispatch = new Block;
    auto *retired = new Block;
    function.getBody().push_back(commonWait);
    function.getBody().push_back(dispatch);
    function.getBody().push_back(retired);
    OpBuilder builder = OpBuilder::atBlockEnd(commonWait);
    auto merged = sim::SimSuspendEdgeOp::create(
        builder, waits.front().getLoc(), waits.front().getEdge(), watched,
        ValueRange{}, waits.front().getSiteAttr(), sim::EventRegionAttr{},
        dispatch);
    if (waits.front()->hasAttr(sim::metadata::proceduralEventWait))
      merged->setAttr(sim::metadata::proceduralEventWait,
                      builder.getUnitAttr());
    builder.setInsertionPointToEnd(retired);
    sim::SimReturnOp::create(builder, function.getLoc(), ValueRange{});

    for (auto [index, wait] : llvm::enumerate(waits)) {
      auto *resume = new Block;
      auto *next = index + 1 == waits.size() ? retired : new Block;
      function.getBody().push_back(resume);
      if (next != retired)
        function.getBody().push_back(next);
      OpBuilder atWait(wait);
      OpBuilder atResume = OpBuilder::atBlockEnd(resume);
      SmallVector<Value> operands;
      for (Value operand : wait.getContinuationOperands()) {
        uint64_t slot = makeSlot(operand.getType());
        sim::SimRefStoreOp::create(
            atWait, wait.getLoc(), operand,
            reference(atWait, wait.getLoc(), slot, operand.getType()));
        operands.push_back(sim::SimRefLoadOp::create(
            atResume, wait.getLoc(), operand.getType(),
            reference(atResume, wait.getLoc(), slot, operand.getType())));
      }
      Value phaseValue =
          arith::ConstantIntOp::create(atWait, wait.getLoc(), index + 1, 32);
      sim::SimRefStoreOp::create(
          atWait, wait.getLoc(), phaseValue,
          reference(atWait, wait.getLoc(), phase, phaseType));
      cf::BranchOp::create(atWait, wait.getLoc(), commonWait);
      cf::BranchOp::create(atResume, wait.getLoc(), wait.getContinuation(),
                           operands);
      builder.setInsertionPointToEnd(dispatch);
      Value current = sim::SimRefLoadOp::create(
          builder, wait.getLoc(), phaseType,
          reference(builder, wait.getLoc(), phase, phaseType));
      Value expected =
          arith::ConstantIntOp::create(builder, wait.getLoc(), index + 1, 32);
      Value matches = arith::CmpIOp::create(
          builder, wait.getLoc(), arith::CmpIPredicate::eq, current, expected);
      cf::CondBranchOp::create(builder, wait.getLoc(), matches, resume, next);
      dispatch = next;
      wait.erase();
    }
    function->setAttr(sim::metadata::clockedControl, builder.getUnitAttr());
  }
}
} // namespace
} // namespace obelisk
