//===- SimulationEvalDispatchMaterialization.cpp ----------------------===//

#include "SimulationAOTPlanning.h"
#include "SimulationEvalNBAQueue.h"
#include "SimulationEvalReadySet.h"
#include "SimulationToLLVMCoroutinePrivate.h"
#include "obelisk/Dialect/Schedule/ScheduleEnums.h"
#include "obelisk/Dialect/Schedule/ScheduleFields.h"
#include "obelisk/Dialect/Schedule/ScheduleMetadata.h"

#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Runtime/Runtime.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/SymbolTable.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringMap.h"

#include <map>

using namespace mlir;

namespace obelisk::detail {
namespace {

constexpr StringLiteral evalFastNBARootsName =
    "__obelisk_eval_fast_nba_roots_v1";
constexpr StringLiteral promotionPendingMaskName =
    "__obelisk_eval_promotion_pending_mask_v1";
constexpr StringLiteral periodicTerminationName =
    "__obelisk_periodic_termination_v1";
constexpr StringLiteral nbaDirtyRootsName = "__obelisk_aot_nba_dirty_roots_v1";
constexpr StringLiteral nbaCommitName = "__obelisk_aot_static_nba_commit_v1";
constexpr StringLiteral nbaKnownName = "__obelisk_eval_nba_known_v1";
constexpr StringLiteral evalFourStateNBAHandoffName =
    "__obelisk_eval_four_state_nba_handoff_v1";

} // namespace

LogicalResult
materializeNativeEvalDispatch(ModuleOp module,
                              const NativeEvalCoordinatorPlan &plan) {
  StringRef functionName = evalDispatchName;
  ArrayRef<std::string> executors = plan.fourStateExecutors;
  ArrayRef<NativeEvalClockKernel> clockKernels = plan.clockKernels;
  ArrayRef<obelisk_rt_native_merged_fragment> mergedFragments = plan.fragments;
  ArrayRef<std::string> mergedTwoStateExecutors = plan.twoStateExecutors;
  ArrayRef<APInt> ownerSubsumptionMasks = plan.ownerSubsumptionMasks;
  uint32_t nbaTaintWordCount = plan.nbaTaintWordCount;
  bool prioritySignalHandoff = plan.prioritySignalHandoff;
  unsigned ownerCount = std::max<size_t>(64, mergedFragments.size());
  APInt allOwners = APInt::getAllOnes(ownerCount);
  const runtime::ReadySetLayout readyLayout(ownerCount);
  APInt allowedOwnerMask = allOwners;

  MLIRContext *context = module.getContext();
  OpBuilder builder(context);
  Location location = module.getLoc();
  Type pointer = LLVM::LLVMPointerType::get(context);
  Type i32 = builder.getI32Type();
  Type i64 = builder.getI64Type();
  auto ownerMayTaintNBA = [&](unsigned owner) {
    return owner < plan.nbaTaintedOwners.size() &&
           plan.nbaTaintedOwners.test(owner);
  };
  auto markOwnerNBATaint = [&](unsigned owner) {
    if (!ownerMayTaintNBA(owner) || nbaTaintWordCount == 0)
      return;
    Value taintBase = LLVM::AddressOfOp::create(builder, location, pointer,
                                                evalStepFourStateNBARootsName);
    for (uint32_t word = 0; word != nbaTaintWordCount; ++word) {
      uint64_t mask = plan.nbaTaintMasks[owner][word];
      if (mask == 0)
        continue;
      Value address = byteGEP(builder, location, taintBase,
                              uint64_t{word} * sizeof(uint64_t));
      Value old = LLVM::LoadOp::create(builder, location, i64, address, 8);
      LLVM::StoreOp::create(
          builder, location,
          arith::OrIOp::create(builder, location, old,
                               llvmConstant(builder, location, i64, mask)),
          address, 8);
    }
  };

  if (clockKernels.empty() || mergedFragments.empty() ||
      executors.size() != mergedFragments.size() ||
      ownerSubsumptionMasks.size() != mergedFragments.size())
    return success();
  // Executor symbols are fixed inputs. The coordinator emitted below is not
  // part of this lookup snapshot, and no executor is added, renamed or erased.
  SymbolTable executorSymbols(module);
  builder.setInsertionPointToEnd(module.getBody());
  SmallVector<std::string> writeGuards;
  if (plan.writableVPI) {
    // IEEE 1800-2023 38.34 and 10.6: a deposit/force invalidates only the
    // affected source dependencies. Keep scheduling and knownness separate;
    // a known forced value still requires override-aware fine execution.
    for (unsigned owner = 0; owner != mergedFragments.size(); ++owner) {
      std::string name =
          "__obelisk_eval_write_guard_" + std::to_string(owner) + "_v1";
      auto guard = LLVM::LLVMFuncOp::create(
          builder, location, name,
          LLVM::LLVMFunctionType::get(builder.getI1Type(), {pointer}, false));
      writeGuards.push_back(name);
      Block *entry = guard.addEntryBlock(builder), *check = new Block,
            *clean = new Block;
      guard.getBody().push_back(check);
      guard.getBody().push_back(clean);
      builder.setInsertionPointToStart(entry);
      Value fast = LLVM::LoadOp::create(
          builder, location, i32,
          LLVM::AddressOfOp::create(builder, location, pointer,
                                    "__obelisk_static_specialization_fast_v1"),
          4);
      Value isClean =
          arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ne,
                                fast, llvmConstant(builder, location, i32, 0));
      cf::CondBranchOp::create(builder, location, isClean, clean, ValueRange{},
                               check, ValueRange{});
      builder.setInsertionPointToStart(clean);
      LLVM::ReturnOp::create(
          builder, location,
          llvmConstant(builder, location, builder.getI1Type(), 1));
      builder.setInsertionPointToStart(check);
      Value allowed = llvmConstant(builder, location, builder.getI1Type(), 1);
      for (uint32_t state : plan.ownerStaticStates[owner]) {
        Value root =
            LLVM::CallOp::create(
                builder, location, TypeRange{i32},
                "obelisk_rt_v1_static_specialization_guard",
                ValueRange{entry->getArgument(0),
                           llvmConstant(builder, location, i32, UINT32_MAX),
                           llvmConstant(builder, location, i32, state),
                           llvmConstant(builder, location, i32,
                                        OBELISK_RT_STATIC_ROOT_READ |
                                            OBELISK_RT_STATIC_ROOT_WRITE)})
                .getResult();
        allowed = arith::AndIOp::create(
            builder, location, allowed,
            arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ne,
                                  root,
                                  llvmConstant(builder, location, i32, 0)));
      }
      LLVM::ReturnOp::create(builder, location, allowed);
      builder.setInsertionPointAfter(guard);
    }
  }
  SmallVector<Type> coordinatorArguments{pointer, pointer};
  auto fastCoordinator = LLVM::LLVMFuncOp::create(
      builder, location, functionName,
      LLVM::LLVMFunctionType::get(i32, coordinatorArguments, false));
  ::obelisk::schedule::set<schedule::metadata::evalCallClosureRoot>(
      fastCoordinator, builder.getUnitAttr());
  Block *fastEntry = fastCoordinator.addEntryBlock(builder);
  Block *dispatch = new Block;
  Block *commit = new Block;
  Block *performCommit = new Block;
  Block *afterCommit = new Block;
  Block *complete = new Block;
  complete->addArgument(i32, location);
  Block *stopped = new Block;
  Block *failed = new Block;
  failed->addArgument(i32, location);
  fastCoordinator.getBody().push_back(dispatch);
  fastCoordinator.getBody().push_back(commit);
  fastCoordinator.getBody().push_back(performCommit);
  fastCoordinator.getBody().push_back(afterCommit);
  fastCoordinator.getBody().push_back(complete);
  fastCoordinator.getBody().push_back(stopped);
  fastCoordinator.getBody().push_back(failed);
  builder.setInsertionPointToStart(fastEntry);
  Value changed = entryAlloca(builder, location, i32, 1, 4);
  Value fourStateFallback =
      entryAlloca(builder, location, builder.getI1Type(), 1, 1);
  LLVM::StoreOp::create(
      builder, location,
      arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ne,
          LLVM::LoadOp::create(
              builder, location, builder.getI8Type(),
              LLVM::AddressOfOp::create(builder, location, pointer,
                                        evalStepFourStateFallbackName),
              1),
          llvmConstant(builder, location, builder.getI8Type(), 0))
          .getResult(),
      fourStateFallback, 1);
  auto ingressAddress = [&] {
    return LLVM::AddressOfOp::create(builder, location, pointer,
                                     evalModelIngressName)
        .getResult();
  };
  auto combinedIngress = [&] {
    return maskedOwnerWords(builder, location, ingressAddress(), allOwners);
  };
  auto ownerWritableClean = [&](unsigned owner, Value context) -> Value {
    return LLVM::CallOp::create(builder, location,
                                TypeRange{builder.getI1Type()},
                                writeGuards[owner], ValueRange{context})
        .getResult();
  };
  auto clearIngressMask = [&](const APInt &mask, Value condition = Value{}) {
    updateOwnerMask(builder, location, ingressAddress(), mask,
                    /*clear=*/true, condition, &readyLayout);
  };
  cf::BranchOp::create(builder, location, dispatch);
  builder.setInsertionPointToStart(dispatch);
  if (plan.hasOrderedNBA) {
    Value status = LLVM::LoadOp::create(
        builder, location, i32, evalNBAQueueField(builder, location, 3), 4);
    Value ok = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, status,
        llvmConstant(builder, location, i32, OBELISK_RT_OK));
    Block *proceed = new Block;
    fastCoordinator.getBody().push_back(proceed);
    cf::CondBranchOp::create(builder, location, ok, proceed, ValueRange{},
                             complete, ValueRange{status});
    builder.setInsertionPointToStart(proceed);
  }
  Block *scanReady = new Block;
  fastCoordinator.getBody().push_back(scanReady);
  if (prioritySignalHandoff) {
    Value prioritySignalPending =
        LLVM::CallOp::create(
            builder, location, TypeRange{i32},
            SymbolRefAttr::get(
                context, "obelisk_rt_v1_scheduler_priority_signal_pending"),
            ValueRange{fastEntry->getArgument(1)})
            .getResult();
    Value mustHandoff = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ne, prioritySignalPending,
        llvmConstant(builder, location, i32, 0));
    cf::CondBranchOp::create(
        builder, location, mustHandoff, complete,
        ValueRange{
            llvmConstant(builder, location, i32, OBELISK_RT_AOT_CHECKPOINT)},
        scanReady, ValueRange{});
  } else {
    cf::BranchOp::create(builder, location, scanReady);
  }

  builder.setInsertionPointToStart(scanReady);
  Value selectedWord, selectedReady;
  if (!readyLayout.hasCache()) {
    SmallVector<Value> ready;
    for (unsigned word = 0; word != allOwners.getNumWords(); ++word)
      ready.push_back(loadOwnerWord(builder, location, ingressAddress(), word));
    llvm::BitVector changedWords(ready.size());
    for (auto [recordIndex, record] : llvm::enumerate(mergedFragments)) {
      const APInt &subsumed = ownerSubsumptionMasks[recordIndex];
      if (subsumed.isZero() || !allowedOwnerMask[record.bit])
        continue;
      Value coordinatorPending = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ne,
          arith::AndIOp::create(builder, location, ready[record.bit / 64],
                                llvmConstant(builder, location, i64,
                                             uint64_t{1} << (record.bit % 64))),
          llvmConstant(builder, location, i64, 0));
      if (plan.writableVPI)
        coordinatorPending = arith::AndIOp::create(
            builder, location, coordinatorPending,
            ownerWritableClean(recordIndex, fastEntry->getArgument(1)));
      for (unsigned word = 0; word != ready.size(); ++word) {
        uint64_t members = ownerMaskWord(subsumed, word);
        if (!members)
          continue;
        changedWords.set(word);
        Value withoutMembers = arith::AndIOp::create(
            builder, location, ready[word],
            llvmConstant(builder, location, i64, ~members));
        ready[word] = arith::SelectOp::create(
            builder, location, coordinatorPending, withoutMembers, ready[word]);
      }
    }
    for (int word : changedWords.set_bits())
      LLVM::StoreOp::create(
          builder, location, ready[word],
          ownerWordAddress(builder, location, ingressAddress(), word), 8);
    selectedWord = llvmConstant(builder, location, i64, 0);
    selectedReady = ready.front();
  } else {
    std::tie(selectedWord, selectedReady) =
        findEvalReadyWord(builder, location, ingressAddress(), readyLayout);
  }
  Value empty = arith::CmpIOp::create(builder, location,
                                      arith::CmpIPredicate::eq, selectedReady,
                                      llvmConstant(builder, location, i64, 0));
  Block *select = new Block;
  fastCoordinator.getBody().push_back(select);
  cf::CondBranchOp::create(builder, location, empty, commit, ValueRange{},
                           select, ValueRange{});
  builder.setInsertionPointToStart(select);
  Value localBit = LLVM::CountTrailingZerosOp::create(builder, location, i64,
                                                      selectedReady, true);
  Value selectedBit = arith::AddIOp::create(
      builder, location, localBit,
      arith::MulIOp::create(builder, location, selectedWord,
                            llvmConstant(builder, location, i64, 64)));
  // Owners at or above firstDeferredBit resume in the Inactive region (IEEE
  // 1800-2023 4.4.2.3). Planning gives them the highest bits, so the lowest
  // pending bit is deferred only when no Active owner is pending: this
  // closure is quiescent. With a runtime calendar, runtime-owned Active and
  // Inactive work must also drain first, which the NBA cut below performs.
  bool runtimeCalendar =
      ::obelisk::schedule::has<::obelisk::schedule::Field::EvalRuntimeCalendar>(
          module);
  bool hasDeferred = plan.firstDeferredBit < mergedFragments.size();
  APInt deferredOwnerMask(ownerCount, 0);
  if (hasDeferred)
    deferredOwnerMask.setBits(plan.firstDeferredBit, mergedFragments.size());
  // A finish request ends the simulation before any remaining scheduled
  // event, deferred owners included (IEEE 1800-2023 9.2.3, 20.2). The NBA cut
  // reports no barrier the same way, so read the termination latch itself.
  auto terminationRequested = [&]() -> Value {
    getOrDeclareLLVMFunction(module,
                             "obelisk_rt_v1_scheduler_termination_requested",
                             i32, {pointer});
    Value latch =
        LLVM::CallOp::create(builder, location, TypeRange{i32},
                             "obelisk_rt_v1_scheduler_termination_requested",
                             ValueRange{fastEntry->getArgument(1)})
            .getResult();
    return arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ne,
                                 latch,
                                 llvmConstant(builder, location, i32, 0));
  };
  Block *switchBlock = select;
  Value bit = selectedBit;
  if (hasDeferred) {
    switchBlock = new Block;
    bit = switchBlock->addArgument(i64, location);
    fastCoordinator.getBody().push_back(switchBlock);
    Value deferred = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::uge, selectedBit,
        llvmConstant(builder, location, i64, plan.firstDeferredBit));
    if (runtimeCalendar) {
      cf::CondBranchOp::create(builder, location, deferred, commit,
                               ValueRange{}, switchBlock,
                               ValueRange{selectedBit});
    } else {
      Block *checkTermination = new Block;
      fastCoordinator.getBody().push_back(checkTermination);
      cf::CondBranchOp::create(builder, location, deferred, checkTermination,
                               ValueRange{}, switchBlock,
                               ValueRange{selectedBit});
      builder.setInsertionPointToStart(checkTermination);
      cf::CondBranchOp::create(builder, location, terminationRequested(),
                               stopped, ValueRange{}, switchBlock,
                               ValueRange{selectedBit});
    }
  }
  SmallVector<APInt> cases;
  SmallVector<Block *> destinations;
  SmallVector<ValueRange> destinationOperands;
  // Every entry policy shares these groups and their local domain proofs.
  auto rankedExecutor = [&](unsigned index) -> StringRef {
    return index < plan.rankedGroupExecutors.size()
               ? StringRef(plan.rankedGroupExecutors[index])
               : StringRef{};
  };
  // Records whose subsumption mask contains each owner bit, computed once:
  // scanning every record per owner is quadratic in the owner count.
  SmallVector<SmallVector<unsigned, 1>> supersedingRecords(ownerCount);
  for (auto [parent, parentRecord] : llvm::enumerate(mergedFragments)) {
    const APInt &members = ownerSubsumptionMasks[parent];
    if (!allowedOwnerMask[parentRecord.bit] || members.isZero())
      continue;
    for (unsigned word = 0; word != members.getNumWords(); ++word)
      for (uint64_t bits = members.getRawData()[word]; bits; bits &= bits - 1)
        supersedingRecords[word * 64 + llvm::countr_zero(bits)].push_back(
            parent);
  }
  // Where one owner's execution continues: dispatch the next ready owner,
  // fail with its status, or stop the generated loop.
  struct OwnerTargets {
    Region *body;
    Value context;
    Value fourStateFallback;
    Block *dispatch;
    Block *failed;
    Block *stopped;
  };
  const OwnerTargets coordinatorTargets{&fastCoordinator.getBody(),
                                        fastEntry->getArgument(1),
                                        fourStateFallback,
                                        dispatch,
                                        failed,
                                        stopped};
  // Owner execution code is linear in the owners that share no ranked group.
  // A cyclic island can hold tens of thousands of them, and one coordinator
  // body of that size can neither be divided among backend partitions nor
  // pass through superlinear LLVM transforms. Beyond one segment, emit them
  // in bounded segment functions that return OK to continue dispatch, or the
  // status for the coordinator to return.
  constexpr unsigned segmentShift = 9;
  unsigned directOwners = 0;
  for (auto [recordIndex, record] : llvm::enumerate(mergedFragments))
    directOwners += !executors[recordIndex].empty() &&
                    allowedOwnerMask[record.bit] &&
                    rankedExecutor(recordIndex).empty();
  const bool segmented = directOwners > (1u << segmentShift);
  struct Segment {
    LLVM::LLVMFuncOp function;
    OwnerTargets targets;
    SmallVector<APInt> cases;
    SmallVector<Block *> destinations;
  };
  std::map<uint64_t, Segment> segments;
  auto segmentFor = [&](uint64_t index) -> Segment & {
    auto [found, inserted] = segments.try_emplace(index);
    Segment &segment = found->second;
    if (!inserted)
      return segment;
    OpBuilder::InsertionGuard guard(builder);
    builder.setInsertionPointToEnd(module.getBody());
    segment.function = LLVM::LLVMFuncOp::create(
        builder, location,
        (Twine(functionName) + ".segment_" + Twine(index)).str(),
        LLVM::LLVMFunctionType::get(i32, {pointer, i64, pointer}, false));
    Block *entry = segment.function.addEntryBlock(builder);
    Block *resume = new Block, *segmentFailed = new Block,
          *segmentStopped = new Block;
    segmentFailed->addArgument(i32, location);
    segment.function.getBody().push_back(resume);
    segment.function.getBody().push_back(segmentFailed);
    segment.function.getBody().push_back(segmentStopped);
    builder.setInsertionPointToStart(resume);
    LLVM::ReturnOp::create(builder, location,
                           llvmConstant(builder, location, i32, OBELISK_RT_OK));
    builder.setInsertionPointToStart(segmentFailed);
    LLVM::ReturnOp::create(builder, location, segmentFailed->getArgument(0));
    builder.setInsertionPointToStart(segmentStopped);
    LLVM::ReturnOp::create(
        builder, location,
        llvmConstant(builder, location, i32, OBELISK_RT_TIER_UNAVAILABLE));
    segment.targets = {&segment.function.getBody(),
                       entry->getArgument(0),
                       entry->getArgument(2),
                       resume,
                       segmentFailed,
                       segmentStopped};
    return segment;
  };
  llvm::StringMap<Block *> rankedEntries;
  for (auto [recordIndex, record] : llvm::enumerate(mergedFragments)) {
    if (executors[recordIndex].empty())
      continue;
    if (!allowedOwnerMask[record.bit])
      continue;
    StringRef ranked = rankedExecutor(recordIndex);
    if (!ranked.empty()) {
      auto [entry, inserted] = rankedEntries.try_emplace(ranked, nullptr);
      if (inserted) {
        Block *execute = new Block, *poll = new Block;
        fastCoordinator.getBody().push_back(execute);
        fastCoordinator.getBody().push_back(poll);
        entry->second = execute;
        builder.setInsertionPointToStart(execute);
        LLVM::CallOp::create(builder, location, TypeRange{},
                             SymbolRefAttr::get(context, ranked),
                             ValueRange{fastEntry->getArgument(1)});
        // Finite sweeps return here even for a true oscillator. Consume no
        // owner after the call: backward publications belong to the next
        // activation. Termination is polled at this real group boundary.
        Value address = LLVM::LoadOp::create(
            builder, location, pointer,
            LLVM::AddressOfOp::create(builder, location, pointer,
                                      periodicTerminationName),
            8);
        Value absent = LLVM::ICmpOp::create(
            builder, location, LLVM::ICmpPredicate::eq, address,
            LLVM::ZeroOp::create(builder, location, pointer));
        cf::CondBranchOp::create(builder, location, absent, dispatch,
                                 ValueRange{}, poll, ValueRange{});
        builder.setInsertionPointToStart(poll);
        Value stopping = arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::ne,
            LLVM::LoadOp::create(builder, location, i32, address, 4),
            llvmConstant(builder, location, i32, 0));
        cf::CondBranchOp::create(builder, location, stopping, stopped,
                                 ValueRange{}, dispatch, ValueRange{});
      }
      cases.push_back(APInt(64, record.bit));
      destinations.push_back(entry->second);
      destinationOperands.push_back(ValueRange{});
      continue;
    }
    // Owners above one segment execute in their segment function.
    Segment *segment =
        segmented ? &segmentFor(record.bit >> segmentShift) : nullptr;
    const OwnerTargets &targets =
        segment ? segment->targets : coordinatorTargets;
    Block *execute = new Block;
    targets.body->push_back(execute);
    if (segment) {
      segment->cases.push_back(APInt(64, record.bit));
      segment->destinations.push_back(execute);
    } else {
      cases.push_back(APInt(64, record.bit));
      destinations.push_back(execute);
      destinationOperands.push_back(ValueRange{});
    }
    builder.setInsertionPointToStart(execute);
    if (readyLayout.hasCache()) {
      APInt superseding;
      if (!supersedingRecords[record.bit].empty()) {
        superseding = APInt(ownerCount, 0);
        for (unsigned parent : supersedingRecords[record.bit])
          superseding.setBit(mergedFragments[parent].bit);
      }
      if (plan.writableVPI) {
        Value parents = llvmConstant(builder, location, i64, 0);
        for (unsigned parent : supersedingRecords[record.bit]) {
          Value pending = maskedOwnerWords(
              builder, location, ingressAddress(),
              APInt::getOneBitSet(ownerCount, mergedFragments[parent].bit));
          pending = arith::SelectOp::create(
              builder, location, ownerWritableClean(parent, targets.context),
              pending, llvmConstant(builder, location, i64, 0));
          parents = arith::OrIOp::create(builder, location, parents, pending);
        }
        // The ordinary normalization below must not drop exact work when its
        // containing Tier-1 owner has lost only its scheduling/override proof.
        superseding.clearAllBits();
        Value covered = arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::ne, parents,
            llvmConstant(builder, location, i64, 0));
        Block *skip = new Block, *uncovered = new Block;
        targets.body->push_back(skip);
        targets.body->push_back(uncovered);
        cf::CondBranchOp::create(builder, location, covered, skip, ValueRange{},
                                 uncovered, ValueRange{});
        builder.setInsertionPointToStart(skip);
        clearIngressMask(APInt::getOneBitSet(ownerCount, record.bit));
        cf::BranchOp::create(builder, location, targets.dispatch);
        builder.setInsertionPointToStart(uncovered);
      }
      if (!superseding.isZero()) {
        // Normalize only the selected exact owner. Eagerly walking every
        // subsumption relation at every dispatch is quadratic in a large
        // clock group. Do not jump straight to its parent: unrelated lower
        // ready owners must retain their deterministic priority.
        Value parents =
            maskedOwnerWords(builder, location, ingressAddress(), superseding);
        Value covered = arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::ne, parents,
            llvmConstant(builder, location, i64, 0));
        Block *skip = new Block;
        Block *uncovered = new Block;
        targets.body->push_back(skip);
        targets.body->push_back(uncovered);
        cf::CondBranchOp::create(builder, location, covered, skip, ValueRange{},
                                 uncovered, ValueRange{});
        builder.setInsertionPointToStart(skip);
        clearIngressMask(APInt::getOneBitSet(ownerCount, record.bit));
        cf::BranchOp::create(builder, location, targets.dispatch);
        builder.setInsertionPointToStart(uncovered);
      }
    }
    if (plan.writableVPI) {
      Block *clean = new Block, *fine = new Block;
      targets.body->push_back(clean);
      targets.body->push_back(fine);
      cf::CondBranchOp::create(builder, location,
                               ownerWritableClean(recordIndex, targets.context),
                               clean, ValueRange{}, fine, ValueRange{});
      builder.setInsertionPointToStart(fine);
      // Consume only this activation. Exact existing ingress is retained;
      // clock-only full sweeps seed the original clock-triggered fine entries.
      // Do not replay upstream producers after an internal VPI deposit (38.34).
      clearIngressMask(APInt::getOneBitSet(ownerCount, record.bit));
      if (!ownerSubsumptionMasks[recordIndex].isZero()) {
        updateOwnerMask(builder, location, ingressAddress(),
                        plan.ownerFineIngress[recordIndex], /*clear=*/false, {},
                        &readyLayout);
        cf::BranchOp::create(builder, location, targets.dispatch);
      } else {
        LLVM::StoreOp::create(
            builder, location,
            llvmConstant(builder, location, builder.getI1Type(), 1),
            targets.fourStateFallback, 1);
        if (ownerMayTaintNBA(recordIndex))
          markOwnerNBATaint(recordIndex);
        Value status =
            LLVM::CallOp::create(
                builder, location, TypeRange{i32},
                "obelisk_rt_v1_scheduler_execute_aot_actor",
                ValueRange{targets.context, llvmConstant(builder, location, i32,
                                                         record.actor_slot)})
                .getResult();
        Value ok = arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::eq, status,
            llvmConstant(builder, location, i32, OBELISK_RT_OK));
        cf::CondBranchOp::create(builder, location, ok, targets.dispatch,
                                 ValueRange{}, targets.failed,
                                 ValueRange{status});
      }
      builder.setInsertionPointToStart(clean);
    }
    Value ownerBit =
        llvmConstant(builder, location, i64, uint64_t{1} << (record.bit % 64));
    Value selectedPromotionPending;
    {
      Value pending = LLVM::AddressOfOp::create(builder, location, pointer,
                                                promotionPendingMaskName);
      selectedPromotionPending = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ne,
          arith::AndIOp::create(
              builder, location, ownerBit,
              loadOwnerWord(builder, location, pending, record.bit / 64)),
          llvmConstant(builder, location, i64, 0));
    }
    if (mergedTwoStateExecutors[recordIndex].empty()) {
      LLVM::StoreOp::create(
          builder, location,
          llvmConstant(builder, location, builder.getI1Type(), 1),
          targets.fourStateFallback, 1);
      if (ownerMayTaintNBA(recordIndex))
        markOwnerNBATaint(recordIndex);
    }
    auto executor =
        executorSymbols.lookup<LLVM::LLVMFuncOp>(executors[recordIndex]);
    auto twoStateExecutor = executorSymbols.lookup<LLVM::LLVMFuncOp>(
        mergedTwoStateExecutors[recordIndex]);
    bool convergenceOwner =
        (executor &&
         ::obelisk::schedule::has<schedule::metadata::evalTier2Convergence>(
             executor)) ||
        (twoStateExecutor &&
         ::obelisk::schedule::has<schedule::metadata::evalTier2Convergence>(
             twoStateExecutor));
    // A convergence owner consumes its old dirty bit before executing. Any
    // transition published by the activation then remains queued and drives
    // another local fixpoint iteration. Non-convergence owners retain the
    // clock-kernel rule below, which suppresses their implicit self fanout.
    const APInt &subsumedOwnerMask = ownerSubsumptionMasks[recordIndex];
    APInt consumed = subsumedOwnerMask;
    consumed.setBit(record.bit);
    if (convergenceOwner)
      clearIngressMask(consumed);
    Value executeStatus;
    if (!mergedTwoStateExecutors[recordIndex].empty()) {
      Block *checkPromotion = new Block;
      Block *executeFourState = new Block;
      Block *executeTwoState = new Block;
      Block *executeJoin = new Block;
      executeJoin->addArgument(i32, location);
      targets.body->push_back(checkPromotion);
      targets.body->push_back(executeFourState);
      targets.body->push_back(executeTwoState);
      targets.body->push_back(executeJoin);
      cf::CondBranchOp::create(builder, location, selectedPromotionPending,
                               checkPromotion, ValueRange{}, executeTwoState,
                               ValueRange{});
      builder.setInsertionPointToStart(checkPromotion);
      Value kernelReady =
          LLVM::CallOp::create(
              builder, location, TypeRange{builder.getI1Type()},
              SymbolRefAttr::get(context, kernelPromotionReadyName),
              ValueRange{llvmConstant(builder, location, i64, recordIndex)})
              .getResult();
      cf::CondBranchOp::create(builder, location, kernelReady, executeTwoState,
                               ValueRange{}, executeFourState, ValueRange{});
      builder.setInsertionPointToStart(executeFourState);
      LLVM::StoreOp::create(
          builder, location,
          llvmConstant(builder, location, builder.getI1Type(), 1),
          targets.fourStateFallback, 1);
      if (ownerMayTaintNBA(recordIndex))
        markOwnerNBATaint(recordIndex);
      Value fourStateStatus =
          LLVM::CallOp::create(
              builder, location, TypeRange{i32},
              SymbolRefAttr::get(context, executors[recordIndex]),
              ValueRange{targets.context})
              .getResult();
      cf::BranchOp::create(builder, location, executeJoin,
                           ValueRange{fourStateStatus});
      builder.setInsertionPointToStart(executeTwoState);
      Value twoStateStatus =
          LLVM::CallOp::create(
              builder, location, TypeRange{i32},
              SymbolRefAttr::get(context, mergedTwoStateExecutors[recordIndex]),
              ValueRange{targets.context})
              .getResult();
      ::obelisk::schedule::set<
          ::obelisk::schedule::Field::EvalProvenTwoStateCall>(
          twoStateStatus.getDefiningOp(), builder.getUnitAttr());
      cf::BranchOp::create(builder, location, executeJoin,
                           ValueRange{twoStateStatus});
      builder.setInsertionPointToStart(executeJoin);
      executeStatus = executeJoin->getArgument(0);
    } else {
      executeStatus = LLVM::CallOp::create(
                          builder, location, TypeRange{i32},
                          SymbolRefAttr::get(context, executors[recordIndex]),
                          ValueRange{targets.context})
                          .getResult();
    }
    if (!convergenceOwner)
      clearIngressMask(consumed);
    else if (subsumedOwnerMask != 0) {
      Value queuedMembers = maskedOwnerWords(
          builder, location, ingressAddress(), subsumedOwnerMask);
      Value membersPending = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ne, queuedMembers,
          llvmConstant(builder, location, i64, 0));
      clearIngressMask(subsumedOwnerMask);
      updateOwnerMask(builder, location, ingressAddress(),
                      APInt::getOneBitSet(ownerCount, record.bit),
                      /*clear=*/false, membersPending, &readyLayout);
    }
    Value executeOK = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, executeStatus,
        llvmConstant(builder, location, i32, OBELISK_RT_OK));
    bool mayTerminate =
        (executor &&
         ::obelisk::schedule::has<schedule::metadata::evalMayTerminate>(
             executor)) ||
        (twoStateExecutor &&
         ::obelisk::schedule::has<schedule::metadata::evalMayTerminate>(
             twoStateExecutor));
    // A convergence owner may republish itself indefinitely. Poll the
    // branch-only termination word after each member activation, before the
    // coordinator follows the dirty-mask backedge. Waiting for the commit
    // boundary cannot interrupt a genuinely oscillating SCC because that
    // boundary is reachable only after its ready bit becomes clear.
    mayTerminate |= convergenceOwner;
    bool infallible =
        executor &&
        ::obelisk::schedule::has<schedule::metadata::evalInfallible>(
            executor) &&
        (!twoStateExecutor ||
         ::obelisk::schedule::has<schedule::metadata::evalInfallible>(
             twoStateExecutor));
    if (infallible && !mayTerminate) {
      cf::BranchOp::create(builder, location, targets.dispatch);
      continue;
    }
    if (!mayTerminate) {
      cf::CondBranchOp::create(builder, location, executeOK, targets.dispatch,
                               ValueRange{}, targets.failed,
                               ValueRange{executeStatus});
      continue;
    }
    Block *checkTermination = new Block;
    Block *loadTermination = new Block;
    targets.body->push_back(checkTermination);
    if (loadTermination)
      targets.body->push_back(loadTermination);
    cf::CondBranchOp::create(builder, location, executeOK, checkTermination,
                             ValueRange{}, targets.failed,
                             ValueRange{executeStatus});
    builder.setInsertionPointToStart(checkTermination);
    Value terminationAddress =
        LLVM::LoadOp::create(builder, location, pointer,
                             LLVM::AddressOfOp::create(builder, location,
                                                       pointer,
                                                       periodicTerminationName),
                             8)
            .getResult();
    Value noTerminationAddress = LLVM::ICmpOp::create(
        builder, location, LLVM::ICmpPredicate::eq, terminationAddress,
        LLVM::ZeroOp::create(builder, location, pointer));
    cf::CondBranchOp::create(builder, location, noTerminationAddress,
                             targets.dispatch, ValueRange{}, loadTermination,
                             ValueRange{});
    builder.setInsertionPointToStart(loadTermination);
    Value termination =
        LLVM::LoadOp::create(builder, location, i32, terminationAddress, 4);
    Value stopping = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ne, termination,
        llvmConstant(builder, location, i32, 0));
    cf::CondBranchOp::create(builder, location, stopping, targets.stopped,
                             ValueRange{}, targets.dispatch, ValueRange{});
  }
  Block *otherOwner = stopped;
  if (segmented) {
    // A segmented owner bit selects its segment; the segment's own switch
    // stops on a bit that names no owner, exactly as the coordinator does.
    otherOwner = new Block;
    fastCoordinator.getBody().push_back(otherOwner);
    SmallVector<APInt> segmentCases;
    SmallVector<Block *> segmentCalls;
    for (auto &[index, segment] : segments) {
      builder.setInsertionPointToEnd(&segment.function.getBody().front());
      LLVM::SwitchOp::create(
          builder, location, segment.function.getArgument(1),
          segment.targets.stopped, ValueRange{}, segment.cases,
          segment.destinations,
          SmallVector<ValueRange>(segment.cases.size(), ValueRange{}),
          ArrayRef<int32_t>{});
      // Infallible owners never branch to the failure exit. Do not leave an
      // unreachable block for later lowering to skip.
      for (Block *exit : {segment.targets.dispatch, segment.targets.failed})
        if (exit->hasNoPredecessors())
          exit->erase();
      Block *call = new Block;
      fastCoordinator.getBody().push_back(call);
      builder.setInsertionPointToStart(call);
      Value status = LLVM::CallOp::create(builder, location, segment.function,
                                          ValueRange{fastEntry->getArgument(1),
                                                     bit, fourStateFallback})
                         .getResult();
      Value ok = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::eq, status,
          llvmConstant(builder, location, i32, OBELISK_RT_OK));
      cf::CondBranchOp::create(builder, location, ok, dispatch, ValueRange{},
                               failed, ValueRange{status});
      segmentCases.push_back(APInt(64, index));
      segmentCalls.push_back(call);
    }
    builder.setInsertionPointToStart(otherOwner);
    Value segmentIndex = arith::ShRUIOp::create(
        builder, location, bit,
        llvmConstant(builder, location, i64, segmentShift));
    LLVM::SwitchOp::create(
        builder, location, segmentIndex, stopped, ValueRange{}, segmentCases,
        segmentCalls,
        SmallVector<ValueRange>(segmentCases.size(), ValueRange{}),
        ArrayRef<int32_t>{});
  }
  builder.setInsertionPointToEnd(switchBlock);
  LLVM::SwitchOp::create(builder, location, bit, otherOwner, ValueRange{},
                         cases, destinations, destinationOperands,
                         ArrayRef<int32_t>{});

  auto emitReturns = [&] {
    builder.setInsertionPointToStart(complete);
    LLVM::ReturnOp::create(builder, location, complete->getArgument(0));
    builder.setInsertionPointToStart(stopped);
    LLVM::ReturnOp::create(
        builder, location,
        llvmConstant(builder, location, i32, OBELISK_RT_TIER_UNAVAILABLE));
    builder.setInsertionPointToStart(failed);
    LLVM::ReturnOp::create(builder, location, failed->getArgument(0));
  };

  builder.setInsertionPointToStart(commit);
  if (::obelisk::schedule::has<::obelisk::schedule::Field::EvalRuntimeCalendar>(
          module)) {
    // Keep the eval continuation active across NBA. The common loop drains
    // runtime Active/Inactive work at the cut, then completes the remaining
    // NBA queues before any generated or runtime Active reader can resume.
    // IEEE 1800-2023 4.4.2.3-4.4.2.4 requires Inactive to settle before NBA;
    // 4.5 permits new Active evaluation only after the whole NBA event batch.
    // The captured prefix retains 4.6(b) assignment order and 4.9.4/10.4.2
    // enqueue-time RHS/target values; runtime cuts never reevaluate them.
    getOrDeclareLLVMFunction(module, "obelisk_rt_v1_scheduler_prepare_eval_nba",
                             i32, {pointer, pointer});
    getOrDeclareLLVMFunction(module,
                             "obelisk_rt_v1_scheduler_complete_eval_nba", i32,
                             {pointer, i32, i32});
    Value region = entryAlloca(builder, location, i32, 1, 4);
    Value prepareStatus =
        LLVM::CallOp::create(builder, location, TypeRange{i32},
                             "obelisk_rt_v1_scheduler_prepare_eval_nba",
                             ValueRange{fastEntry->getArgument(1), region})
            .getResult();
    Block *prepared = new Block, *commitPrefix = new Block;
    fastCoordinator.getBody().push_back(prepared);
    fastCoordinator.getBody().push_back(commitPrefix);
    Value prepareOK = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, prepareStatus,
        llvmConstant(builder, location, i32, OBELISK_RT_OK));
    cf::CondBranchOp::create(builder, location, prepareOK, prepared,
                             ValueRange{}, failed, ValueRange{prepareStatus});
    builder.setInsertionPointToStart(prepared);
    Value ingress = maskedOwnerWords(builder, location, ingressAddress(),
                                     allOwners & ~deferredOwnerMask);
    Value hasIngress = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ne, ingress,
        llvmConstant(builder, location, ingress.getType(), 0));
    Block *quiescent = performCommit;
    if (hasDeferred) {
      // Every Active and Inactive event of this pass has run. Execute the
      // lowest pending deferred owner, then settle what it wakes before the
      // cut is taken again.
      quiescent = new Block;
      fastCoordinator.getBody().push_back(quiescent);
    }
    cf::CondBranchOp::create(builder, location, hasIngress, dispatch,
                             ValueRange{}, quiescent, ValueRange{});
    if (hasDeferred) {
      builder.setInsertionPointToStart(quiescent);
      Block *runDeferred = new Block;
      fastCoordinator.getBody().push_back(runDeferred);
      cf::CondBranchOp::create(builder, location, terminationRequested(),
                               performCommit, ValueRange{}, runDeferred,
                               ValueRange{});
      builder.setInsertionPointToStart(runDeferred);
      for (unsigned word = plan.firstDeferredBit / 64;
           word * 64 < mergedFragments.size(); ++word) {
        Value pending = arith::AndIOp::create(
            builder, location,
            loadOwnerWord(builder, location, ingressAddress(), word),
            llvmConstant(builder, location, i64,
                         ownerMaskWord(deferredOwnerMask, word)));
        Value found = arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::ne, pending,
            llvmConstant(builder, location, i64, 0));
        Block *execute = new Block, *next = new Block;
        fastCoordinator.getBody().push_back(execute);
        fastCoordinator.getBody().push_back(next);
        cf::CondBranchOp::create(builder, location, found, execute,
                                 ValueRange{}, next, ValueRange{});
        builder.setInsertionPointToStart(execute);
        Value owner = arith::AddIOp::create(
            builder, location,
            LLVM::CountTrailingZerosOp::create(builder, location, i64, pending,
                                               true),
            llvmConstant(builder, location, i64, uint64_t{word} * 64));
        cf::BranchOp::create(builder, location, switchBlock, ValueRange{owner});
        builder.setInsertionPointToStart(next);
      }
      cf::BranchOp::create(builder, location, performCommit);
    }
    builder.setInsertionPointToStart(performCommit);
    Value barrier = LLVM::LoadOp::create(builder, location, i32, region, 4);
    Value isNBA = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, barrier,
        llvmConstant(builder, location, i32, OBELISK_RT_REGION_NBA));
    cf::CondBranchOp::create(
        builder, location, isNBA, commitPrefix, ValueRange{}, complete,
        ValueRange{llvmConstant(builder, location, i32, OBELISK_RT_OK)});
    builder.setInsertionPointToStart(commitPrefix);
    LLVM::StoreOp::create(builder, location,
                          llvmConstant(builder, location, i32, 0), changed, 4);
    Value prefixStatus =
        LLVM::CallOp::create(builder, location, TypeRange{i32},
                             "__obelisk_aot_runtime_nba_commit_v1",
                             ValueRange{fastEntry->getArgument(0),
                                        fastEntry->getArgument(1), barrier,
                                        changed})
            .getResult();
    Value prefixOK = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, prefixStatus,
        llvmConstant(builder, location, i32, OBELISK_RT_OK));
    cf::CondBranchOp::create(builder, location, prefixOK, afterCommit,
                             ValueRange{}, failed, ValueRange{prefixStatus});
    builder.setInsertionPointToStart(afterCommit);
    Value status =
        LLVM::CallOp::create(builder, location, TypeRange{i32},
                             "obelisk_rt_v1_scheduler_complete_eval_nba",
                             ValueRange{fastEntry->getArgument(1), barrier,
                                        LLVM::LoadOp::create(builder, location,
                                                             i32, changed, 4)})
            .getResult();
    Value ok = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, status,
        llvmConstant(builder, location, i32, OBELISK_RT_OK));
    cf::CondBranchOp::create(builder, location, ok, dispatch, ValueRange{},
                             failed, ValueRange{status});
    emitReturns();
    return success();
  }
  // NBA fanout often needs a blocking-only settle iteration. At quiescence,
  // skip the entire commit/probe path if that iteration staged no new NBA.
  // Dynamic slots do not set fixed-root dirty bits, so both representations
  // must be empty. This does not clear any four-state/promotion evidence.
  Value pendingNBA = llvmConstant(builder, location, i64, 0);
  if (nbaTaintWordCount != 0) {
    Value dirtyBase = LLVM::AddressOfOp::create(builder, location, pointer,
                                                nbaDirtyRootsName);
    for (uint32_t word = 0; word != nbaTaintWordCount; ++word) {
      Value dirty =
          LLVM::LoadOp::create(builder, location, i64,
                               byteGEP(builder, location, dirtyBase,
                                       uint64_t{word} * sizeof(uint64_t)),
                               8);
      pendingNBA = arith::OrIOp::create(builder, location, pendingNBA, dirty);
    }
  }
  for (const std::string &name : plan.dynamicNBAValidNames) {
    Value valid = LLVM::LoadOp::create(
        builder, location, i32,
        LLVM::AddressOfOp::create(builder, location, pointer, name), 4);
    pendingNBA = arith::OrIOp::create(
        builder, location, pendingNBA,
        arith::ExtUIOp::create(builder, location, i64, valid));
  }
  if (plan.hasOrderedNBA)
    pendingNBA = arith::OrIOp::create(
        builder, location, pendingNBA,
        arith::ExtUIOp::create(builder, location, i64,
                               evalNBAQueueSize(builder, location)));
  Value noNBA = arith::CmpIOp::create(builder, location,
                                      arith::CmpIPredicate::eq, pendingNBA,
                                      llvmConstant(builder, location, i64, 0));
  cf::CondBranchOp::create(
      builder, location, noNBA, complete,
      ValueRange{llvmConstant(builder, location, i32, OBELISK_RT_OK)},
      performCommit, ValueRange{});

  builder.setInsertionPointToStart(performCommit);
  {
    // A path dispatcher can reject after coordinator entry.  Observe that
    // rejection at the barrier so its four-state staging is never committed
    // by the compact value-plane-only path.
    Value dispatcherFallback = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ne,
        LLVM::LoadOp::create(
            builder, location, builder.getI8Type(),
            LLVM::AddressOfOp::create(builder, location, pointer,
                                      evalStepFourStateFallbackName),
            1),
        llvmConstant(builder, location, builder.getI8Type(), 0));
    Value priorFallback = LLVM::LoadOp::create(
        builder, location, builder.getI1Type(), fourStateFallback, 1);
    LLVM::StoreOp::create(builder, location,
                          arith::OrIOp::create(builder, location, priorFallback,
                                               dispatcherFallback),
                          fourStateFallback, 1);
  }
  LLVM::StoreOp::create(builder, location,
                        llvmConstant(builder, location, i32, 0), changed, 4);
  Value commitStatus;
  {
    Block *fastTwoStateCommit = new Block;
    Block *canonicalTwoStateCommit = new Block;
    Block *fourStateCommit = new Block;
    Block *checkFallbackNBA = new Block;
    Block *checkCanonicalNBA = new Block;
    Block *selectFastNBA = new Block;
    Block *latchFastNBA = new Block;
    Block *commitJoin = new Block;
    commitJoin->addArgument(i32, location);
    fastCoordinator.getBody().push_back(checkFallbackNBA);
    fastCoordinator.getBody().push_back(checkCanonicalNBA);
    fastCoordinator.getBody().push_back(selectFastNBA);
    fastCoordinator.getBody().push_back(latchFastNBA);
    fastCoordinator.getBody().push_back(fastTwoStateCommit);
    fastCoordinator.getBody().push_back(canonicalTwoStateCommit);
    fastCoordinator.getBody().push_back(fourStateCommit);
    fastCoordinator.getBody().push_back(commitJoin);
    Value usedFallback = LLVM::LoadOp::create(
        builder, location, builder.getI1Type(), fourStateFallback, 1);
    cf::CondBranchOp::create(builder, location, usedFallback, checkFallbackNBA,
                             ValueRange{}, selectFastNBA, ValueRange{});
    auto markCurrentDirtyRootsFast = [&] {
      if (nbaTaintWordCount == 0)
        return;
      Value dirtyBase = LLVM::AddressOfOp::create(builder, location, pointer,
                                                  nbaDirtyRootsName);
      Value fastRootsBase = LLVM::AddressOfOp::create(
          builder, location, pointer, evalFastNBARootsName);
      for (uint32_t word = 0; word != nbaTaintWordCount; ++word) {
        Value dirty =
            LLVM::LoadOp::create(builder, location, i64,
                                 byteGEP(builder, location, dirtyBase,
                                         uint64_t{word} * sizeof(uint64_t)),
                                 8);
        Value fastAddress = byteGEP(builder, location, fastRootsBase,
                                    uint64_t{word} * sizeof(uint64_t));
        Value fastRoots =
            LLVM::LoadOp::create(builder, location, i64, fastAddress, 8);
        LLVM::StoreOp::create(
            builder, location,
            arith::OrIOp::create(builder, location, fastRoots, dirty),
            fastAddress, 8);
      }
    };
    builder.setInsertionPointToStart(selectFastNBA);
    Value fastRootsKnown =
        llvmConstant(builder, location, builder.getI1Type(), 0);
    if (nbaTaintWordCount != 0) {
      Value dirtyBase = LLVM::AddressOfOp::create(builder, location, pointer,
                                                  nbaDirtyRootsName);
      Value fastRootsBase = LLVM::AddressOfOp::create(
          builder, location, pointer, evalFastNBARootsName);
      Value missingFastRoot =
          llvmConstant(builder, location, builder.getI1Type(), 0);
      for (uint32_t word = 0; word != nbaTaintWordCount; ++word) {
        Value dirty =
            LLVM::LoadOp::create(builder, location, i64,
                                 byteGEP(builder, location, dirtyBase,
                                         uint64_t{word} * sizeof(uint64_t)),
                                 8);
        Value fastRoots =
            LLVM::LoadOp::create(builder, location, i64,
                                 byteGEP(builder, location, fastRootsBase,
                                         uint64_t{word} * sizeof(uint64_t)),
                                 8);
        Value missing = arith::AndIOp::create(
            builder, location, dirty,
            arith::XOrIOp::create(
                builder, location, fastRoots,
                llvmConstant(builder, location, i64, UINT64_MAX)));
        missingFastRoot = arith::OrIOp::create(
            builder, location, missingFastRoot,
            arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ne,
                                  missing,
                                  llvmConstant(builder, location, i64, 0)));
      }
      fastRootsKnown = arith::OrIOp::create(
          builder, location, fastRootsKnown,
          arith::XOrIOp::create(
              builder, location, missingFastRoot,
              llvmConstant(builder, location, builder.getI1Type(), 1)));
    }
    cf::CondBranchOp::create(builder, location, fastRootsKnown,
                             fastTwoStateCommit, ValueRange{},
                             checkCanonicalNBA, ValueRange{});
    builder.setInsertionPointToStart(checkFallbackNBA);
    // A four-state owner may stage only known values. If its dirty roots have
    // zero canonical and staged unknown bits, rejoin the ordinary handoff:
    // canonicalize all dirty roots once if needed, latch the compact barrier
    // at that quiescent boundary, and keep it selected on later clocks. This
    // prevents an X-valued monitor closure from downgrading unrelated DUT
    // registers forever while retaining the four-state barrier for a
    // genuinely unknown write.
    Value fallbackNBAKnown =
        LLVM::CallOp::create(
            builder, location, TypeRange{builder.getI1Type()},
            SymbolRefAttr::get(context, nbaKnownName),
            ValueRange{llvmConstant(builder, location, i32, 2),
                       llvmConstant(builder, location, builder.getI1Type(), 1),
                       llvmConstant(builder, location, builder.getI1Type(), 1)})
            .getResult();
    cf::CondBranchOp::create(builder, location, fallbackNBAKnown, selectFastNBA,
                             ValueRange{}, fourStateCommit, ValueRange{});
    builder.setInsertionPointToStart(checkCanonicalNBA);
    Value canonicalNBAKnown =
        LLVM::CallOp::create(
            builder, location, TypeRange{builder.getI1Type()},
            SymbolRefAttr::get(context, nbaKnownName),
            ValueRange{llvmConstant(builder, location, i32, 2),
                       llvmConstant(builder, location, builder.getI1Type(), 1),
                       llvmConstant(builder, location, builder.getI1Type(), 0)})
            .getResult();
    cf::CondBranchOp::create(builder, location, canonicalNBAKnown, latchFastNBA,
                             ValueRange{}, canonicalTwoStateCommit,
                             ValueRange{});
    builder.setInsertionPointToStart(latchFastNBA);
    markCurrentDirtyRootsFast();
    cf::BranchOp::create(builder, location, fastTwoStateCommit);
    builder.setInsertionPointToStart(fastTwoStateCommit);
    auto fastTwoStateCall = LLVM::CallOp::create(
        builder, location, TypeRange{i32},
        SymbolRefAttr::get(context, nbaCommitName),
        ValueRange{fastEntry->getArgument(0), fastEntry->getArgument(1),
                   llvmConstant(builder, location, i32, 2), changed});
    ::obelisk::schedule::set<
        ::obelisk::schedule::Field::EvalUseFastTwoStateNba>(
        fastTwoStateCall, builder.getUnitAttr());
    cf::BranchOp::create(builder, location, commitJoin,
                         ValueRange{fastTwoStateCall.getResult()});
    builder.setInsertionPointToStart(canonicalTwoStateCommit);
    markCurrentDirtyRootsFast();
    auto canonicalTwoStateCall = LLVM::CallOp::create(
        builder, location, TypeRange{i32},
        SymbolRefAttr::get(context, nbaCommitName),
        ValueRange{fastEntry->getArgument(0), fastEntry->getArgument(1),
                   llvmConstant(builder, location, i32, 2), changed});
    ::obelisk::schedule::set<
        ::obelisk::schedule::Field::EvalUseCanonicalTwoStateNba>(
        canonicalTwoStateCall, builder.getUnitAttr());
    cf::BranchOp::create(builder, location, commitJoin,
                         ValueRange{canonicalTwoStateCall.getResult()});
    builder.setInsertionPointToStart(fourStateCommit);
    Value fourStateStatus =
        LLVM::CallOp::create(
            builder, location, TypeRange{i32},
            SymbolRefAttr::get(context, evalFourStateNBAHandoffName),
            ValueRange{fastEntry->getArgument(0), fastEntry->getArgument(1),
                       changed})
            .getResult();
    cf::BranchOp::create(builder, location, commitJoin,
                         ValueRange{fourStateStatus});
    builder.setInsertionPointToStart(commitJoin);
    commitStatus = commitJoin->getArgument(0);
  }
  Value commitOK = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::eq, commitStatus,
      llvmConstant(builder, location, i32, OBELISK_RT_OK));
  cf::CondBranchOp::create(builder, location, commitOK, afterCommit,
                           ValueRange{}, failed, ValueRange{commitStatus});

  builder.setInsertionPointToStart(afterCommit);
  Value postNBAReady = combinedIngress();
  Value postNBAEmpty = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::eq, postNBAReady,
      llvmConstant(builder, location, i64, 0));
  cf::CondBranchOp::create(builder, location, postNBAEmpty, complete,
                           ValueRange{commitStatus}, dispatch, ValueRange{});
  emitReturns();
  return success();
}

} // namespace obelisk::detail
