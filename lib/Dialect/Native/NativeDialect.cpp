//===- NativeDialect.cpp - Native backend metadata ------------------------===//

#include "obelisk/Dialect/Native/NativeDialect.h"

#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/DialectImplementation.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/TypeSwitch.h"

using namespace mlir;
using namespace obelisk::native;

#include "obelisk/Dialect/Native/NativeDialect.cpp.inc"

#define GET_ATTRDEF_CLASSES
#include "obelisk/Dialect/Native/NativeAttrs.cpp.inc"

void NativeDialect::initialize() {
  addAttributes<
#define GET_ATTRDEF_LIST
#include "obelisk/Dialect/Native/NativeAttrs.cpp.inc"
      >();
}

template <typename... Elements> static bool allOf(ArrayAttr array) {
  return llvm::all_of(array, llvm::IsaPred<Elements...>);
}

LogicalResult
NativeDialect::verifyOperationAttribute(Operation *operation,
                                        NamedAttribute attribute) {
  if (failed(discardable::verify<
             OptimizationLevel, MaxStateDomainFunctions, DebugTiming,
             ClosedExecutable, Partition, PartitionManifest, PartitionManifests,
             ElementDefaults, PhysicalPartitionManifest, Frame, FrameInputs,
             FrameCheckpoint, EntryKind, ScratchSize, ObserverWidth,
             ObserverFourState, SpawnCaptureOffsets, SpawnCaptureSize,
             MethodArgumentSizes, MethodArgumentRoots, TableWait, Unmanaged,
             SharedCallback, RuntimePublicationCertified,
             OwnerReleaseInstrumented, TwoStateBlockUnknowns, DriverId,
             DriverLow, NetId, ExactDriverRange, WholeDriver, GuardedBulkDrive,
             CleanBulkDrive, BulkCopySourceAssumeClean, GuardedRefStore,
             InBoundsHandleOffset, ManagedRootRangeRecord,
             ManagedRootRangePushCheck, ManagedRootActivationPrologue,
             TransferredReferences, NoTransitionRoots, StringGlobal,
             ScanPrefixGlobal, FileScanPrefixGlobal,
             AggregateBitstreamPlanGlobal, ContainerPatternPlanGlobal,
             ContainerPatternPlanSize, BoxTypeId, VpiCallId, VpiLocalId>(
          operation, attribute)))
    return failure();
  StringRef name = attribute.getName().strref();
  Attribute value = attribute.getValue();
  auto error = [&]() {
    return operation->emitOpError("attribute '") << name << "' ";
  };
  bool moduleOption = name == OptimizationLevel::getNameStr() ||
                      name == MaxStateDomainFunctions::getNameStr() ||
                      name == DebugTiming::getNameStr() ||
                      name == ClosedExecutable::getNameStr() ||
                      name == PartitionManifests::getNameStr() ||
                      name == ElementDefaults::getNameStr() ||
                      name == PhysicalPartitionManifest::getNameStr() ||
                      name == FrameCheckpoint::getNameStr();
  if (moduleOption && !isa<ModuleOp>(operation))
    return error() << "belongs on a module";
  if ((name == PartitionManifest::getNameStr() ||
       name == PhysicalPartitionManifest::getNameStr()) &&
      !allOf<PartitionAttr>(cast<ArrayAttr>(value)))
    return error() << "must contain only #native.partition entries";
  if (name == PartitionManifests::getNameStr() &&
      !allOf<DesignPartitionsAttr>(cast<ArrayAttr>(value)))
    return error() << "must contain only #native.design_partitions entries";
  if (name == ElementDefaults::getNameStr() &&
      !allOf<ElementDefaultAttr>(cast<ArrayAttr>(value)))
    return error() << "must contain only #native.element_default entries";
  if (name == TwoStateBlockUnknowns::getNameStr() &&
      !allOf<DenseI64ArrayAttr>(cast<ArrayAttr>(value)))
    return error() << "must contain one index array per block";
  return success();
}

LogicalResult
ElementDefaultAttr::verify(function_ref<InFlightDiagnostic()> emitError,
                           uint64_t typeId, DenseI8ArrayAttr image) {
  if (typeId == 0)
    return emitError() << "element default requires a nonzero type ID";
  if (!image || image.empty())
    return emitError() << "element default requires a nonempty image";
  return success();
}

LogicalResult
PartitionAttr::verify(function_ref<InFlightDiagnostic()> emitError,
                      StringAttr id, ArrayAttr owners, ArrayAttr members,
                      ArrayAttr imports, ArrayAttr exports,
                      ArrayAttr dependencies) {
  if (!id || id.empty())
    return emitError() << "native partition requires an ID";
  if (owners && !allOf<StringAttr>(owners))
    return emitError() << "native partition owners must be strings";
  if (!allOf<FlatSymbolRefAttr>(members) ||
      !allOf<FlatSymbolRefAttr>(imports) || !allOf<FlatSymbolRefAttr>(exports))
    return emitError()
           << "native partition members, imports and exports must be symbols";
  if (!allOf<StringAttr>(dependencies))
    return emitError() << "native partition dependencies must be partition IDs";
  return success();
}

LogicalResult
DesignPartitionsAttr::verify(function_ref<InFlightDiagnostic()> emitError,
                             StringAttr design, ArrayAttr partitions) {
  if (!allOf<PartitionAttr>(partitions))
    return emitError()
           << "design partitions must contain only #native.partition entries";
  return success();
}

LogicalResult FrameAttr::verify(function_ref<InFlightDiagnostic()> emitError,
                                uint64_t size, uint64_t alignment,
                                uint64_t checksum,
                                DenseI32ArrayAttr continuations,
                                ArrayAttr fields) {
  if (alignment == 0 || (alignment & (alignment - 1)) != 0)
    return emitError() << "native frame alignment must be a power of two";
  if (!allOf<FrameFieldAttr>(fields))
    return emitError() << "native frame fields must be #native.frame_field";
  return success();
}

LogicalResult
FrameSiteAttr::verify(function_ref<InFlightDiagnostic()> emitError,
                      DenseI64ArrayAttr identity, ArrayAttr types) {
  if (identity.size() != 4)
    return emitError() << "native frame site identity has four words";
  if (!allOf<TypeAttr>(types))
    return emitError() << "native frame site types must be type attributes";
  return success();
}

LogicalResult
FrameInputsAttr::verify(function_ref<InFlightDiagnostic()> emitError,
                        FunctionType signature, DenseI64ArrayAttr identity,
                        DenseI64ArrayAttr abi, ArrayAttr continuations,
                        ArrayAttr captures) {
  if (identity.size() != 3 || abi.size() != 4)
    return emitError()
           << "native frame inputs have three identity and four ABI words";
  if (!allOf<FrameSiteAttr>(continuations))
    return emitError()
           << "native frame continuations must be #native.frame_site";
  return success();
}

LogicalResult
FrameCheckpointAttr::verify(function_ref<InFlightDiagnostic()> emitError,
                            Attribute layout, ArrayAttr functions) {
  if (!allOf<SymbolRefAttr>(functions))
    return emitError() << "native frame checkpoint lists function symbols";
  return success();
}
