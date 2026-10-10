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
  auto dictionary = dyn_cast_or_null<DictionaryAttr>(attr);
  if (!dictionary)
    return std::nullopt;
  auto umin = dictionary.getAs<IntegerAttr>("umin");
  auto umax = dictionary.getAs<IntegerAttr>("umax");
  auto smin = dictionary.getAs<IntegerAttr>("smin");
  auto smax = dictionary.getAs<IntegerAttr>("smax");
  if (!umin || !umax || !smin || !smax || umin.getType() != umax.getType() ||
      umin.getType() != smin.getType() || umin.getType() != smax.getType())
    return std::nullopt;
  auto type = dyn_cast<IntegerType>(umin.getType());
  if (!type || !type.getWidth() ||
      (expectedWidth && type.getWidth() != *expectedWidth) ||
      umin.getValue().ugt(umax.getValue()) ||
      smin.getValue().sgt(smax.getValue()))
    return std::nullopt;
  auto unsignedRange =
      umin.getValue().isZero() && umax.getValue().isMaxValue()
          ? llvm::ConstantRange(type.getWidth(), true)
          : llvm::ConstantRange(umin.getValue(), umax.getValue() + 1);
  auto signedRange =
      smin.getValue().isMinSignedValue() && smax.getValue().isMaxSignedValue()
          ? llvm::ConstantRange(type.getWidth(), true)
          : llvm::ConstantRange(smin.getValue(), smax.getValue() + 1);
  if (unsignedRange.intersectWith(signedRange).isEmptySet())
    return std::nullopt;
  return ConstantIntRanges(umin.getValue(), umax.getValue(), smin.getValue(),
                           smax.getValue());
}
std::optional<ConstantIntRanges> getIntegerResultRange(Value value) {
  auto result = dyn_cast<OpResult>(value);
  if (!result || !isa<IntegerType, sim::LogicType>(value.getType()))
    return std::nullopt;
  auto summaries =
      result.getOwner()->getAttrOfType<ArrayAttr>(integerResultRangesAttr);
  return summaries && result.getResultNumber() < summaries.size()
             ? decodeIntegerRange(summaries[result.getResultNumber()],
                                  sim::getPackedWidth(value.getType()))
             : std::nullopt;
}
namespace {
bool validateExpression(DictionaryAttr expression, Operation *op,
                        unsigned &budget, unsigned depth = 0) {
  if (!expression || !budget || depth > 32)
    return false;
  --budget;
  auto kind = expression.getAs<StringAttr>("kind");
  auto widthAttr = expression.getAs<IntegerAttr>("width");
  if (!kind || !widthAttr || widthAttr.getValue().isNegative() ||
      widthAttr.getValue().getActiveBits() > 8)
    return false;
  unsigned width = widthAttr.getInt();
  if (!width || width > 128)
    return false;
  if (kind.getValue() == "constant") {
    auto bits = expression.getAs<IntegerAttr>("value");
    return bits && isa<IntegerType>(bits.getType()) &&
           bits.getValue().getBitWidth() == width;
  }
  if (kind.getValue() == "argument") {
    auto index = expression.getAs<IntegerAttr>("index");
    if (!index || index.getValue().isNegative() ||
        index.getValue().getActiveBits() > 32 ||
        index.getInt() >= op->getNumOperands())
      return false;
    Type type = op->getOperand(index.getInt()).getType();
    return isa<IntegerType, sim::LogicType>(type) &&
           sim::getPackedWidth(type) == width;
  }
  bool unary = kind.getValue() == "identity" || kind.getValue() == "sext" ||
               kind.getValue() == "zext" || kind.getValue() == "trunc";
  bool binary = kind.getValue() == "add" || kind.getValue() == "sub" ||
                kind.getValue() == "mul" || kind.getValue() == "and" ||
                kind.getValue() == "or" || kind.getValue() == "xor";
  auto operands = expression.getAs<ArrayAttr>("operands");
  if ((!unary && !binary) || !operands || operands.size() != (unary ? 1u : 2u))
    return false;
  for (Attribute operand : operands) {
    auto child = dyn_cast<DictionaryAttr>(operand);
    if (!validateExpression(child, op, budget, depth + 1))
      return false;
    if ((binary || kind.getValue() == "identity") &&
        child.getAs<IntegerAttr>("width").getInt() != width)
      return false;
  }
  return true;
}
} // namespace
Attribute getIntegerResultExpression(Value value) {
  auto result = dyn_cast<OpResult>(value);
  if (!result)
    return {};
  auto expressions =
      result.getOwner()->getAttrOfType<ArrayAttr>(integerResultExprsAttr);
  if (!expressions || result.getResultNumber() >= expressions.size())
    return {};
  auto expression =
      dyn_cast<DictionaryAttr>(expressions[result.getResultNumber()]);
  unsigned budget = 64;
  if (!validateExpression(expression, result.getOwner(), budget) ||
      !isa<IntegerType, sim::LogicType>(value.getType()) ||
      sim::getPackedWidth(value.getType()) !=
          expression.getAs<IntegerAttr>("width").getInt())
    return {};
  return expression;
}
namespace {
Attribute encodeRange(MLIRContext *context,
                      std::optional<ConstantIntRanges> range) {
  if (!range)
    return UnitAttr::get(context);
  auto type = IntegerType::get(context, range->umin().getBitWidth());
  return DictionaryAttr::get(context,
                             {{StringAttr::get(context, "umin"),
                               IntegerAttr::get(type, range->umin())},
                              {StringAttr::get(context, "umax"),
                               IntegerAttr::get(type, range->umax())},
                              {StringAttr::get(context, "smin"),
                               IntegerAttr::get(type, range->smin())},
                              {StringAttr::get(context, "smax"),
                               IntegerAttr::get(type, range->smax())}});
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
  NamedAttrList fields;
  fields.set("width", builder.getI32IntegerAttr(*width));
  APInt bits;
  if (matchPattern(value, m_ConstantInt(&bits))) {
    fields.set("kind", builder.getStringAttr("constant"));
    fields.set("value",
               builder.getIntegerAttr(builder.getIntegerType(*width), bits));
    return fields.getDictionary(context);
  }
  if (auto constant = value.getDefiningOp<sim::SimLogicConstantOp>()) {
    if (!constant.getUnknown().isZero())
      return {};
    fields.set("kind", builder.getStringAttr("constant"));
    fields.set("value", builder.getIntegerAttr(builder.getIntegerType(*width),
                                               constant.getValue()));
    return fields.getDictionary(context);
  }
  if (auto argument = dyn_cast<BlockArgument>(value)) {
    if (argument.getOwner() != &function.getBody().front())
      return {};
    fields.set("kind", builder.getStringAttr("argument"));
    fields.set("index", builder.getI32IntegerAttr(argument.getArgNumber()));
    return fields.getDictionary(context);
  }
  auto *op = value.getDefiningOp();
  StringRef kind;
  if (isa<arith::AddIOp>(op))
    kind = "add";
  else if (isa<arith::SubIOp>(op))
    kind = "sub";
  else if (isa<arith::MulIOp>(op))
    kind = "mul";
  else if (isa<arith::AndIOp>(op))
    kind = "and";
  else if (isa<arith::OrIOp>(op))
    kind = "or";
  else if (isa<arith::XOrIOp>(op))
    kind = "xor";
  else if (isa<arith::ExtSIOp>(op))
    kind = "sext";
  else if (isa<arith::ExtUIOp>(op))
    kind = "zext";
  else if (isa<arith::TruncIOp>(op))
    kind = "trunc";
  else if (isa<sim::SimLogicFromBitsOp, sim::SimLogicToBitsOp>(op))
    kind = "identity";
  else if (auto resize = dyn_cast<sim::SimLogicResizeOp>(op))
    kind = resize.getIsSigned() ? "sext" : "zext";
  else if (auto binary = dyn_cast<sim::SimLogicBinaryOp>(op)) {
    switch (binary.getKind()) {
    case sim::BinaryKind::Add:
      kind = "add";
      break;
    case sim::BinaryKind::Sub:
      kind = "sub";
      break;
    case sim::BinaryKind::Mul:
      kind = "mul";
      break;
    case sim::BinaryKind::And:
      kind = "and";
      break;
    case sim::BinaryKind::Or:
      kind = "or";
      break;
    case sim::BinaryKind::Xor:
      kind = "xor";
      break;
    default:
      return {};
    }
  }
  if (kind.empty())
    return {};
  SmallVector<Attribute> operands;
  for (Value operand : op->getOperands()) {
    auto expression = summarizeExpression(operand, function, budget);
    if (!expression)
      return {};
    operands.push_back(expression);
  }
  fields.set("kind", builder.getStringAttr(kind));
  fields.set("operands", builder.getArrayAttr(operands));
  return fields.getDictionary(context);
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
    op->removeAttr(integerResultRangesAttr);
    op->removeAttr(integerResultExprsAttr);
    if (auto function = dyn_cast<sim::SimFuncOp>(op))
      for (unsigned i = 0; i < function.getNumArguments(); ++i)
        function.removeArgAttr(i, integerRangeAttr);
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
      function.removeArgAttr(i, integerRangeAttr);
    function->removeAttr(integerResultRangesAttr);
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
    function->setAttr(integerResultExprsAttr,
                      ArrayAttr::get(scope->getContext(), expressions));
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
    call->removeAttr(integerResultRangesAttr);
    call->removeAttr(integerResultExprsAttr);
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
    call->setAttr(integerResultExprsAttr,
                  target->getAttr(integerResultExprsAttr));
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
      changed |= function->getAttr(integerResultRangesAttr) != attribute;
      function->setAttr(integerResultRangesAttr, attribute);
      if (closed.contains(function) && visited[i])
        for (unsigned j = 0; j < arguments[i].size(); ++j) {
          auto range = encodeRange(scope->getContext(), arguments[i][j]);
          changed |= function.getArgAttr(j, integerRangeAttr) != range;
          function.setArgAttr(j, integerRangeAttr, range);
        }
    }
    for (auto call : calls)
      if (auto found = targets.find(call); found != targets.end())
        call->setAttr(
            integerResultRangesAttr,
            functions[found->second]->getAttr(integerResultRangesAttr));
    if (!changed)
      break;
  }
}
} // namespace obelisk::analysis
