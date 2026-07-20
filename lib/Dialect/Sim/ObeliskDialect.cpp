//===- ObeliskDialect.cpp - Obelisk simulation dialect -------------------===//

#include "obelisk/Dialect/Sim/ObeliskOps.h"

#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/TypeUtilities.h"

#include "llvm/ADT/ArrayRef.h"

#include <limits>

using namespace mlir;

#include "obelisk/Dialect/Sim/ObeliskDialect.cpp.inc"
#include "obelisk/Dialect/Sim/ObeliskEnums.cpp.inc"

#define GET_TYPEDEF_CLASSES
#include "obelisk/Dialect/Sim/ObeliskTypes.cpp.inc"

#define GET_OP_CLASSES
#include "obelisk/Dialect/Sim/ObeliskOps.cpp.inc"

namespace obelisk::ir {

void ObeliskDialect::initialize() {
  addTypes<
#define GET_TYPEDEF_LIST
#include "obelisk/Dialect/Sim/ObeliskTypes.cpp.inc"
      >();

  addOperations<
#define GET_OP_LIST
#include "obelisk/Dialect/Sim/ObeliskOps.cpp.inc"
      >();
}

LogicalResult
LogicType::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                  unsigned width) {
  if (width == 0)
    return emitError() << "logic width must be greater than zero";
  return success();
}

static FailureOr<uint64_t> getPackedBitWidth(Type type);

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
                      Type fields, bool expectUnion, bool requirePacked) {
  if (expectUnion && !isa<circt::hw::UnionType>(fields))
    return emitError() << "union fields must use !hw.union, got " << fields;
  if (!expectUnion && !isa<circt::hw::StructType>(fields))
    return emitError() << "struct fields must use !hw.struct, got " << fields;
  if (requirePacked && failed(getPackedBitWidth(fields)))
    return emitError() << "packed aggregate contains an unpacked field in "
                       << fields;
  return success();
}

LogicalResult
PackedStructType::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                         Type fields) {
  if (failed(verifyAggregateFields(emitError, fields, false, true)))
    return failure();
  if (cast<circt::hw::StructType>(fields).getElements().empty())
    return emitError() << "packed struct must contain at least one field";
  return success();
}

LogicalResult
UnpackedStructType::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                           Type fields) {
  return verifyAggregateFields(emitError, fields, false, false);
}

LogicalResult
PackedUnionType::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                        Type fields) {
  if (failed(verifyAggregateFields(emitError, fields, true, true)))
    return failure();
  auto elements = cast<circt::hw::UnionType>(fields).getElements();
  if (elements.empty())
    return emitError() << "packed union must contain at least one field";
  FailureOr<uint64_t> expectedWidth = getPackedBitWidth(elements.front().type);
  assert(succeeded(expectedWidth));
  for (auto field : elements.drop_front()) {
    FailureOr<uint64_t> width = getPackedBitWidth(field.type);
    assert(succeeded(width));
    if (*width != *expectedWidth)
      return emitError() << "packed union fields must have equal widths; field "
                         << field.name << " has width " << *width
                         << " but expected " << *expectedWidth;
  }
  return success();
}

LogicalResult
UnpackedUnionType::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                          Type fields) {
  return verifyAggregateFields(emitError, fields, true, false);
}

