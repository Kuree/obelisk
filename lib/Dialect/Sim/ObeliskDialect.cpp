//===- ObeliskDialect.cpp - Obelisk simulation dialect -------------------===//

#include "obelisk/Dialect/Sim/ObeliskOps.h"

#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/TypeUtilities.h"

#include "llvm/ADT/ArrayRef.h"

#include <limits>
#include <optional>

using namespace mlir;

#include "obelisk/Dialect/Sim/ObeliskDialect.cpp.inc"
#include "obelisk/Dialect/Sim/ObeliskEnums.cpp.inc"

#define GET_TYPEDEF_CLASSES
#include "obelisk/Dialect/Sim/ObeliskTypes.cpp.inc"

#define GET_OP_CLASSES
#include "obelisk/Dialect/Sim/ObeliskOps.cpp.inc"

#define GET_OP_CLASSES
#include "obelisk/Dialect/Sim/ObeliskASTOps.cpp.inc"

namespace obelisk::ir {

namespace {

std::optional<uint64_t> getInclusiveRangeWidth(int64_t left, int64_t right) {
  uint64_t distance =
      left >= right
          ? static_cast<uint64_t>(left) - static_cast<uint64_t>(right)
          : static_cast<uint64_t>(right) - static_cast<uint64_t>(left);
  if (distance == std::numeric_limits<uint64_t>::max())
    return std::nullopt;
  return distance + 1;
}

LogicalResult
verifySourceRange(llvm::function_ref<InFlightDiagnostic()> emitError,
                  StringAttr startFile, uint32_t startLine,
                  uint32_t startColumn, StringAttr endFile, uint32_t endLine,
                  uint32_t endColumn) {
  if (startFile.getValue().empty() || endFile.getValue().empty())
    return emitError() << "source range files must not be empty";
  if (startLine == 0 || startColumn == 0 || endLine == 0 || endColumn == 0)
    return emitError() << "source range lines and columns are one-based";
  return success();
}

} // namespace

void ObeliskDialect::initialize() {
  addTypes<
#define GET_TYPEDEF_LIST
#include "obelisk/Dialect/Sim/ObeliskTypes.cpp.inc"
      >();

  addOperations<
#define GET_OP_LIST
#include "obelisk/Dialect/Sim/ObeliskOps.cpp.inc"
      >();

  addOperations<
#define GET_OP_LIST
#include "obelisk/Dialect/Sim/ObeliskASTOps.cpp.inc"
      >();
}

LogicalResult
IntegralType::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                     unsigned width, bool, bool, int64_t left, int64_t right,
                     SVIntegralFlavor) {
  if (width == 0)
    return emitError() << "integral width must be greater than zero";
  std::optional<uint64_t> rangeWidth = getInclusiveRangeWidth(left, right);
  if (!rangeWidth)
    return emitError() << "declared range width exceeds uint64_t";
  if (*rangeWidth != width)
    return emitError() << "declared range width " << *rangeWidth
                       << " does not match integral width " << width;
  return success();
}

LogicalResult
ErrorType::verify(llvm::function_ref<InFlightDiagnostic()> emitError, bool) {
  return emitError() << "error recovery type cannot appear in valid Obelisk IR";
}

LogicalResult
LogicType::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                  unsigned width) {
  if (width == 0)
    return emitError() << "logic width must be greater than zero";
  return success();
}

static FailureOr<uint64_t> getPackedBitWidth(Type type);
static bool isPackedType(Type type);

LogicalResult RangedPackedArrayType::verify(
    llvm::function_ref<InFlightDiagnostic()> emitError, Type elementType,
    int64_t left, int64_t right) {
  if (!getInclusiveRangeWidth(left, right))
    return emitError() << "packed array range width exceeds uint64_t";
  if (!isPackedType(elementType))
    return emitError() << "packed array element must be packed, got "
                       << elementType;
  return success();
}

LogicalResult
EnumType::verify(llvm::function_ref<InFlightDiagnostic()> emitError, StringAttr,
                 Type baseType) {
  if (!isPackedType(baseType))
    return emitError() << "enum base must be an integral type, got "
                       << baseType;
  return success();
}

