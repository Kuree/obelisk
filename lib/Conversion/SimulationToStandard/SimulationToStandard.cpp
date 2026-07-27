//===- SimulationToStandard.cpp - Lower packed simulation values --------===//

#include "obelisk/Conversion/SimulationToStandard.h"

#include "obelisk/Conversion/Passes.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/Func/Transforms/FuncConversions.h"
#include "mlir/Dialect/Math/IR/Math.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Transforms/DialectConversion.h"

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/MathExtras.h"

#include <algorithm>
#include <limits>
#include <optional>

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_CONVERTOBELISKSIMVALUESTOSTANDARDPASS
#include "obelisk/Conversion/Passes.h.inc"

SimulationToStandardTypeConverter::SimulationToStandardTypeConverter() {
  // Keep every non-value/runtime type stable. Future runtime conversion can
  // add more-specific callbacks to this same converter.
  addConversion([](Type type) { return type; });
  addConversion(
      [](sim::LogicType type, SmallVectorImpl<Type> &results) -> LogicalResult {
        auto plane = IntegerType::get(type.getContext(), type.getWidth());
        results.append({plane, plane});
        return success();
      });
}

static LogicalResult convertPackedAggregateType(
    Type type, SmallVectorImpl<Type> &results) {
  Type scalar = sim::getPackedScalarType(type);
  if (auto logic = dyn_cast_or_null<sim::LogicType>(scalar)) {
    Type plane = IntegerType::get(type.getContext(), logic.getWidth());
    results.append({plane, plane});
    return success();
  }
  if (auto integer = dyn_cast_or_null<IntegerType>(scalar)) {
    results.push_back(integer);
    return success();
  }
  return failure();
}

void addSimulationPackedAggregateTypeConversions(TypeConverter &converter) {
  converter.addConversion(
      [](sim::PackedArrayType type, SmallVectorImpl<Type> &results) {
        return convertPackedAggregateType(type, results);
      });
  converter.addConversion(
      [](sim::PackedStructType type, SmallVectorImpl<Type> &results) {
        return convertPackedAggregateType(type, results);
      });
  converter.addConversion(
      [](sim::PackedUnionType type, SmallVectorImpl<Type> &results) {
        return convertPackedAggregateType(type, results);
      });
}

namespace {

struct LogicValue {
  Value value;
  Value unknown;
};

struct TruthValue {
  Value value;
  Value unknown;
};

static IntegerType integerType(Value value) {
  return cast<IntegerType>(value.getType());
}

static Value integerConstant(OpBuilder &builder, Location loc, IntegerType type,
                             const APInt &value) {
  return arith::ConstantOp::create(builder, loc, type,
                                   IntegerAttr::get(type, value));
}

static Value integerConstant(OpBuilder &builder, Location loc, IntegerType type,
                             uint64_t value) {
  return integerConstant(builder, loc, type, APInt(type.getWidth(), value));
}

static Value signedConstant(OpBuilder &builder, Location loc, IntegerType type,
                            int64_t value) {
  return integerConstant(builder, loc, type,
                         APInt(type.getWidth(), value, true));
}

static Value zero(OpBuilder &builder, Location loc, IntegerType type) {
  return integerConstant(builder, loc, type, 0);
}

static Value ones(OpBuilder &builder, Location loc, IntegerType type) {
  return integerConstant(builder, loc, type,
                         APInt::getAllOnes(type.getWidth()));
}

static Value boolConstant(OpBuilder &builder, Location loc, bool value) {
  return integerConstant(builder, loc, builder.getI1Type(), value ? 1 : 0);
}

static Value bitNot(OpBuilder &builder, Location loc, Value value) {
  return arith::XOrIOp::create(builder, loc, value,
                               ones(builder, loc, integerType(value)));
}

static Value boolNot(OpBuilder &builder, Location loc, Value value) {
  return arith::XOrIOp::create(builder, loc, value,
                               boolConstant(builder, loc, true));
}

static Value boolAnd(OpBuilder &builder, Location loc, Value lhs, Value rhs) {
  return arith::AndIOp::create(builder, loc, lhs, rhs);
}

static Value boolOr(OpBuilder &builder, Location loc, Value lhs, Value rhs) {
  return arith::OrIOp::create(builder, loc, lhs, rhs);
}

static Value isNonZero(OpBuilder &builder, Location loc, Value value) {
  return arith::CmpIOp::create(builder, loc, arith::CmpIPredicate::ne, value,
                               zero(builder, loc, integerType(value)));
}

static Value isZero(OpBuilder &builder, Location loc, Value value) {
  return arith::CmpIOp::create(builder, loc, arith::CmpIPredicate::eq, value,
                               zero(builder, loc, integerType(value)));
}

static Value select(OpBuilder &builder, Location loc, Value condition,
                    Value trueValue, Value falseValue) {
  return arith::SelectOp::create(builder, loc, condition, trueValue,
                                 falseValue);
}

static Value resizeInteger(OpBuilder &builder, Location loc, Value value,
                           unsigned width, bool isSigned) {
  auto source = integerType(value);
  if (source.getWidth() == width)
    return value;
  auto target = builder.getIntegerType(width);
  if (source.getWidth() > width)
    return arith::TruncIOp::create(builder, loc, target, value);
  if (isSigned)
    return arith::ExtSIOp::create(builder, loc, target, value);
  return arith::ExtUIOp::create(builder, loc, target, value);
}

static LogicValue getLogic(ArrayRef<ValueRange> operands, unsigned index) {
  assert(operands[index].size() == 2 && "logic operand did not expand 1:2");
  return {operands[index][0], operands[index][1]};
}

static void replaceLogicResult(Operation *op, LogicValue result,
                               ConversionPatternRewriter &rewriter,
                               bool provenTwoState) {
  // Preserve the ordinary two-plane ABI while making a proven unknown plane
  // a compile-time zero, so downstream LLVM optimization can erase X/Z
  // propagation through the two-state island.
  if (provenTwoState)
    result.unknown =
        zero(rewriter, op->getLoc(), integerType(result.value));
  SmallVector<SmallVector<Value>> replacements;
  replacements.push_back({result.value, result.unknown});
  rewriter.replaceOpWithMultiple(op, std::move(replacements));
}

static void replaceInteger(Operation *op, Value result,
                           ConversionPatternRewriter &rewriter) {
  rewriter.replaceOp(op, result);
}

template <typename Op>
class PackedAggregateViewConversion final : public OpConversionPattern<Op> {
public:
  using OpConversionPattern<Op>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(Op op,
                  typename OpConversionPattern<Op>::OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    SmallVector<ValueRange> replacements{adaptor.getInput()};
    rewriter.replaceOpWithMultiple(op, replacements);
    return success();
  }
};

static LogicValue canonicalUnknown(OpBuilder &builder, Location loc,
                                   IntegerType type) {
  return {zero(builder, loc, type), ones(builder, loc, type)};
}

/// Produce the SystemVerilog three-valued truth state. `value` is true only
/// for a definitely true operand and `unknown` is true only for an ambiguous
/// operand. They are mutually exclusive.
static TruthValue getTruth(OpBuilder &builder, Location loc, LogicValue input) {
  Value knownBits = bitNot(builder, loc, input.unknown);
  Value knownOnes = arith::AndIOp::create(builder, loc, input.value, knownBits);
  Value isTrue = isNonZero(builder, loc, knownOnes);
  Value hasUnknown = isNonZero(builder, loc, input.unknown);
  Value isUnknown =
      boolAnd(builder, loc, boolNot(builder, loc, isTrue), hasUnknown);
  return {isTrue, isUnknown};
}

static Value parity(OpBuilder &builder, Location loc, Value value) {
  auto type = integerType(value);
  Value folded = value;
  for (uint64_t shift = 1; shift < type.getWidth(); shift <<= 1) {
    Value amount = integerConstant(builder, loc, type, shift);
    Value shifted = arith::ShRUIOp::create(builder, loc, folded, amount);
    folded = arith::XOrIOp::create(builder, loc, folded, shifted);
  }
  if (type.getWidth() == 1)
    return folded;
  return arith::TruncIOp::create(builder, loc, builder.getI1Type(), folded);
}

/// Build a poison-free partial dynamic selection. The input is placed between
/// low and high padding, then a guarded shift selects the requested window.
/// Indices are interpreted as signed two's-complement values so negative
/// selections can overlap bit zero.
static FailureOr<LogicValue> dynamicExtract(OpBuilder &builder, Location loc,
                                            Value input, Value inputUnknown,
                                            Value low, Value lowUnknown,
                                            unsigned resultWidth,
                                            bool fourStateResult) {
  auto inputType = integerType(input);
  auto lowType = integerType(low);
  auto resultType = builder.getIntegerType(resultWidth);

  uint64_t padding = static_cast<uint64_t>(resultWidth) - 1;
  uint64_t paddedWidth64 =
      static_cast<uint64_t>(inputType.getWidth()) + 2 * padding;
  if (paddedWidth64 > std::numeric_limits<unsigned>::max())
    return failure();
  auto paddedType =
      builder.getIntegerType(static_cast<unsigned>(paddedWidth64));

  unsigned boundBits =
      std::max(2u, llvm::Log2_64_Ceil(static_cast<uint64_t>(std::max(
                                          inputType.getWidth(), resultWidth)) +
                                      1) +
                       2);
  uint64_t checkWidth64 =
      std::max(static_cast<uint64_t>(lowType.getWidth()) + 1,
               static_cast<uint64_t>(boundBits));
  if (checkWidth64 > std::numeric_limits<unsigned>::max())
    return failure();
  unsigned checkWidth = static_cast<unsigned>(checkWidth64);
  Value checkedLow = resizeInteger(builder, loc, low, checkWidth, true);
  auto checkType = integerType(checkedLow);
  Value indexKnown =
      lowUnknown ? boolNot(builder, loc, isNonZero(builder, loc, lowUnknown))
                 : boolConstant(builder, loc, true);

  Value aboveLower = arith::CmpIOp::create(
      builder, loc, arith::CmpIPredicate::sge, checkedLow,
      signedConstant(builder, loc, checkType, -static_cast<int64_t>(padding)));
  Value belowUpper = arith::CmpIOp::create(
      builder, loc, arith::CmpIPredicate::slt, checkedLow,
      signedConstant(builder, loc, checkType, inputType.getWidth()));
  Value overlaps = boolAnd(builder, loc, indexKnown,
                           boolAnd(builder, loc, aboveLower, belowUpper));

  Value adjusted =
      arith::AddIOp::create(builder, loc, checkedLow,
                            integerConstant(builder, loc, checkType, padding));
  Value paddedAmount =
      resizeInteger(builder, loc, adjusted, paddedType.getWidth(), false);
  Value safeAmount = select(builder, loc, overlaps, paddedAmount,
                            zero(builder, loc, paddedType));

  auto padPlane = [&](Value plane, Value paddingValue) -> Value {
    Value padded =
        resizeInteger(builder, loc, plane, paddedType.getWidth(), false);
    if (padding != 0)
      padded = arith::ShLIOp::create(
          builder, loc, padded,
          integerConstant(builder, loc, paddedType, padding));
    if (paddingValue)
      padded = arith::OrIOp::create(builder, loc, padded, paddingValue);
    return padded;
  };
  auto selectPlane = [&](Value padded, Value fallback) -> Value {
    Value shifted = arith::ShRUIOp::create(builder, loc, padded, safeAmount);
    Value extracted =
        resizeInteger(builder, loc, shifted, resultType.getWidth(), false);
    return select(builder, loc, overlaps, extracted, fallback);
  };

  Value paddedValue = padPlane(input, Value());
  Value resultValue = selectPlane(paddedValue, zero(builder, loc, resultType));
  Value resultUnknown;
  if (fourStateResult) {
    APInt validMask =
        APInt::getLowBitsSet(paddedType.getWidth(), inputType.getWidth())
            .shl(padding);
    Value invalidMask = integerConstant(builder, loc, paddedType, ~validMask);
    Value paddedUnknown = padPlane(inputUnknown, invalidMask);
    resultUnknown = selectPlane(paddedUnknown, ones(builder, loc, resultType));
  }
  return LogicValue{resultValue, resultUnknown};
}

static SmallVector<Value> flattenConverted(ArrayRef<ValueRange> ranges) {
  SmallVector<Value> flattened;
  for (ValueRange range : ranges)
    flattened.append(range.begin(), range.end());
  return flattened;
}

static void copyDiscardableAttrs(Operation *source, Operation *target) {
  target->setDiscardableAttrs(source->getDiscardableAttrDictionary());
}

/// Duplicate each semantic argument attribute dictionary onto every physical
/// value produced by its 1:N signature conversion. Newly inserted arguments
/// that are not mapped from a source argument receive an empty dictionary.
static ArrayAttr
expandInputAttrs(ArrayAttr attrs,
                 const TypeConverter::SignatureConversion &conversion,
                 OpBuilder &builder) {
  if (!attrs)
    return {};
  DictionaryAttr empty = builder.getDictionaryAttr({});
  SmallVector<Attribute> expanded(conversion.getConvertedTypes().size(), empty);
  for (auto [index, attr] : llvm::enumerate(attrs)) {
    std::optional<TypeConverter::SignatureConversion::InputMapping> mapping =
        conversion.getInputMapping(index);
    if (!mapping || mapping->replacedWithValues())
      continue;
    for (size_t offset = 0; offset < mapping->size; ++offset)
      expanded[mapping->inputNo + offset] = attr;
  }
  return builder.getArrayAttr(expanded);
}

/// Duplicate each semantic result/operand attribute dictionary onto every
/// physical value in its converted range.
static ArrayAttr expandSequentialAttrs(ArrayAttr attrs, ArrayRef<size_t> sizes,
                                       OpBuilder &builder) {
  if (!attrs)
    return {};
  assert(attrs.size() == sizes.size() && "attribute arity must match values");
  SmallVector<Attribute> expanded;
  for (auto [attr, size] : llvm::zip_equal(attrs, sizes))
    expanded.append(size, attr);
  return builder.getArrayAttr(expanded);
}

/// Convert every block in a function body, not just its entry block. This is
/// important for unreachable blocks, which have no predecessor branch pattern
/// that could otherwise discover and convert their arguments.
class FuncConversion final : public OpConversionPattern<func::FuncOp> {
public:
  FuncConversion(const TypeConverter &converter, MLIRContext *context)
      : OpConversionPattern(converter, context, /*benefit=*/2) {}

