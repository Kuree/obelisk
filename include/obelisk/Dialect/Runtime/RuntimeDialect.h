//===- RuntimeDialect.h - Typed runtime ABI dialect -----------*- C++ -*-===//

#ifndef OBELISK_DIALECT_RUNTIME_RUNTIMEDIALECT_H
#define OBELISK_DIALECT_RUNTIME_RUNTIMEDIALECT_H

#include "obelisk/Dialect/DiscardableAttributes.h"

#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/Dialect.h"
#include "mlir/IR/DialectImplementation.h"

#include "obelisk/Dialect/Runtime/RuntimeEnums.h.inc"

// The dialect's attribute helpers name its attribute classes.
#define GET_ATTRDEF_CLASSES
#include "obelisk/Dialect/Runtime/RuntimeAttrs.h.inc"

#include "obelisk/Dialect/Runtime/RuntimeDialect.h.inc"

namespace obelisk::runtime {

using BytecodeImage = ObeliskRuntimeDialect::BytecodeImageAttrHelper;
using DesignDatabase = ObeliskRuntimeDialect::DesignDatabaseAttrHelper;
using ExecutionFlagSet = ObeliskRuntimeDialect::ExecutionFlagsAttrHelper;
using StateBits = ObeliskRuntimeDialect::StateBitsAttrHelper;
using SampledRanges = ObeliskRuntimeDialect::SampledRangesAttrHelper;
using Materialized = ObeliskRuntimeDialect::MaterializedAttrHelper;
using ClassBitstreamBlob = ObeliskRuntimeDialect::ClassBitstreamBlobAttrHelper;
using CoverageSchemaBlob = ObeliskRuntimeDialect::CoverageSchemaBlobAttrHelper;
using FeatureSet = ObeliskRuntimeDialect::FeaturesAttrHelper;
using BytecodeFunction = ObeliskRuntimeDialect::BytecodeFunctionAttrHelper;
using BytecodeScratchSize =
    ObeliskRuntimeDialect::BytecodeScratchSizeAttrHelper;
using BytecodeScratchAlignment =
    ObeliskRuntimeDialect::BytecodeScratchAlignmentAttrHelper;
using BytecodeTwoStateLogicRegisters =
    ObeliskRuntimeDialect::BytecodeTwoStateLogicRegistersAttrHelper;
using LLVMPrepared = ObeliskRuntimeDialect::LlvmPreparedAttrHelper;
using LLVMByteGlobals = ObeliskRuntimeDialect::LlvmByteGlobalsAttrHelper;

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

} // namespace obelisk::runtime

#endif // OBELISK_DIALECT_RUNTIME_RUNTIMEDIALECT_H