LogicalResult SourceAggregateType::verify(
    llvm::function_ref<InFlightDiagnostic()> emitError, StringAttr,
    bool isPacked, bool isUnion, bool isTagged, bool isSigned, bool isFourState,
    bool isSoft, uint64_t bitWidth, uint64_t selectableWidth,
    uint64_t bitstreamWidth, uint32_t tagBits) {
  if (isTagged && !isUnion)
    return emitError() << "only a union can be tagged";
  if (isSoft && (!isPacked || !isUnion))
    return emitError() << "only a packed union can be soft";
  if (!isTagged && tagBits != 0)
    return emitError() << "only a tagged union can reserve tag bits";
  if (isPacked && (bitWidth != selectableWidth || bitWidth != bitstreamWidth))
    return emitError() << "packed aggregate widths must agree";
  if (!isPacked && (bitWidth != 0 || isSigned || isFourState || isSoft))
    return emitError() << "unpacked aggregate has packed-only metadata";
  return success();
}

LogicalResult
AssocArrayType::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                       Type keyType, Type, bool wildcardIndex) {
  if (wildcardIndex != isa<UntypedType>(keyType))
    return emitError()
           << "wildcard associative index must use !obelisk.untyped and "
              "typed indices must not";
  return success();
}

LogicalResult
SubroutineType::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                       Type signatureType, bool isTask) {
  auto signature = dyn_cast<FunctionType>(signatureType);
  if (!signature)
    return emitError() << "subroutine signature must be a function type";
  if (isTask && signature.getNumResults() != 0)
    return emitError() << "task signature must not have a result";
  if (!isTask && signature.getNumResults() != 1)
    return emitError() << "function signature must have exactly one result";
  return success();
}

LogicalResult
SourceRangeType::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                        StringAttr startFile, uint32_t startLine,
                        uint32_t startColumn, StringAttr endFile,
                        uint32_t endLine, uint32_t endColumn, StringAttr) {
  return verifySourceRange(emitError, startFile, startLine, startColumn,
                           endFile, endLine, endColumn);
}

LogicalResult
PackedArrayType::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                        Type elementType, unsigned size) {
  if (size == 0)
    return emitError() << "packed array size must be greater than zero";
  FailureOr<uint64_t> elementWidth = getPackedBitWidth(elementType);
  if (failed(elementWidth))
    return emitError() << "packed array element must be a packed type, got "
                       << elementType;
  if (*elementWidth > std::numeric_limits<uint64_t>::max() / size)
    return emitError() << "packed array bit width overflows uint64_t";
  return success();
}

static LogicalResult
verifyAggregateFields(llvm::function_ref<InFlightDiagnostic()> emitError,
                      DictionaryAttr fields, bool requirePacked) {
  for (NamedAttribute field : fields) {
    auto type = dyn_cast<TypeAttr>(field.getValue());
    if (!type)
      return emitError() << "aggregate field " << field.getName()
                         << " must contain a type attribute";
    if (requirePacked && failed(getPackedBitWidth(type.getValue())))
      return emitError() << "packed aggregate field " << field.getName()
                         << " has unpacked type " << type.getValue();
  }
  return success();
}

LogicalResult
PackedStructType::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                         DictionaryAttr fields) {
  if (failed(verifyAggregateFields(emitError, fields, true)))
    return failure();
  if (fields.empty())
    return emitError() << "packed struct must contain at least one field";
  return success();
}

LogicalResult
UnpackedStructType::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                           DictionaryAttr fields) {
  return verifyAggregateFields(emitError, fields, false);
}

