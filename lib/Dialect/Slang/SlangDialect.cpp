//===- SlangDialect.cpp - Elaborated slang semantic AST dialect ----------===//

#include "obelisk/Dialect/Slang/SlangOps.h"

#include "mlir/IR/Diagnostics.h"

#include "llvm/ADT/SmallSet.h"

#include <limits>
#include <optional>

using namespace mlir;

#include "obelisk/Dialect/Slang/SlangDialect.cpp.inc"
#include "obelisk/Dialect/Slang/SlangEnums.cpp.inc"

#define GET_TYPEDEF_CLASSES
#include "obelisk/Dialect/Slang/SlangTypes.cpp.inc"

#define GET_OP_CLASSES
#include "obelisk/Dialect/Slang/SlangOps.cpp.inc"

namespace obelisk::slangir {

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

bool isPackedType(Type type) {
  if (isa<IntegralType, EnumType>(type))
    return true;
  if (auto array = dyn_cast<PackedArrayType>(type))
    return isPackedType(array.getElementType());
  if (auto aggregate = dyn_cast<AggregateType>(type))
    return aggregate.getIsPacked();
  return false;
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

void SlangDialect::initialize() {
  addTypes<
#define GET_TYPEDEF_LIST
#include "obelisk/Dialect/Slang/SlangTypes.cpp.inc"
      >();

  addOperations<
#define GET_OP_LIST
#include "obelisk/Dialect/Slang/SlangOps.cpp.inc"
      >();
}

LogicalResult
IntegralType::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                     unsigned width, bool, bool, int64_t left, int64_t right,
                     IntegralFlavor) {
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
  return emitError() << "error recovery type cannot appear in valid Slang IR";
}

LogicalResult
PackedArrayType::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                        Type elementType, int64_t left, int64_t right) {
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

LogicalResult
AssociativeArrayType::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                             Type, Type indexType, bool wildcardIndex) {
  if (wildcardIndex != isa<UntypedType>(indexType))
    return emitError()
           << "wildcard associative index must use !slang.untyped and "
              "typed indices must not";
  return success();
}