  LogicalResult
  matchAndRewrite(func::FuncOp op, OneToNOpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    FunctionType type = op.getFunctionType();
    ArrayAttr oldArgAttrs = op.getArgAttrsAttr();
    ArrayAttr oldResultAttrs = op.getResAttrsAttr();
    TypeConverter::SignatureConversion entryConversion(type.getNumInputs());
    SmallVector<Type> results;
    SmallVector<size_t> resultSizes;
    if (failed(getTypeConverter()->convertSignatureArgs(type.getInputs(),
                                                        entryConversion)))
      return failure();
    for (Type result : type.getResults()) {
      size_t oldSize = results.size();
      if (failed(getTypeConverter()->convertType(result, results)))
        return failure();
      resultSizes.push_back(results.size() - oldSize);
    }

    if (!op.getBody().empty() &&
        failed(rewriter.convertRegionTypes(&op.getBody(), *getTypeConverter(),
                                           &entryConversion)))
      return failure();

    FunctionType converted = FunctionType::get(
        rewriter.getContext(), entryConversion.getConvertedTypes(), results);
    ArrayAttr convertedArgAttrs =
        expandInputAttrs(oldArgAttrs, entryConversion, rewriter);
    ArrayAttr convertedResultAttrs =
        expandSequentialAttrs(oldResultAttrs, resultSizes, rewriter);
    rewriter.modifyOpInPlace(op, [&] {
      op.setType(converted);
      if (convertedArgAttrs)
        op.setArgAttrsAttr(convertedArgAttrs);
      if (convertedResultAttrs)
        op.setResAttrsAttr(convertedResultAttrs);
    });
    return success();
  }
};

class CallConversion final : public OpConversionPattern<func::CallOp> {
public:
  CallConversion(const TypeConverter &converter, MLIRContext *context)
      : OpConversionPattern(converter, context, /*benefit=*/2) {}