LogicalResult
PackedUnionType::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                        DictionaryAttr fields) {
  if (failed(verifyAggregateFields(emitError, fields, true)))
    return failure();
  if (fields.empty())
    return emitError() << "packed union must contain at least one field";
  auto firstType = cast<TypeAttr>(fields.begin()->getValue()).getValue();
  FailureOr<uint64_t> expectedWidth = getPackedBitWidth(firstType);
  if (failed(expectedWidth))
    return emitError() << "packed union field " << fields.begin()->getName()
                       << " has an unrepresentable bit width";
  for (NamedAttribute field : llvm::drop_begin(fields)) {
    auto fieldType = cast<TypeAttr>(field.getValue()).getValue();
    FailureOr<uint64_t> width = getPackedBitWidth(fieldType);
    if (failed(width))
      return emitError() << "packed union field " << field.getName()
                         << " has an unrepresentable bit width";
    if (*width != *expectedWidth)
      return emitError() << "packed union fields must have equal widths; field "
                         << field.getName() << " has width " << *width
                         << " but expected " << *expectedWidth;
  }
  return success();
}

LogicalResult
UnpackedUnionType::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                          DictionaryAttr fields) {
  return verifyAggregateFields(emitError, fields, false);
}

static FailureOr<uint64_t> getPackedBitWidth(Type type) {
  if (auto integral = dyn_cast<IntegralType>(type))
    return integral.getWidth();
  if (auto integer = dyn_cast<IntegerType>(type))
    return integer.getWidth();
  if (auto logic = dyn_cast<LogicType>(type))
    return logic.getWidth();
  if (isa<TimeType>(type))
    return uint64_t{64};
  if (auto enumeration = dyn_cast<EnumType>(type))
    return getPackedBitWidth(enumeration.getBaseType());
  if (auto array = dyn_cast<RangedPackedArrayType>(type)) {
    std::optional<uint64_t> size =
        getInclusiveRangeWidth(array.getLeft(), array.getRight());
    FailureOr<uint64_t> elementWidth =
        getPackedBitWidth(array.getElementType());
    if (!size || failed(elementWidth) ||
        *elementWidth > std::numeric_limits<uint64_t>::max() / *size)
      return failure();
    return *elementWidth * *size;
  }
  if (auto array = dyn_cast<PackedArrayType>(type)) {
    if (array.getSize() == 0)
      return failure();
    FailureOr<uint64_t> elementWidth =
        getPackedBitWidth(array.getElementType());
    if (failed(elementWidth) ||
        *elementWidth > std::numeric_limits<uint64_t>::max() / array.getSize())
      return failure();
    return *elementWidth * array.getSize();
  }
  if (auto structure = dyn_cast<PackedStructType>(type)) {
    uint64_t width = 0;
    for (NamedAttribute field : structure.getFields()) {
      Type fieldType = cast<TypeAttr>(field.getValue()).getValue();
      FailureOr<uint64_t> fieldWidth = getPackedBitWidth(fieldType);
      if (failed(fieldWidth) ||
          *fieldWidth > std::numeric_limits<uint64_t>::max() - width)
        return failure();
      width += *fieldWidth;
    }
    return width;
  }
  if (auto unionType = dyn_cast<PackedUnionType>(type)) {
    uint64_t width = 0;
    for (NamedAttribute field : unionType.getFields()) {
      Type fieldType = cast<TypeAttr>(field.getValue()).getValue();
      FailureOr<uint64_t> fieldWidth = getPackedBitWidth(fieldType);
      if (failed(fieldWidth))
        return failure();
      width = std::max(width, *fieldWidth);
    }
    return width;
  }
  return failure();
}

static bool isPackedType(Type type) {
  if (isa<IntegralType, IntegerType, LogicType, TimeType, EnumType,
          PackedStructType, PackedUnionType>(type))
    return true;
  if (auto array = dyn_cast<PackedArrayType>(type))
    return isPackedType(array.getElementType());
  if (auto array = dyn_cast<RangedPackedArrayType>(type))
    return isPackedType(array.getElementType());
  if (auto aggregate = dyn_cast<SourceAggregateType>(type))
    return aggregate.getIsPacked();
  return false;
}

static LogicalResult requireSameType(Operation *op, Type lhs, Type rhs,
                                     StringRef roles) {
  if (lhs != rhs)
    return op->emitOpError() << roles << " must have identical types (got "
                             << lhs << " and " << rhs << ")";
  return success();
}

