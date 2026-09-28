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

using namespace mlir;

namespace obelisk::detail {
namespace {

constexpr StringLiteral evalStepFourStateFallbackName =
    "__obelisk_eval_step_four_state_fallback_v1";
constexpr StringLiteral evalStepFourStateNBARootsName =
    "__obelisk_eval_step_four_state_nba_roots_v1";
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
                                     clockKernels.front().ingressName)
        .getResult();
  };
  auto combinedIngress = [&] {
    return maskedOwnerWords(builder, location, ingressAddress(), allOwners);
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
  Value bit = arith::AddIOp::create(
      builder, location, localBit,
      arith::MulIOp::create(builder, location, selectedWord,
                            llvmConstant(builder, location, i64, 64)));
  Block *switchBlock = select;
  SmallVector<APInt> cases;
  SmallVector<Block *> destinations;
  SmallVector<ValueRange> destinationOperands;
  // Every entry policy shares these groups and their local domain proofs.
  auto rankedExecutor = [&](unsigned index) -> StringRef {
    return index < plan.rankedGroupExecutors.size()
               ? StringRef(plan.rankedGroupExecutors[index])
               : StringRef{};
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
    Block *execute = new Block;
    fastCoordinator.getBody().push_back(execute);
    cases.push_back(APInt(64, record.bit));
    destinations.push_back(execute);
    destinationOperands.push_back(ValueRange{});
    builder.setInsertionPointToStart(execute);
    if (readyLayout.hasCache()) {
      APInt superseding(ownerCount, 0);
      for (auto [parent, parentRecord] : llvm::enumerate(mergedFragments))
        if (allowedOwnerMask[parentRecord.bit] &&
            ownerSubsumptionMasks[parent][record.bit])
          superseding.setBit(parentRecord.bit);
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
        fastCoordinator.getBody().push_back(skip);
        fastCoordinator.getBody().push_back(uncovered);
        cf::CondBranchOp::create(builder, location, covered, skip, ValueRange{},
                                 uncovered, ValueRange{});
        builder.setInsertionPointToStart(skip);
        clearIngressMask(APInt::getOneBitSet(ownerCount, record.bit));
        cf::BranchOp::create(builder, location, dispatch);
        builder.setInsertionPointToStart(uncovered);
      }
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
          fourStateFallback, 1);
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
      fastCoordinator.getBody().push_back(checkPromotion);
      fastCoordinator.getBody().push_back(executeFourState);
      fastCoordinator.getBody().push_back(executeTwoState);
      fastCoordinator.getBody().push_back(executeJoin);
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
          fourStateFallback, 1);
      if (ownerMayTaintNBA(recordIndex))
        markOwnerNBATaint(recordIndex);
      Value fourStateStatus =
          LLVM::CallOp::create(
              builder, location, TypeRange{i32},
              SymbolRefAttr::get(context, executors[recordIndex]),
              ValueRange{fastEntry->getArgument(1)})
              .getResult();
      cf::BranchOp::create(builder, location, executeJoin,
                           ValueRange{fourStateStatus});
      builder.setInsertionPointToStart(executeTwoState);
      Value twoStateStatus =
          LLVM::CallOp::create(
              builder, location, TypeRange{i32},
              SymbolRefAttr::get(context, mergedTwoStateExecutors[recordIndex]),
              ValueRange{fastEntry->getArgument(1)})
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
                          ValueRange{fastEntry->getArgument(1)})
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
      cf::BranchOp::create(builder, location, dispatch);
      continue;
    }
    if (!mayTerminate) {
      cf::CondBranchOp::create(builder, location, executeOK, dispatch,
                               ValueRange{}, failed, ValueRange{executeStatus});
      continue;
    }
    Block *checkTermination = new Block;
    Block *loadTermination = new Block;
    fastCoordinator.getBody().push_back(checkTermination);
    if (loadTermination)
      fastCoordinator.getBody().push_back(loadTermination);
    cf::CondBranchOp::create(builder, location, executeOK, checkTermination,
                             ValueRange{}, failed, ValueRange{executeStatus});
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
    cf::CondBranchOp::create(builder, location, noTerminationAddress, dispatch,
                             ValueRange{}, loadTermination, ValueRange{});
    builder.setInsertionPointToStart(loadTermination);
    Value termination =
        LLVM::LoadOp::create(builder, location, i32, terminationAddress, 4);
    Value stopping = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ne, termination,
        llvmConstant(builder, location, i32, 0));
    cf::CondBranchOp::create(builder, location, stopping, stopped, ValueRange{},
                             dispatch, ValueRange{});
  }
  builder.setInsertionPointToEnd(switchBlock);
  LLVM::SwitchOp::create(builder, location, bit, stopped, ValueRange{}, cases,
                         destinations, destinationOperands,
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
    // IEEE 1800-2023 4.4.2.2-4.4.2.4, 4.5, 10.4.2: finish generated
    // Active work as one batch. The shared scheduler owns
    // this mixed NBA barrier and must first drain runtime Active observers.
    // Its canonical commit consumes the generated scalar accumulators and
    // publishes both runtime waiters and subsequent generated ingress.
    LLVM::ReturnOp::create(builder, location,
                           llvmConstant(builder, location, i32, OBELISK_RT_OK));
    performCommit->erase();
    afterCommit->erase();
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