  LogicalResult
  matchAndRewrite(func::CallOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    ArrayAttr oldArgAttrs = op.getArgAttrsAttr();
    ArrayAttr oldResultAttrs = op.getResAttrsAttr();
    SmallVector<size_t> resultSizes;
    SmallVector<Type> convertedResults;
    for (Type type : op.getResultTypes()) {
      size_t oldSize = convertedResults.size();
      if (failed(getTypeConverter()->convertType(type, convertedResults)))
        return failure();
      resultSizes.push_back(convertedResults.size() - oldSize);
    }

    SmallVector<size_t> operandSizes;
    for (ValueRange operands : adaptor.getOperands())
      operandSizes.push_back(operands.size());
    ArrayAttr convertedArgAttrs =
        expandSequentialAttrs(oldArgAttrs, operandSizes, rewriter);
    ArrayAttr convertedResultAttrs =
        expandSequentialAttrs(oldResultAttrs, resultSizes, rewriter);

    auto converted = func::CallOp::create(
        rewriter, op.getLoc(), op.getCallee(), convertedResults,
        flattenConverted(adaptor.getOperands()));
    if (convertedArgAttrs)
      converted.setArgAttrsAttr(convertedArgAttrs);
    if (convertedResultAttrs)
      converted.setResAttrsAttr(convertedResultAttrs);
    converted.setNoInline(op.getNoInline());
    copyDiscardableAttrs(op, converted);

    SmallVector<ValueRange> replacements;
    size_t offset = 0;
    for (size_t size : resultSizes) {
      replacements.push_back(converted.getResults().slice(offset, size));
      offset += size;
    }
    rewriter.replaceOpWithMultiple(op, replacements);
    return success();
  }
};

class ReturnConversion final : public OpConversionPattern<func::ReturnOp> {
public:
  ReturnConversion(const TypeConverter &converter, MLIRContext *context)
      : OpConversionPattern(converter, context, /*benefit=*/2) {}

  LogicalResult
  matchAndRewrite(func::ReturnOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    auto converted = func::ReturnOp::create(
        rewriter, op.getLoc(), flattenConverted(adaptor.getOperands()));
    copyDiscardableAttrs(op, converted);
    rewriter.replaceOp(op, converted);
    return success();
  }
};

static FailureOr<Block *>
convertBlockArguments(Block *block, const TypeConverter &converter,
                      ConversionPatternRewriter &rewriter) {
  if (llvm::all_of(block->getArguments(),
                   [&](Value argument) { return converter.isLegal(argument); }))
    return block;
  std::optional<TypeConverter::SignatureConversion> conversion =
      converter.convertBlockSignature(block);
  if (!conversion)
    return failure();
  return rewriter.applySignatureConversion(block, *conversion, &converter);
}

/// The upstream branch adaptor flattens converted operands but intentionally
/// does not own successor block signature conversion. These patterns keep the
/// two actions in one rewrite so 1:N logic values flow through arbitrary CFGs
/// without a materialization.
class BranchConversion final : public OpConversionPattern<cf::BranchOp> {
public:
  BranchConversion(const TypeConverter &converter, MLIRContext *context)
      : OpConversionPattern(converter, context, /*benefit=*/2) {}

  LogicalResult
  matchAndRewrite(cf::BranchOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    FailureOr<Block *> destination =
        convertBlockArguments(op.getDest(), *getTypeConverter(), rewriter);
    if (failed(destination))
      return failure();
    SmallVector<Value> operands = flattenConverted(adaptor.getDestOperands());
    auto converted =
        cf::BranchOp::create(rewriter, op.getLoc(), *destination, operands);
    copyDiscardableAttrs(op, converted);
    rewriter.replaceOp(op, converted);
    return success();
  }
};

class CondBranchConversion final
    : public OpConversionPattern<cf::CondBranchOp> {
public:
  CondBranchConversion(const TypeConverter &converter, MLIRContext *context)
      : OpConversionPattern(converter, context, /*benefit=*/2) {}

  LogicalResult
  matchAndRewrite(cf::CondBranchOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Block *originalTrue = op.getTrueDest();
    Block *originalFalse = op.getFalseDest();
    FailureOr<Block *> trueDestination =
        convertBlockArguments(originalTrue, *getTypeConverter(), rewriter);
    if (failed(trueDestination))
      return failure();
    FailureOr<Block *> falseDestination =
        originalFalse == originalTrue
            ? trueDestination
            : convertBlockArguments(originalFalse, *getTypeConverter(),
                                    rewriter);
    if (failed(falseDestination))
      return failure();
    SmallVector<Value> trueOperands =
        flattenConverted(adaptor.getTrueDestOperands());
    SmallVector<Value> falseOperands =
        flattenConverted(adaptor.getFalseDestOperands());
    ArrayRef<int32_t> weights =
        op.getBranchWeights().value_or(ArrayRef<int32_t>{});
    auto converted = cf::CondBranchOp::create(
        rewriter, op.getLoc(), adaptor.getCondition().front(), *trueDestination,
        trueOperands, *falseDestination, falseOperands, weights);
    copyDiscardableAttrs(op, converted);
    rewriter.replaceOp(op, converted);
    return success();
  }
};

class SwitchConversion final : public OpConversionPattern<cf::SwitchOp> {
public:
  SwitchConversion(const TypeConverter &converter, MLIRContext *context)
      : OpConversionPattern(converter, context, /*benefit=*/2) {}

  LogicalResult
  matchAndRewrite(cf::SwitchOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    llvm::DenseMap<Block *, Block *> convertedBlocks;
    auto convertDestination = [&](Block *destination) -> FailureOr<Block *> {
      if (auto found = convertedBlocks.find(destination);
          found != convertedBlocks.end())
        return found->second;
      FailureOr<Block *> converted =
          convertBlockArguments(destination, *getTypeConverter(), rewriter);
      if (succeeded(converted))
        convertedBlocks.try_emplace(destination, *converted);
      return converted;
    };

    Block *originalDefault = op.getDefaultDestination();
    SmallVector<Block *> originalCases(op.getCaseDestinations());
    FailureOr<Block *> defaultDestination = convertDestination(originalDefault);
    if (failed(defaultDestination))
      return failure();
    SmallVector<Block *> caseDestinations;
    for (Block *destination : originalCases) {
      FailureOr<Block *> converted = convertDestination(destination);
      if (failed(converted))
        return failure();
      caseDestinations.push_back(*converted);
    }

    SmallVector<Value> defaultOperands =
        flattenConverted(adaptor.getDefaultOperands());
    SmallVector<SmallVector<Value>> caseStorage;
    for (ArrayRef<ValueRange> group : adaptor.getCaseOperands())
      caseStorage.push_back(flattenConverted(group));
    SmallVector<ValueRange> caseOperands;
    for (SmallVector<Value> &group : caseStorage)
      caseOperands.push_back(group);
    auto converted = cf::SwitchOp::create(
        rewriter, op.getLoc(), adaptor.getFlag().front(), *defaultDestination,
        defaultOperands, op.getCaseValuesAttr(), caseDestinations,
        caseOperands);
    copyDiscardableAttrs(op, converted);
    rewriter.replaceOp(op, converted);
    return success();
  }
};

template <typename Op>
class LogicOpConversion : public OpConversionPattern<Op> {
public:
  using OpConversionPattern<Op>::OpConversionPattern;

  LogicOpConversion(
      const TypeConverter &converter, MLIRContext *context,
      const llvm::DenseSet<Operation *> *provenTwoStateOperations)
      : OpConversionPattern<Op>(converter, context),
        provenTwoStateOperations(provenTwoStateOperations) {}

protected:
  void replaceLogic(Op op, LogicValue result,
                    ConversionPatternRewriter &rewriter) const {
    replaceLogicResult(
        op, result, rewriter,
        provenTwoStateOperations &&
            provenTwoStateOperations->contains(op.getOperation()));
  }

private:
  const llvm::DenseSet<Operation *> *provenTwoStateOperations = nullptr;
};

class ConstantConversion final
    : public LogicOpConversion<sim::SimLogicConstantOp> {
public:
  using LogicOpConversion::LogicOpConversion;

  LogicalResult
  matchAndRewrite(sim::SimLogicConstantOp op, OneToNOpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    auto type = rewriter.getIntegerType(op.getResult().getType().getWidth());
    replaceLogic(
        op,
        {integerConstant(rewriter, op.getLoc(), type, op.getValue()),
         integerConstant(rewriter, op.getLoc(), type, op.getUnknown())},
        rewriter);
    return success();
  }
};

class FromBitsConversion final
    : public LogicOpConversion<sim::SimLogicFromBitsOp> {
public:
  using LogicOpConversion::LogicOpConversion;

  LogicalResult
  matchAndRewrite(sim::SimLogicFromBitsOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    ArrayRef<ValueRange> operands = adaptor.getOperands();
    Value input = operands[0][0];
    replaceLogic(op, {input, zero(rewriter, op.getLoc(), integerType(input))},
                 rewriter);
    return success();
  }
};

class ToBitsConversion final : public LogicOpConversion<sim::SimLogicToBitsOp> {
public:
  using LogicOpConversion::LogicOpConversion;