static LogicalResult verifyDynamicIndex(Operation *op, Type type) {
  if (!isa<IntegerType, LogicType>(type))
    return op->emitOpError()
           << "dynamic index must be a two- or four-state integer, got "
           << type;
  return success();
}

static LogicalResult verifyExtract(Operation *op, Type inputElement,
                                   Type resultElement, uint64_t lowBit) {
  FailureOr<uint64_t> inputWidth = getPackedBitWidth(inputElement);
  FailureOr<uint64_t> resultWidth = getPackedBitWidth(resultElement);
  if (failed(inputWidth))
    return op->emitOpError()
           << "input element must be packed, got " << inputElement;
  if (failed(resultWidth))
    return op->emitOpError()
           << "result element must be packed, got " << resultElement;
  if (lowBit > *inputWidth || *resultWidth > *inputWidth - lowBit)
    return op->emitOpError()
           << "part-select [" << (lowBit + *resultWidth - 1) << ":" << lowBit
           << "] exceeds input width " << *inputWidth;
  return success();
}

template <typename Range>
static LogicalResult verifyConcat(Operation *op, Range inputs,
                                  Type resultElement) {
  if (inputs.empty())
    return op->emitOpError("requires at least one input");
  uint64_t totalWidth = 0;
  for (Value input : inputs) {
    Type inputElement;
    if (auto reference = dyn_cast<RefType>(input.getType()))
      inputElement = reference.getElementType();
    else
      inputElement = cast<NetType>(input.getType()).getElementType();
    FailureOr<uint64_t> width = getPackedBitWidth(inputElement);
    if (failed(width))
      return op->emitOpError()
             << "input element must be packed, got " << inputElement;
    if (*width > std::numeric_limits<uint64_t>::max() - totalWidth)
      return op->emitOpError("concatenated bit width overflows uint64_t");
    totalWidth += *width;
  }
  FailureOr<uint64_t> resultWidth = getPackedBitWidth(resultElement);
  if (failed(resultWidth))
    return op->emitOpError()
           << "result element must be packed, got " << resultElement;
  if (totalWidth != *resultWidth)
    return op->emitOpError() << "input widths sum to " << totalWidth
                             << " but result width is " << *resultWidth;
  return success();
}

LogicalResult RefExtractOp::verify() {
  return verifyExtract(*this, getInput().getType().getElementType(),
                       getResult().getType().getElementType(), getLowBit());
}

LogicalResult RefDynExtractOp::verify() {
  if (failed(verifyDynamicIndex(*this, getLowBit().getType())))
    return failure();
  return verifyExtract(*this, getInput().getType().getElementType(),
                       getResult().getType().getElementType(), 0);
}

LogicalResult RefConcatOp::verify() {
  return verifyConcat(*this, getInputs(),
                      getResult().getType().getElementType());
}

LogicalResult NetExtractOp::verify() {
  return verifyExtract(*this, getInput().getType().getElementType(),
                       getResult().getType().getElementType(), getLowBit());
}

LogicalResult NetDynExtractOp::verify() {
  if (failed(verifyDynamicIndex(*this, getLowBit().getType())))
    return failure();
  return verifyExtract(*this, getInput().getType().getElementType(),
                       getResult().getType().getElementType(), 0);
}

LogicalResult NetConcatOp::verify() {
  return verifyConcat(*this, getInputs(),
                      getResult().getType().getElementType());
}

LogicalResult LogicConstantOp::verify() {
  auto type = cast<LogicType>(getResult().getType());
  if (getValueAttr().getValue().getBitWidth() != type.getWidth())
    return emitOpError() << "value attribute width must match result width "
                         << type.getWidth();
  if (getUnknownAttr().getValue().getBitWidth() != type.getWidth())
    return emitOpError() << "unknown attribute width must match result width "
                         << type.getWidth();
  return success();
}

