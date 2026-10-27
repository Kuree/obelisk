//===- RuntimeDialect.h - Typed runtime ABI dialect -----------*- C++ -*-===//

#ifndef TALOS_DIALECT_RUNTIME_RUNTIMEDIALECT_H
#define TALOS_DIALECT_RUNTIME_RUNTIMEDIALECT_H

#include "talos/Dialect/DiscardableAttributes.h"

#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/Dialect.h"
#include "mlir/IR/DialectImplementation.h"

#include "talos/Dialect/Runtime/RuntimeEnums.h.inc"

// The dialect's attribute helpers name its attribute classes.
#define GET_ATTRDEF_CLASSES
#include "talos/Dialect/Runtime/RuntimeAttrs.h.inc"

#include "talos/Dialect/Runtime/RuntimeDialect.h.inc"

namespace talos::runtime {

using BytecodeImage = TalosRuntimeDialect::BytecodeImageAttrHelper;
using DesignDatabase = TalosRuntimeDialect::DesignDatabaseAttrHelper;
using ExecutionFlagSet = TalosRuntimeDialect::ExecutionFlagsAttrHelper;
using StateBits = TalosRuntimeDialect::StateBitsAttrHelper;
using SampledRanges = TalosRuntimeDialect::SampledRangesAttrHelper;
using Materialized = TalosRuntimeDialect::MaterializedAttrHelper;
using ClassBitstreamBlob = TalosRuntimeDialect::ClassBitstreamBlobAttrHelper;
using CoverageSchemaBlob = TalosRuntimeDialect::CoverageSchemaBlobAttrHelper;
using FeatureSet = TalosRuntimeDialect::FeaturesAttrHelper;
using BytecodeFunction = TalosRuntimeDialect::BytecodeFunctionAttrHelper;
using BytecodeScratchSize =
    TalosRuntimeDialect::BytecodeScratchSizeAttrHelper;
using BytecodeScratchAlignment =
    TalosRuntimeDialect::BytecodeScratchAlignmentAttrHelper;
using BytecodeTwoStateLogicRegisters =
    TalosRuntimeDialect::BytecodeTwoStateLogicRegistersAttrHelper;
using LLVMPrepared = TalosRuntimeDialect::LlvmPreparedAttrHelper;
using LLVMByteGlobals = TalosRuntimeDialect::LlvmByteGlobalsAttrHelper;

using discardable::get;
using discardable::has;
using discardable::mark;
using discardable::named;
using discardable::remove;
using discardable::set;

/// The execution flags of `module`, or none.
ExecutionFlags getExecutionFlags(mlir::Operation *module);
void setExecutionFlags(mlir::Operation *module, ExecutionFlags flags);

/// The optional runtime libraries `module` links, or none.
Features getFeatures(mlir::Operation *module);
/// Require or release `features`; an empty set removes the attribute.
void setFeatures(mlir::Operation *module, Features features, bool required);

} // namespace talos::runtime

#endif // TALOS_DIALECT_RUNTIME_RUNTIMEDIALECT_H