  LogicalResult
  matchAndRewrite(sim::SimLogicToBitsOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    ArrayRef<ValueRange> operands = adaptor.getOperands();
    LogicValue input = getLogic(operands, 0);
    Value known = bitNot(rewriter, op.getLoc(), input.unknown);
    Value converted =
        arith::AndIOp::create(rewriter, op.getLoc(), input.value, known);
    replaceInteger(op, converted, rewriter);
    return success();
  }
};

class IsTrueConversion final : public LogicOpConversion<sim::SimLogicIsTrueOp> {
public:
  using LogicOpConversion::LogicOpConversion;

  LogicalResult
  matchAndRewrite(sim::SimLogicIsTrueOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    ArrayRef<ValueRange> operands = adaptor.getOperands();
    replaceInteger(op,
                   getTruth(rewriter, op.getLoc(), getLogic(operands, 0)).value,
                   rewriter);
    return success();
  }
};

class MuxConversion final : public LogicOpConversion<sim::SimLogicMuxOp> {
public:
  using LogicOpConversion::LogicOpConversion;

  LogicalResult
  matchAndRewrite(sim::SimLogicMuxOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    ArrayRef<ValueRange> operands = adaptor.getOperands();
    Location loc = op.getLoc();
    LogicValue condition = getLogic(operands, 0);
    LogicValue trueValue = getLogic(operands, 1);
    LogicValue falseValue = getLogic(operands, 2);

    Value valueMismatch = arith::XOrIOp::create(
        rewriter, loc, trueValue.value, falseValue.value);
    Value unknownMismatch = arith::XOrIOp::create(
        rewriter, loc, trueValue.unknown, falseValue.unknown);
    Value mismatch = arith::OrIOp::create(rewriter, loc, valueMismatch,
                                          unknownMismatch);
    LogicValue merged{
        arith::AndIOp::create(rewriter, loc, trueValue.value,
                              bitNot(rewriter, loc, mismatch)),
        arith::OrIOp::create(rewriter, loc, trueValue.unknown, mismatch)};

    Value conditionKnown = boolNot(rewriter, loc, condition.unknown);
    LogicValue selected{
        select(rewriter, loc, condition.value, trueValue.value,
               falseValue.value),
        select(rewriter, loc, condition.value, trueValue.unknown,
               falseValue.unknown)};
    replaceLogic(op,
                 {select(rewriter, loc, conditionKnown, selected.value,
                         merged.value),
                  select(rewriter, loc, conditionKnown, selected.unknown,
                         merged.unknown)},
                 rewriter);
    return success();
  }
};

class CountBitsConversion final
    : public LogicOpConversion<sim::SimLogicCountBitsOp> {
public:
  using LogicOpConversion::LogicOpConversion;

  LogicalResult
  matchAndRewrite(sim::SimLogicCountBitsOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    ArrayRef<ValueRange> operands = adaptor.getOperands();
    if (operands.empty() ||
        (operands[0].size() != 1 && operands[0].size() != 2))
      return rewriter.notifyMatchFailure(
          op, "bitstream input did not convert to one or two planes");

    Location loc = op.getLoc();
    Value inputValue = operands[0][0];
    if (!isa<IntegerType>(inputValue.getType()))
      return rewriter.notifyMatchFailure(
          op, "bitstream input did not convert to integer planes");
    auto type = integerType(inputValue);
    Value inputUnknown =
        operands[0].size() == 2 ? operands[0][1] : zero(rewriter, loc, type);
    Value known = bitNot(rewriter, loc, inputUnknown);
    Value stateMasks[4] = {
        arith::AndIOp::create(rewriter, loc, bitNot(rewriter, loc, inputValue),
                              known),
        arith::AndIOp::create(rewriter, loc, inputValue, known),
        arith::AndIOp::create(rewriter, loc, bitNot(rewriter, loc, inputValue),
                              inputUnknown),
        arith::AndIOp::create(rewriter, loc, inputValue, inputUnknown)};

    Value selected = zero(rewriter, loc, type);
    for (size_t index = 1; index < operands.size(); ++index) {
      if (operands[index].size() != 2)
        return rewriter.notifyMatchFailure(
            op, "state control did not convert to two planes");
      Value controlValue = operands[index][0];
      Value controlUnknown = operands[index][1];
      Value knownControl = boolNot(rewriter, loc, controlUnknown);
      Value zeroControl = boolNot(rewriter, loc, controlValue);
      Value selectors[4] = {
          boolAnd(rewriter, loc, knownControl, zeroControl),
          boolAnd(rewriter, loc, knownControl, controlValue),
          boolAnd(rewriter, loc, controlUnknown, zeroControl),
          boolAnd(rewriter, loc, controlUnknown, controlValue)};
      for (unsigned state = 0; state != 4; ++state) {
        Value mask = select(rewriter, loc, selectors[state], stateMasks[state],
                            zero(rewriter, loc, type));
        selected = arith::OrIOp::create(rewriter, loc, selected, mask);
      }
    }
    Value count =
        math::CtPopOp::create(rewriter, loc, type, selected).getResult();
    replaceInteger(op, resizeInteger(rewriter, loc, count, 32, false),
                   rewriter);
    return success();
  }
};

class Clog2Conversion final : public LogicOpConversion<sim::SimLogicClog2Op> {
public:
  using LogicOpConversion::LogicOpConversion;

  LogicalResult
  matchAndRewrite(sim::SimLogicClog2Op op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    ArrayRef<ValueRange> operands = adaptor.getOperands();
    LogicValue input = getLogic(operands, 0);
    Location loc = op.getLoc();
    auto type = integerType(input.value);
    Value value = arith::AndIOp::create(rewriter, loc, input.value,
                                        bitNot(rewriter, loc, input.unknown));
    Value decremented = arith::SubIOp::create(
        rewriter, loc, value, integerConstant(rewriter, loc, type, 1));
    Value leading =
        math::CountLeadingZerosOp::create(rewriter, loc, type, decremented)
            .getResult();
    Value width = integerConstant(rewriter, loc, type, type.getWidth());
    Value nonzeroResult = arith::SubIOp::create(rewriter, loc, width, leading);
    Value result = select(rewriter, loc, isZero(rewriter, loc, value),
                          zero(rewriter, loc, type), nonzeroResult);
    replaceInteger(op, resizeInteger(rewriter, loc, result, 32, false),
                   rewriter);
    return success();
  }
};

class ResizeConversion final : public LogicOpConversion<sim::SimLogicResizeOp> {
public:
  using LogicOpConversion::LogicOpConversion;

  LogicalResult
  matchAndRewrite(sim::SimLogicResizeOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    ArrayRef<ValueRange> operands = adaptor.getOperands();
    LogicValue input = getLogic(operands, 0);
    unsigned width = op.getResult().getType().getWidth();
    bool isSigned = op.getIsSigned();
    replaceLogic(
        op,
        {resizeInteger(rewriter, op.getLoc(), input.value, width, isSigned),
         resizeInteger(rewriter, op.getLoc(), input.unknown, width, isSigned)},
        rewriter);
    return success();
  }
};

class UnaryConversion final : public LogicOpConversion<sim::SimLogicUnaryOp> {
public:
  using LogicOpConversion::LogicOpConversion;

