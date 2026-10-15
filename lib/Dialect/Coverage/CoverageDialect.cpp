//===- CoverageDialect.cpp - Coverage metadata ----------------------------===//

#include "obelisk/Dialect/Coverage/CoverageDialect.h"

#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/DialectImplementation.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/TypeSwitch.h"

using namespace mlir;
using namespace obelisk::cov;

#include "obelisk/Dialect/Coverage/CoverageDialect.cpp.inc"

#define GET_ATTRDEF_CLASSES
#include "obelisk/Dialect/Coverage/CoverageAttrs.cpp.inc"

void CoverageDialect::initialize() {
  addAttributes<
#define GET_ATTRDEF_LIST
#include "obelisk/Dialect/Coverage/CoverageAttrs.cpp.inc"
      >();
}

LogicalResult
CoverageDialect::verifyOperationAttribute(Operation *operation,
                                          NamedAttribute attribute) {
  if (failed(discardable::verify<
             Metrics, Config, PrefixMaps, SourceFiles, LanguageVersion,
             LinePointCount, ToggleBitCount, LinePointIndex,
             AssertionPointCount, AssertionPointIndex, ToggleObservable,
             ToggleBindings, ToggleInitialValue, ToggleInitialUnknown,
             FunctionalExpressionId, FunctionalExpressionOrdinal,
             FunctionalExpressionKind, FunctionalExpressionBitWidth,
             FunctionalExpressionSignedness, FunctionalExpressionPhase,
             FunctionalExpressionRole, FunctionalFormalId, FunctionalItemId,
             RetainedSymbols, BaseGroup, InheritedOriginType,
             InheritedOverridden, EffectiveTargetSymbol, EffectiveTargetSymbols,
             SourceAuthored, SourceType, TypeOptionConstant, FieldStorageType,
             BlockEventTargetId, BlockEventInstanceMethod, WithExpressionIds,
             WithExpressionOrdinals, WithCandidateValues, WithIteratorPath,
             CrossWithScalar, CrossWithTupleType, CrossWithSelectorId,
             CrossWithTargetPaths>(operation, attribute)))
    return failure();
  StringRef name = attribute.getName().strref();
  Attribute value = attribute.getValue();
  auto error = [&]() {
    return operation->emitOpError("attribute '") << name << "' ";
  };
  bool moduleOption =
      name == Metrics::getNameStr() || name == Config::getNameStr() ||
      name == PrefixMaps::getNameStr() || name == SourceFiles::getNameStr() ||
      name == LanguageVersion::getNameStr() ||
      name == LinePointCount::getNameStr() ||
      name == ToggleBitCount::getNameStr() ||
      name == AssertionPointCount::getNameStr();
  if (moduleOption && !isa<ModuleOp>(operation))
    return error() << "belongs on a module";
  if ((name == Metrics::getNameStr() || name == PrefixMaps::getNameStr() ||
       name == SourceFiles::getNameStr()) &&
      !llvm::all_of(cast<ArrayAttr>(value), llvm::IsaPred<StringAttr>))
    return error() << "must contain only strings";
  if (name == ToggleBindings::getNameStr() &&
      !llvm::all_of(cast<ArrayAttr>(value), llvm::IsaPred<ToggleBindingAttr>))
    return error() << "must contain only #coverage.toggle_binding entries";
  return success();
}

LogicalResult
ToggleBindingAttr::verify(function_ref<InFlightDiagnostic()> emitError,
                          uint64_t base, int64_t low, uint64_t width) {
  if (width == 0)
    return emitError() << "toggle binding must cover at least one bit";
  if (low < 0)
    return emitError() << "toggle binding must start at a nonnegative bit";
  return success();
}