LogicalResult LogicConcatOp::verify() {
  if (getInputs().empty())
    return emitOpError("requires at least one input");
  uint64_t totalWidth = 0;
  for (Value input : getInputs()) {
    uint64_t inputWidth = cast<LogicType>(input.getType()).getWidth();
    if (inputWidth > std::numeric_limits<uint64_t>::max() - totalWidth)
      return emitOpError("concatenated bit width overflows uint64_t");
    totalWidth += inputWidth;
  }
  auto resultWidth = cast<LogicType>(getResult().getType()).getWidth();
  if (totalWidth != resultWidth)
    return emitOpError() << "input widths sum to " << totalWidth
                         << " but result width is " << resultWidth;
  return success();
}

LogicalResult LogicCompareOp::verify() {
  if (getLhs().getType() != getRhs().getType())
    return emitOpError("operands must have identical types");
  bool isCase = getKind() == LogicCompareKind::CaseEq ||
                getKind() == LogicCompareKind::CaseNe ||
                getKind() == LogicCompareKind::CaseZEq ||
                getKind() == LogicCompareKind::CaseXZEq;
  if (isCase) {
    if (!getResult().getType().isSignlessInteger(1))
      return emitOpError("case comparisons must produce i1");
    return success();
  }
  auto resultType = dyn_cast<LogicType>(getResult().getType());
  if (!resultType || resultType.getWidth() != 1)
    return emitOpError("four-state comparison must produce !obelisk.logic<1>");
  return success();
}

LogicalResult LogicExtractOp::verify() {
  auto inputWidth = cast<LogicType>(getInput().getType()).getWidth();
  auto resultWidth = cast<LogicType>(getResult().getType()).getWidth();
  uint64_t lowBit = getLowBit();
  if (lowBit > inputWidth || resultWidth > inputWidth - lowBit)
    return emitOpError() << "part-select [" << (lowBit + resultWidth - 1) << ":"
                         << lowBit << "] exceeds input width " << inputWidth;
  return success();
}

LogicalResult LogicReplicateOp::verify() {
  auto inputWidth = cast<LogicType>(getInput().getType()).getWidth();
  auto resultWidth = cast<LogicType>(getResult().getType()).getWidth();
  if (resultWidth % inputWidth != 0)
    return emitOpError() << "result width " << resultWidth
                         << " is not a multiple of input width " << inputWidth;
  return success();
}

LogicalResult LogicShiftOp::verify() {
  auto kind = getKind();
  if (kind != LogicBinaryKind::ShiftLeft &&
      kind != LogicBinaryKind::ShiftRight &&
      kind != LogicBinaryKind::AShiftRight)
    return emitOpError("kind must be shift_left, shift_right, or ashift_right");
  if (getInput().getType() != getResult().getType())
    return emitOpError("input and result must have identical types");
  if (!isa<IntegerType, LogicType>(getAmount().getType()))
    return emitOpError("shift amount must be a two- or four-state integer");
  return success();
}

LogicalResult LogicInsertOp::verify() {
  if (failed(requireSameType(*this, getInput().getType(), getResult().getType(),
                             "input and result")))
    return failure();
  auto inputWidth = cast<LogicType>(getInput().getType()).getWidth();
  auto replacementWidth =
      cast<LogicType>(getReplacement().getType()).getWidth();
  uint64_t lowBit = getLowBit();
  if (lowBit > inputWidth || replacementWidth > inputWidth - lowBit)
    return emitOpError() << "replacement at bit " << lowBit
                         << " exceeds input width " << inputWidth;
  return success();
}

LogicalResult LogicFromBitsOp::verify() {
  unsigned inputWidth = getInput().getType().getIntOrFloatBitWidth();
  unsigned resultWidth = cast<LogicType>(getResult().getType()).getWidth();
  if (inputWidth != resultWidth)
    return emitOpError() << "input width " << inputWidth
                         << " does not match result width " << resultWidth;
  return success();
}

LogicalResult LogicToBitsOp::verify() {
  unsigned inputWidth = cast<LogicType>(getInput().getType()).getWidth();
  unsigned resultWidth = getResult().getType().getIntOrFloatBitWidth();
  if (inputWidth != resultWidth)
    return emitOpError() << "input width " << inputWidth
                         << " does not match result width " << resultWidth;
  return success();
}

} // namespace obelisk::ir