  LogicalResult
  matchAndRewrite(sim::SimLogicUnaryOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    ArrayRef<ValueRange> operands = adaptor.getOperands();
    Location loc = op.getLoc();
    LogicValue input = getLogic(operands, 0);
    auto type = integerType(input.value);
    LogicValue result;
    switch (op.getKind()) {
    case sim::UnaryKind::Plus:
      result = input;
      break;
    case sim::UnaryKind::Negate: {
      Value unknown = isNonZero(rewriter, loc, input.unknown);
      Value negated = arith::SubIOp::create(
          rewriter, loc, zero(rewriter, loc, type), input.value);
      result = {
          select(rewriter, loc, unknown, zero(rewriter, loc, type), negated),
          select(rewriter, loc, unknown, ones(rewriter, loc, type),
                 zero(rewriter, loc, type))};
      break;
    }
    case sim::UnaryKind::BitNot: {
      Value inverted = bitNot(rewriter, loc, input.value);
      result = {arith::AndIOp::create(rewriter, loc, inverted,
                                      bitNot(rewriter, loc, input.unknown)),
                input.unknown};
      break;
    }
    case sim::UnaryKind::LogicalNot: {
      TruthValue truth = getTruth(rewriter, loc, input);
      Value resultValue =
          boolAnd(rewriter, loc, boolNot(rewriter, loc, truth.value),
                  boolNot(rewriter, loc, truth.unknown));
      result = {resultValue, truth.unknown};
      break;
    }
    }
    replaceLogic(op, result, rewriter);
    return success();
  }
};

class ReductionConversion final
    : public LogicOpConversion<sim::SimLogicReductionOp> {
public:
  using LogicOpConversion::LogicOpConversion;

  LogicalResult
  matchAndRewrite(sim::SimLogicReductionOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    ArrayRef<ValueRange> operands = adaptor.getOperands();
    Location loc = op.getLoc();
    LogicValue input = getLogic(operands, 0);
    Value hasUnknown = isNonZero(rewriter, loc, input.unknown);
    Value resultValue;
    Value resultUnknown;

    bool invert = op.getKind() == sim::ReductionKind::Nand ||
                  op.getKind() == sim::ReductionKind::Nor ||
                  op.getKind() == sim::ReductionKind::Xnor;
    if (op.getKind() == sim::ReductionKind::And ||
        op.getKind() == sim::ReductionKind::Nand) {
      Value knownZeros = arith::AndIOp::create(
          rewriter, loc, bitNot(rewriter, loc, input.value),
          bitNot(rewriter, loc, input.unknown));
      Value hasKnownZero = isNonZero(rewriter, loc, knownZeros);
      resultUnknown = boolAnd(rewriter, loc,
                              boolNot(rewriter, loc, hasKnownZero), hasUnknown);
      resultValue = boolAnd(rewriter, loc, boolNot(rewriter, loc, hasKnownZero),
                            boolNot(rewriter, loc, hasUnknown));
    } else if (op.getKind() == sim::ReductionKind::Or ||
               op.getKind() == sim::ReductionKind::Nor) {
      Value knownOnes = arith::AndIOp::create(
          rewriter, loc, input.value, bitNot(rewriter, loc, input.unknown));
      Value hasKnownOne = isNonZero(rewriter, loc, knownOnes);
      resultUnknown = boolAnd(rewriter, loc,
                              boolNot(rewriter, loc, hasKnownOne), hasUnknown);
      resultValue = hasKnownOne;
    } else {
      resultUnknown = hasUnknown;
      resultValue =
          select(rewriter, loc, hasUnknown, boolConstant(rewriter, loc, false),
                 parity(rewriter, loc, input.value));
    }
    if (invert)
      resultValue = arith::XOrIOp::create(
          rewriter, loc, resultValue, boolNot(rewriter, loc, resultUnknown));
    replaceLogic(op, {resultValue, resultUnknown}, rewriter);
    return success();
  }
};

class BinaryConversion final : public LogicOpConversion<sim::SimLogicBinaryOp> {
public:
  using LogicOpConversion::LogicOpConversion;

  LogicalResult
  matchAndRewrite(sim::SimLogicBinaryOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    ArrayRef<ValueRange> operands = adaptor.getOperands();
    Location loc = op.getLoc();
    LogicValue lhs = getLogic(operands, 0);
    LogicValue rhs = getLogic(operands, 1);
    auto type = integerType(lhs.value);

    if (op.getKind() == sim::BinaryKind::And ||
        op.getKind() == sim::BinaryKind::Or ||
        op.getKind() == sim::BinaryKind::Xor ||
        op.getKind() == sim::BinaryKind::Xnor) {
      Value lhsKnown = bitNot(rewriter, loc, lhs.unknown);
      Value rhsKnown = bitNot(rewriter, loc, rhs.unknown);
      Value resultValue;
      Value resultUnknown;
      if (op.getKind() == sim::BinaryKind::And) {
        Value knownZero = arith::OrIOp::create(
            rewriter, loc,
            arith::AndIOp::create(rewriter, loc,
                                  bitNot(rewriter, loc, lhs.value), lhsKnown),
            arith::AndIOp::create(rewriter, loc,
                                  bitNot(rewriter, loc, rhs.value), rhsKnown));
        Value knownOne = arith::AndIOp::create(
            rewriter, loc,
            arith::AndIOp::create(rewriter, loc, lhs.value, lhsKnown),
            arith::AndIOp::create(rewriter, loc, rhs.value, rhsKnown));
        resultUnknown =
            bitNot(rewriter, loc,
                   arith::OrIOp::create(rewriter, loc, knownZero, knownOne));
        resultValue = knownOne;
      } else if (op.getKind() == sim::BinaryKind::Or) {
        Value knownOne = arith::OrIOp::create(
            rewriter, loc,
            arith::AndIOp::create(rewriter, loc, lhs.value, lhsKnown),
            arith::AndIOp::create(rewriter, loc, rhs.value, rhsKnown));
        Value knownZero = arith::AndIOp::create(
            rewriter, loc,
            arith::AndIOp::create(rewriter, loc,
                                  bitNot(rewriter, loc, lhs.value), lhsKnown),
            arith::AndIOp::create(rewriter, loc,
                                  bitNot(rewriter, loc, rhs.value), rhsKnown));
        resultUnknown =
            bitNot(rewriter, loc,
                   arith::OrIOp::create(rewriter, loc, knownZero, knownOne));
        resultValue = knownOne;
      } else {
        resultUnknown =
            arith::OrIOp::create(rewriter, loc, lhs.unknown, rhs.unknown);
        Value computed =
            arith::XOrIOp::create(rewriter, loc, lhs.value, rhs.value);
        if (op.getKind() == sim::BinaryKind::Xnor)
          computed = bitNot(rewriter, loc, computed);
        resultValue = arith::AndIOp::create(
            rewriter, loc, computed, bitNot(rewriter, loc, resultUnknown));
      }
      replaceLogic(op, {resultValue, resultUnknown}, rewriter);
      return success();
    }

    Value anyUnknown =
        boolOr(rewriter, loc, isNonZero(rewriter, loc, lhs.unknown),
               isNonZero(rewriter, loc, rhs.unknown));
    Value computed;
    Value extraInvalid = boolConstant(rewriter, loc, false);
    switch (op.getKind()) {
    case sim::BinaryKind::Add:
      computed = arith::AddIOp::create(rewriter, loc, lhs.value, rhs.value);
      break;
    case sim::BinaryKind::Sub:
      computed = arith::SubIOp::create(rewriter, loc, lhs.value, rhs.value);
      break;
    case sim::BinaryKind::Mul:
      computed = arith::MulIOp::create(rewriter, loc, lhs.value, rhs.value);
      break;
    case sim::BinaryKind::UDiv:
    case sim::BinaryKind::UMod:
    case sim::BinaryKind::SDiv:
    case sim::BinaryKind::SMod: {
      Value divisorZero = isZero(rewriter, loc, rhs.value);
      extraInvalid = divisorZero;
      bool isSigned = op.getKind() == sim::BinaryKind::SDiv ||
                      op.getKind() == sim::BinaryKind::SMod;
      bool isRemainder = op.getKind() == sim::BinaryKind::UMod ||
                         op.getKind() == sim::BinaryKind::SMod;
      Value overflow = boolConstant(rewriter, loc, false);
      if (isSigned) {
        Value lhsIsMin = arith::CmpIOp::create(
            rewriter, loc, arith::CmpIPredicate::eq, lhs.value,
            integerConstant(rewriter, loc, type,
                            APInt::getSignedMinValue(type.getWidth())));
        Value rhsIsMinusOne =
            arith::CmpIOp::create(rewriter, loc, arith::CmpIPredicate::eq,
                                  rhs.value, ones(rewriter, loc, type));
        overflow = boolAnd(rewriter, loc, lhsIsMin, rhsIsMinusOne);
      }
      Value dangerous = boolOr(rewriter, loc, divisorZero, overflow);
      Value safeDivisor =
          select(rewriter, loc, dangerous,
                 integerConstant(rewriter, loc, type, 1), rhs.value);
      if (isSigned && isRemainder)
        computed =
            arith::RemSIOp::create(rewriter, loc, lhs.value, safeDivisor);
      else if (isSigned)
        computed =
            arith::DivSIOp::create(rewriter, loc, lhs.value, safeDivisor);
      else if (isRemainder)
        computed =
            arith::RemUIOp::create(rewriter, loc, lhs.value, safeDivisor);
      else
        computed =
            arith::DivUIOp::create(rewriter, loc, lhs.value, safeDivisor);
      if (isSigned) {
        Value wrapped = isRemainder ? zero(rewriter, loc, type) : lhs.value;
        computed = select(rewriter, loc, overflow, wrapped, computed);
      }
      break;
    }
    default:
      llvm_unreachable("bitwise binary kind handled above");
    }
    Value invalid = boolOr(rewriter, loc, anyUnknown, extraInvalid);
    replaceLogic(
        op,
        {select(rewriter, loc, invalid, zero(rewriter, loc, type), computed),
         select(rewriter, loc, invalid, ones(rewriter, loc, type),
                zero(rewriter, loc, type))},
        rewriter);
    return success();
  }
};

class LogicalConversion final
    : public LogicOpConversion<sim::SimLogicLogicalOp> {
public:
  using LogicOpConversion::LogicOpConversion;

  LogicalResult
  matchAndRewrite(sim::SimLogicLogicalOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    ArrayRef<ValueRange> operands = adaptor.getOperands();
    Location loc = op.getLoc();
    TruthValue lhs = getTruth(rewriter, loc, getLogic(operands, 0));
    TruthValue rhs = getTruth(rewriter, loc, getLogic(operands, 1));
    Value resultValue;
    Value resultUnknown;
    if (op.getKind() == sim::LogicalKind::And) {
      resultValue = boolAnd(rewriter, loc, lhs.value, rhs.value);
      Value knownFalse =
          boolOr(rewriter, loc,
                 boolAnd(rewriter, loc, boolNot(rewriter, loc, lhs.value),
                         boolNot(rewriter, loc, lhs.unknown)),
                 boolAnd(rewriter, loc, boolNot(rewriter, loc, rhs.value),
                         boolNot(rewriter, loc, rhs.unknown)));
      resultUnknown = boolNot(rewriter, loc,
                              boolOr(rewriter, loc, knownFalse, resultValue));
    } else {
      resultValue = boolOr(rewriter, loc, lhs.value, rhs.value);
      Value lhsFalse = boolAnd(rewriter, loc, boolNot(rewriter, loc, lhs.value),
                               boolNot(rewriter, loc, lhs.unknown));
      Value rhsFalse = boolAnd(rewriter, loc, boolNot(rewriter, loc, rhs.value),
                               boolNot(rewriter, loc, rhs.unknown));
      Value knownFalse = boolAnd(rewriter, loc, lhsFalse, rhsFalse);
      resultUnknown = boolNot(rewriter, loc,
                              boolOr(rewriter, loc, knownFalse, resultValue));
    }
    replaceLogic(op, {resultValue, resultUnknown}, rewriter);
    return success();
  }
};

class ShiftConversion final : public LogicOpConversion<sim::SimLogicShiftOp> {
public:
  using LogicOpConversion::LogicOpConversion;