static FailureOr<uint64_t> getPackedBitWidth(Type type) {
  if (auto integer = dyn_cast<IntegerType>(type))
    return integer.getWidth();
  if (auto logic = dyn_cast<LogicType>(type))
    return logic.getWidth();
  if (isa<TimeType>(type))
    return uint64_t{64};
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
  if (auto array = dyn_cast<circt::hw::ArrayType>(type)) {
    if (array.getNumElements() == 0)
      return uint64_t{0};
    FailureOr<uint64_t> elementWidth =
        getPackedBitWidth(array.getElementType());
    if (failed(elementWidth) ||
        *elementWidth >
            std::numeric_limits<uint64_t>::max() / array.getNumElements())
      return failure();
    return *elementWidth * array.getNumElements();
  }
  if (auto packed = dyn_cast<PackedStructType>(type))
    return getPackedBitWidth(packed.getFields());
  if (auto packed = dyn_cast<PackedUnionType>(type))
    return getPackedBitWidth(packed.getFields());
  if (auto structure = dyn_cast<circt::hw::StructType>(type)) {
    uint64_t width = 0;
    for (auto field : structure.getElements()) {
      FailureOr<uint64_t> fieldWidth = getPackedBitWidth(field.type);
      if (failed(fieldWidth) ||
          *fieldWidth > std::numeric_limits<uint64_t>::max() - width)
        return failure();
      width += *fieldWidth;
    }
    return width;
  }
  if (auto unionType = dyn_cast<circt::hw::UnionType>(type)) {
    uint64_t width = 0;
    for (auto field : unionType.getElements()) {
      FailureOr<uint64_t> fieldWidth = getPackedBitWidth(field.type);
      if (failed(fieldWidth))
        return failure();
      width = std::max(width, *fieldWidth);
    }
    return width;
  }
  return failure();
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

SemanticFamily getSemanticFamily(SemanticKind kind) {
  unsigned encoded = static_cast<uint32_t>(kind);
  auto family = symbolizeSemanticFamily((encoded >> 24) & 0xf);
  assert(family && "invalid encoded SemanticKind family");
  return *family;
}

static std::optional<unsigned> decodeSemanticArity(SemanticKind kind,
                                                   unsigned shift) {
  unsigned encoded = static_cast<uint32_t>(kind);
  unsigned arity = (encoded >> shift) & 0xf;
  if (arity == 0xf)
    return std::nullopt;
  return arity;
}

template <typename EnumAttr>
static LogicalResult
verifySemanticEnumAttr(Operation *op, DictionaryAttr sourceAttrs,
                       StringRef name, bool optional = false) {
  Attribute attr = sourceAttrs ? sourceAttrs.get(name) : Attribute();
  if (!attr)
    return optional ? success()
                    : op->emitOpError()
                          << "opcode "
                          << stringifySemanticKind(
                                 cast<SemanticKindAttr>(op->getAttr("opcode"))
                                     .getValue())
                          << " requires source_attrs." << name;
  if (!isa<EnumAttr>(attr))
    return op->emitOpError()
           << "source_attrs." << name
           << " is not a valid target enum value, got " << attr;
  return success();
}

static LogicalResult verifySemanticMetadata(Operation *op, SemanticKind kind) {
  auto sourceAttrs = op->getAttrOfType<DictionaryAttr>("source_attrs");
  switch (kind) {
  case SemanticKind::Procedure:
    return verifySemanticEnumAttr<ProcessKindAttr>(op, sourceAttrs, "kind");
  case SemanticKind::ForkJoin:
    return verifySemanticEnumAttr<JoinKindAttr>(op, sourceAttrs, "kind");
  case SemanticKind::Net:
    return verifySemanticEnumAttr<NetKindAttr>(op, sourceAttrs, "kind");
  case SemanticKind::DetectEvent:
    return verifySemanticEnumAttr<EdgeKindAttr>(op, sourceAttrs, "edge");
  case SemanticKind::Assert:
  case SemanticKind::Assume:
  case SemanticKind::Cover:
    return verifySemanticEnumAttr<DeferAssertAttr>(op, sourceAttrs, "defer");
  case SemanticKind::SeverityBI:
    return verifySemanticEnumAttr<SeverityAttr>(op, sourceAttrs, "severity");
  case SemanticKind::FOpenBI:
    return verifySemanticEnumAttr<FileOpenModeAttr>(op, sourceAttrs, "mode",
                                                    true);
  case SemanticKind::FormatInt:
    if (failed(verifySemanticEnumAttr<IntegerFormatAttr>(op, sourceAttrs,
                                                         "format")) ||
        failed(verifySemanticEnumAttr<IntegerAlignmentAttr>(op, sourceAttrs,
                                                            "alignment")) ||
        failed(verifySemanticEnumAttr<IntegerPaddingAttr>(op, sourceAttrs,
                                                          "padding")))
      return failure();
    return success();
  case SemanticKind::FormatReal:
    if (failed(verifySemanticEnumAttr<RealFormatAttr>(op, sourceAttrs,
                                                      "format")) ||
        failed(verifySemanticEnumAttr<IntegerAlignmentAttr>(op, sourceAttrs,
                                                            "alignment")))
      return failure();
    return success();
  case SemanticKind::UArrayCmp:
  case SemanticKind::QueueCmp:
    return verifySemanticEnumAttr<ArrayCmpPredicateAttr>(op, sourceAttrs,
                                                         "predicate");
  case SemanticKind::StringCmp:
    return verifySemanticEnumAttr<StringCmpPredicateAttr>(op, sourceAttrs,
                                                          "predicate");
  case SemanticKind::DPIFunc: {
    auto directions =
        sourceAttrs
            ? dyn_cast_or_null<ArrayAttr>(sourceAttrs.get("dpi_arg_dirs"))
            : ArrayAttr();
    if (!directions)
      return op->emitOpError(
          "opcode func.dpi requires source_attrs.dpi_arg_dirs");
    for (Attribute direction : directions)
      if (!isa<DPIArgDirectionAttr>(direction))
        return op->emitOpError()
               << "source_attrs.dpi_arg_dirs contains invalid direction "
               << direction;
    return success();
  }
  default:
    return success();
  }
}

static LogicalResult verifySemanticOp(Operation *op, SemanticKind kind,
                                      SemanticFamily expectedFamily) {
  SemanticFamily actualFamily = getSemanticFamily(kind);
  if (actualFamily != expectedFamily)
    return op->emitOpError()
           << "opcode " << stringifySemanticKind(kind) << " belongs to the "
           << stringifySemanticFamily(actualFamily) << " family, not "
           << stringifySemanticFamily(expectedFamily);

  auto verifyArity = [&](std::optional<unsigned> expected, unsigned actual,
                         StringRef name) -> LogicalResult {
    if (expected && *expected != actual)
      return op->emitOpError()
             << "opcode " << stringifySemanticKind(kind) << " requires "
             << *expected << ' ' << name << (*expected == 1 ? "" : "s")
             << ", got " << actual;
    return success();
  };
  if (failed(verifyArity(decodeSemanticArity(kind, 20), op->getNumOperands(),
                         "operand")) ||
      failed(verifyArity(decodeSemanticArity(kind, 16), op->getNumResults(),
                         "result")) ||
      failed(verifyArity(decodeSemanticArity(kind, 12), op->getNumRegions(),
                         "region")))
    return failure();
  return verifySemanticMetadata(op, kind);
}

#define DEFINE_SEMANTIC_VERIFIER(Op, Family)                                   \
  LogicalResult Op::verify() {                                                 \
    return verifySemanticOp(*this, getOpcode(), SemanticFamily::Family);       \
  }

DEFINE_SEMANTIC_VERIFIER(SemanticValueOp, Value)
DEFINE_SEMANTIC_VERIFIER(SemanticEffectOp, Effect)
DEFINE_SEMANTIC_VERIFIER(SemanticRegionOp, Region)
DEFINE_SEMANTIC_VERIFIER(SemanticIsolatedRegionOp, IsolatedRegion)
DEFINE_SEMANTIC_VERIFIER(SemanticSymbolOp, Symbol)
DEFINE_SEMANTIC_VERIFIER(SemanticIsolatedSymbolOp, IsolatedSymbol)
DEFINE_SEMANTIC_VERIFIER(SemanticSymbolTableOp, SymbolTable)
DEFINE_SEMANTIC_VERIFIER(SemanticGraphOp, Graph)
DEFINE_SEMANTIC_VERIFIER(SemanticGraphSymbolOp, GraphSymbol)
DEFINE_SEMANTIC_VERIFIER(SemanticTerminatorOp, Terminator)

#undef DEFINE_SEMANTIC_VERIFIER

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
  for (Value input : getInputs())
    totalWidth += cast<LogicType>(input.getType()).getWidth();
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
