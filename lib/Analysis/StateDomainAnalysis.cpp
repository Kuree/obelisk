//===- StateDomainAnalysis.cpp - Whole-value X/Z analysis ----------------===//

#include "obelisk/Analysis/StateDomainAnalysis.h"
#include "obelisk/Analysis/ClassDispatchAnalysis.h"
#include "obelisk/Analysis/LogicBitAnalysis.h"
#include "obelisk/Analysis/SimulationAnalysis.h"
#include "obelisk/Dialect/Native/NativeDialect.h"
#include "obelisk/Dialect/Schedule/ScheduleEnums.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/Matchers.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/IR/Threading.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/TypeSwitch.h"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <limits>
#include <optional>
#include <tuple>

using namespace mlir;

namespace obelisk {
namespace {

using RootKey = std::pair<unsigned, uint64_t>;
using RootSet = DenseSet<RootKey>;

RootKey getRootKey(schedule::ComputeResourceKind resource,
                   uint64_t descriptor) {
  return {static_cast<unsigned>(resource), descriptor};
}

bool isGuardableRoot(Type type) {
  std::optional<uint64_t> width = sim::getProvenanceSpan(type);
  return hasUnknownPlane(type) && width && *width <= maximumGuardedRootBits;
}

/// Every four-state root a guarded proof may assume known.
RootSet collectGuardableRoots(sim::SimDesignOp design) {
  RootSet roots;
  for (Operation &operation : design.getBody().front()) {
    if (auto storage = dyn_cast<sim::SimStorageDeclOp>(operation)) {
      if (isGuardableRoot(storage.getType()))
        roots.insert(getRootKey(schedule::ComputeResourceKind::Storage,
                                storage.getId()));
    } else if (auto net = dyn_cast<sim::SimNetDeclOp>(operation)) {
      if (isGuardableRoot(net.getType()))
        roots.insert(
            getRootKey(schedule::ComputeResourceKind::Net, net.getId()));
    }
  }
  return roots;
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
StateDomainFact
combineValues(ValueRange operands,
              llvm::function_ref<StateDomainFact(Value)> operandFact,
              StateDomainReason reason) {
  bool sawLogic = false;
  bool sawBottom = false;
  for (Value operand : operands) {
    if (!hasUnknownPlane(operand.getType()))
      continue;
    sawLogic = true;
    StateDomainFact fact = operandFact(operand);
    if (fact.domain == StateDomain::MayFourState)
      return mayFourState(reason);
    if (fact.domain == StateDomain::Bottom)
      sawBottom = true;
  }
  if (!sawLogic)
    return twoState(reason);
  return sawBottom ? bottomFact() : twoState(reason);
}

StateDomainFact
combineOperands(Operation *op,
                llvm::function_ref<StateDomainFact(Value)> operandFact,
                StateDomainReason reason) {
  return combineValues(op->getOperands(), operandFact, reason);
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

bool isSuspensionTerminator(Operation *op) { return sim::isSuspensionOp(op); }

} // namespace

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

namespace {

StateDomainFact transferOperation(
    Operation *op, const DenseMap<Value, StateDomainFact> &facts,
    const analysis::HandleFacts &provenance,
    const DenseMap<Value, analysis::HandleCertificate> &certificates,
    const RootSet &assumedKnownRoots) {
  auto operandFact = [&](Value operand) {
    auto found = facts.find(operand);
    return found == facts.end()
               ? mayFourState(StateDomainReason::UnsupportedProducer)
               : found->second;
  };
  // A dynamic load reads its root only through a valid selection; otherwise
  // it yields the element default (IEEE 1800-2023 7.4.6, 11.5.1). Selections
  // that are valid for every known value of their selectors are valid
  // whenever this solve proves those selectors two-state.
  StateDomain selection = StateDomain::TwoState;
  if (auto load = dyn_cast<sim::SimRefLoadOp>(op)) {
    auto root = provenance.find(load.getReference());
    if (root != provenance.end() && root->second.dynamic) {
      auto certificate = certificates.find(load.getReference());
      selection =
          certificate == certificates.end() ||
                  !certificate->second.inBoundsIfKnown
              ? StateDomain::MayFourState
              : combineValues(certificate->second.knownSelectors, operandFact,
                              StateDomainReason::DynamicExtractIndex)
                    .domain;
    }
    if (selection == StateDomain::Bottom)
      return bottomFact();
  }
  auto isKnownRead = [&](Operation *read) {
    Value handle;
    if (auto load = dyn_cast<sim::SimRefLoadOp>(read))
      handle = load.getReference();
    else
      handle = cast<sim::SimNetReadOp>(read).getNet();
    auto root = provenance.find(handle);
    if (root == provenance.end() || !root->second.descriptor ||
        selection != StateDomain::TwoState)
      return false;
    return assumedKnownRoots.contains(
        getRootKey(root->second.resource, *root->second.descriptor));
  };
  return transferStateDomain(op, operandFact, isKnownRead,
                             isConstantInRangeSelection);
}
} // namespace

/// Approximate instructions of the unknown-plane expansion of `op` in
/// SimulationToStandard, which a two-state copy no longer executes.
unsigned getUnknownPlaneWork(Operation *op) {
  if (auto binary = dyn_cast<sim::SimLogicBinaryOp>(op)) {
    switch (binary.getKind()) {
    case sim::BinaryKind::And:
    case sim::BinaryKind::Or:
      return 9; // known-zero/known-one dominance on both operands
    case sim::BinaryKind::Xor:
    case sim::BinaryKind::Xnor:
      return 3;
    default:
      return 5; // any-unknown test and X selection
    }
  }
  if (auto compare = dyn_cast<sim::SimLogicCompareOp>(op))
    return compare.getKind() == sim::CompareKind::CaseEq ||
                   compare.getKind() == sim::CompareKind::CaseNe
               ? 2
               : 5;
  if (auto shift = dyn_cast<sim::SimLogicShiftOp>(op))
    return isa_and_nonnull<arith::ConstantOp>(shift.getAmount().getDefiningOp())
               ? 1
               : 5;
  return llvm::TypeSwitch<Operation *, unsigned>(op)
      .Case<sim::SimLogicMuxOp>([](auto) { return 8; })
      .Case<sim::SimLogicLogicalOp, sim::SimLogicPowerOp>(
          [](auto) { return 5; })
      .Case<sim::SimLogicReductionOp>([](auto) { return 4; })
      .Case<sim::SimLogicIsTrueOp, sim::SimLogicDynExtractOp,
            sim::SimLogicDynInsertOp, sim::SimArrayDynExtractOp,
            sim::SimArrayDynInsertOp>([](auto) { return 3; })
      .Case<sim::SimLogicUnaryOp>([](auto) { return 2; })
      .Case<sim::SimLogicConstantOp, sim::SimLogicFromBitsOp,
            sim::SimLogicToBitsOp, sim::SimPackedFlattenOp,
            sim::SimPackedUnflattenOp, sim::SimRefLoadOp, sim::SimNetReadOp,
            sim::SimRefStoreOp>([](auto) { return 0; })
      .Default([](Operation *) { return 1; });
}

bool hasUnknownPlane(Type type) {
  if (isa<sim::LogicType>(type))
    return true;
  if (!sim::isAggregateType(type))
    return false;
  for (unsigned index = 0; index < sim::getAggregateNumElements(type); ++index)
    if (hasUnknownPlane(sim::getAggregateElementType(type, index)))
      return true;
  return false;
}

bool isConstantInRangeSelection(Operation *op) {
  if (auto extract = dyn_cast<sim::SimLogicDynExtractOp>(op)) {
    std::optional<APInt> index = getConstantInteger(extract.getLowBit());
    if (!index || index->isNegative() || index->getActiveBits() > 64)
      return false;
    uint64_t low = index->getZExtValue();
    uint64_t inputWidth = extract.getInput().getType().getWidth();
    uint64_t resultWidth = extract.getResult().getType().getWidth();
    return low <= inputWidth && resultWidth <= inputWidth - low;
  }
  if (auto extract = dyn_cast<sim::SimArrayDynExtractOp>(op)) {
    std::optional<APInt> index = getConstantInteger(extract.getIndex());
    auto bounds = getArraySelectionBounds(extract.getInput().getType());
    return index && bounds && index->getSignificantBits() <= 64 &&
           index->getSExtValue() >= bounds->first &&
           index->getSExtValue() <= bounds->second;
  }
  return false;
}

FailureOr<uint64_t> getNativeStateDomainFunctionLimit(Operation *module) {
  if (auto limit = native::get<native::MaxStateDomainFunctions>(module)) {
    if (limit.getInt() < 0)
      return module->emitError(
          "native state-domain function limit cannot be negative");
    return limit.getUInt();
  }
  auto level = native::get<native::OptimizationLevel>(module);
  if (level && level.getInt() >= 3)
    return 4096;
  return std::numeric_limits<uint64_t>::max();
}

std::optional<std::pair<int64_t, int64_t>>
getArraySelectionBounds(Type arrayType) {
  auto bounds = [](int64_t left, int64_t right) {
    return std::make_pair(std::min(left, right), std::max(left, right));
  };
  if (auto packed = dyn_cast<sim::PackedArrayType>(arrayType))
    return bounds(packed.getLeft(), packed.getRight());
  if (auto unpacked = dyn_cast<sim::UnpackedArrayType>(arrayType))
    return bounds(unpacked.getLeft(), unpacked.getRight());
  return std::nullopt;
}

StateDomainFact
transferStateDomain(Operation *op,
                    llvm::function_ref<StateDomainFact(Value)> operandFact,
                    llvm::function_ref<bool(Operation *)> isKnownRead,
                    llvm::function_ref<bool(Operation *)> isInRangeSelection) {
  if (auto constant = dyn_cast<sim::SimLogicConstantOp>(op))
    return constant.getUnknown().isZero()
               ? twoState(StateDomainReason::LogicConstant)
               : mayFourState(StateDomainReason::UnknownConstant);
  if (isa<sim::SimLogicFromBitsOp>(op))
    return twoState(StateDomainReason::LogicFromBits);
  if (isa<sim::SimRefLoadOp>(op))
    return isKnownRead(op) ? twoState(StateDomainReason::InductiveRootRead)
                           : mayFourState(StateDomainReason::RefLoad);
  if (isa<sim::SimNetReadOp>(op))
    return isKnownRead(op) ? twoState(StateDomainReason::InductiveRootRead)
                           : mayFourState(StateDomainReason::NetRead);

  if (auto binary = dyn_cast<sim::SimLogicBinaryOp>(op)) {
    if ((binary.getKind() == sim::BinaryKind::And &&
         (isKnownZero(binary.getLhs()) || isKnownZero(binary.getRhs()))) ||
        (binary.getKind() == sim::BinaryKind::Or &&
         (isKnownAllOnes(binary.getLhs()) || isKnownAllOnes(binary.getRhs()))))
      return twoState(StateDomainReason::AbsorbingConstant);
    StateDomainFact operands =
        combineOperands(op, operandFact, StateDomainReason::LogicBinary);
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
    return combineOperands(op, operandFact, StateDomainReason::LogicLogical);
  }

  if (auto compare = dyn_cast<sim::SimLogicCompareOp>(op)) {
    if (compare.getKind() == sim::CompareKind::CaseEq ||
        compare.getKind() == sim::CompareKind::CaseNe ||
        compare.getKind() == sim::CompareKind::CaseZEq ||
        compare.getKind() == sim::CompareKind::CaseXZEq)
      return twoState(StateDomainReason::CaseComparison);
    return combineOperands(op, operandFact, StateDomainReason::LogicCompare);
  }

  // An invalid selection yields X bits for a packed slice and the element
  // default for an array (IEEE 1800-2023 7.4.6, 11.5.1); a known selection
  // only moves bits. An invalid insertion performs no write, so the index
  // never affects an insertion's domain.
  if (isa<sim::SimLogicDynExtractOp, sim::SimArrayDynExtractOp>(op)) {
    if (!hasUnknownPlane(op->getResult(0).getType()))
      return twoState(StateDomainReason::NonLogic);
    if (!isInRangeSelection(op))
      return mayFourState(StateDomainReason::DynamicExtractIndex);
    return combineOperands(op, operandFact, StateDomainReason::DynamicExtract);
  }
  if (auto insert = dyn_cast<sim::SimLogicDynInsertOp>(op))
    return combineValues({insert.getInput(), insert.getReplacement()},
                         operandFact, StateDomainReason::DynamicExtract);
  if (auto insert = dyn_cast<sim::SimArrayDynInsertOp>(op))
    return combineValues({insert.getInput(), insert.getReplacement()},
                         operandFact, StateDomainReason::DynamicExtract);

  return llvm::TypeSwitch<Operation *, StateDomainFact>(op)
      .Case<sim::SimLogicResizeOp>([&](auto) {
        return combineOperands(op, operandFact, StateDomainReason::LogicResize);
      })
      .Case<sim::SimLogicUnaryOp>([&](auto) {
        return combineOperands(op, operandFact, StateDomainReason::LogicUnary);
      })
      .Case<sim::SimLogicReductionOp>([&](auto) {
        return combineOperands(op, operandFact,
                               StateDomainReason::LogicReduction);
      })
      .Case<sim::SimLogicMuxOp>([&](auto) {
        return combineOperands(op, operandFact, StateDomainReason::LogicMux);
      })
      .Case<SelectLikeOpInterface>([&](auto select) {
        return combineValues({select.getTrueValue(), select.getFalseValue()},
                             operandFact, StateDomainReason::Select);
      })
      .Case<sim::SimLogicShiftOp>([&](auto) {
        return combineOperands(op, operandFact, StateDomainReason::LogicShift);
      })
      .Case<sim::SimLogicConcatOp>([&](auto) {
        return combineOperands(op, operandFact, StateDomainReason::LogicConcat);
      })
      .Case<sim::SimLogicReplicateOp>([&](auto) {
        return combineOperands(op, operandFact,
                               StateDomainReason::LogicReplicate);
      })
      .Case<sim::SimLogicExtractOp>([&](auto) {
        return combineOperands(op, operandFact,
                               StateDomainReason::LogicExtract);
      })
      .Case<sim::SimLogicInsertOp>([&](auto) {
        return combineOperands(op, operandFact, StateDomainReason::LogicInsert);
      })
      // Flatten and unflatten are representation-only views of the same
      // packed value.  They neither synthesize nor discard unknown bits, so
      // their state domain is exactly the domain of the input value.
      .Case<sim::SimPackedFlattenOp, sim::SimPackedUnflattenOp>([&](auto) {
        return combineOperands(op, operandFact, StateDomainReason::PackedView);
      })
      // Static subelement selection and whole-aggregate construction move
      // bits without creating X/Z; an aggregate default value is X for every
      // four-state member (IEEE 1800-2023 6.8, Table 6-7).
      .Case<sim::SimAggregateExtractOp, sim::SimAggregateInsertOp,
            sim::SimAggregateConstructOp, sim::SimAggregateSplatOp>([&](auto) {
        return combineOperands(op, operandFact, StateDomainReason::Aggregate);
      })
      .Case<sim::SimAggregateDefaultOp>([&](auto) {
        return hasUnknownPlane(op->getResult(0).getType())
                   ? mayFourState(StateDomainReason::UnknownConstant)
                   : twoState(StateDomainReason::Aggregate);
      })
      .Default([&](Operation *) {
        return mayFourState(StateDomainReason::UnsupportedProducer);
      });
}

namespace {

struct InvocationSummary {
  Operation *operation = nullptr;
  bool spawn = false;
  FlatSymbolRefAttr target;
  std::optional<unsigned> callee;
};

SmallVector<Value> getInvocationOperands(Operation *operation) {
  if (auto task = dyn_cast<sim::SimClassVirtualTaskCallOp>(operation)) {
    auto caller = task->getParentOfType<sim::SimFuncOp>();
    SmallVector<Value> operands;
    if (caller && !caller.getBody().empty() &&
        !caller.getBody().front().getArguments().empty())
      operands.push_back(caller.getBody().front().getArgument(0));
    operands.push_back(task.getReceiver());
    llvm::append_range(operands, task.getArguments());
    return operands;
  }
  if (auto task = dyn_cast<sim::SimTaskCallOp>(operation))
    return SmallVector<Value>(task.getArguments());
  return SmallVector<Value>(operation->getOperands());
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
  analysis::HandleFacts provenance;
  DenseMap<Value, analysis::HandleCertificate> certificates;
  LogicBitAnalysis bitAnalysis;
  SmallVector<Block *> blocks;
  SmallVector<BlockArgumentSummary> blockArguments;
  SmallVector<Operation *> operations;
  DenseMap<Value, SmallVector<unsigned>> dependents;
  SmallVector<InvocationSummary> invocations;
  SmallVector<sim::SimReturnOp> returns;
};

FunctionSummary
buildSummary(sim::SimFuncOp function,
             const analysis::ClassDispatchAnalysis &classDispatch,
             const analysis::HandleDataflowAnalysis &descriptorProvenance) {
  FunctionSummary summary;
  summary.function = function;
  summary.bitAnalysis = LogicBitAnalysis(function);
  auto handles = descriptorProvenance.analyze(function);
  summary.provenance = std::move(handles.facts);
  summary.certificates = std::move(handles.certificates);
  if (function.getBody().empty())
    return summary;

  function.walk<WalkOrder::PreOrder>(
      [&](Block *block) { summary.blocks.push_back(block); });

  DenseMap<Value, unsigned> argumentIndex;
  Block *entry = &function.getBody().front();
  for (Block *block : summary.blocks) {
    for (BlockArgument argument : block->getArguments()) {
      if (block == entry || !hasUnknownPlane(argument.getType()))
        continue;
      argumentIndex[argument] = summary.blockArguments.size();
      summary.blockArguments.push_back({argument, {}});
    }
    for (Operation &operation : *block) {
      summary.operations.push_back(&operation);
      if (auto call = dyn_cast<sim::SimCallOp>(operation))
        summary.invocations.push_back(
            {&operation, false, call.getCalleeAttr(), std::nullopt});
      else if (auto task = dyn_cast<sim::SimTaskCallOp>(operation))
        summary.invocations.push_back(
            {&operation, false, task.getCalleeAttr(), std::nullopt});
      else if (auto task =
                   dyn_cast<sim::SimClassVirtualTaskCallOp>(operation)) {
        auto staticType =
            cast<sim::ClassHandleType>(task.getReceiver().getType());
        for (sim::SimClassMethodDeclOp method :
             classDispatch.compatibleImplementations(
                 classDispatch.lookup(staticType), task.getSlot(),
                 task.getSignatureId(), /*isTask=*/true)) {
          summary.invocations.push_back({&operation, false,
                                         method.getImplementationAttr(),
                                         std::nullopt});
        }
      } else if (auto spawn = dyn_cast<sim::SimSpawnOp>(operation))
        summary.invocations.push_back(
            {&operation, true, spawn.getCalleeAttr(), std::nullopt});
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
        StateDomainReason reason =
            incoming && hasUnknownPlane(incoming.getType())
                ? (isSuspensionTerminator(terminator)
                       ? StateDomainReason::Continuation
                       : StateDomainReason::CFGJoin)
                : StateDomainReason::UnsupportedProducer;
        summary.blockArguments[found->second].incoming.push_back(
            {incoming && hasUnknownPlane(incoming.getType()) ? incoming
                                                             : Value(),
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
  // The finite SSA lattice only needs to revisit users of a changed value.
  // Preserve both successor lanes when one predecessor targets a join twice.
  for (auto [node, argument] : llvm::enumerate(summary.blockArguments))
    for (const IncomingSummary &incoming : argument.incoming)
      if (incoming.value)
        summary.dependents[incoming.value].push_back(node);
  for (auto [index, operation] : llvm::enumerate(summary.operations)) {
    unsigned node = summary.blockArguments.size() + index;
    for (Value operand : operation->getOperands())
      summary.dependents[operand].push_back(node);
    // A dynamic load also depends on the selectors its bounds proof needs.
    if (auto load = dyn_cast<sim::SimRefLoadOp>(operation)) {
      auto certificate = summary.certificates.find(load.getReference());
      if (certificate != summary.certificates.end())
        for (Value selector : certificate->second.knownSelectors)
          summary.dependents[selector].push_back(node);
    }
  }
  return summary;
}

struct LocalFacts {
  DenseMap<Value, StateDomainFact> values;
  SmallVector<StateDomainFact> results;
  DenseSet<Block *> reachable;
};

bool shouldTrackResult(Operation *operation, Value result) {
  return hasUnknownPlane(result.getType()) ||
         (isa<sim::SimLogicCompareOp>(operation) &&
          result.getType().isSignlessInteger(1));
}

StateDomainFact lookupLocalFact(const DenseMap<Value, StateDomainFact> &facts,
                                Value value) {
  auto found = facts.find(value);
  if (found != facts.end())
    return found->second;
  if (!hasUnknownPlane(value.getType()))
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
      if (!hasUnknownPlane(argument.getType()))
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
getProvableCondition(Value value, const DenseMap<Value, StateDomainFact> &facts,
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
  if (matchPattern(value, m_ConstantInt(&constant)) &&
      constant.getBitWidth() == 1)
    return done(!constant.isZero());

  if (auto argument = dyn_cast<BlockArgument>(value)) {
    std::optional<bool> result;
    bool sawIncoming = false;
    for (Block *predecessor : argument.getOwner()->getPredecessors()) {
      auto branch = dyn_cast<BranchOpInterface>(predecessor->getTerminator());
      if (!branch)
        return done(std::nullopt);
      for (unsigned successor = 0; successor != predecessor->getNumSuccessors();
           ++successor) {
        if (predecessor->getSuccessor(successor) != argument.getOwner())
          continue;
        SuccessorOperands operands = branch.getSuccessorOperands(successor);
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
        auto toBits =
            fromBits
                ? fromBits.getInput().getDefiningOp<sim::SimLogicToBitsOp>()
                : sim::SimLogicToBitsOp{};
        auto fact = facts.find(original);
        return toBits && toBits.getInput() == original && fact != facts.end() &&
               fact->second.domain == StateDomain::TwoState;
      };
      if (isKnownRoundTrip(compare.getLhs(), compare.getRhs()) ||
          isKnownRoundTrip(compare.getRhs(), compare.getLhs()))
        return done(equality);
    }
  }
  return done(std::nullopt);
}

bool isProvablyDeadSuccessor(Operation *terminator, unsigned successorIndex,
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
  const auto &bitAnalysis = summary.bitAnalysis;
  for (BlockArgument argument : entry.getArguments())
    if (hasUnknownPlane(argument.getType()) &&
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
    unsigned nodeCount =
        summary.blockArguments.size() + summary.operations.size();
    std::deque<unsigned> pending;
    DenseSet<unsigned> queued;
    auto enqueue = [&](unsigned node) {
      if (queued.insert(node).second)
        pending.push_back(node);
    };
    for (unsigned node = 0; node != nodeCount; ++node)
      enqueue(node);
    auto update = [&](Value value, StateDomainFact fact) {
      if (!updateFact(local.values, value, fact))
        return;
      auto found = summary.dependents.find(value);
      if (found != summary.dependents.end())
        for (unsigned dependent : found->second)
          enqueue(dependent);
    };
    while (!pending.empty()) {
      unsigned node = pending.front();
      pending.pop_front();
      queued.erase(node);
      if (node < summary.blockArguments.size()) {
        const BlockArgumentSummary &argument = summary.blockArguments[node];
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
        update(argument.argument, joined);
        continue;
      }
      Operation *operation =
          summary.operations[node - summary.blockArguments.size()];
      if (auto call = dyn_cast<sim::SimCallOp>(operation)) {
        auto callee = calleeIndex.find(operation);
        for (auto [index, result] : llvm::enumerate(call.getResults())) {
          if (!hasUnknownPlane(result.getType()))
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
          update(result, next);
        }
        continue;
      }
      StateDomainFact transferred =
          transferOperation(operation, local.values, summary.provenance,
                            summary.certificates, assumedKnownRoots);
      if (transferred.domain != StateDomain::TwoState &&
          operation->getNumResults() == 1)
        if (auto bits = bitAnalysis.get(operation->getResult(0));
            bits && bits->known.isAllOnes())
          transferred = twoState(StateDomainReason::PartialKnownBits);
      for (Value result : operation->getResults())
        if (shouldTrackResult(operation, result))
          update(result, transferred);
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
    if (hasUnknownPlane(argument.getType()) &&
        argument.getArgNumber() < formalBoundaries.size())
      updateFact(local.values, argument,
                 formalBoundaries[argument.getArgNumber()]);
  solveValues(&proofFacts);

  for (sim::SimReturnOp returnOp : summary.returns)
    for (auto [index, operand] : llvm::enumerate(returnOp.getOperands())) {
      if (!hasUnknownPlane(operand.getType()) || index >= local.results.size())
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
  case StateDomainReason::Select:
    return "select";
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
  case StateDomainReason::Aggregate:
    return "aggregate";
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
  case StateDomainReason::PartialKnownBits:
    return "partial-known-bits";
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

// Immutable structure for one analysis of an unchanged design. Inductive
// rejection changes assumed roots, never IR; share summaries and boundary
// seeds across those waves, but rebuild all propagated facts for every solve.
struct ValueFactProgram {
  explicit ValueFactProgram(sim::SimDesignOp design);

  sim::SimDesignOp design;
  SmallVector<sim::SimFuncOp> functions;
  SmallVector<FunctionSummary, 0> summaries;
  DenseMap<Operation *, unsigned> calleeIndex;
  SmallVector<SmallVector<unsigned>> callers;
  SmallVector<SmallVector<StateDomainFact>> formalBoundaries;
  SmallVector<SmallVector<StateDomainFact>> resultBoundaries;
};

ValueFactProgram::ValueFactProgram(sim::SimDesignOp design)
    : design(design),
      functions(design.getBody().front().getOps<sim::SimFuncOp>()) {
  llvm::sort(functions, [](sim::SimFuncOp lhs, sim::SimFuncOp rhs) {
    return lhs.getSymName() < rhs.getSymName();
  });

  analysis::ClassDispatchAnalysis classDispatch(design);
  analysis::HandleDataflowAnalysis descriptorProvenance(design);
  summaries.resize(functions.size());
  parallelFor(design.getContext(), 0, functions.size(), [&](size_t index) {
    summaries[index] =
        buildSummary(functions[index], classDispatch, descriptorProvenance);
  });

  DenseMap<Operation *, unsigned> functionIndex;
  for (auto [index, function] : llvm::enumerate(functions))
    functionIndex[function.getOperation()] = index;

  // Resolve symbol references serially using MLIR's symbol-table semantics,
  // then retain operation indices for read-only local propagation.
  SymbolTableCollection symbolTables;
  callers.resize(functions.size());
  for (auto [caller, summary] : llvm::enumerate(summaries)) {
    for (InvocationSummary &invocation : summary.invocations) {
      sim::SimFuncOp callee =
          symbolTables.lookupNearestSymbolFrom<sim::SimFuncOp>(
              invocation.operation, invocation.target);
      if (!callee)
        continue;
      auto found = functionIndex.find(callee.getOperation());
      if (found == functionIndex.end())
        continue;
      invocation.callee = found->second;
      if (!isa<sim::SimClassVirtualTaskCallOp>(invocation.operation))
        calleeIndex[invocation.operation] = found->second;
      if (!invocation.spawn &&
          std::find(callers[found->second].begin(),
                    callers[found->second].end(),
                    caller) == callers[found->second].end())
        callers[found->second].push_back(caller);
    }
  }

  formalBoundaries.resize(functions.size());
  resultBoundaries.resize(functions.size());
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
        if (hasUnknownPlane(type))
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
            hasUnknownPlane(operand.getType()))
          hasIncoming[*invocation.callee][index] = true;
    }

  // A formal is closed-world only if the function is private and every symbol
  // use is one of the direct calls or spawns indexed above.  Public / nested
  // symbols and opaque uses can introduce values from outside the analyzed
  // invocation graph.
  SmallVector<char> hasNonCallUse(functions.size(), false);
  // Scan each function body once. Asking for uses of every function symbol
  // from the enclosing design performs a complete design walk per symbol,
  // while asking from the design itself deliberately stops at nested symbol
  // boundaries and misses references attached to or nested in SimFuncOp.
  // The per-body scans remain linear in the total IR size.
  for (const FunctionSummary &summary : summaries) {
    sim::SimFuncOp scope = summary.function;
    std::optional<SymbolTable::UseRange> uses =
        SymbolTable::getSymbolUses(scope);
    if (!uses) {
      llvm::fill(hasNonCallUse, true);
      break;
    }
    for (const SymbolTable::SymbolUse &use : *uses) {
      Operation *user = use.getUser();
      SymbolRefAttr reference = use.getSymbolRef();
      // Functions in a simulation design are direct symbols of that design.
      // Resolve against the owning symbol table: lookup-nearest from an
      // attribute attached to the symbol operation itself can skip that
      // operation's sibling symbols and incorrectly classify opaque uses as
      // absent.
      // Resolve through the cached table. A bare lookupSymbol rebuilds nothing
      // but scans the design's symbols linearly on every use, which turns this
      // otherwise linear scan quadratic in the number of code units.
      auto function = symbolTables.lookupSymbolIn<sim::SimFuncOp>(
          design, reference.getRootReference());
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
      if (hasUnknownPlane(type) &&
          (functions[function].isExternal() || hasNonCallUse[function] ||
           SymbolTable::getSymbolVisibility(functions[function]) !=
               SymbolTable::Visibility::Private ||
           !hasIncoming[function][index]))
        boundaries[index] = mayFourState(StateDomainReason::FunctionEntry);
}

FailureOr<DenseMap<Value, StateDomainFact>>
computeValueFacts(const ValueFactProgram &program,
                  const RootSet &assumedKnownRoots) {
  sim::SimDesignOp design = program.design;
  const auto &functions = program.functions;
  const auto &summaries = program.summaries;
  const auto &calleeIndex = program.calleeIndex;
  const auto &callers = program.callers;
  auto formalBoundaries = program.formalBoundaries;
  auto resultBoundaries = program.resultBoundaries;
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
      sim::SimFuncOp current = functions[function];
      if (!hasUnknownPlane(current.getFunctionType().getResult(result)) ||
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
            !hasUnknownPlane(operand.getType()))
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

FailureOr<DenseMap<Value, StateDomainFact>>
computeValueFacts(sim::SimDesignOp design, const RootSet &assumedKnownRoots) {
  if (design.getBody().empty()) {
    design.emitOpError("cannot analyze a design with no body");
    return failure();
  }
  return computeValueFacts(ValueFactProgram(design), assumedKnownRoots);
}

std::optional<RootKey>
getConcreteRoot(Value handle, const analysis::HandleFacts &provenance) {
  auto found = provenance.find(handle);
  if (found == provenance.end() || !found->second.descriptor ||
      (found->second.resource != schedule::ComputeResourceKind::Storage &&
       found->second.resource != schedule::ComputeResourceKind::Net))
    return std::nullopt;
  return getRootKey(found->second.resource, *found->second.descriptor);
}

StateDomainFact getValueFact(const DenseMap<Value, StateDomainFact> &facts,
                             Value value) {
  auto found = facts.find(value);
  if (found != facts.end())
    return found->second;
  return hasUnknownPlane(value.getType())
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
  ValueFactProgram program(design);
  if (!proveInductiveRoots) {
    FailureOr<DenseMap<Value, StateDomainFact>> facts =
        computeValueFacts(program, RootSet{});
    if (failed(facts))
      return failure();
    DenseMap<Value, StateDomainFact> guarded = *facts;
    return StateDomainAnalysis(std::move(*facts), std::move(guarded), {});
  }
  FailureOr<StateDomainAnalysis> analysis = computeInductiveOnly(program);
  if (failed(analysis))
    return failure();
  // Keep the longstanding value facts unconditional. The inductive root set
  // is a separate guarded capability: consumers may assume it only after
  // checking the corresponding unknown planes at a kernel boundary.
  FailureOr<DenseMap<Value, StateDomainFact>> unconditional =
      computeValueFacts(program, RootSet{});
  if (failed(unconditional))
    return failure();
  analysis->facts = std::move(*unconditional);
  return analysis;
}

FailureOr<StateDomainAnalysis>
StateDomainAnalysis::computeInductiveOnly(sim::SimDesignOp design) {
  if (design.getBody().empty()) {
    design.emitOpError("cannot analyze a design with no body");
    return failure();
  }
  return computeInductiveOnly(ValueFactProgram(design));
}

FailureOr<StateDomainAnalysis>
StateDomainAnalysis::computeInductiveOnly(const ValueFactProgram &program) {
  sim::SimDesignOp design = program.design;
  RootSet candidates;
  DenseMap<uint64_t, unsigned> netDriverCounts;
  DenseMap<uint64_t, uint64_t> netWidths;
  DenseMap<uint64_t, bool> netHasFullDriver;
  DenseMap<uint64_t, SmallVector<uint64_t>> connectedNets;
  DenseSet<uint64_t> partialConnections;
  for (Operation &operation : design.getBody().front()) {
    if (auto storage = dyn_cast<sim::SimStorageDeclOp>(operation)) {
      if (isGuardableRoot(storage.getType()))
        candidates.insert(getRootKey(schedule::ComputeResourceKind::Storage,
                                     storage.getId()));
      continue;
    }
    if (auto net = dyn_cast<sim::SimNetDeclOp>(operation)) {
      if (hasUnknownPlane(net.getType())) {
        if (isGuardableRoot(net.getType()))
          candidates.insert(
              getRootKey(schedule::ComputeResourceKind::Net, net.getId()));
        if (std::optional<uint64_t> width =
                sim::getProvenanceSpan(net.getType()))
          netWidths[net.getId()] = *width;
        connectedNets[net.getId()];
        // LRM 6.6.7 and 28.16.2: custom resolution and charge decay are
        // additional value producers, absent from this driver-only proof.
        if (net.getNettypeAttr() ||
            net.getResolutionKind() == sim::NetResolutionKind::TriReg)
          partialConnections.insert(net.getId());
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
          width && low == 0 && drivenWidth == *width &&
          driver.getStrength0() != sim::Strength::HighZ &&
          driver.getStrength1() != sim::Strength::HighZ;
    }
  }
  // Full-width net connections form one resolved value.  A component with
  // exactly one full-width driver cannot create X from resolution and is a
  // valid guarded two-state root.  Multiple drivers can conflict, and partial
  // connections can leave Z bits, so the whole-root proof rejects those
  // components conservatively. High-Z drive strength can create Z from a
  // known payload (LRM 6.3.2), so neither polarity may have high-Z strength.
  // This retains ordinary one-driver port aliases
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
      candidates.erase(getRootKey(schedule::ComputeResourceKind::Net, member));
  }

  DenseMap<Value, StateDomainFact> facts;
  struct RootWrite {
    Value destination;
    Value value;
    std::optional<RootKey> copiedRoot;
    bool copyKnown = false;
    std::optional<RootKey> root;
    schedule::ComputeResourceKind resource =
        schedule::ComputeResourceKind::Unknown;
  };
  SmallVector<RootWrite> writes;
  llvm::SmallDenseSet<unsigned, 8> opaqueWriteResources;
  // Writes and provenance do not change during root rejection. Inventory
  // them once, including effects without an SSA payload, instead of walking
  // every function again for each wave of the guarded fixed point.
  for (const FunctionSummary &summary : program.summaries) {
    size_t firstWrite = writes.size();
    DenseSet<Operation *> closedInvocations, opaqueInvocations;
    for (const InvocationSummary &invocation : summary.invocations) {
      sim::SimFuncOp callee = invocation.callee
                                  ? program.functions[*invocation.callee]
                                  : sim::SimFuncOp{};
      if (callee && !callee.isExternal())
        closedInvocations.insert(invocation.operation);
      else
        opaqueInvocations.insert(invocation.operation);
    }
    sim::SimFuncOp function = summary.function;
    function.walk([&](Operation *operation) {
      if (auto copy = dyn_cast<sim::SimRefCopyOp>(operation)) {
        writes.push_back(
            {copy.getDestination(),
             {},
             getConcreteRoot(copy.getSource(), summary.provenance),
             !hasUnknownPlane(copy.getSource().getType().getElementType())});
        return;
      }
      if (auto pair = dyn_cast<sim::SimDriverDriveInertialPathStrengthPairOp>(
              operation)) {
        writes.push_back({pair.getLowDriver(), pair.getLowValue()});
        writes.push_back({pair.getHighDriver(), pair.getHighValue()});
        return;
      }
      if (auto pair =
              dyn_cast<sim::SimDriverDriveInertialStrengthPairOp>(operation)) {
        writes.push_back({pair.getLowDriver(), pair.getLowValue()});
        writes.push_back({pair.getHighDriver(), pair.getHighValue()});
        return;
      }
      Value destination, value;
      if (auto store = dyn_cast<sim::SimRefStoreOp>(operation)) {
        destination = store.getReference();
        value = store.getValue();
      } else if (auto nba = dyn_cast<sim::SimNBAEnqueueOp>(operation)) {
        destination = nba.getDestination();
        value = nba.getValue();
      } else if (auto store =
                     dyn_cast<sim::SimRefStoreInertialPathOp>(operation)) {
        destination = store.getReference();
        value = store.getValue();
      } else if (auto write = dyn_cast<sim::SimNetWriteOp>(operation)) {
        destination = write.getNet();
        value = write.getValue();
      } else if (auto force = dyn_cast<sim::SimOverrideOp>(operation)) {
        destination = force.getTarget();
        value = force.getValue();
      } else if (auto force = dyn_cast<sim::SimDynamicOverrideOp>(operation)) {
        destination = force.getTarget();
        value = force.getValue();
      } else if (auto drive = dyn_cast<sim::SimDriverDriveOp>(operation)) {
        destination = drive.getDriver();
        value = drive.getValue();
      } else if (auto drive =
                     dyn_cast<sim::SimDriverDriveInertialOp>(operation)) {
        destination = drive.getDriver();
        value = drive.getValue();
      } else if (auto drive =
                     dyn_cast<sim::SimDriverDriveInertialPathOp>(operation)) {
        destination = drive.getDriver();
        value = drive.getValue();
      } else if (auto drive =
                     dyn_cast<sim::SimDriverDriveDelayedNetOp>(operation)) {
        destination = drive.getDriver();
        value = drive.getValue();
      } else if (auto drive =
                     dyn_cast<sim::SimDriverDriveChangedOp>(operation)) {
        destination = drive.getDriver();
        value = drive.getValue();
      }
      if (destination) {
        writes.push_back({destination, value});
        return;
      }
      // Closed-world callees are inventoried separately. Unknown invocations
      // and unrecognized writers must not silently certify persistent state.
      if (closedInvocations.contains(operation) &&
          !opaqueInvocations.contains(operation))
        return;
      auto effects = dyn_cast<MemoryEffectOpInterface>(operation);
      if (!effects)
        return;
      SmallVector<MemoryEffects::EffectInstance> instances;
      effects.getEffects(instances);
      for (const auto &effect : instances) {
        if (!isa<MemoryEffects::Write>(effect.getEffect()))
          continue;
        auto resource = schedule::ComputeResourceKind::Unknown;
        if (isa<sim::StorageResource>(effect.getResource()))
          resource = schedule::ComputeResourceKind::Storage;
        else if (isa<sim::NetResource>(effect.getResource()))
          resource = schedule::ComputeResourceKind::Net;
        else
          continue;
        if (Value target = effect.getValue())
          writes.push_back({target, {}});
        else
          opaqueWriteResources.insert(static_cast<unsigned>(resource));
      }
    });
    for (RootWrite &write : llvm::drop_begin(writes, firstWrite)) {
      write.root = getConcreteRoot(write.destination, summary.provenance);
      auto found = summary.provenance.find(write.destination);
      if (found != summary.provenance.end())
        write.resource = found->second.resource;
    }
  }
  while (true) {
    FailureOr<DenseMap<Value, StateDomainFact>> solved =
        computeValueFacts(program, candidates);
    if (failed(solved))
      return failure();
    facts = std::move(*solved);
    RootSet rejected;
    bool rejectAllResources = false;
    llvm::SmallDenseSet<unsigned, 8> rejectedResources;
    rejectedResources.insert(opaqueWriteResources.begin(),
                             opaqueWriteResources.end());
    for (const RootWrite &write : writes) {
      if ((write.value &&
           getValueFact(facts, write.value).domain == StateDomain::TwoState) ||
          write.copyKnown ||
          (write.copiedRoot && candidates.contains(*write.copiedRoot)))
        continue;
      if (write.root) {
        if (candidates.contains(*write.root))
          rejected.insert(*write.root);
      } else if (write.resource == schedule::ComputeResourceKind::Unknown) {
        rejectAllResources = true;
      } else {
        rejectedResources.insert(static_cast<unsigned>(write.resource));
      }
    }
    if (rejectAllResources || !rejectedResources.empty())
      for (RootKey root : candidates)
        if (rejectAllResources || rejectedResources.contains(root.first))
          rejected.insert(root);
    if (rejected.empty())
      break;
    for (RootKey root : rejected)
      candidates.erase(root);
  }

  DenseMap<Value, StateDomainFact> guardedFacts = std::move(facts);

  SmallVector<InductiveStateRoot> inductiveRoots;
  inductiveRoots.reserve(candidates.size());
  for (RootKey root : candidates)
    inductiveRoots.push_back(
        {static_cast<schedule::ComputeResourceKind>(root.first), root.second});
  llvm::sort(inductiveRoots, [](const auto &lhs, const auto &rhs) {
    return std::tie(lhs.resource, lhs.descriptor) <
           std::tie(rhs.resource, rhs.descriptor);
  });
  return StateDomainAnalysis(DenseMap<Value, StateDomainFact>{},
                             std::move(guardedFacts),
                             std::move(inductiveRoots));
}

FailureOr<StateDomainAnalysis>
StateDomainAnalysis::computeAssumingKnownState(sim::SimDesignOp design) {
  if (design.getBody().empty()) {
    design.emitOpError("cannot analyze a design with no body");
    return failure();
  }
  RootSet assumedKnown = collectGuardableRoots(design);
  ValueFactProgram program(design);
  FailureOr<DenseMap<Value, StateDomainFact>> guarded =
      computeValueFacts(program, assumedKnown);
  if (failed(guarded))
    return failure();
  FailureOr<DenseMap<Value, StateDomainFact>> unconditional =
      computeValueFacts(program, RootSet{});
  if (failed(unconditional))
    return failure();
  SmallVector<InductiveStateRoot> roots;
  roots.reserve(assumedKnown.size());
  for (RootKey root : assumedKnown)
    roots.push_back(
        {static_cast<schedule::ComputeResourceKind>(root.first), root.second});
  llvm::sort(roots, [](const auto &lhs, const auto &rhs) {
    return std::tie(lhs.resource, lhs.descriptor) <
           std::tie(rhs.resource, rhs.descriptor);
  });
  return StateDomainAnalysis(std::move(*unconditional), std::move(*guarded),
                             std::move(roots));
}

FailureOr<std::pair<StateDomainAnalysis, StateDomainAnalysis>>
StateDomainAnalysis::computeForSpecialization(sim::SimDesignOp design,
                                              bool proveInductiveRoots) {
  if (design.getBody().empty()) {
    design.emitOpError("cannot analyze a design with no body");
    return failure();
  }
  ValueFactProgram program(design);
  auto unconditional = computeValueFacts(program, RootSet{});
  if (failed(unconditional))
    return failure();
  FailureOr<StateDomainAnalysis> inductive =
      proveInductiveRoots
          ? computeInductiveOnly(program)
          : FailureOr<StateDomainAnalysis>(StateDomainAnalysis(
                DenseMap<Value, StateDomainFact>{}, *unconditional, {}));
  if (failed(inductive))
    return failure();
  inductive->facts = std::move(*unconditional);
  RootSet assumedKnown = collectGuardableRoots(design);
  auto guarded = computeValueFacts(program, assumedKnown);
  if (failed(guarded))
    return failure();
  SmallVector<InductiveStateRoot> roots;
  roots.reserve(assumedKnown.size());
  for (RootKey root : assumedKnown)
    roots.push_back(
        {static_cast<schedule::ComputeResourceKind>(root.first), root.second});
  llvm::sort(roots, [](const auto &lhs, const auto &rhs) {
    return std::tie(lhs.resource, lhs.descriptor) <
           std::tie(rhs.resource, rhs.descriptor);
  });
  return std::make_pair(std::move(*inductive),
                        StateDomainAnalysis(DenseMap<Value, StateDomainFact>{},
                                            std::move(*guarded),
                                            std::move(roots)));
}

StateDomainFact StateDomainAnalysis::get(Value value) const {
  auto found = facts.find(value);
  if (found != facts.end() && found->second.domain != StateDomain::Bottom)
    return found->second;
  if (!hasUnknownPlane(value.getType()))
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
  if (!hasUnknownPlane(value.getType()))
    return twoState(StateDomainReason::NonLogic);
  return mayFourState(StateDomainReason::Unresolved);
}

bool StateDomainAnalysis::isTwoStateWithInductiveRoots(Value value) const {
  return getWithInductiveRoots(value).domain == StateDomain::TwoState;
}

bool StateDomainAnalysis::isInductivelyTwoState(
    schedule::ComputeResourceKind resource, uint64_t descriptor) const {
  return llvm::is_contained(inductiveRoots,
                            InductiveStateRoot{resource, descriptor});
}

} // namespace obelisk