  LogicalResult
  matchAndRewrite(sim::SimLogicShiftOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    ArrayRef<ValueRange> operands = adaptor.getOperands();
    Location loc = op.getLoc();
    LogicValue input = getLogic(operands, 0);
    Value amount = operands[1][0];
    Value amountUnknown = operands[1].size() == 2
                              ? isNonZero(rewriter, loc, operands[1][1])
                              : boolConstant(rewriter, loc, false);
    auto inputType = integerType(input.value);
    auto amountType = integerType(amount);
    unsigned checkWidth = std::max(
        amountType.getWidth(),
        std::max(2u, llvm::Log2_64_Ceil(
                         static_cast<uint64_t>(inputType.getWidth()) + 1) +
                         1));
    Value checkedAmount =
        resizeInteger(rewriter, loc, amount, checkWidth, false);
    auto checkType = integerType(checkedAmount);
    Value oversized = arith::CmpIOp::create(
        rewriter, loc, arith::CmpIPredicate::uge, checkedAmount,
        integerConstant(rewriter, loc, checkType, inputType.getWidth()));
    Value amountInInputWidth = resizeInteger(rewriter, loc, checkedAmount,
                                             inputType.getWidth(), false);
    Value safeAmount =
        select(rewriter, loc, oversized, zero(rewriter, loc, inputType),
               amountInInputWidth);

    LogicValue shifted;
    LogicValue oversizedResult{zero(rewriter, loc, inputType),
                               zero(rewriter, loc, inputType)};
    switch (op.getKind()) {
    case sim::ShiftKind::Left:
      shifted = {
          arith::ShLIOp::create(rewriter, loc, input.value, safeAmount),
          arith::ShLIOp::create(rewriter, loc, input.unknown, safeAmount)};
      break;
    case sim::ShiftKind::Right:
      shifted = {
          arith::ShRUIOp::create(rewriter, loc, input.value, safeAmount),
          arith::ShRUIOp::create(rewriter, loc, input.unknown, safeAmount)};
      break;
    case sim::ShiftKind::RightArith: {
      shifted = {
          arith::ShRSIOp::create(rewriter, loc, input.value, safeAmount),
          arith::ShRSIOp::create(rewriter, loc, input.unknown, safeAmount)};
      Value last =
          integerConstant(rewriter, loc, inputType, inputType.getWidth() - 1);
      oversizedResult = {
          arith::ShRSIOp::create(rewriter, loc, input.value, last),
          arith::ShRSIOp::create(rewriter, loc, input.unknown, last)};
      break;
    }
    }
    LogicValue widthSelected{
        select(rewriter, loc, oversized, oversizedResult.value, shifted.value),
        select(rewriter, loc, oversized, oversizedResult.unknown,
               shifted.unknown)};
    LogicValue x = canonicalUnknown(rewriter, loc, inputType);
    replaceLogic(
        op,
        {select(rewriter, loc, amountUnknown, x.value, widthSelected.value),
         select(rewriter, loc, amountUnknown, x.unknown,
                widthSelected.unknown)},
        rewriter);
    return success();
  }
};

class CompareConversion final
    : public LogicOpConversion<sim::SimLogicCompareOp> {
public:
  using LogicOpConversion::LogicOpConversion;

  LogicalResult
  matchAndRewrite(sim::SimLogicCompareOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    ArrayRef<ValueRange> operands = adaptor.getOperands();
    Location loc = op.getLoc();
    LogicValue lhs = getLogic(operands, 0);
    LogicValue rhs = getLogic(operands, 1);
    if (op.getKind() == sim::CompareKind::CaseEq ||
        op.getKind() == sim::CompareKind::CaseNe) {
      Value valuesEqual = arith::CmpIOp::create(
          rewriter, loc, arith::CmpIPredicate::eq, lhs.value, rhs.value);
      Value unknownsEqual = arith::CmpIOp::create(
          rewriter, loc, arith::CmpIPredicate::eq, lhs.unknown, rhs.unknown);
      Value equal = boolAnd(rewriter, loc, valuesEqual, unknownsEqual);
      if (op.getKind() == sim::CompareKind::CaseNe)
        equal = boolNot(rewriter, loc, equal);
      replaceInteger(op, equal, rewriter);
      return success();
    }
    if (op.getKind() == sim::CompareKind::WildEq ||
        op.getKind() == sim::CompareKind::WildNe ||
        op.getKind() == sim::CompareKind::CaseZEq ||
        op.getKind() == sim::CompareKind::CaseXZEq) {
      Value wildcard;
      if (op.getKind() == sim::CompareKind::WildEq ||
          op.getKind() == sim::CompareKind::WildNe) {
        wildcard = rhs.unknown;
      } else if (op.getKind() == sim::CompareKind::CaseZEq) {
        wildcard = arith::OrIOp::create(
            rewriter, loc,
            arith::AndIOp::create(rewriter, loc, lhs.unknown, lhs.value),
            arith::AndIOp::create(rewriter, loc, rhs.unknown, rhs.value));
      } else {
        wildcard =
            arith::OrIOp::create(rewriter, loc, lhs.unknown, rhs.unknown);
      }
      Value valueMismatch =
          arith::XOrIOp::create(rewriter, loc, lhs.value, rhs.value);
      Value mismatch;
      Value relevantUnknown =
          zero(rewriter, loc, integerType(lhs.unknown));
      if (op.getKind() == sim::CompareKind::WildEq ||
          op.getKind() == sim::CompareKind::WildNe) {
        Value compared = bitNot(rewriter, loc, rhs.unknown);
        mismatch = arith::AndIOp::create(
            rewriter, loc,
            arith::AndIOp::create(
                rewriter, loc, valueMismatch,
                bitNot(rewriter, loc, lhs.unknown)),
            compared);
        relevantUnknown =
            arith::AndIOp::create(rewriter, loc, lhs.unknown, compared);
      } else {
        Value unknownMismatch =
            arith::XOrIOp::create(rewriter, loc, lhs.unknown, rhs.unknown);
        mismatch = arith::AndIOp::create(
            rewriter, loc,
            arith::OrIOp::create(rewriter, loc, valueMismatch,
                                 unknownMismatch),
            bitNot(rewriter, loc, wildcard));
      }
      Value equal = isZero(rewriter, loc, mismatch);
      Value unknown = boolAnd(rewriter, loc, equal,
                              isNonZero(rewriter, loc, relevantUnknown));
      equal = boolAnd(rewriter, loc, equal,
                      boolNot(rewriter, loc, unknown));
      if (op.getKind() == sim::CompareKind::WildNe)
        equal = select(rewriter, loc, unknown, equal,
                       boolNot(rewriter, loc, equal));
      if (op.getKind() == sim::CompareKind::CaseZEq ||
          op.getKind() == sim::CompareKind::CaseXZEq)
        replaceInteger(op, equal, rewriter);
      else
        replaceLogic(op, {equal, unknown}, rewriter);
      return success();
    }

    if (op.getKind() == sim::CompareKind::Eq ||
        op.getKind() == sim::CompareKind::Ne) {
      Value unknownBits =
          arith::OrIOp::create(rewriter, loc, lhs.unknown, rhs.unknown);
      Value mismatchBits = arith::AndIOp::create(
          rewriter, loc,
          arith::XOrIOp::create(rewriter, loc, lhs.value, rhs.value),
          bitNot(rewriter, loc, unknownBits));
      Value knownMismatch = isNonZero(rewriter, loc, mismatchBits);
      Value unknown = boolAnd(
          rewriter, loc, isNonZero(rewriter, loc, unknownBits),
          boolNot(rewriter, loc, knownMismatch));
      Value value =
          op.getKind() == sim::CompareKind::Eq
              ? boolAnd(rewriter, loc,
                        boolNot(rewriter, loc, knownMismatch),
                        boolNot(rewriter, loc, unknown))
              : knownMismatch;
      replaceLogic(op, {value, unknown}, rewriter);
      return success();
    }

    arith::CmpIPredicate predicate;
    switch (op.getKind()) {
    case sim::CompareKind::Eq:
      predicate = arith::CmpIPredicate::eq;
      break;
    case sim::CompareKind::Ne:
      predicate = arith::CmpIPredicate::ne;
      break;
    case sim::CompareKind::ULT:
      predicate = arith::CmpIPredicate::ult;
      break;
    case sim::CompareKind::ULE:
      predicate = arith::CmpIPredicate::ule;
      break;
    case sim::CompareKind::UGT:
      predicate = arith::CmpIPredicate::ugt;
      break;
    case sim::CompareKind::UGE:
      predicate = arith::CmpIPredicate::uge;
      break;
    case sim::CompareKind::SLT:
      predicate = arith::CmpIPredicate::slt;
      break;
    case sim::CompareKind::SLE:
      predicate = arith::CmpIPredicate::sle;
      break;
    case sim::CompareKind::SGT:
      predicate = arith::CmpIPredicate::sgt;
      break;
    case sim::CompareKind::SGE:
      predicate = arith::CmpIPredicate::sge;
      break;
    default:
      llvm_unreachable("deterministic comparisons handled above");
    }
    Value compared =
        arith::CmpIOp::create(rewriter, loc, predicate, lhs.value, rhs.value);
    Value unknown = boolOr(rewriter, loc, isNonZero(rewriter, loc, lhs.unknown),
                           isNonZero(rewriter, loc, rhs.unknown));
    replaceLogic(op,
                 {select(rewriter, loc, unknown,
                         boolConstant(rewriter, loc, false), compared),
                  unknown},
                 rewriter);
    return success();
  }
};

class ConcatConversion final : public LogicOpConversion<sim::SimLogicConcatOp> {
public:
  using LogicOpConversion::LogicOpConversion;

