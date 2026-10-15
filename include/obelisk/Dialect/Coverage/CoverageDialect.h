//===- CoverageDialect.h - Coverage metadata -------------------*- C++ -*-===//

#ifndef OBELISK_DIALECT_COVERAGE_COVERAGEDIALECT_H
#define OBELISK_DIALECT_COVERAGE_COVERAGEDIALECT_H

#include "obelisk/Dialect/DiscardableAttributes.h"

#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/Dialect.h"
#include "mlir/IR/DialectImplementation.h"

#define GET_ATTRDEF_CLASSES
#include "obelisk/Dialect/Coverage/CoverageAttrs.h.inc"

#include "obelisk/Dialect/Coverage/CoverageDialect.h.inc"

namespace obelisk::cov {

using Metrics = CoverageDialect::MetricsAttrHelper;
using Config = CoverageDialect::ConfigAttrHelper;
using PrefixMaps = CoverageDialect::PrefixMapsAttrHelper;
using SourceFiles = CoverageDialect::SourceFilesAttrHelper;
using LanguageVersion = CoverageDialect::LanguageVersionAttrHelper;
using LinePointCount = CoverageDialect::LinePointCountAttrHelper;
using ToggleBitCount = CoverageDialect::ToggleBitCountAttrHelper;
using LinePointIndex = CoverageDialect::LinePointIndexAttrHelper;
using AssertionPointCount = CoverageDialect::AssertionPointCountAttrHelper;
using AssertionPointIndex = CoverageDialect::AssertionPointIndexAttrHelper;
using ToggleObservable = CoverageDialect::ToggleObservableAttrHelper;
using ToggleBindings = CoverageDialect::ToggleBindingsAttrHelper;
using FsmBindings = CoverageDialect::FsmBindingsAttrHelper;
using ToggleInitialValue = CoverageDialect::ToggleInitialValueAttrHelper;
using ToggleInitialUnknown = CoverageDialect::ToggleInitialUnknownAttrHelper;
using FunctionalExpressionId =
    CoverageDialect::FunctionalExpressionIdAttrHelper;
using FunctionalExpressionOrdinal =
    CoverageDialect::FunctionalExpressionOrdinalAttrHelper;
using FunctionalExpressionKind =
    CoverageDialect::FunctionalExpressionKindAttrHelper;
using FunctionalExpressionBitWidth =
    CoverageDialect::FunctionalExpressionBitWidthAttrHelper;
using FunctionalExpressionSignedness =
    CoverageDialect::FunctionalExpressionSignednessAttrHelper;
using FunctionalExpressionPhase =
    CoverageDialect::FunctionalExpressionPhaseAttrHelper;
using FunctionalExpressionRole =
    CoverageDialect::FunctionalExpressionRoleAttrHelper;
using FunctionalFormalId = CoverageDialect::FunctionalFormalIdAttrHelper;
using FunctionalItemId = CoverageDialect::FunctionalItemIdAttrHelper;
using RetainedSymbols = CoverageDialect::RetainedSymbolsAttrHelper;
using BaseGroup = CoverageDialect::BaseGroupAttrHelper;
using InheritedOriginType = CoverageDialect::InheritedOriginTypeAttrHelper;
using InheritedOverridden = CoverageDialect::InheritedOverriddenAttrHelper;
using EffectiveTargetSymbol = CoverageDialect::EffectiveTargetSymbolAttrHelper;
using EffectiveTargetSymbols =
    CoverageDialect::EffectiveTargetSymbolsAttrHelper;
using SourceAuthored = CoverageDialect::SourceAuthoredAttrHelper;
using SourceType = CoverageDialect::SourceTypeAttrHelper;
using TypeOptionConstant = CoverageDialect::TypeOptionConstantAttrHelper;
using FieldStorageType = CoverageDialect::FieldStorageTypeAttrHelper;
using BlockEventTargetId = CoverageDialect::BlockEventTargetIdAttrHelper;
using BlockEventInstanceMethod =
    CoverageDialect::BlockEventInstanceMethodAttrHelper;
using WithExpressionIds = CoverageDialect::WithExpressionIdsAttrHelper;
using WithExpressionOrdinals =
    CoverageDialect::WithExpressionOrdinalsAttrHelper;
using WithCandidateValues = CoverageDialect::WithCandidateValuesAttrHelper;
using WithIteratorPath = CoverageDialect::WithIteratorPathAttrHelper;
using CrossWithScalar = CoverageDialect::CrossWithScalarAttrHelper;
using CrossWithTupleType = CoverageDialect::CrossWithTupleTypeAttrHelper;
using CrossWithSelectorId = CoverageDialect::CrossWithSelectorIdAttrHelper;
using CrossWithTargetPaths = CoverageDialect::CrossWithTargetPathsAttrHelper;

using discardable::get;
using discardable::has;
using discardable::mark;
using discardable::named;
using discardable::remove;
using discardable::set;

} // namespace obelisk::cov

#endif // OBELISK_DIALECT_COVERAGE_COVERAGEDIALECT_H
