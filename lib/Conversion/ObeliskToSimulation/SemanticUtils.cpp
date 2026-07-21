//===- SemanticUtils.cpp - Shared semantic-to-simulation helpers --------===//

#include "Detail.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringExtras.h"

#include <cctype>
#include <functional>
#include <limits>
#include <string>

using namespace mlir;

namespace obelisk::simlowering {

bool isSemanticOp(Operation *op) {
  return op->hasTrait<OpTrait::SemanticASTNode>();
}

bool isCodeUnit(Operation *op) {
  return isa<semantic::SVProceduralBlockSymbolOp,
             semantic::SVContinuousAssignSymbolOp,
             semantic::SVSubroutineSymbolOp>(op);
}

Location getSemanticLocation(Operation *op) {
  if (auto typeAttr = op->getAttrOfType<TypeAttr>("source_range")) {
    if (auto range = dyn_cast<semantic::SourceRangeType>(typeAttr.getValue()))
      return FileLineColLoc::get(op->getContext(), range.getStartFile(),
                                 range.getStartLine(), range.getStartColumn());
  }
  if (auto file = op->getAttrOfType<StringAttr>("source_file"))
    return FileLineColLoc::get(op->getContext(), file.getValue(), 1, 1);
  return op->getLoc();
}

SmallVector<Operation *> getChildren(Operation *op) {
  SmallVector<Operation *> children;
  if (op->getNumRegions() && !op->getRegion(0).empty())
    for (Operation &child : op->getRegion(0).front())
      children.push_back(&child);
  return children;
}

static std::optional<uint64_t> getSemanticPackedWidth(Type type) {
  if (auto integral = dyn_cast<semantic::IntegralType>(type))
    return integral.getWidth();
  if (auto integer = dyn_cast<IntegerType>(type))
    return integer.getWidth();
  if (auto logic = dyn_cast<semantic::LogicType>(type))
    return logic.getWidth();
  if (auto array = dyn_cast<semantic::RangedPackedArrayType>(type)) {
    uint64_t count = array.getLeft() >= array.getRight()
                         ? static_cast<uint64_t>(array.getLeft()) -
                               static_cast<uint64_t>(array.getRight()) + 1
                         : static_cast<uint64_t>(array.getRight()) -
                               static_cast<uint64_t>(array.getLeft()) + 1;
    std::optional<uint64_t> element =
        getSemanticPackedWidth(array.getElementType());
    if (!element ||
        (count && *element > std::numeric_limits<uint64_t>::max() / count))
      return std::nullopt;
    return *element * count;
  }
  if (auto enumeration = dyn_cast<semantic::EnumType>(type))
    return getSemanticPackedWidth(enumeration.getBaseType());
  return std::nullopt;
}

static bool isFourState(Type type) {
  if (auto integral = dyn_cast<semantic::IntegralType>(type))
    return integral.getIsFourState();
  if (isa<semantic::LogicType>(type))
    return true;
  if (auto array = dyn_cast<semantic::RangedPackedArrayType>(type))
    return isFourState(array.getElementType());
  if (auto enumeration = dyn_cast<semantic::EnumType>(type))
    return isFourState(enumeration.getBaseType());
  return false;
}

bool isSignedSemanticType(Type type) {
  if (auto integral = dyn_cast<semantic::IntegralType>(type))
    return integral.getIsSigned();
  if (auto array = dyn_cast<semantic::RangedPackedArrayType>(type))
    return isSignedSemanticType(array.getElementType());
  if (auto enumeration = dyn_cast<semantic::EnumType>(type))
    return isSignedSemanticType(enumeration.getBaseType());
  return false;
}

static FailureOr<Type> normalizeType(Type type, Location location) {
  MLIRContext *context = type.getContext();
  if (auto integer = dyn_cast<IntegerType>(type)) {
    if (!integer.isSignless()) {
      emitError(location) << "signed or unsigned builtin integer survived "
                             "semantic normalization";
      return failure();
    }
    return type;
  }
  if (auto width = getSemanticPackedWidth(type)) {
    if (*width == 0 || *width > std::numeric_limits<unsigned>::max()) {
      emitError(location) << "packed type has unsupported width " << *width;
      return failure();
    }
    if (isFourState(type))
      return sim::LogicType::get(context, static_cast<unsigned>(*width));
    return IntegerType::get(context, static_cast<unsigned>(*width));
  }
  if (isa<semantic::TimeType>(type))
    return sim::TimeType::get(context);
  if (isa<sim::LogicType, sim::TimeType, sim::ContextType, sim::RefType,
          sim::NetType, sim::DriverType, sim::EventType, sim::ProcessType>(
          type))
    return type;

  emitError(location) << "unsupported semantic type in the first simulation "
                         "slice: "
                      << type;
  return failure();
}

FailureOr<Type> getNormalizedSemanticType(Operation *op) {
  auto typeAttr = op->getAttrOfType<TypeAttr>("semantic_type");
  if (!typeAttr) {
    op->emitError(
        "semantic node requires semantic_type for simulation lowering");
    return failure();
  }
  return normalizeType(typeAttr.getValue(), getSemanticLocation(op));
}

StringRef getHierarchyName(Operation *op) {
  if (auto name = op->getAttrOfType<StringAttr>("hierarchical_name"))
    return name.getValue();
  if (auto name = op->getAttrOfType<StringAttr>("name"))
    return name.getValue();
  return {};
}

StringRef getDebugName(Operation *op) {
  if (auto name = op->getAttrOfType<StringAttr>("name"))
    return name.getValue();
  return {};
}

FailureOr<ParsedConstant> parseSVInteger(StringRef spelling, unsigned width,
                                         Location location) {
  std::string clean;
  clean.reserve(spelling.size());
  for (char c : spelling)
    if (c != '_')
      clean.push_back(static_cast<char>(std::tolower(c)));
  StringRef text(clean);
  size_t quote = text.find('\'');
  unsigned radix = 10;
  StringRef digits = text;
  if (quote != StringRef::npos) {
    StringRef suffix = text.drop_front(quote + 1);
    suffix.consume_front("s");
    if (suffix.empty()) {
      emitError(location) << "invalid SystemVerilog integer literal '"
                          << spelling << "'";
      return failure();
    }
    switch (suffix.front()) {
    case 'b':
      radix = 2;
      break;
    case 'o':
      radix = 8;
      break;
    case 'd':
      radix = 10;
      break;
    case 'h':
      radix = 16;
      break;
    default:
      emitError(location) << "unsupported literal base in '" << spelling << "'";
      return failure();
    }
    digits = suffix.drop_front();
  }
  if (digits.empty()) {
    emitError(location) << "invalid SystemVerilog integer literal '" << spelling
                        << "'";
    return failure();
  }

  APInt value(width, 0), unknown(width, 0);
  if (!digits.contains('x') && !digits.contains('z') && !digits.contains('?')) {
    // APInt's string constructor requires a width that can hold the literal,
    // and wraps silently in a no-assert build otherwise. Parse at the width
    // the digits need, then reject anything that does not fit the target.
    for (char c : digits) {
      unsigned digit = llvm::hexDigitValue(c);
      if (digit == static_cast<unsigned>(-1) || digit >= radix) {
        emitError(location)
            << "invalid digit in integer literal '" << spelling << "'";
        return failure();
      }
    }
    unsigned needed = APInt::getSufficientBitsNeeded(digits, radix);
    APInt parsed(std::max(needed, width), digits, radix);
    if (parsed.getActiveBits() > width) {
      emitError(location) << "integer literal '" << spelling
                          << "' does not fit " << "in " << width << " bits";
      return failure();
    }
    return ParsedConstant{parsed.trunc(width), unknown};
  }
  if (radix == 10) {
    emitError(location) << "decimal X/Z integer literals are not yet supported";
    return failure();
  }
  unsigned group = radix == 2 ? 1 : radix == 8 ? 3 : 4;
  unsigned bit = 0;
  for (char c : llvm::reverse(digits)) {
    if (bit >= width)
      break;
    if (c == 'x' || c == 'z' || c == '?') {
      for (unsigned i = 0; i < group && bit + i < width; ++i) {
        unknown.setBit(bit + i);
        if (c == 'z' || c == '?')
          value.setBit(bit + i);
      }
    } else {
      unsigned digit = llvm::hexDigitValue(c);
      if (digit == static_cast<unsigned>(-1) || digit >= radix) {
        emitError(location)
            << "invalid digit in integer literal '" << spelling << "'";
        return failure();
      }
      for (unsigned i = 0; i < group && bit + i < width; ++i)
        if (digit & (1u << i))
          value.setBit(bit + i);
    }
    bit += group;
  }
  return ParsedConstant{value, unknown};
}

Value createDefaultValue(OpBuilder &builder, Location location, Type type) {
  if (auto logic = dyn_cast<sim::LogicType>(type)) {
    auto planeType = IntegerType::get(type.getContext(), logic.getWidth());
    return sim::SimLogicConstantOp::create(
        builder, location, logic,
        builder.getIntegerAttr(planeType, APInt(logic.getWidth(), 0)),
        builder.getIntegerAttr(planeType, APInt::getAllOnes(logic.getWidth())));
  }
  if (auto integer = dyn_cast<IntegerType>(type))
    return arith::ConstantOp::create(builder, location, integer,
                                     builder.getIntegerAttr(integer, 0));
  return {};
}

DictionaryAttr captureMetadata(OpBuilder &builder, sim::CaptureKind kind,
                               std::optional<uint64_t> descriptorId) {
  SmallVector<NamedAttribute> values;
  values.push_back(builder.getNamedAttr(
      captureKindAttrName,
      sim::CaptureKindAttr::get(builder.getContext(), kind)));
  if (descriptorId)
    values.push_back(builder.getNamedAttr(
        descriptorIdAttrName, builder.getI64IntegerAttr(*descriptorId)));
  return builder.getDictionaryAttr(values);
}

bool isSuspensionTerminator(Operation *op) {
  return getFragmentActionKind(op) != sim::ComputeActionKind::Continue &&
         !isa<sim::SimReturnOp>(op);
}

sim::ComputeActionKind getFragmentActionKind(Operation *terminator) {
  return llvm::TypeSwitch<Operation *, sim::ComputeActionKind>(terminator)
      .Case<sim::SimSuspendDelayOp>(
          [](auto) { return sim::ComputeActionKind::SuspendDelay; })
      .Case<sim::SimSuspendChangeOp>(
          [](auto) { return sim::ComputeActionKind::SuspendChange; })
      .Case<sim::SimSuspendEdgeOp>(
          [](auto) { return sim::ComputeActionKind::SuspendEdge; })
      .Case<sim::SimSuspendAnyOp>(
          [](auto) { return sim::ComputeActionKind::SuspendAny; })
      .Case<sim::SimSuspendEventOp>(
          [](auto) { return sim::ComputeActionKind::SuspendEvent; })
      .Case<sim::SimSuspendAwaitOp>(
          [](auto) { return sim::ComputeActionKind::SuspendAwait; })
      .Case<sim::SimSuspendJoinOp>(
          [](auto) { return sim::ComputeActionKind::SuspendJoin; })
      .Case<sim::SimReturnOp>(
          [](auto) { return sim::ComputeActionKind::Terminate; })
      .Default([](Operation *) { return sim::ComputeActionKind::Continue; });
}

sim::ContinuationSiteAttr getContinuationSite(Operation *operation) {
  sim::ContinuationSiteAttr site;
  llvm::TypeSwitch<Operation *>(operation)
      .Case<sim::SimSuspendDelayOp, sim::SimSuspendChangeOp,
            sim::SimSuspendEdgeOp, sim::SimSuspendAnyOp, sim::SimSuspendEventOp,
            sim::SimSuspendAwaitOp, sim::SimSuspendJoinOp>(
          [&](auto op) { site = op.getSiteAttr(); });
  return site;
}

void setContinuationSite(Operation *operation, sim::ContinuationSiteAttr site) {
  llvm::TypeSwitch<Operation *>(operation)
      .Case<sim::SimSuspendDelayOp, sim::SimSuspendChangeOp,
            sim::SimSuspendEdgeOp, sim::SimSuspendAnyOp, sim::SimSuspendEventOp,
            sim::SimSuspendAwaitOp, sim::SimSuspendJoinOp>(
          [&](auto op) { op.setSiteAttr(site); });
}

ReexecutingBlockSet getReexecutingBlocks(sim::SimFuncOp function) {
  ReexecutingBlockSet reexecuting;
  DenseMap<Block *, unsigned> indices, lowlinks;
  SmallVector<Block *> stack;
  llvm::SmallPtrSet<Block *, 16> onStack;
  unsigned nextIndex = 0;

  std::function<void(Block *)> visit = [&](Block *block) {
    unsigned index = nextIndex++;
    indices.try_emplace(block, index);
    lowlinks.try_emplace(block, index);
    stack.push_back(block);
    onStack.insert(block);
    for (Block *successor : block->getTerminator()->getSuccessors()) {
      auto successorIndex = indices.find(successor);
      if (successorIndex == indices.end()) {
        visit(successor);
        lowlinks[block] = std::min(lowlinks[block], lowlinks[successor]);
      } else if (onStack.contains(successor)) {
        lowlinks[block] = std::min(lowlinks[block], successorIndex->second);
      }
    }
    if (lowlinks[block] != index)
      return;
    SmallVector<Block *> component;
    while (true) {
      Block *member = stack.pop_back_val();
      onStack.erase(member);
      component.push_back(member);
      if (member == block)
        break;
    }
    bool cyclic =
        component.size() > 1 ||
        llvm::is_contained(block->getTerminator()->getSuccessors(), block);
    if (cyclic)
      reexecuting.insert(component.begin(), component.end());
  };

  for (Block &block : function.getBody())
    if (!indices.contains(&block))
      visit(&block);
  return reexecuting;
}

bool isConstantTimeValue(Value value) {
  SmallVector<Value> worklist{value};
  DenseSet<Value> visited;
  std::optional<APInt> constantValue;
  while (!worklist.empty()) {
    Value current = worklist.pop_back_val();
    if (!visited.insert(current).second)
      continue;
    if (auto constant = current.getDefiningOp<sim::SimTimeConstantOp>()) {
      APInt value = constant.getValueAttr().getValue();
      if (constantValue && *constantValue != value)
        return false;
      constantValue = value;
      continue;
    }
    auto argument = dyn_cast<BlockArgument>(current);
    if (!argument)
      return false;
    Block *block = argument.getOwner();
    if (block->isEntryBlock() || block->hasNoPredecessors())
      return false;
    bool sawIncoming = false;
    for (Block *predecessor : block->getPredecessors()) {
      auto branch = dyn_cast<BranchOpInterface>(predecessor->getTerminator());
      if (!branch)
        return false;
      for (unsigned successor = 0;
           successor != predecessor->getTerminator()->getNumSuccessors();
           ++successor) {
        if (predecessor->getTerminator()->getSuccessor(successor) != block)
          continue;
        auto forwarded =
            branch.getSuccessorOperands(successor).getForwardedOperands();
        if (argument.getArgNumber() >= forwarded.size())
          return false;
        Value incoming = forwarded[argument.getArgNumber()];
        if (incoming != current) {
          worklist.push_back(incoming);
          sawIncoming = true;
        }
      }
    }
    if (!sawIncoming && worklist.empty() && !constantValue)
      return false;
  }
  return constantValue.has_value();
}

} // namespace obelisk::simlowering