  LogicalResult
  matchAndRewrite(sim::SimLogicConcatOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    ArrayRef<ValueRange> operands = adaptor.getOperands();
    Location loc = op.getLoc();
    auto resultType =
        rewriter.getIntegerType(op.getResult().getType().getWidth());
    Value resultValue = zero(rewriter, loc, resultType);
    Value resultUnknown = zero(rewriter, loc, resultType);
    unsigned offset = resultType.getWidth();
    for (ValueRange operand : operands) {
      LogicValue input{operand[0], operand[1]};
      offset -= integerType(input.value).getWidth();
      Value value = resizeInteger(rewriter, loc, input.value,
                                  resultType.getWidth(), false);
      Value unknown = resizeInteger(rewriter, loc, input.unknown,
                                    resultType.getWidth(), false);
      if (offset != 0) {
        Value amount = integerConstant(rewriter, loc, resultType, offset);
        value = arith::ShLIOp::create(rewriter, loc, value, amount);
        unknown = arith::ShLIOp::create(rewriter, loc, unknown, amount);
      }
      resultValue = arith::OrIOp::create(rewriter, loc, resultValue, value);
      resultUnknown =
          arith::OrIOp::create(rewriter, loc, resultUnknown, unknown);
    }
    replaceLogic(op, {resultValue, resultUnknown}, rewriter);
    return success();
  }
};

class ReplicateConversion final
    : public LogicOpConversion<sim::SimLogicReplicateOp> {
public:
  using LogicOpConversion::LogicOpConversion;

  LogicalResult
  matchAndRewrite(sim::SimLogicReplicateOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    ArrayRef<ValueRange> operands = adaptor.getOperands();
    Location loc = op.getLoc();
    LogicValue input = getLogic(operands, 0);
    auto resultType =
        rewriter.getIntegerType(op.getResult().getType().getWidth());
    Value wideValue =
        resizeInteger(rewriter, loc, input.value, resultType.getWidth(), false);
    Value wideUnknown = resizeInteger(rewriter, loc, input.unknown,
                                      resultType.getWidth(), false);
    Value resultValue = zero(rewriter, loc, resultType);
    Value resultUnknown = zero(rewriter, loc, resultType);
    uint64_t inputWidth = integerType(input.value).getWidth();
    uint64_t remaining = static_cast<uint64_t>(op.getCount());
    uint64_t chunkCopies = 1;
    uint64_t placedCopies = 0;
    LogicValue chunk{wideValue, wideUnknown};
    while (remaining != 0) {
      if (remaining & 1) {
        uint64_t offset = placedCopies * inputWidth;
        LogicValue placed = chunk;
        if (offset != 0) {
          Value amount = integerConstant(rewriter, loc, resultType, offset);
          placed = {
              arith::ShLIOp::create(rewriter, loc, chunk.value, amount),
              arith::ShLIOp::create(rewriter, loc, chunk.unknown, amount)};
        }
        resultValue =
            arith::OrIOp::create(rewriter, loc, resultValue, placed.value);
        resultUnknown =
            arith::OrIOp::create(rewriter, loc, resultUnknown, placed.unknown);
        placedCopies += chunkCopies;
      }

      remaining >>= 1;
      if (remaining == 0)
        break;
      uint64_t chunkWidth = chunkCopies * inputWidth;
      Value amount = integerConstant(rewriter, loc, resultType, chunkWidth);
      chunk = {arith::OrIOp::create(
                   rewriter, loc, chunk.value,
                   arith::ShLIOp::create(rewriter, loc, chunk.value, amount)),
               arith::OrIOp::create(rewriter, loc, chunk.unknown,
                                    arith::ShLIOp::create(
                                        rewriter, loc, chunk.unknown, amount))};
      chunkCopies <<= 1;
    }
    replaceLogic(op, {resultValue, resultUnknown}, rewriter);
    return success();
  }
};

class ExtractConversion final
    : public LogicOpConversion<sim::SimLogicExtractOp> {
public:
  using LogicOpConversion::LogicOpConversion;

  LogicalResult
  matchAndRewrite(sim::SimLogicExtractOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    ArrayRef<ValueRange> operands = adaptor.getOperands();
    Location loc = op.getLoc();
    LogicValue input = getLogic(operands, 0);
    auto inputType = integerType(input.value);
    unsigned resultWidth = op.getResult().getType().getWidth();
    uint64_t low = op.getLowBitAttr().getValue().getZExtValue();
    Value value = input.value;
    Value unknown = input.unknown;
    if (low != 0) {
      Value amount = integerConstant(rewriter, loc, inputType, low);
      value = arith::ShRUIOp::create(rewriter, loc, value, amount);
      unknown = arith::ShRUIOp::create(rewriter, loc, unknown, amount);
    }
    value = resizeInteger(rewriter, loc, value, resultWidth, false);
    unknown = resizeInteger(rewriter, loc, unknown, resultWidth, false);
    replaceLogic(op, {value, unknown}, rewriter);
    return success();
  }
};

class DynamicExtractConversion final
    : public LogicOpConversion<sim::SimLogicDynExtractOp> {
public:
  using LogicOpConversion::LogicOpConversion;

  LogicalResult
  matchAndRewrite(sim::SimLogicDynExtractOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    ArrayRef<ValueRange> operands = adaptor.getOperands();
    LogicValue input = getLogic(operands, 0);
    Value lowUnknown = operands[1].size() == 2 ? operands[1][1] : Value();
    FailureOr<LogicValue> result = dynamicExtract(
        rewriter, op.getLoc(), input.value, input.unknown, operands[1][0],
        lowUnknown, op.getResult().getType().getWidth(), true);
    if (failed(result))
      return rewriter.notifyMatchFailure(op, "padded selection width overflow");
    replaceLogic(op, *result, rewriter);
    return success();
  }
};

class BitsDynamicExtractConversion final
    : public LogicOpConversion<sim::SimBitsDynExtractOp> {
public:
  using LogicOpConversion::LogicOpConversion;

  LogicalResult
  matchAndRewrite(sim::SimBitsDynExtractOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    ArrayRef<ValueRange> operands = adaptor.getOperands();
    Value lowUnknown = operands[1].size() == 2 ? operands[1][1] : Value();
    FailureOr<LogicValue> result = dynamicExtract(
        rewriter, op.getLoc(), operands[0][0], Value(), operands[1][0],
        lowUnknown, op.getResult().getType().getWidth(), false);
    if (failed(result))
      return rewriter.notifyMatchFailure(op, "padded selection width overflow");
    replaceInteger(op, result->value, rewriter);
    return success();
  }
};

class InsertConversion final : public LogicOpConversion<sim::SimLogicInsertOp> {
public:
  using LogicOpConversion::LogicOpConversion;