LogicalResult AggregateType::verify(
    llvm::function_ref<InFlightDiagnostic()> emitError, StringAttr,
    bool isPacked, bool isUnion, bool isTagged, bool isSigned, bool isFourState,
    bool isSoft, uint64_t bitWidth, uint64_t selectableWidth,
    uint64_t bitstreamWidth, uint32_t tagBits, ArrayAttr fields) {
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
  llvm::SmallDenseSet<StringRef> names;
  for (auto [ordinal, attribute] : llvm::enumerate(fields)) {
    auto field = dyn_cast<DictionaryAttr>(attribute);
    auto name = field ? field.getAs<StringAttr>("name") : StringAttr{};
    auto type = field ? field.getAs<TypeAttr>("type") : TypeAttr{};
    auto index = field ? field.getAs<IntegerAttr>("ordinal") : IntegerAttr{};
    auto offset =
        field ? field.getAs<IntegerAttr>("packed_offset") : IntegerAttr{};
    if (!name || name.getValue().empty() || !type || !index || !offset)
      return emitError() << "aggregate fields require name, type, ordinal, and "
                            "packed_offset metadata";
    if (index.getValue().isNegative() ||
        index.getValue().getZExtValue() != ordinal)
      return emitError()
             << "aggregate field ordinals must be dense and ordered";
    if (offset.getValue().isNegative() ||
        (!isPacked && !offset.getValue().isZero()))
      return emitError() << "aggregate field has invalid packed offset";
    if (!names.insert(name.getValue()).second)
      return emitError() << "aggregate field names must be unique";
  }
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

static uint64_t astBodySize(Operation *operation) {
  if (operation->getNumRegions() != 1 || operation->getRegion(0).empty())
    return 0;
  return static_cast<uint64_t>(
      std::distance(operation->getRegion(0).front().begin(),
                    operation->getRegion(0).front().end()));
}

static LogicalResult verifyFlags(Operation *operation, ArrayRef<int64_t> flags,
                                 uint64_t expected, StringRef name,
                                 uint64_t &setCount) {
  if (flags.size() != expected)
    return operation->emitOpError()
           << name << " must contain one entry per inventory item";
  setCount = 0;
  for (int64_t flag : flags) {
    if (flag != 0 && flag != 1)
      return operation->emitOpError()
             << name << " entries must be zero or one";
    setCount += flag;
  }
  return success();
}

static LogicalResult addInventory(Operation *operation, uint64_t amount,
                                  uint64_t &total, StringRef name) {
  if (amount > std::numeric_limits<uint64_t>::max() - total)
    return operation->emitOpError() << name << " inventory overflows";
  total += amount;
  return success();
}

LogicalResult ConditionalStatementOp::verify() {
  uint64_t patterns = 0;
  if (failed(verifyFlags(*this, getConditionPatternFlags(),
                         getConditionCount(), "condition_pattern_flags",
                         patterns)))
    return failure();
  if (getConditionCount() == 0)
    return emitOpError("must contain at least one condition");
  uint64_t expected = getConditionCount();
  if (failed(addInventory(*this, patterns, expected,
                          "condition and statement")) ||
      failed(addInventory(*this, 1, expected, "condition and statement")) ||
      failed(addInventory(*this, getHasElse(), expected,
                          "condition and statement")))
    return failure();
  if (astBodySize(*this) != expected)
    return emitOpError("malformed condition and statement inventory");
  return success();
}

LogicalResult CaseStatementOp::verify() {
  if (getItemLabelCounts().size() != getItemCount())
    return emitOpError(
        "item_label_counts must contain one entry per case item");
  uint64_t labels = 0;
  for (int64_t count : getItemLabelCounts()) {
    if (count <= 0)
      return emitOpError("every case item must contain at least one label");
    if (static_cast<uint64_t>(count) >
        std::numeric_limits<uint64_t>::max() - labels)
      return emitOpError("case item label inventory overflows");
    labels += static_cast<uint64_t>(count);
  }
  uint64_t expected = 1;
  if (failed(addInventory(*this, getItemCount(), expected, "case item")) ||
      failed(addInventory(*this, getHasDefault(), expected, "case item")) ||
      failed(addInventory(*this, labels, expected, "case item")))
    return failure();
  if (astBodySize(*this) != expected)
    return emitOpError("malformed case item inventory");
  return success();
}

LogicalResult PatternCaseStatementOp::verify() {
  if (getConditionKind() == CaseCondition::Inside)
    return emitOpError(
        "pattern case cannot use the case-inside matching mode");
  uint64_t filters = 0;
  if (failed(verifyFlags(*this, getItemFilterFlags(), getItemCount(),
                         "item_filter_flags", filters)))
    return failure();
  uint64_t expected = 1;
  if (failed(addInventory(*this, getItemCount(), expected,
                          "pattern case item")) ||
      failed(addInventory(*this, getItemCount(), expected,
                          "pattern case item")) ||
      failed(addInventory(*this, filters, expected, "pattern case item")) ||
      failed(addInventory(*this, getHasDefault(), expected,
                          "pattern case item")))
    return failure();
  if (astBodySize(*this) != expected)
    return emitOpError("malformed pattern case item inventory");
  return success();
}

LogicalResult InsideExpressionOp::verify() {
  if (getItemCount() == 0)
    return emitOpError("inside set must contain at least one item");
  uint64_t expected = 1;
  if (failed(addInventory(*this, getItemCount(), expected, "inside item")))
    return failure();
  if (astBodySize(*this) != expected)
    return emitOpError("malformed inside item inventory");
  return success();
}

LogicalResult ValueRangeExpressionOp::verify() {
  if (astBodySize(*this) != 2)
    return emitOpError("value range must contain exactly two endpoints");
  return success();
}

LogicalResult StructurePatternOp::verify() {
  if (getFieldOrdinals().size() != astBodySize(*this))
    return emitOpError("field_ordinals must contain one entry per pattern");
  llvm::SmallDenseSet<int64_t> ordinals;
  for (int64_t ordinal : getFieldOrdinals())
    if (ordinal < 0 || !ordinals.insert(ordinal).second)
      return emitOpError(
          "structure pattern field ordinals must be nonnegative and unique");
  return success();
}

LogicalResult VariablePatternOp::verify() {
  if (getReferencedPath().empty())
    return emitOpError("pattern variable must have a resolved binding path");
  if (getFieldOrdinalAttr() || getPackedOffsetAttr())
    return emitOpError("pattern variable cannot carry field metadata");
  if (astBodySize(*this) != 0)
    return emitOpError("pattern variable cannot contain nested patterns");
  return success();
}

LogicalResult TaggedPatternOp::verify() {
  if (getReferencedPath().empty())
    return emitOpError("tagged pattern must have a resolved member path");
  auto ordinal = getFieldOrdinalAttr();
  auto offset = getPackedOffsetAttr();
  if (!ordinal || ordinal.getValue().isNegative())
    return emitOpError("tagged pattern must have a nonnegative field ordinal");
  if (!offset || offset.getValue().isNegative())
    return emitOpError("tagged pattern must have a nonnegative packed offset");
  if (astBodySize(*this) > 1)
    return emitOpError("tagged pattern can contain at most one nested pattern");
  return success();
}

} // namespace obelisk::slangir
