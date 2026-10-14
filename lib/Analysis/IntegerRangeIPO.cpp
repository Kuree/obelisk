#include "obelisk/Analysis/IntegerRangeIPO.h"
#include "mlir/Analysis/DataFlow/ConstantPropagationAnalysis.h"
#include "mlir/Analysis/DataFlow/DeadCodeAnalysis.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/IR/Matchers.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/IR/Threading.h"
#include "obelisk/Analysis/ScopedIntegerRangeAnalysis.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/ConstantRange.h"

using namespace mlir;
namespace obelisk::analysis {
std::optional<ConstantIntRanges>
decodeIntegerRange(Attribute attr, std::optional<unsigned> expectedWidth) {
  auto range = dyn_cast_or_null<sim::IntegerRangeAttr>(attr);
  if (!range)
    return std::nullopt;
  const APInt &umin = range.getUmin().getValue();
  const APInt &umax = range.getUmax().getValue();
  const APInt &smin = range.getSmin().getValue();
  const APInt &smax = range.getSmax().getValue();
  unsigned width = umin.getBitWidth();
  if (!width || (expectedWidth && width != *expectedWidth) ||
      umax.getBitWidth() != width || smin.getBitWidth() != width ||
      smax.getBitWidth() != width || umin.ugt(umax) || smin.sgt(smax))
    return std::nullopt;
  auto unsignedRange = umin.isZero() && umax.isMaxValue()
                           ? llvm::ConstantRange(width, true)
                           : llvm::ConstantRange(umin, umax + 1);
  auto signedRange = smin.isMinSignedValue() && smax.isMaxSignedValue()
                         ? llvm::ConstantRange(width, true)
                         : llvm::ConstantRange(smin, smax + 1);
  if (unsignedRange.intersectWith(signedRange).isEmptySet())
    return std::nullopt;
  return ConstantIntRanges(umin, umax, smin, smax);
}
std::optional<ConstantIntRanges> getIntegerResultRange(Value value) {
  auto result = dyn_cast<OpResult>(value);
  if (!result || !isa<IntegerType, sim::LogicType>(value.getType()))
    return std::nullopt;
  auto summaries = sim::get<sim::ResultRanges>(result.getOwner());
  return summaries && result.getResultNumber() < summaries.size()
             ? decodeIntegerRange(summaries[result.getResultNumber()],
                                  sim::getPackedWidth(value.getType()))
             : std::nullopt;
}
namespace {
bool validateExpression(sim::IntegerExpressionAttr expression, Operation *op,
                        unsigned &budget, unsigned depth = 0) {
  if (!expression || !budget || depth > 32)
    return false;
  --budget;
  unsigned width = expression.getWidth();
  if (!width || width > 128)
    return false;
  switch (expression.getKind()) {
  case sim::IntegerExpressionKind::Constant: {
    IntegerAttr bits = expression.getValue();
    return bits && isa<IntegerType>(bits.getType()) &&
           bits.getValue().getBitWidth() == width;
  }
  case sim::IntegerExpressionKind::Argument: {
    std::optional<uint32_t> index = expression.getIndex();
    if (!index || *index >= op->getNumOperands())
      return false;
    Type type = op->getOperand(*index).getType();
    return isa<IntegerType, sim::LogicType>(type) &&
           sim::getPackedWidth(type) == width;
  }
  default:
    break;
  }
  bool unary = expression.getKind() == sim::IntegerExpressionKind::Identity ||
               expression.getKind() == sim::IntegerExpressionKind::SExt ||
               expression.getKind() == sim::IntegerExpressionKind::ZExt ||
               expression.getKind() == sim::IntegerExpressionKind::Trunc;
  ArrayAttr operands = expression.getOperands();
  if (!operands || operands.size() != (unary ? 1u : 2u))
    return false;
  for (Attribute operand : operands) {
    auto child = dyn_cast<sim::IntegerExpressionAttr>(operand);
    if (!validateExpression(child, op, budget, depth + 1))
      return false;
    if ((!unary ||
         expression.getKind() == sim::IntegerExpressionKind::Identity) &&
        child.getWidth() != width)
      return false;
  }
  return true;
}
} // namespace
Attribute getIntegerResultExpression(Value value) {
  auto result = dyn_cast<OpResult>(value);
  if (!result)
    return {};
  ArrayAttr expressions = sim::get<sim::ResultExprs>(result.getOwner());
  if (!expressions || result.getResultNumber() >= expressions.size())
    return {};
  auto expression = dyn_cast<sim::IntegerExpressionAttr>(
      expressions[result.getResultNumber()]);
  unsigned budget = 64;
  if (!validateExpression(expression, result.getOwner(), budget) ||
      !isa<IntegerType, sim::LogicType>(value.getType()) ||
      sim::getPackedWidth(value.getType()) != expression.getWidth())
    return {};
  return expression;
}
namespace {
Attribute encodeRange(MLIRContext *context,
                      std::optional<ConstantIntRanges> range) {
  if (!range)
    return UnitAttr::get(context);
  auto type = IntegerType::get(context, range->umin().getBitWidth());
  return sim::IntegerRangeAttr::get(context,
                                    IntegerAttr::get(type, range->umin()),
                                    IntegerAttr::get(type, range->umax()),
                                    IntegerAttr::get(type, range->smin()),
                                    IntegerAttr::get(type, range->smax()));
}
// Symbolic return summaries keep parameter relationships that a range hull
// would lose. Their algebra is used only when the actual logic inputs are
// known; an X/Z literal or a memory-dependent producer has no such summary.
Attribute summarizeExpression(Value value, sim::SimFuncOp function,
                              unsigned &budget) {
  if (!budget)
    return {};
  --budget;
  auto width = sim::getPackedWidth(value.getType());
  if (!width || !*width || *width > 128 ||
      !isa<IntegerType, sim::LogicType>(value.getType()))
    return {};
  auto *context = function.getContext();
  Builder builder(context);
  auto node = [&](sim::IntegerExpressionKind kind, IntegerAttr value,
                  std::optional<uint32_t> index, ArrayAttr operands) {
    return sim::IntegerExpressionAttr::get(context, kind, *width, value, index,
                                           operands);
  };
  APInt bits;
  if (matchPattern(value, m_ConstantInt(&bits)))
    return node(sim::IntegerExpressionKind::Constant,
                builder.getIntegerAttr(builder.getIntegerType(*width), bits),
                std::nullopt, ArrayAttr{});
  if (auto constant = value.getDefiningOp<sim::SimLogicConstantOp>()) {
    if (!constant.getUnknown().isZero())
      return {};
    return node(sim::IntegerExpressionKind::Constant,
                builder.getIntegerAttr(builder.getIntegerType(*width),
                                       constant.getValue()),
                std::nullopt, ArrayAttr{});
  }
  if (auto argument = dyn_cast<BlockArgument>(value)) {
    if (argument.getOwner() != &function.getBody().front())
      return {};
    return node(sim::IntegerExpressionKind::Argument, IntegerAttr{},
                argument.getArgNumber(), ArrayAttr{});
  }
  auto *op = value.getDefiningOp();
  std::optional<sim::IntegerExpressionKind> kind;
  if (isa<arith::AddIOp>(op))
    kind = sim::IntegerExpressionKind::Add;
  else if (isa<arith::SubIOp>(op))
    kind = sim::IntegerExpressionKind::Sub;
  else if (isa<arith::MulIOp>(op))
    kind = sim::IntegerExpressionKind::Mul;
  else if (isa<arith::AndIOp>(op))
    kind = sim::IntegerExpressionKind::And;
  else if (isa<arith::OrIOp>(op))
    kind = sim::IntegerExpressionKind::Or;
  else if (isa<arith::XOrIOp>(op))
    kind = sim::IntegerExpressionKind::Xor;
  else if (isa<arith::ExtSIOp>(op))
    kind = sim::IntegerExpressionKind::SExt;
  else if (isa<arith::ExtUIOp>(op))
    kind = sim::IntegerExpressionKind::ZExt;
  else if (isa<arith::TruncIOp>(op))
    kind = sim::IntegerExpressionKind::Trunc;
  else if (isa<sim::SimLogicFromBitsOp, sim::SimLogicToBitsOp>(op))
    kind = sim::IntegerExpressionKind::Identity;
  else if (auto resize = dyn_cast<sim::SimLogicResizeOp>(op))
    kind = resize.getIsSigned() ? sim::IntegerExpressionKind::SExt
                                : sim::IntegerExpressionKind::ZExt;
  else if (auto extract = dyn_cast<sim::SimLogicExtractOp>(op);
           extract && extract.getLowBit() == 0)
    // A truncation is canonically the low slice.
    kind = sim::IntegerExpressionKind::Trunc;
  else if (auto binary = dyn_cast<sim::SimLogicBinaryOp>(op)) {
    switch (binary.getKind()) {
    case sim::BinaryKind::Add:
      kind = sim::IntegerExpressionKind::Add;
      break;
    case sim::BinaryKind::Sub:
      kind = sim::IntegerExpressionKind::Sub;
      break;
    case sim::BinaryKind::Mul:
      kind = sim::IntegerExpressionKind::Mul;
      break;
    case sim::BinaryKind::And:
      kind = sim::IntegerExpressionKind::And;
      break;
    case sim::BinaryKind::Or:
      kind = sim::IntegerExpressionKind::Or;
      break;
    case sim::BinaryKind::Xor:
      kind = sim::IntegerExpressionKind::Xor;
      break;
    default:
      return {};
    }
  }
  if (!kind)
    return {};
  SmallVector<Attribute> operands;
  for (Value operand : op->getOperands()) {
    auto expression = summarizeExpression(operand, function, budget);
    if (!expression)
      return {};
    operands.push_back(expression);
  }
  return node(*kind, IntegerAttr{}, std::nullopt,
              builder.getArrayAttr(operands));
}
struct FunctionFacts {
  bool valid = true;
  SmallVector<std::optional<ConstantIntRanges>> returns;
  SmallVector<
      std::pair<sim::SimCallOp, SmallVector<std::optional<ConstantIntRanges>>>>
      calls;
};
} // namespace

void prepareIntegerRangeIPO(Operation *scope) {
  SmallVector<sim::SimFuncOp> functions;
  SmallVector<sim::SimCallOp> calls;
  scope->walk([&](sim::SimCallOp call) { calls.push_back(call); });
  // Recompute from unconstrained entries even if a rewrite removed every
  // call. Certificates from the previous graph must never survive that case.
  scope->walk([&](Operation *op) {
    sim::remove<sim::ResultRanges>(op);
    sim::remove<sim::ResultExprs>(op);
    if (auto function = dyn_cast<sim::SimFuncOp>(op))
      for (unsigned i = 0; i < function.getNumArguments(); ++i)
        function.removeArgAttr(i, sim::IntegerRange::getNameStr());
  });
  if (calls.empty())
    return;
  llvm::DenseMap<Operation *, unsigned> indices;
  scope->walk([&](sim::SimFuncOp function) {
    if (function.isExternal() || function.getBody().empty())
      return;
    indices[function] = functions.size();
    functions.push_back(function);
    for (unsigned i = 0; i < function.getNumArguments(); ++i)
      function.removeArgAttr(i, sim::IntegerRange::getNameStr());
    sim::remove<sim::ResultRanges>(function);
    SmallVector<Attribute> expressions(function.getNumResults(),
                                       UnitAttr::get(scope->getContext()));
    bool first = true;
    function.walk([&](sim::SimReturnOp ret) {
      for (auto [i, value] : llvm::enumerate(ret.getOperands())) {
        unsigned budget = 64;
        Attribute expression = summarizeExpression(value, function, budget);
        if (first)
          expressions[i] =
              expression ? expression : UnitAttr::get(scope->getContext());
        else if (expressions[i] != expression)
          expressions[i] = UnitAttr::get(scope->getContext());
      }
      first = false;
    });
    sim::set<sim::ResultExprs>(
        function, ArrayAttr::get(scope->getContext(), expressions));
  });
  SymbolTableCollection symbols;
  llvm::DenseMap<Operation *, unsigned> targets;
  llvm::DenseSet<Operation *> closed;
  llvm::DenseSet<Operation *> parents;
  for (auto function : functions) {
    parents.insert(function->getParentOp());
    // Runtime activations have implicit callers that symbol-use scanning
    // cannot enumerate, regardless of their symbol visibility.
    if (function.isPrivate() &&
        (function.getEntryKind() == sim::EntryKind::Function ||
         function.getEntryKind() == sim::EntryKind::Task))
      closed.insert(function);
  }
  // Scan each symbol table once. Querying all uses separately for thousands
  // of private functions would repeatedly walk the entire design.
  for (Operation *parent : parents) {
    auto uses = SymbolTable::getSymbolUses(parent);
    SmallVector<SymbolTable::SymbolUse> allUses;
    bool complete = static_cast<bool>(uses);
    if (uses)
      llvm::append_range(allUses, *uses);
    // The operation overload skips a symbol table's body. Walk its regions
    // explicitly to include both calls and non-call escapes in that scope.
    for (Region &region : parent->getRegions()) {
      auto regionUses = SymbolTable::getSymbolUses(&region);
      complete &= static_cast<bool>(regionUses);
      if (regionUses)
        llvm::append_range(allUses, *regionUses);
    }
    if (!complete) {
      for (auto function : functions)
        if (function->getParentOp() == parent)
          closed.erase(function);
      continue;
    }
    for (const auto &use : allUses) {
      auto target = symbols.lookupNearestSymbolFrom<sim::SimFuncOp>(
          use.getUser(), use.getSymbolRef());
      if (!target || !closed.contains(target))
        continue;
      auto call = dyn_cast<sim::SimCallOp>(use.getUser());
      if (!call || call.getCalleeAttr() != use.getSymbolRef())
        closed.erase(target);
    }
  }
  for (auto call : calls) {
    sim::remove<sim::ResultRanges>(call);
    sim::remove<sim::ResultExprs>(call);
    auto target = symbols.lookupNearestSymbolFrom<sim::SimFuncOp>(
        call, call.getCalleeAttr());
    auto found = target ? indices.find(target) : indices.end();
    if (found == indices.end())
      continue;
    bool scalarReturns = llvm::any_of(target.getResultTypes(), [](Type type) {
      return isa<IntegerType, sim::LogicType>(type);
    });
    bool scalarArguments =
        closed.contains(target) &&
        llvm::any_of(target.getArgumentTypes(), [](Type type) {
          return isa<IntegerType, sim::LogicType>(type);
        });
    if (!scalarReturns && !scalarArguments)
      continue;
    targets[call] = found->second;
    sim::set<sim::ResultExprs>(call, sim::get<sim::ResultExprs>(target));
  }
  // Every round starts from sound may facts. Refinement is bounded rather
  // than waiting for a potentially enormous recursive range chain to settle.
  for (unsigned round = 0; round < 8; ++round) {
    std::vector<FunctionFacts> facts(functions.size());
    parallelForEach(
        scope->getContext(), llvm::seq<unsigned>(0, functions.size()),
        [&](unsigned index) {
          auto function = functions[index];
          auto &result = facts[index];
          result.returns.resize(function.getNumResults());
          bool needed = llvm::any_of(function.getResultTypes(), [](Type type) {
            return isa<IntegerType, sim::LogicType>(type);
          });
          function.walk(
              [&](sim::SimCallOp call) { needed |= targets.contains(call); });
          if (!needed)
            return;
          DataFlowSolver solver(DataFlowConfig().setInterprocedural(false));
          solver.load<dataflow::DeadCodeAnalysis>();
          solver.load<dataflow::SparseConstantPropagation>();
          auto *ranges = solver.load<ScopedIntegerRangeAnalysis>(function);
          if (failed(solver.initializeAndRun(function))) {
            result.valid = false;
            return;
          }
          bool first = true;
          function.walk([&](Operation *op) {
            auto *state = solver.lookupState<ScopedRangeLattice>(
                solver.getProgramPointBefore(op));
            if (!state || !state->reachable)
              return;
            if (auto ret = dyn_cast<sim::SimReturnOp>(op)) {
              for (auto [i, value] : llvm::enumerate(ret.getOperands())) {
                auto range = ranges->getRange(value, *state);
                if (first)
                  result.returns[i] = range;
                else if (result.returns[i] && range)
                  result.returns[i] = result.returns[i]->rangeUnion(*range);
                else
                  result.returns[i].reset();
              }
              first = false;
            } else if (auto call = dyn_cast<sim::SimCallOp>(op);
                       call && targets.contains(call)) {
              SmallVector<std::optional<ConstantIntRanges>> arguments;
              for (Value value : call.getOperands())
                arguments.push_back(ranges->getRange(value, *state));
              result.calls.emplace_back(call, std::move(arguments));
            }
          });
        });
    if (llvm::any_of(facts,
                     [](const FunctionFacts &fact) { return !fact.valid; }))
      return;
    SmallVector<SmallVector<std::optional<ConstantIntRanges>>> arguments(
        functions.size());
    SmallVector<bool> visited(functions.size(), false);
    for (const auto &result : facts)
      for (const auto &[call, input] : result.calls) {
        unsigned target = targets.lookup(call);
        if (!visited[target]) {
          arguments[target] = input;
          visited[target] = true;
        } else
          for (unsigned i = 0; i < input.size(); ++i) {
            if (arguments[target][i] && input[i])
              arguments[target][i] =
                  arguments[target][i]->rangeUnion(*input[i]);
            else
              arguments[target][i].reset();
          }
      }
    bool changed = false;
    for (unsigned i = 0; i < functions.size(); ++i) {
      auto function = functions[i];
      SmallVector<Attribute> returns;
      for (auto range : facts[i].returns)
        returns.push_back(encodeRange(scope->getContext(), range));
      auto attribute = ArrayAttr::get(scope->getContext(), returns);
      changed |= sim::get<sim::ResultRanges>(function) != attribute;
      sim::set<sim::ResultRanges>(function, attribute);
      if (closed.contains(function) && visited[i])
        for (unsigned j = 0; j < arguments[i].size(); ++j) {
          // An argument without a range carries no certificate.
          Attribute range = encodeRange(scope->getContext(), arguments[i][j]);
          if (isa<UnitAttr>(range))
            range = {};
          changed |=
              function.getArgAttr(j, sim::IntegerRange::getNameStr()) != range;
          function.setArgAttr(j, sim::IntegerRange::getNameStr(), range);
        }
    }
    for (auto call : calls)
      if (auto found = targets.find(call); found != targets.end())
        sim::set<sim::ResultRanges>(
            call, sim::get<sim::ResultRanges>(functions[found->second]));
    if (!changed)
      break;
  }
}
} // namespace obelisk::analysis