  LogicalResult
  matchAndRewrite(sim::SimLogicInsertOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    ArrayRef<ValueRange> operands = adaptor.getOperands();
    Location loc = op.getLoc();
    LogicValue input = getLogic(operands, 0);
    LogicValue replacement = getLogic(operands, 1);
    auto type = integerType(input.value);
    unsigned replacementWidth = integerType(replacement.value).getWidth();
    uint64_t low = op.getLowBitAttr().getValue().getZExtValue();
    APInt replacementMask =
        APInt::getLowBitsSet(type.getWidth(), replacementWidth).shl(low);
    Value keepMask = integerConstant(rewriter, loc, type, ~replacementMask);
    auto insertPlane = [&](Value base, Value piece) -> Value {
      Value kept = arith::AndIOp::create(rewriter, loc, base, keepMask);
      Value wide = resizeInteger(rewriter, loc, piece, type.getWidth(), false);
      if (low != 0)
        wide = arith::ShLIOp::create(rewriter, loc, wide,
                                     integerConstant(rewriter, loc, type, low));
      return arith::OrIOp::create(rewriter, loc, kept, wide);
    };
    replaceLogic(op,
                 {insertPlane(input.value, replacement.value),
                  insertPlane(input.unknown, replacement.unknown)},
                 rewriter);
    return success();
  }
};

static bool containsLogic(Type type) {
  if (sim::isManagedHandleType(type))
    return false;
  bool contains = false;
  type.walk([&](sim::LogicType) { contains = true; });
  return contains;
}

static bool hasNoLogicTypesOrAttrs(Operation *op) {
  for (Type type : op->getOperandTypes())
    if (containsLogic(type))
      return false;
  for (Type type : op->getResultTypes())
    if (containsLogic(type))
      return false;
  for (NamedAttribute named : op->getAttrs()) {
    bool legal = true;
    named.getValue().walk([&](Type type) {
      if (containsLogic(type))
        legal = false;
    });
    if (!legal)
      return false;
  }
  for (Region &region : op->getRegions())
    for (Block &block : region)
      for (BlockArgument argument : block.getArguments())
        if (containsLogic(argument.getType()))
          return false;
  return true;
}

class ConvertObeliskSimValuesToStandardPass final
    : public impl::ConvertObeliskSimValuesToStandardPassBase<
          ConvertObeliskSimValuesToStandardPass> {
public:
  void runOnOperation() override {
    MLIRContext &context = getContext();
    SimulationToStandardTypeConverter converter;
    RewritePatternSet patterns(&context);
    populateSimulationToStandardPatterns(converter, patterns);

    ConversionTarget target(context);
    target.addIllegalOp<
        sim::SimLogicConstantOp, sim::SimLogicFromBitsOp, sim::SimLogicToBitsOp,
        sim::SimLogicIsTrueOp, sim::SimLogicMuxOp, sim::SimLogicCountBitsOp,
        sim::SimLogicClog2Op,
        sim::SimLogicResizeOp, sim::SimLogicUnaryOp, sim::SimLogicReductionOp,
        sim::SimLogicBinaryOp, sim::SimLogicLogicalOp, sim::SimLogicShiftOp,
        sim::SimLogicCompareOp, sim::SimLogicConcatOp, sim::SimLogicReplicateOp,
        sim::SimLogicExtractOp, sim::SimLogicDynExtractOp,
        sim::SimBitsDynExtractOp, sim::SimLogicInsertOp>();
    target.addDynamicallyLegalDialect<arith::ArithDialect, func::FuncDialect,
                                      cf::ControlFlowDialect, scf::SCFDialect,
                                      sim::ObeliskSimulationDialect>(
        hasNoLogicTypesOrAttrs);
    target.addDynamicallyLegalOp<ModuleOp>(hasNoLogicTypesOrAttrs);
    target.markUnknownOpDynamicallyLegal(hasNoLogicTypesOrAttrs);

    if (failed(
            applyFullConversion(getOperation(), target, std::move(patterns))))
      signalPassFailure();
  }
};

} // namespace

void populateSimulationPackedAggregateViewPatterns(
    const TypeConverter &converter, RewritePatternSet &patterns) {
  patterns.add<PackedAggregateViewConversion<sim::SimPackedFlattenOp>,
               PackedAggregateViewConversion<sim::SimPackedUnflattenOp>>(
      converter, patterns.getContext());
}

static void populateSimulationToStandardPatternsImpl(
    const TypeConverter &converter, RewritePatternSet &patterns,
    const llvm::DenseSet<Operation *> *provenTwoStateOperations) {
  patterns.add<ConstantConversion, FromBitsConversion, ToBitsConversion,
               IsTrueConversion, MuxConversion, CountBitsConversion,
               Clog2Conversion,
               ResizeConversion, UnaryConversion, ReductionConversion,
               BinaryConversion, LogicalConversion, ShiftConversion,
               CompareConversion, ConcatConversion, ReplicateConversion,
               ExtractConversion, DynamicExtractConversion,
               BitsDynamicExtractConversion, InsertConversion>(
      converter, patterns.getContext(), provenTwoStateOperations);
  patterns.add<BranchConversion, CondBranchConversion, SwitchConversion>(
      converter, patterns.getContext());
  patterns.add<FuncConversion, CallConversion, ReturnConversion>(
      converter, patterns.getContext());
  populateBranchOpInterfaceTypeConversionPattern(patterns, converter);
}

void populateSimulationToStandardPatterns(const TypeConverter &converter,
                                          RewritePatternSet &patterns) {
  populateSimulationToStandardPatternsImpl(converter, patterns, nullptr);
}

void populateSimulationToStandardPatterns(
    const TypeConverter &converter, RewritePatternSet &patterns,
    const llvm::DenseSet<Operation *> &provenTwoStateOperations) {
  populateSimulationToStandardPatternsImpl(converter, patterns,
                                           &provenTwoStateOperations);
}

} // namespace obelisk
