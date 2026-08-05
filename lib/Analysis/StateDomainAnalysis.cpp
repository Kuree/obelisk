//===- StateDomainAnalysis.cpp - Whole-value X/Z analysis ----------------===//

#include "obelisk/Analysis/StateDomainAnalysis.h"
#include "obelisk/Analysis/SimulationAnalysis.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/Matchers.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/IR/Threading.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/TypeSwitch.h"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <optional>
#include <tuple>

using namespace mlir;

namespace obelisk {
namespace {

using RootKey = std::pair<unsigned, uint64_t>;
using RootSet = DenseSet<RootKey>;

RootKey getRootKey(sim::ComputeResourceKind resource, uint64_t descriptor) {
  return {static_cast<unsigned>(resource), descriptor};
}

constexpr StateDomainFact bottomFact() {
  return {StateDomain::Bottom, StateDomainReason::Unresolved};
}

constexpr StateDomainFact twoState(StateDomainReason reason) {
  return {StateDomain::TwoState, reason};
}

constexpr StateDomainFact mayFourState(StateDomainReason reason) {
  return {StateDomain::MayFourState, reason};
}

bool isLogic(Type type) {
  if (isa<sim::LogicType>(type))
    return true;
  if (!sim::isAggregateType(type))
    return false;
  for (unsigned index = 0; index < sim::getAggregateNumElements(type); ++index)
    if (isLogic(sim::getAggregateElementType(type, index)))
      return true;
  return false;
}

/// Count all references to a symbol on one operation.  A direct invocation is
/// only a closed-world use when its canonical callee attribute is the sole
/// reference; an additional reference can represent an address-taken or other
/// opaque use that the invocation index does not model.
unsigned countSymbolReferences(Operation *operation, SymbolRefAttr reference) {
  unsigned count = 0;
  for (NamedAttribute named : operation->getAttrs())
    named.getValue().walk([&](SymbolRefAttr candidate) {
      if (candidate == reference)
        ++count;
    });
  return count;
}

/// Join facts arriving along alternative control-flow or invocation edges.
/// Bottom means that an edge has not contributed information yet, so it is the
/// identity for this join.
StateDomainFact joinAlternatives(StateDomainFact lhs, StateDomainFact rhs,
                                 StateDomainReason reason) {
  if (static_cast<unsigned>(rhs.domain) > static_cast<unsigned>(lhs.domain))
    return {rhs.domain, reason};
  return lhs;
}

/// Combine dependencies of one transfer. Unlike a control-flow join, every
/// operand must be resolved before a normal operation result can be resolved.
StateDomainFact combineOperands(Operation *op,
                                const DenseMap<Value, StateDomainFact> &facts,
                                StateDomainReason reason) {
  bool sawLogic = false;
  bool sawBottom = false;
  for (Value operand : op->getOperands()) {
    if (!isLogic(operand.getType()))
      continue;
    sawLogic = true;
    auto found = facts.find(operand);
    StateDomainFact fact =
        found == facts.end()
            ? mayFourState(StateDomainReason::UnsupportedProducer)
            : found->second;
    if (fact.domain == StateDomain::MayFourState)
      return mayFourState(reason);
    if (fact.domain == StateDomain::Bottom)
      sawBottom = true;
  }
  if (!sawLogic)
    return twoState(reason);
  return sawBottom ? bottomFact() : twoState(reason);
}

bool updateFact(DenseMap<Value, StateDomainFact> &facts, Value value,
                StateDomainFact next) {
  StateDomainFact &current =
      facts.try_emplace(value, bottomFact()).first->second;
  if (static_cast<unsigned>(next.domain) <=
      static_cast<unsigned>(current.domain))
    return false;
  current = next;
  return true;
}

bool updateBoundary(StateDomainFact &current, StateDomainFact contribution,
                    StateDomainReason reason) {
  StateDomainFact next = joinAlternatives(current, contribution, reason);
  if (next.domain == current.domain)
    return false;
  current = next;
  return true;
}

bool isSuspensionTerminator(Operation *op) {
  return isa<
      sim::SimSuspendDelayOp, sim::SimSuspendChangeOp, sim::SimSuspendEdgeOp,
      sim::SimSuspendEdgeIffOp, sim::SimSuspendLevelOp, sim::SimSuspendAnyOp,
      sim::SimSuspendEventOp, sim::SimSuspendForeverOp, sim::SimSuspendAwaitOp,
      sim::SimSuspendJoinOp, sim::SimSuspendChildrenOp,
      sim::SimSuspendObserveOp, sim::SimTaskCallOp>(op);
}

std::optional<APInt> getConstantInteger(Value value) {
  Attribute attribute;
  if (matchPattern(value, m_Constant(&attribute))) {
    if (auto integer = dyn_cast<IntegerAttr>(attribute))
      return integer.getValue();
    if (auto planes = dyn_cast<ArrayAttr>(attribute);
        planes && planes.size() == 2) {
      auto valuePlane = dyn_cast<IntegerAttr>(planes[0]);
      auto unknownPlane = dyn_cast<IntegerAttr>(planes[1]);
      if (valuePlane && unknownPlane && unknownPlane.getValue().isZero())
        return valuePlane.getValue();
    }
    return std::nullopt;
  }
  if (auto fromBits = value.getDefiningOp<sim::SimLogicFromBitsOp>())
    return getConstantInteger(fromBits.getInput());
  return std::nullopt;
}

bool isKnownZero(Value value) {
  std::optional<APInt> constant = getConstantInteger(value);
  return constant && constant->isZero();
}

bool isKnownAllOnes(Value value) {
  std::optional<APInt> constant = getConstantInteger(value);
  return constant && constant->isAllOnes();
}

bool isKnownNonzero(Value value) {
  std::optional<APInt> constant = getConstantInteger(value);
  return constant && !constant->isZero();
}

bool hasInRangeConstantIndex(sim::SimLogicDynExtractOp op) {
  std::optional<APInt> index = getConstantInteger(op.getLowBit());
  if (!index || index->isNegative() || index->getActiveBits() > 64)
    return false;
  uint64_t low = index->getZExtValue();
  uint64_t inputWidth = op.getInput().getType().getWidth();
  uint64_t resultWidth = op.getResult().getType().getWidth();
  return low <= inputWidth && resultWidth <= inputWidth - low;
}

StateDomainFact
transferOperation(Operation *op, const DenseMap<Value, StateDomainFact> &facts,
                  const analysis::DescriptorProvenanceMap &provenance,
                  const RootSet &assumedKnownRoots) {
  if (auto constant = dyn_cast<sim::SimLogicConstantOp>(op))
    return constant.getUnknown().isZero()
               ? twoState(StateDomainReason::LogicConstant)
               : mayFourState(StateDomainReason::UnknownConstant);
  if (isa<sim::SimLogicFromBitsOp>(op))
    return twoState(StateDomainReason::LogicFromBits);
  if (auto load = dyn_cast<sim::SimRefLoadOp>(op)) {
    auto root = provenance.find(load.getReference());
    if (root != provenance.end() && root->second.descriptor &&
        assumedKnownRoots.contains(
            getRootKey(root->second.resource, *root->second.descriptor)))
      return twoState(StateDomainReason::InductiveRootRead);
    return mayFourState(StateDomainReason::RefLoad);
  }
  if (auto read = dyn_cast<sim::SimNetReadOp>(op)) {
    auto root = provenance.find(read.getNet());
    if (root != provenance.end() && root->second.descriptor &&
        assumedKnownRoots.contains(
            getRootKey(root->second.resource, *root->second.descriptor)))
      return twoState(StateDomainReason::InductiveRootRead);
    return mayFourState(StateDomainReason::NetRead);
  }

  if (auto binary = dyn_cast<sim::SimLogicBinaryOp>(op)) {
    if ((binary.getKind() == sim::BinaryKind::And &&
         (isKnownZero(binary.getLhs()) || isKnownZero(binary.getRhs()))) ||
        (binary.getKind() == sim::BinaryKind::Or &&
         (isKnownAllOnes(binary.getLhs()) || isKnownAllOnes(binary.getRhs()))))
      return twoState(StateDomainReason::AbsorbingConstant);
    StateDomainFact operands =
        combineOperands(op, facts, StateDomainReason::LogicBinary);
    if (operands.domain != StateDomain::TwoState)
      return operands;
    switch (binary.getKind()) {
    case sim::BinaryKind::UDiv:
    case sim::BinaryKind::SDiv:
    case sim::BinaryKind::UMod:
    case sim::BinaryKind::SMod:
      return isKnownNonzero(binary.getRhs())
                 ? operands
                 : mayFourState(StateDomainReason::DivisionDivisor);
    default:
      return operands;
    }
  }

  if (auto logical = dyn_cast<sim::SimLogicLogicalOp>(op)) {
    if ((logical.getKind() == sim::LogicalKind::And &&
         (isKnownZero(logical.getLhs()) || isKnownZero(logical.getRhs()))) ||
        (logical.getKind() == sim::LogicalKind::Or &&
         (isKnownAllOnes(logical.getLhs()) ||
          isKnownAllOnes(logical.getRhs()))))
      return twoState(StateDomainReason::AbsorbingConstant);
    return combineOperands(op, facts, StateDomainReason::LogicLogical);
  }

  if (auto compare = dyn_cast<sim::SimLogicCompareOp>(op)) {
    if (compare.getKind() == sim::CompareKind::CaseEq ||
        compare.getKind() == sim::CompareKind::CaseNe ||
        compare.getKind() == sim::CompareKind::CaseZEq ||
        compare.getKind() == sim::CompareKind::CaseXZEq)
      return twoState(StateDomainReason::CaseComparison);
    return combineOperands(op, facts, StateDomainReason::LogicCompare);
  }

  if (auto extract = dyn_cast<sim::SimLogicDynExtractOp>(op)) {
    if (!hasInRangeConstantIndex(extract))
      return mayFourState(StateDomainReason::DynamicExtractIndex);
    return combineOperands(op, facts, StateDomainReason::DynamicExtract);
  }

  return llvm::TypeSwitch<Operation *, StateDomainFact>(op)
      .Case<sim::SimLogicResizeOp>([&](auto) {
        return combineOperands(op, facts, StateDomainReason::LogicResize);
      })
      .Case<sim::SimLogicUnaryOp>([&](auto) {
        return combineOperands(op, facts, StateDomainReason::LogicUnary);
      })
      .Case<sim::SimLogicReductionOp>([&](auto) {
        return combineOperands(op, facts, StateDomainReason::LogicReduction);
      })
      .Case<sim::SimLogicMuxOp>([&](auto) {
        return combineOperands(op, facts, StateDomainReason::LogicMux);
      })
      .Case<sim::SimLogicShiftOp>([&](auto) {
        return combineOperands(op, facts, StateDomainReason::LogicShift);
      })
      .Case<sim::SimLogicConcatOp>([&](auto) {
        return combineOperands(op, facts, StateDomainReason::LogicConcat);
      })
      .Case<sim::SimLogicReplicateOp>([&](auto) {
        return combineOperands(op, facts, StateDomainReason::LogicReplicate);
      })
      .Case<sim::SimLogicExtractOp>([&](auto) {
        return combineOperands(op, facts, StateDomainReason::LogicExtract);
      })
      .Case<sim::SimLogicInsertOp>([&](auto) {
        return combineOperands(op, facts, StateDomainReason::LogicInsert);
      })
      // Flatten and unflatten are representation-only views of the same
      // packed value.  They neither synthesize nor discard unknown bits, so
      // their state domain is exactly the domain of the input value.
      .Case<sim::SimPackedFlattenOp, sim::SimPackedUnflattenOp>([&](auto) {
        return combineOperands(op, facts, StateDomainReason::PackedView);
      })
      .Default([&](Operation *) {
        return mayFourState(StateDomainReason::UnsupportedProducer);
      });
}

struct InvocationSummary {
  Operation *operation = nullptr;
  bool spawn = false;
  std::optional<unsigned> callee;
};

ValueRange getInvocationOperands(Operation *operation) {
  if (auto task = dyn_cast<sim::SimTaskCallOp>(operation))
    return task.getArguments();
  return operation->getOperands();
}

struct IncomingSummary {
  Value value;
  StateDomainReason reason = StateDomainReason::CFGJoin;
  Operation *terminator = nullptr;
  unsigned successorIndex = 0;
};

struct BlockArgumentSummary {
  BlockArgument argument;
  SmallVector<IncomingSummary> incoming;
};

/// Read-only structural dependencies collected independently for one function.
struct FunctionSummary {
  sim::SimFuncOp function;
  analysis::DescriptorProvenanceMap provenance;
  SmallVector<Block *> blocks;
  SmallVector<BlockArgumentSummary> blockArguments;
  SmallVector<Operation *> operations;
  SmallVector<InvocationSummary> invocations;
  SmallVector<sim::SimReturnOp> returns;
};

FunctionSummary buildSummary(sim::SimFuncOp function) {
  FunctionSummary summary;
  summary.function = function;
  summary.provenance = analysis::deriveDescriptorProvenance(function);
  if (function.getBody().empty())
    return summary;

  function.walk<WalkOrder::PreOrder>(
      [&](Block *block) { summary.blocks.push_back(block); });

  DenseMap<Value, unsigned> argumentIndex;
  Block *entry = &function.getBody().front();
  for (Block *block : summary.blocks) {
    for (BlockArgument argument : block->getArguments()) {
      if (block == entry || !isLogic(argument.getType()))
        continue;
      argumentIndex[argument] = summary.blockArguments.size();
      summary.blockArguments.push_back({argument, {}});
    }
    for (Operation &operation : *block) {
      summary.operations.push_back(&operation);
      if (isa<sim::SimCallOp, sim::SimTaskCallOp>(operation))
        summary.invocations.push_back({&operation, false, std::nullopt});
      else if (isa<sim::SimSpawnOp>(operation))
        summary.invocations.push_back({&operation, true, std::nullopt});
      else if (auto returnOp = dyn_cast<sim::SimReturnOp>(operation))
        summary.returns.push_back(returnOp);
    }
  }

  // Record CFG dependencies once, in structural predecessor/successor order.
  // Produced or otherwise unmodelled successor inputs are represented by a
  // null value and conservatively contribute MayFourState during propagation.
  for (Block *predecessor : summary.blocks) {
    if (predecessor->empty())
      continue;
    Operation *terminator = predecessor->getTerminator();
    auto branch = dyn_cast<BranchOpInterface>(terminator);
    for (unsigned successorIndex = 0;
         successorIndex != terminator->getNumSuccessors(); ++successorIndex) {
      Block *successor = terminator->getSuccessor(successorIndex);
      for (BlockArgument argument : successor->getArguments()) {
        auto found = argumentIndex.find(argument);
        if (found == argumentIndex.end())
          continue;

        Value incoming;
        if (branch) {
          SuccessorOperands operands =
              branch.getSuccessorOperands(successorIndex);
          unsigned argumentNumber = argument.getArgNumber();
          if (argumentNumber < operands.size() &&
              !operands.isOperandProduced(argumentNumber))
            incoming = operands[argumentNumber];
        }
        StateDomainReason reason = incoming && isLogic(incoming.getType())
                                       ? (isSuspensionTerminator(terminator)
                                              ? StateDomainReason::Continuation
                                              : StateDomainReason::CFGJoin)
                                       : StateDomainReason::UnsupportedProducer;
        summary.blockArguments[found->second].incoming.push_back(
            {incoming && isLogic(incoming.getType()) ? incoming : Value(),
             reason, terminator, successorIndex});
      }
    }
  }
  for (BlockArgumentSummary &argument : summary.blockArguments) {
    bool continuationOnly =
        !argument.incoming.empty() &&
        llvm::all_of(argument.incoming, [](auto incoming) {
          return incoming.reason == StateDomainReason::Continuation;
        });
    if (!continuationOnly)
      for (IncomingSummary &incoming : argument.incoming)
        if (incoming.reason == StateDomainReason::Continuation)
          incoming.reason = StateDomainReason::CFGJoin;
  }
  return summary;
}

struct LocalFacts {
  DenseMap<Value, StateDomainFact> values;
  SmallVector<StateDomainFact> results;
  DenseSet<Block *> reachable;
};

bool shouldTrackResult(Operation *operation, Value result) {
  return isLogic(result.getType()) || (isa<sim::SimLogicCompareOp>(operation) &&
                                       result.getType().isSignlessInteger(1));
}

StateDomainFact lookupLocalFact(const DenseMap<Value, StateDomainFact> &facts,
                                Value value) {
  auto found = facts.find(value);
  if (found != facts.end())
    return found->second;
  if (!isLogic(value.getType()))
    return twoState(StateDomainReason::NonLogic);
  return mayFourState(StateDomainReason::UnsupportedProducer);
}

LocalFacts initializeLocalFacts(const FunctionSummary &summary) {
  LocalFacts local;
  sim::SimFuncOp function = summary.function;
  local.results.assign(function.getFunctionType().getNumResults(),
                       bottomFact());
  if (function.getBody().empty())
    return local;

  for (Block *block : summary.blocks) {
    for (BlockArgument argument : block->getArguments()) {
      if (!isLogic(argument.getType()))
        continue;
      local.values[argument] = bottomFact();
    }
    for (Operation &operation : *block)
      for (Value result : operation.getResults())
        if (shouldTrackResult(&operation, result))
          local.values[result] = bottomFact();
  }
  return local;
}

std::optional<bool>
getProvableCondition(Value value,
                     const DenseMap<Value, StateDomainFact> &facts,
                     DenseSet<Value> &active,
                     DenseMap<Value, std::optional<bool>> &memo) {
  auto cached = memo.find(value);
  if (cached != memo.end())
    return cached->second;
  if (!active.insert(value).second)
    return std::nullopt;
  auto done = [&](std::optional<bool> result) {
    active.erase(value);
    memo.try_emplace(value, result);
    return result;
  };

  APInt constant;
  if (matchPattern(value, m_ConstantInt(&constant)) && constant.getBitWidth() == 1)
    return done(!constant.isZero());

  if (auto argument = dyn_cast<BlockArgument>(value)) {
    std::optional<bool> result;
    bool sawIncoming = false;
    for (Block *predecessor : argument.getOwner()->getPredecessors()) {
      auto branch =
          dyn_cast<BranchOpInterface>(predecessor->getTerminator());
      if (!branch)
        return done(std::nullopt);
      for (unsigned successor = 0;
           successor != predecessor->getNumSuccessors(); ++successor) {
        if (predecessor->getSuccessor(successor) != argument.getOwner())
          continue;
        SuccessorOperands operands =
            branch.getSuccessorOperands(successor);
        if (argument.getArgNumber() >= operands.size() ||
            operands.isOperandProduced(argument.getArgNumber()))
          return done(std::nullopt);
        std::optional<bool> incoming = getProvableCondition(
            operands[argument.getArgNumber()], facts, active, memo);
        if (!incoming || (result && *result != *incoming))
          return done(std::nullopt);
        result = incoming;
        sawIncoming = true;
      }
    }
    return done(sawIncoming ? result : std::nullopt);
  }

  if (auto xorOp = value.getDefiningOp<arith::XOrIOp>()) {
    APInt rhs;
    if (matchPattern(xorOp.getRhs(), m_ConstantInt(&rhs)) &&
        rhs.getBitWidth() == 1) {
      std::optional<bool> lhs =
          getProvableCondition(xorOp.getLhs(), facts, active, memo);
      return done(lhs ? std::optional<bool>(*lhs != !rhs.isZero())
                      : std::nullopt);
    }
  }
  if (auto andOp = value.getDefiningOp<arith::AndIOp>()) {
    std::optional<bool> lhs =
        getProvableCondition(andOp.getLhs(), facts, active, memo);
    std::optional<bool> rhs =
        getProvableCondition(andOp.getRhs(), facts, active, memo);
    if ((lhs && !*lhs) || (rhs && !*rhs))
      return done(false);
    if (lhs && rhs)
      return done(*lhs && *rhs);
  }
  if (auto orOp = value.getDefiningOp<arith::OrIOp>()) {
    std::optional<bool> lhs =
        getProvableCondition(orOp.getLhs(), facts, active, memo);
    std::optional<bool> rhs =
        getProvableCondition(orOp.getRhs(), facts, active, memo);
    if ((lhs && *lhs) || (rhs && *rhs))
      return done(true);
    if (lhs && rhs)
      return done(*lhs || *rhs);
  }
  if (auto compare = value.getDefiningOp<sim::SimLogicCompareOp>()) {
    bool equality = compare.getKind() == sim::CompareKind::CaseEq;
    bool inequality = compare.getKind() == sim::CompareKind::CaseNe;
    if (equality || inequality) {
      auto isKnownRoundTrip = [&](Value original, Value roundTrip) {
        auto fromBits = roundTrip.getDefiningOp<sim::SimLogicFromBitsOp>();
        auto toBits = fromBits
                          ? fromBits.getInput()
                                .getDefiningOp<sim::SimLogicToBitsOp>()
                          : sim::SimLogicToBitsOp{};
        auto fact = facts.find(original);
        return toBits && toBits.getInput() == original &&
               fact != facts.end() &&
               fact->second.domain == StateDomain::TwoState;
      };
      if (isKnownRoundTrip(compare.getLhs(), compare.getRhs()) ||
          isKnownRoundTrip(compare.getRhs(), compare.getLhs()))
        return done(equality);
    }
  }
  return done(std::nullopt);
}

bool isProvablyDeadSuccessor(
    Operation *terminator, unsigned successorIndex,
    const DenseMap<Value, StateDomainFact> &proofFacts,
    DenseMap<Value, std::optional<bool>> &memo) {
  auto branch = dyn_cast_or_null<cf::CondBranchOp>(terminator);
  if (!branch)
    return false;
  DenseSet<Value> active;
  std::optional<bool> condition =
      getProvableCondition(branch.getCondition(), proofFacts, active, memo);
  if (!condition)
    return false;
  return successorIndex != (*condition ? 0u : 1u);
}

void propagateFunction(const FunctionSummary &summary,
                       ArrayRef<StateDomainFact> formalBoundaries,
                       ArrayRef<SmallVector<StateDomainFact>> resultBoundaries,
                       const DenseMap<Operation *, unsigned> &calleeIndex,
                       const RootSet &assumedKnownRoots, LocalFacts &local) {
  sim::SimFuncOp function = summary.function;
  if (function.getBody().empty())
    return;

  Block &entry = function.getBody().front();
  for (BlockArgument argument : entry.getArguments())
    if (isLogic(argument.getType()) &&
        argument.getArgNumber() < formalBoundaries.size())
      updateFact(local.values, argument,
                 formalBoundaries[argument.getArgNumber()]);

  auto solveValues = [&](const DenseMap<Value, StateDomainFact> *proofFacts) {
    DenseSet<Block *> reachable;
    DenseMap<Value, std::optional<bool>> conditionMemo;
    if (proofFacts) {
      SmallVector<Block *> worklist{&entry};
      reachable.insert(&entry);
      while (!worklist.empty()) {
        Block *block = worklist.pop_back_val();
        if (block->empty())
          continue;
        Operation *terminator = block->getTerminator();
        for (unsigned successor = 0;
             successor != terminator->getNumSuccessors(); ++successor) {
          if (isProvablyDeadSuccessor(terminator, successor, *proofFacts,
                                      conditionMemo))
            continue;
          Block *target = terminator->getSuccessor(successor);
          if (reachable.insert(target).second)
            worklist.push_back(target);
        }
      }
      local.reachable = reachable;
    }
    while (true) {
      bool changed = false;
    for (const BlockArgumentSummary &argument : summary.blockArguments) {
      StateDomainFact joined = bottomFact();
      if (argument.incoming.empty()) {
        joined = mayFourState(StateDomainReason::UnsupportedProducer);
      } else {
        for (const IncomingSummary &incoming : argument.incoming) {
          if (proofFacts &&
              (!reachable.contains(incoming.terminator->getBlock()) ||
               isProvablyDeadSuccessor(incoming.terminator,
                                       incoming.successorIndex, *proofFacts,
                                       conditionMemo)))
            continue;
          StateDomainFact contribution =
              incoming.value
                  ? lookupLocalFact(local.values, incoming.value)
                  : mayFourState(StateDomainReason::UnsupportedProducer);
          joined = joinAlternatives(joined, contribution, incoming.reason);
        }
      }
      changed |= updateFact(local.values, argument.argument, joined);
    }

    for (Operation *operation : summary.operations) {
      if (auto call = dyn_cast<sim::SimCallOp>(operation)) {
        auto callee = calleeIndex.find(operation);
        for (auto [index, result] : llvm::enumerate(call.getResults())) {
          if (!isLogic(result.getType()))
            continue;
          StateDomainFact next =
              callee == calleeIndex.end() ||
                      index >= resultBoundaries[callee->second].size()
                  ? mayFourState(StateDomainReason::UnknownCall)
                  : resultBoundaries[callee->second][index];
          if (callee == calleeIndex.end())
            next.reason = StateDomainReason::UnknownCall;
          else if (next.reason != StateDomainReason::ExternalDeclaration)
            next.reason = StateDomainReason::CallResult;
          changed |= updateFact(local.values, result, next);
        }
        continue;
      }
      StateDomainFact transferred = transferOperation(
          operation, local.values, summary.provenance, assumedKnownRoots);
      for (Value result : operation->getResults())
        if (shouldTrackResult(operation, result))
          changed |= updateFact(local.values, result, transferred);
    }
    if (!changed)
      break;
    }
  };

  solveValues(nullptr);

  // Four-state lowering frequently represents `cond ? known : X` with an
  // explicit branch that is unreachable once the condition's inductive roots
  // are known.  The first solve establishes those domain facts.  A fresh
  // second solve may then ignore only edges whose i1 condition is structurally
  // proven constant by the two-state round-trip identity.  Restarting from
  // bottom is necessary because the ordinary domain lattice intentionally
  // never refines MayFourState downward.
  DenseMap<Value, StateDomainFact> proofFacts = local.values;
  LocalFacts pruned = initializeLocalFacts(summary);
  std::swap(local.values, pruned.values);
  for (BlockArgument argument : entry.getArguments())
    if (isLogic(argument.getType()) &&
        argument.getArgNumber() < formalBoundaries.size())
      updateFact(local.values, argument,
                 formalBoundaries[argument.getArgNumber()]);
  solveValues(&proofFacts);

  for (sim::SimReturnOp returnOp : summary.returns)
    for (auto [index, operand] : llvm::enumerate(returnOp.getOperands())) {
      if (!isLogic(operand.getType()) || index >= local.results.size())
        continue;
      StateDomainFact fact = lookupLocalFact(local.values, operand);
      local.results[index] =
          joinAlternatives(local.results[index], fact, fact.reason);
    }
}

} // namespace

StringRef stringifyStateDomain(StateDomain domain) {
  switch (domain) {
  case StateDomain::Bottom:
    return "bottom";
  case StateDomain::TwoState:
    return "two-state";
  case StateDomain::MayFourState:
    return "may-four-state";
  }
  llvm_unreachable("unknown state domain");
}

StringRef stringifyStateDomainReason(StateDomainReason reason) {
  switch (reason) {
  case StateDomainReason::Unresolved:
    return "unresolved";
  case StateDomainReason::NonLogic:
    return "non-logic";
  case StateDomainReason::FunctionEntry:
    return "function-entry";
  case StateDomainReason::CallActual:
    return "call-actual";
  case StateDomainReason::SpawnActual:
    return "spawn-actual";
  case StateDomainReason::CFGJoin:
    return "cfg-join";
  case StateDomainReason::InfeasibleCFG:
    return "infeasible-cfg";
  case StateDomainReason::Continuation:
    return "continuation";
  case StateDomainReason::LogicConstant:
    return "logic-constant";
  case StateDomainReason::UnknownConstant:
    return "unknown-constant";
  case StateDomainReason::LogicFromBits:
    return "logic-from-bits";
  case StateDomainReason::LogicResize:
    return "logic-resize";
  case StateDomainReason::LogicUnary:
    return "logic-unary";
  case StateDomainReason::LogicReduction:
    return "logic-reduction";
  case StateDomainReason::LogicBinary:
    return "logic-binary";
  case StateDomainReason::LogicMux:
    return "logic-mux";
  case StateDomainReason::LogicLogical:
    return "logic-logical";
  case StateDomainReason::LogicShift:
    return "logic-shift";
  case StateDomainReason::LogicCompare:
    return "logic-compare";
  case StateDomainReason::LogicConcat:
    return "logic-concat";
  case StateDomainReason::LogicReplicate:
    return "logic-replicate";
  case StateDomainReason::LogicExtract:
    return "logic-extract";
  case StateDomainReason::LogicInsert:
    return "logic-insert";
  case StateDomainReason::PackedView:
    return "packed-view";
  case StateDomainReason::DynamicExtract:
    return "dynamic-extract";
  case StateDomainReason::DynamicExtractIndex:
    return "dynamic-extract-index";
  case StateDomainReason::DivisionDivisor:
    return "division-divisor";
  case StateDomainReason::CaseComparison:
    return "case-comparison";
  case StateDomainReason::AbsorbingConstant:
    return "absorbing-constant";
  case StateDomainReason::InductiveRootRead:
    return "inductive-root-read";
  case StateDomainReason::RefLoad:
    return "ref-load";
  case StateDomainReason::NetRead:
    return "net-read";
  case StateDomainReason::CallResult:
    return "call-result";
  case StateDomainReason::UnknownCall:
    return "unknown-call";
  case StateDomainReason::ExternalDeclaration:
    return "external-declaration";
  case StateDomainReason::UnsupportedProducer:
    return "unsupported-producer";
  }
  llvm_unreachable("unknown state-domain reason");
}

FailureOr<DenseMap<Value, StateDomainFact>>
computeValueFacts(sim::SimDesignOp design, const RootSet &assumedKnownRoots) {
  if (design.getBody().empty()) {
    design.emitOpError("cannot analyze a design with no body");
    return failure();
  }

  SmallVector<sim::SimFuncOp> functions(
      design.getBody().front().getOps<sim::SimFuncOp>());
  llvm::sort(functions, [](sim::SimFuncOp lhs, sim::SimFuncOp rhs) {
    return lhs.getSymName() < rhs.getSymName();
  });

  SmallVector<FunctionSummary, 0> summaries(functions.size());
  parallelFor(design.getContext(), 0, functions.size(), [&](size_t index) {
    summaries[index] = buildSummary(functions[index]);
  });

  DenseMap<Operation *, unsigned> functionIndex;
  for (auto [index, function] : llvm::enumerate(functions))
    functionIndex[function.getOperation()] = index;

  // Resolve symbol references serially using MLIR's symbol-table semantics,
  // then retain operation indices for read-only local propagation.
  SymbolTableCollection symbolTables;
  DenseMap<Operation *, unsigned> calleeIndex;
  SmallVector<SmallVector<unsigned>> callers(functions.size());
  for (auto [caller, summary] : llvm::enumerate(summaries)) {
    for (InvocationSummary &invocation : summary.invocations) {
      sim::SimFuncOp callee;
      if (auto call = dyn_cast<sim::SimCallOp>(invocation.operation))
        callee = symbolTables.lookupNearestSymbolFrom<sim::SimFuncOp>(
            invocation.operation, call.getCalleeAttr());
      else if (auto task = dyn_cast<sim::SimTaskCallOp>(invocation.operation))
        callee = symbolTables.lookupNearestSymbolFrom<sim::SimFuncOp>(
            invocation.operation, task.getCalleeAttr());
      else if (auto spawn = dyn_cast<sim::SimSpawnOp>(invocation.operation))
        callee = symbolTables.lookupNearestSymbolFrom<sim::SimFuncOp>(
            invocation.operation, spawn.getCalleeAttr());
      if (!callee)
        continue;
      auto found = functionIndex.find(callee.getOperation());
      if (found == functionIndex.end())
        continue;
      invocation.callee = found->second;
      calleeIndex[invocation.operation] = found->second;
      if (!invocation.spawn &&
          std::find(callers[found->second].begin(),
                    callers[found->second].end(),
                    caller) == callers[found->second].end())
        callers[found->second].push_back(caller);
    }
  }

  SmallVector<SmallVector<StateDomainFact>> formalBoundaries(functions.size());
  SmallVector<SmallVector<StateDomainFact>> resultBoundaries(functions.size());
  SmallVector<SmallVector<bool>> hasIncoming(functions.size());
  for (auto [index, function] : llvm::enumerate(functions)) {
    unsigned numInputs = function.getFunctionType().getNumInputs();
    unsigned numResults = function.getFunctionType().getNumResults();
    formalBoundaries[index].assign(numInputs, bottomFact());
    resultBoundaries[index].assign(numResults, bottomFact());
    hasIncoming[index].assign(numInputs, false);
    if (function.getBody().empty())
      for (auto [resultIndex, type] :
           llvm::enumerate(function.getFunctionType().getResults()))
        if (isLogic(type))
          resultBoundaries[index][resultIndex] =
              mayFourState(StateDomainReason::ExternalDeclaration);
  }

  // Construct the combined call/spawn index deterministically from summaries
  // already stored in function-symbol and IR walk order.
  for (const FunctionSummary &summary : summaries)
    for (const InvocationSummary &invocation : summary.invocations) {
      if (!invocation.callee)
        continue;
      for (auto [index, operand] :
           llvm::enumerate(getInvocationOperands(invocation.operation)))
        if (index < hasIncoming[*invocation.callee].size() &&
            isLogic(operand.getType()))
          hasIncoming[*invocation.callee][index] = true;
    }

  // A formal is closed-world only if the function is private and every symbol
  // use is one of the direct calls or spawns indexed above.  Public / nested
  // symbols and opaque uses can introduce values from outside the analyzed
  // invocation graph.
  SmallVector<char> hasNonCallUse(functions.size(), false);
  std::optional<SymbolTable::UseRange> uses =
      SymbolTable::getSymbolUses(design);
  if (!uses) {
    llvm::fill(hasNonCallUse, true);
  } else {
    // Query the symbol table once for the whole design. Asking for uses of
    // every function separately performs a complete attribute walk each time;
    // an outlined RTL instance therefore turned this closed-world check into
    // O(functions * design-size) compile time.
    for (const SymbolTable::SymbolUse &use : *uses) {
      Operation *user = use.getUser();
      SymbolRefAttr reference = use.getSymbolRef();
      sim::SimFuncOp function =
          symbolTables.lookupNearestSymbolFrom<sim::SimFuncOp>(user,
                                                                reference);
      if (!function)
        continue;
      auto found = functionIndex.find(function.getOperation());
      if (found == functionIndex.end())
        continue;
      unsigned index = found->second;
      bool direct = false;
      if (auto call = dyn_cast<sim::SimCallOp>(user))
        direct = call.getCalleeAttr() == reference;
      else if (auto task = dyn_cast<sim::SimTaskCallOp>(user))
        direct = task.getCalleeAttr() == reference;
      else if (auto spawn = dyn_cast<sim::SimSpawnOp>(user))
        direct = spawn.getCalleeAttr() == reference;
      if (direct && calleeIndex.contains(user) &&
          calleeIndex.lookup(user) == index &&
          countSymbolReferences(user, reference) == 1)
        continue;
      hasNonCallUse[index] = true;
    }
  }

  for (auto [function, boundaries] : llvm::enumerate(formalBoundaries))
    for (auto [index, type] :
         llvm::enumerate(functions[function].getFunctionType().getInputs()))
      if (isLogic(type) &&
          (functions[function].isExternal() || hasNonCallUse[function] ||
           SymbolTable::getSymbolVisibility(functions[function]) !=
               SymbolTable::Visibility::Private ||
           !hasIncoming[function][index]))
        boundaries[index] = mayFourState(StateDomainReason::FunctionEntry);

  SmallVector<LocalFacts> locals;
  locals.reserve(summaries.size());
  for (const FunctionSummary &summary : summaries)
    locals.push_back(initializeLocalFacts(summary));

  // Solve boundary facts serially with a deterministic function worklist.
  // Local facts are retained between visits, so each fact only moves upward
  // through the finite lattice even across recursive call/spawn cycles.
  std::deque<unsigned> worklist;
  SmallVector<bool> queued(functions.size(), false);
  auto enqueue = [&](unsigned function) {
    if (queued[function])
      return;
    queued[function] = true;
    worklist.push_back(function);
  };
  for (unsigned function = 0; function != functions.size(); ++function)
    enqueue(function);

  while (!worklist.empty()) {
    unsigned function = worklist.front();
    worklist.pop_front();
    queued[function] = false;

    propagateFunction(summaries[function], formalBoundaries[function],
                      resultBoundaries, calleeIndex, assumedKnownRoots,
                      locals[function]);

    for (auto [result, fact] : llvm::enumerate(locals[function].results)) {
      if (!isLogic(functions[function].getFunctionType().getResult(result)) ||
          !updateBoundary(resultBoundaries[function][result], fact,
                          fact.reason))
        continue;
      for (unsigned caller : callers[function])
        enqueue(caller);
    }

    for (const InvocationSummary &invocation :
         summaries[function].invocations) {
      if (!invocation.callee)
        continue;
      StateDomainReason reason = invocation.spawn
                                     ? StateDomainReason::SpawnActual
                                     : StateDomainReason::CallActual;
      for (auto [index, operand] :
           llvm::enumerate(getInvocationOperands(invocation.operation))) {
        if (index >= formalBoundaries[*invocation.callee].size() ||
            !isLogic(operand.getType()))
          continue;
        StateDomainFact actual =
            lookupLocalFact(locals[function].values, operand);
        if (updateBoundary(formalBoundaries[*invocation.callee][index], actual,
                           reason))
          enqueue(*invocation.callee);
      }
    }
  }

  auto resolveBottom = [](StateDomainFact &fact) {
    if (fact.domain == StateDomain::Bottom)
      fact = mayFourState(StateDomainReason::Unresolved);
  };
  for (auto &boundaries : formalBoundaries)
    for (StateDomainFact &fact : boundaries)
      resolveBottom(fact);
  for (auto &boundaries : resultBoundaries)
    for (StateDomainFact &fact : boundaries)
      resolveBottom(fact);

  SmallVector<LocalFacts> finalLocals(functions.size());
  parallelFor(design.getContext(), 0, summaries.size(), [&](size_t index) {
    finalLocals[index] = initializeLocalFacts(summaries[index]);
    propagateFunction(summaries[index], formalBoundaries[index],
                      resultBoundaries, calleeIndex, assumedKnownRoots,
                      finalLocals[index]);
  });

  DenseMap<Value, StateDomainFact> facts;
  for (LocalFacts &local : finalLocals)
    for (auto [value, fact] : local.values) {
      if (!local.reachable.empty() &&
          !local.reachable.contains(value.getParentBlock()))
        fact = twoState(StateDomainReason::InfeasibleCFG);
      resolveBottom(fact);
      facts.try_emplace(value, fact);
    }
  return facts;
}

std::optional<RootKey>
getConcreteRoot(Value handle,
                const analysis::DescriptorProvenanceMap &provenance) {
  auto found = provenance.find(handle);
  if (found == provenance.end() || !found->second.descriptor ||
      (found->second.resource != sim::ComputeResourceKind::Storage &&
       found->second.resource != sim::ComputeResourceKind::Net))
    return std::nullopt;
  return getRootKey(found->second.resource, *found->second.descriptor);
}

StateDomainFact getValueFact(const DenseMap<Value, StateDomainFact> &facts,
                             Value value) {
  auto found = facts.find(value);
  if (found != facts.end())
    return found->second;
  return isLogic(value.getType())
             ? mayFourState(StateDomainReason::UnsupportedProducer)
             : twoState(StateDomainReason::NonLogic);
}

FailureOr<StateDomainAnalysis>
StateDomainAnalysis::compute(sim::SimDesignOp design,
                             bool proveInductiveRoots) {
  if (design.getBody().empty()) {
    design.emitOpError("cannot analyze a design with no body");
    return failure();
  }
  if (!proveInductiveRoots) {
    FailureOr<DenseMap<Value, StateDomainFact>> facts =
        computeValueFacts(design, RootSet{});
    if (failed(facts))
      return failure();
    DenseMap<Value, StateDomainFact> guarded = *facts;
    return StateDomainAnalysis(std::move(*facts), std::move(guarded), {});
  }
  RootSet candidates;
  DenseMap<uint64_t, unsigned> netDriverCounts;
  DenseMap<uint64_t, uint64_t> netWidths;
  DenseMap<uint64_t, bool> netHasFullDriver;
  DenseMap<uint64_t, SmallVector<uint64_t>> connectedNets;
  DenseSet<uint64_t> partialConnections;
  for (Operation &operation : design.getBody().front()) {
    if (auto storage = dyn_cast<sim::SimStorageDeclOp>(operation)) {
      if (isLogic(storage.getType()))
        candidates.insert(
            getRootKey(sim::ComputeResourceKind::Storage, storage.getId()));
      continue;
    }
    if (auto net = dyn_cast<sim::SimNetDeclOp>(operation)) {
      if (isLogic(net.getType())) {
        candidates.insert(
            getRootKey(sim::ComputeResourceKind::Net, net.getId()));
        if (std::optional<uint64_t> width =
                sim::getProvenanceSpan(net.getType()))
          netWidths[net.getId()] = *width;
        connectedNets[net.getId()];
      }
      continue;
    }
    if (auto driver = dyn_cast<sim::SimDriverDeclOp>(operation)) {
      ++netDriverCounts[driver.getNetId()];
      std::optional<uint64_t> width = sim::getProvenanceSpan(driver.getType());
      uint64_t low = driver.getDrivenLowAttr()
                         ? driver.getDrivenLowAttr().getValue().getZExtValue()
                         : 0;
      uint64_t drivenWidth =
          driver.getDrivenWidthAttr()
              ? driver.getDrivenWidthAttr().getValue().getZExtValue()
              : width.value_or(0);
      netHasFullDriver[driver.getNetId()] =
          width && low == 0 && drivenWidth == *width;
    }
  }
  // Full-width net connections form one resolved value.  A component with
  // exactly one full-width driver cannot create X from resolution and is a
  // valid guarded two-state root.  Multiple drivers can conflict, and partial
  // connections can leave Z bits, so the whole-root proof rejects those
  // components conservatively.  This retains ordinary one-driver port aliases
  // instead of poisoning the entire transitive state closure.
  for (sim::SimNetConnectDeclOp connection :
       design.getBody().front().getOps<sim::SimNetConnectDeclOp>()) {
    uint64_t lhs = connection.getLhsNetId();
    uint64_t rhs = connection.getRhsNetId();
    connectedNets[lhs].push_back(rhs);
    connectedNets[rhs].push_back(lhs);
    auto lhsWidth = netWidths.find(lhs);
    auto rhsWidth = netWidths.find(rhs);
    if (lhsWidth == netWidths.end() || rhsWidth == netWidths.end() ||
        connection.getLhsOffset() != 0 || connection.getRhsOffset() != 0 ||
        connection.getWidth() != lhsWidth->second ||
        connection.getWidth() != rhsWidth->second) {
      partialConnections.insert(lhs);
      partialConnections.insert(rhs);
    }
  }
  DenseSet<uint64_t> visitedNets;
  for (auto [net, unused] : connectedNets) {
    if (!visitedNets.insert(net).second)
      continue;
    SmallVector<uint64_t> component{net};
    bool partial = false;
    uint64_t driverCount = 0;
    bool fullDriver = false;
    for (size_t index = 0; index != component.size(); ++index) {
      uint64_t member = component[index];
      partial |= partialConnections.contains(member);
      driverCount += netDriverCounts.lookup(member);
      fullDriver |= netHasFullDriver.lookup(member);
      for (uint64_t neighbor : connectedNets.lookup(member))
        if (visitedNets.insert(neighbor).second)
          component.push_back(neighbor);
    }
    if (!partial && driverCount == 1 && fullDriver)
      continue;
    for (uint64_t member : component)
      candidates.erase(
          getRootKey(sim::ComputeResourceKind::Net, member));
  }

  DenseMap<Value, StateDomainFact> facts;
  while (true) {
    FailureOr<DenseMap<Value, StateDomainFact>> solved =
        computeValueFacts(design, candidates);
    if (failed(solved))
      return failure();
    facts = std::move(*solved);
    RootSet rejected;
    for (sim::SimFuncOp function :
         design.getBody().front().getOps<sim::SimFuncOp>()) {
      analysis::DescriptorProvenanceMap provenance =
          analysis::deriveDescriptorProvenance(function);
      function.walk([&](Operation *operation) {
        Value destination;
        Value value;
        if (auto store = dyn_cast<sim::SimRefStoreOp>(operation)) {
          destination = store.getReference();
          value = store.getValue();
        } else if (auto nba = dyn_cast<sim::SimNBAEnqueueOp>(operation)) {
          destination = nba.getDestination();
          value = nba.getValue();
        } else if (auto drive = dyn_cast<sim::SimDriverDriveOp>(operation)) {
          destination = drive.getDriver();
          value = drive.getValue();
        } else if (auto drive =
                       dyn_cast<sim::SimDriverDriveChangedOp>(operation)) {
          destination = drive.getDriver();
          value = drive.getValue();
        } else {
          return;
        }
        if (getValueFact(facts, value).domain == StateDomain::TwoState)
          return;
        if (std::optional<RootKey> root =
                getConcreteRoot(destination, provenance)) {
          if (candidates.contains(*root))
            rejected.insert(*root);
          return;
        }
        auto found = provenance.find(destination);
        sim::ComputeResourceKind resource =
            found == provenance.end() ? sim::ComputeResourceKind::Unknown
                                      : found->second.resource;
        for (RootKey root : candidates)
          if (resource == sim::ComputeResourceKind::Unknown ||
              root.first == static_cast<unsigned>(resource))
            rejected.insert(root);
      });
    }
    if (rejected.empty())
      break;
    for (RootKey root : rejected)
      candidates.erase(root);
  }

  DenseMap<Value, StateDomainFact> guardedFacts = facts;

  // Keep the longstanding value facts unconditional. The inductive root set
  // is a separate guarded capability: consumers may assume it only after
  // checking the corresponding unknown planes at a kernel boundary.
  FailureOr<DenseMap<Value, StateDomainFact>> unconditional =
      computeValueFacts(design, RootSet{});
  if (failed(unconditional))
    return failure();
  facts = std::move(*unconditional);

  SmallVector<InductiveStateRoot> inductiveRoots;
  inductiveRoots.reserve(candidates.size());
  for (RootKey root : candidates)
    inductiveRoots.push_back(
        {static_cast<sim::ComputeResourceKind>(root.first), root.second});
  llvm::sort(inductiveRoots, [](const auto &lhs, const auto &rhs) {
    return std::tie(lhs.resource, lhs.descriptor) <
           std::tie(rhs.resource, rhs.descriptor);
  });
  return StateDomainAnalysis(std::move(facts), std::move(guardedFacts),
                             std::move(inductiveRoots));
}

FailureOr<StateDomainAnalysis>
StateDomainAnalysis::computeAssumingKnownState(sim::SimDesignOp design) {
  if (design.getBody().empty()) {
    design.emitOpError("cannot analyze a design with no body");
    return failure();
  }
  RootSet assumedKnown;
  for (Operation &operation : design.getBody().front()) {
    if (auto storage = dyn_cast<sim::SimStorageDeclOp>(operation)) {
      if (isLogic(storage.getType()))
        assumedKnown.insert(
            getRootKey(sim::ComputeResourceKind::Storage, storage.getId()));
      continue;
    }
    if (auto net = dyn_cast<sim::SimNetDeclOp>(operation))
      if (isLogic(net.getType()))
        assumedKnown.insert(
            getRootKey(sim::ComputeResourceKind::Net, net.getId()));
  }
  FailureOr<DenseMap<Value, StateDomainFact>> guarded =
      computeValueFacts(design, assumedKnown);
  if (failed(guarded))
    return failure();
  FailureOr<DenseMap<Value, StateDomainFact>> unconditional =
      computeValueFacts(design, RootSet{});
  if (failed(unconditional))
    return failure();
  SmallVector<InductiveStateRoot> roots;
  roots.reserve(assumedKnown.size());
  for (RootKey root : assumedKnown)
    roots.push_back(
        {static_cast<sim::ComputeResourceKind>(root.first), root.second});
  llvm::sort(roots, [](const auto &lhs, const auto &rhs) {
    return std::tie(lhs.resource, lhs.descriptor) <
           std::tie(rhs.resource, rhs.descriptor);
  });
  return StateDomainAnalysis(std::move(*unconditional), std::move(*guarded),
                             std::move(roots));
}

StateDomainFact StateDomainAnalysis::get(Value value) const {
  auto found = facts.find(value);
  if (found != facts.end() && found->second.domain != StateDomain::Bottom)
    return found->second;
  if (!isLogic(value.getType()))
    return twoState(StateDomainReason::NonLogic);
  return mayFourState(StateDomainReason::Unresolved);
}

bool StateDomainAnalysis::isTwoState(Value value) const {
  return get(value).domain == StateDomain::TwoState;
}

StateDomainFact StateDomainAnalysis::getWithInductiveRoots(Value value) const {
  auto found = guardedFacts.find(value);
  if (found != guardedFacts.end() &&
      found->second.domain != StateDomain::Bottom)
    return found->second;
  if (!isLogic(value.getType()))
    return twoState(StateDomainReason::NonLogic);
  return mayFourState(StateDomainReason::Unresolved);
}

bool StateDomainAnalysis::isTwoStateWithInductiveRoots(Value value) const {
  return getWithInductiveRoots(value).domain == StateDomain::TwoState;
}

bool StateDomainAnalysis::isInductivelyTwoState(
    sim::ComputeResourceKind resource, uint64_t descriptor) const {
  return llvm::is_contained(inductiveRoots,
                            InductiveStateRoot{resource, descriptor});
}

} // namespace obelisk
