//===- SimulationAOTPlanning.cpp - Native AOT plan derivation -----------===//

#include "../../../Conversion/SimulationToLLVMCoroutine/SimulationAOTPlanning.h"
#include "../../../Conversion/SimulationToLLVMCoroutine/SimulationToLLVMCoroutinePrivate.h"
#include "obelisk/Dialect/Schedule/ScheduleFields.h"

#include "obelisk/Analysis/SimulationAnalysis.h"
#include "obelisk/Analysis/SimulationScheduleAnalysis.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Runtime/StableHandle.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/SymbolTable.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringMap.h"

using namespace mlir;

namespace obelisk::detail {

LogicalResult
specializeNativeAOTCaptures(ModuleOp module,
                            const analysis::NativeAOTAnalysis &eligibility) {
  (void)eligibility;
  sim::SimFuncOp root;
  module.walk([&](sim::SimFuncOp function) {
    if (function.getEntryKind() == sim::EntryKind::RootInitializer)
      root = function;
  });
  if (!root)
    return module.emitError(
        "cannot specialize AOT captures without a root initializer");
  // Capture rewriting changes operands and signatures, never symbol identity.
  // Keep lookup local to each design while sharing its index across actors.
  SymbolTableCollection symbols;

  // Capture addressing is independent of scheduler eligibility.  A callee
  // with exactly one whole-design spawn has the same fixed context object on
  // every activation even when conditional waits or control loops keep that
  // actor on the generic scheduler.  Duplicate or dynamic spawns remain on
  // ordinary frame captures.
  llvm::StringMap<unsigned> spawnCounts;
  module.walk([&](sim::SimSpawnOp spawn) { ++spawnCounts[spawn.getCallee()]; });

  WalkResult specialized = root.walk([&](sim::SimSpawnOp spawn) {
    sim::SimDesignOp design = spawn->getParentOfType<sim::SimDesignOp>();
    sim::SimFuncOp target = design ? symbols.lookupSymbolIn<sim::SimFuncOp>(
                                         design, spawn.getCalleeAttr())
                                   : nullptr;
    if (!target || spawnCounts.lookup(spawn.getCallee()) != 1)
      return WalkResult::advance();
    Block &entry = target.getBody().front();
    if (spawn.getNumOperands() != entry.getNumArguments()) {
      spawn.emitOpError("AOT capture specialization found an invalid arity");
      return WalkResult::interrupt();
    }
    if (entry.getNumArguments() == 0 ||
        !isa<sim::ContextType>(entry.getArgument(0).getType())) {
      target.emitOpError(
          "AOT capture specialization requires a context entry capture");
      return WalkResult::interrupt();
    }

    sim::SimFuncOp evalBody;
    if (auto evalBodyRef =
            ::obelisk::schedule::get<::obelisk::schedule::Field::EvalBody>(
                target)) {
      evalBody = symbols.lookupSymbolIn<sim::SimFuncOp>(design, evalBodyRef);
      if (!evalBody || evalBody.getBody().front().getNumArguments() !=
                           entry.getNumArguments()) {
        target.emitOpError("AOT eval body has an invalid capture signature");
        return WalkResult::interrupt();
      }
    }

    for (unsigned index = 1; index != entry.getNumArguments(); ++index) {
      Operation *producer = spawn.getOperand(index).getDefiningOp();
      if (!producer ||
          !isa<sim::SimContextStorageOp, sim::SimContextNetOp,
               sim::SimContextDriverOp, sim::SimContextEventOp>(producer))
        continue;
      if (producer->getNumOperands() != 1 ||
          producer->getOperand(0) != spawn.getOperand(0) ||
          producer->getNumResults() != 1 ||
          producer->getResult(0) != spawn.getOperand(index))
        continue;

      auto specializeFunctionArgument = [&](sim::SimFuncOp function) {
        Block &functionEntry = function.getBody().front();
        SmallVector<OpOperand *> uses;
        for (OpOperand &use : functionEntry.getArgument(index).getUses())
          uses.push_back(&use);
        DenseMap<Block *, Value> specializedByBlock;
        for (OpOperand *use : uses) {
          Block *block = use->getOwner()->getBlock();
          auto [position, inserted] =
              specializedByBlock.try_emplace(block, Value{});
          if (inserted) {
            OpBuilder builder(function.getContext());
            builder.setInsertionPointToStart(block);
            IRMapping mapping;
            mapping.map(spawn.getOperand(0), functionEntry.getArgument(0));
            position->second = builder.clone(*producer, mapping)->getResult(0);
          }
          use->set(position->second);
        }
      };
      specializeFunctionArgument(target);
      if (evalBody)
        specializeFunctionArgument(evalBody);
    }
    if (evalBody) {
      // Eval bodies consume persistent captures through canonical state
      // projections after specialization. Keep the required context operand,
      // but do not carry dead actor-frame captures into the native hot ABI.
      llvm::BitVector erase(evalBody.getNumArguments());
      for (BlockArgument argument : evalBody.getArguments())
        if (argument.getArgNumber() != 0 && argument.use_empty())
          erase.set(argument.getArgNumber());
      if (erase.any() && failed(evalBody.eraseArguments(erase))) {
        evalBody.emitOpError("could not prune unused AOT eval captures");
        return WalkResult::interrupt();
      }
    }
    return WalkResult::advance();
  });
  if (specialized.wasInterrupted())
    return failure();

  // Body fusion may leave large activation bodies outlined behind an
  // instance coordinator.  Their capture operands are the same fixed context
  // projections proven above, but they are now reached by sim.call rather
  // than sim.spawn.  Specialize singleton direct callees as well so keeping a
  // large body out of line does not turn every state access back into dynamic
  // stable-handle decoding.
  SmallVector<sim::SimCallOp> calls;
  module.walk([&](sim::SimCallOp call) { calls.push_back(call); });
  for (sim::SimCallOp call : calls) {
    if (call.getNumOperands() == 0)
      continue;
    sim::SimDesignOp design = call->getParentOfType<sim::SimDesignOp>();
    sim::SimFuncOp callee = design ? symbols.lookupSymbolIn<sim::SimFuncOp>(
                                         design, call.getCalleeAttr())
                                   : sim::SimFuncOp{};
    if (!callee || callee.isExternal() ||
        SymbolTable::getSymbolVisibility(callee) !=
            SymbolTable::Visibility::Private ||
        callee.getNumArguments() != call.getNumOperands() ||
        callee.getNumArguments() == 0 ||
        !isa<sim::ContextType>(callee.getArgument(0).getType()))
      continue;
    auto getStaticProjection = [&](Value actual) -> Operation * {
      Operation *producer = actual.getDefiningOp();
      if (!producer ||
          !isa<sim::SimContextStorageOp, sim::SimContextNetOp,
               sim::SimContextDriverOp, sim::SimContextEventOp>(producer) ||
          producer->getNumOperands() != 1 ||
          producer->getOperand(0) != call.getOperand(0) ||
          producer->getNumResults() != 1 || producer->getResult(0) != actual)
        return nullptr;
      return producer;
    };
    // Pure value calls cannot change their ABI here. Do not scan the design's
    // symbol uses unless at least one operand is a specializable projection.
    if (llvm::none_of(call.getOperands().drop_front(), [&](Value actual) {
          return getStaticProjection(actual) != nullptr;
        }))
      continue;

    // Rewriting the private function ABI is valid only when this exact call
    // is its sole symbol use.  Counting sim.call operations is insufficient:
    // spawn/callback/eval metadata may reference the same symbol while still
    // requiring its original signature.
    std::optional<SymbolTable::UseRange> symbolUses =
        SymbolTable::getSymbolUses(callee, design);
    if (!symbolUses)
      continue;
    auto use = symbolUses->begin();
    if (use == symbolUses->end() || use->getUser() != call ||
        ++use != symbolUses->end())
      continue;

    llvm::BitVector erase(callee.getNumArguments());
    for (unsigned index = 1; index != callee.getNumArguments(); ++index) {
      Value actual = call.getOperand(index);
      Operation *producer = getStaticProjection(actual);
      if (!producer)
        continue;

      BlockArgument argument = callee.getArgument(index);
      SmallVector<OpOperand *> uses;
      for (OpOperand &use : argument.getUses())
        uses.push_back(&use);
      DenseMap<Block *, Value> specializedByBlock;
      for (OpOperand *use : uses) {
        Block *block = use->getOwner()->getBlock();
        auto [position, inserted] =
            specializedByBlock.try_emplace(block, Value{});
        if (inserted) {
          OpBuilder builder(callee.getContext());
          builder.setInsertionPointToStart(block);
          IRMapping mapping;
          mapping.map(call.getOperand(0), callee.getArgument(0));
          position->second = builder.clone(*producer, mapping)->getResult(0);
        }
        use->set(position->second);
      }
      if (argument.use_empty())
        erase.set(index);
    }
    if (!erase.any())
      continue;
    if (failed(callee.eraseArguments(erase)))
      return callee.emitOpError(
                 "could not prune specialized direct-call captures"),
             failure();
    call->eraseOperands(erase);
  }
  return success();
}
} // namespace obelisk::detail
