//===- SlangDialect.cpp - Elaborated slang semantic AST dialect ----------===//

#include "obelisk/Dialect/Slang/SlangOps.h"

#include "mlir/IR/Diagnostics.h"

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

LogicalResult
AggregateType::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                      StringAttr, bool isPacked, bool isUnion, bool isTagged,
                      bool isSigned, bool isFourState, bool isSoft,
                      uint64_t bitWidth, uint64_t selectableWidth,
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

} // namespace obelisk::slangir
