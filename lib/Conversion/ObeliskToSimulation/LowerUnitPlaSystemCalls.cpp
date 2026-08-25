//===- LowerUnitPlaSystemCalls.cpp - Lower PLA system tasks -------------===//

#include "LowerUnit.h"

#include "mlir/Dialect/Arith/IR/Arith.h"

#include "llvm/ADT/APInt.h"

using namespace mlir;

namespace obelisk::simlowering {

FailureOr<Value>
UnitLowering::lowerPlaSystemCall(semantic::SVCallExpressionOp op) {
  Location location = getSemanticLocation(op);
  StringRef name = op.getCalleeName();
  SmallVector<Operation *> children = getChildren(op);
  if (children.size() != 3) {
    emitError(location) << name << " requires memory, input, and output";
    return failure();
  }

  bool asynchronous = name.starts_with("$async$");
  bool plane = name.ends_with("$plane");
  bool nand = name.contains("$nand$");
  bool nor = name.contains("$nor$");
  bool conjunction = name.contains("$and$") || nand;
  bool disjunction = name.contains("$or$") || nor;
  if ((!asynchronous && !name.starts_with("$sync$")) ||
      (!plane && !name.ends_with("$array")) ||
      (!conjunction && !disjunction)) {
    emitError(location) << "invalid PLA system task " << name;
    return failure();
  }

  // An asynchronous invocation performs the same immediate update as its
  // synchronous counterpart and installs one persistent reevaluator. Outline
  // first while the semantic call tree is intact, then emit the update once.
  FailureOr<std::pair<sim::SimFuncOp, SmallVector<Value>>> callback = failure();
  if (asynchronous) {
    std::string synchronousName = name.str();
    synchronousName.replace(1, 5, "sync");
    callback = outlineAsyncPla(op, synchronousName);
    if (failed(callback))
      return failure();
  }

  FailureOr<Value> memoryValue = lowerExpression(children[0]);
  FailureOr<Value> inputValue = lowerExpression(children[1]);
  FailureOr<Type> outputType = getNormalizedSemanticType(children[2]);
  if (failed(memoryValue) || failed(inputValue) || failed(outputType))
    return failure();

  auto memoryType = dyn_cast<sim::UnpackedArrayType>((*memoryValue).getType());
  unsigned outputs = memoryType ? sim::getAggregateNumElements(memoryType) : 0;
  std::optional<unsigned> inputWidth =
      memoryType ? sim::getPackedWidth(memoryType.getElementType())
                 : std::nullopt;
  std::optional<unsigned> actualInputWidth =
      sim::getPackedWidth((*inputValue).getType());
  std::optional<unsigned> actualOutputWidth = sim::getPackedWidth(*outputType);
  if (!memoryType || !inputWidth || !actualInputWidth || !actualOutputWidth ||
      outputs == 0 || *inputWidth != *actualInputWidth ||
      outputs != *actualOutputWidth) {
    emitError(location)
        << name
        << " requires a fixed memory whose element width equals the "
           "input width and whose depth equals the output width";
    return failure();
  }

  FailureOr<Value> input = toLogic(*inputValue, location);
  if (failed(input))
    return failure();
  auto rowLogic = sim::LogicType::get(function.getContext(), *inputWidth);
  auto rowBits = IntegerType::get(function.getContext(), *inputWidth);
  APInt ones = APInt::getAllOnes(*inputWidth);
  Value allOnes = arith::ConstantOp::create(
      builder, location, rowBits, builder.getIntegerAttr(rowBits, ones));
  Value highZ = sim::SimLogicConstantOp::create(
      builder, location, rowLogic, builder.getIntegerAttr(rowBits, ones),
      builder.getIntegerAttr(rowBits, ones));

  SmallVector<Value> outputBits;
  outputBits.reserve(outputs);
  for (unsigned ordinal = 0; ordinal != outputs; ++ordinal) {
    Value rawRow = sim::SimAggregateExtractOp::create(
        builder, location, memoryType.getElementType(), *memoryValue, ordinal);
    FailureOr<Value> convertedRow = toLogic(rawRow, location);
    if (failed(convertedRow))
      return failure();
    Value row = *convertedRow;
    if (row.getType() != rowLogic) {
      emitError(location) << name << " memory element has inconsistent width";
      return failure();
    }

    Value terms;
    if (!plane) {
      // Clause 20.17's array encoding admits 0 and 1: only an exact known 1
      // includes a term. to_bits maps X and Z to zero, which conservatively
      // excludes nonconforming memory symbols instead of inventing a
      // four-state membership rule.
      Value includedBits =
          sim::SimLogicToBitsOp::create(builder, location, rowBits, row);
      Value included = sim::SimLogicFromBitsOp::create(builder, location,
                                                       rowLogic, includedBits);
      if (conjunction) {
        Value excluded = sim::SimLogicUnaryOp::create(
            builder, location, rowLogic, sim::UnaryKind::BitNot, included);
        terms = sim::SimLogicBinaryOp::create(
            builder, location, rowLogic, sim::BinaryKind::Or, *input, excluded);
      } else {
        terms = sim::SimLogicBinaryOp::create(builder, location, rowLogic,
                                              sim::BinaryKind::And, *input,
                                              included);
      }
    } else {
      // XNOR selects the true input for 1, its complement for 0, and X for an
      // X plane symbol. Z is the don't-care identity and must therefore be
      // replaced with 1 for product terms and 0 for sum terms.
      Value selected = sim::SimLogicBinaryOp::create(
          builder, location, rowLogic, sim::BinaryKind::Xnor, *input, row);
      Value differsFromZ = sim::SimLogicCaseDifferenceMaskOp::create(
          builder, location, rowBits, row, highZ);
      Value zBits =
          arith::XOrIOp::create(builder, location, differsFromZ, allOnes);
      Value zMask =
          sim::SimLogicFromBitsOp::create(builder, location, rowLogic, zBits);
      if (conjunction)
        terms = sim::SimLogicBinaryOp::create(
            builder, location, rowLogic, sim::BinaryKind::Or, selected, zMask);
      else {
        Value careMask = sim::SimLogicUnaryOp::create(
            builder, location, rowLogic, sim::UnaryKind::BitNot, zMask);
        terms = sim::SimLogicBinaryOp::create(builder, location, rowLogic,
                                              sim::BinaryKind::And, selected,
                                              careMask);
      }
    }

    sim::ReductionKind reduction =
        conjunction
            ? (nand ? sim::ReductionKind::Nand : sim::ReductionKind::And)
            : (nor ? sim::ReductionKind::Nor : sim::ReductionKind::Or);
    outputBits.push_back(sim::SimLogicReductionOp::create(
        builder, location, sim::LogicType::get(function.getContext(), 1),
        reduction, terms));
  }

  Value result = outputs == 1
                     ? outputBits.front()
                     : Value(sim::SimLogicConcatOp::create(
                           builder, location,
                           sim::LogicType::get(function.getContext(), outputs),
                           outputBits));
  if (failed(writeLValue(children[2], result, /*sourceSigned=*/false,
                         /*nonblocking=*/false, location)))
    return failure();

  if (asynchronous)
    sim::SimSpawnOp::create(builder, location, callback->first.getSymNameAttr(),
                            callback->second, ArrayAttr{}, ArrayAttr{});

  auto i1 = builder.getI1Type();
  return arith::ConstantOp::create(builder, location, i1,
                                   builder.getIntegerAttr(i1, 0))
      .getResult();
}

} // namespace obelisk::simlowering
