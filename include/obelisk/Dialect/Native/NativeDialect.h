//===- NativeDialect.h - Native backend metadata ---------------*- C++ -*-===//

#ifndef OBELISK_DIALECT_NATIVE_NATIVEDIALECT_H
#define OBELISK_DIALECT_NATIVE_NATIVEDIALECT_H

#include "obelisk/Dialect/DiscardableAttributes.h"

#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Dialect.h"
#include "mlir/IR/DialectImplementation.h"

// The dialect's attribute helpers name its attribute classes.
#define GET_ATTRDEF_CLASSES
#include "obelisk/Dialect/Native/NativeAttrs.h.inc"

#include "obelisk/Dialect/Native/NativeDialect.h.inc"

namespace obelisk::native {

using OptimizationLevel = NativeDialect::OptimizationLevelAttrHelper;
using MaxStateDomainFunctions =
    NativeDialect::MaxStateDomainFunctionsAttrHelper;
using DebugTiming = NativeDialect::DebugTimingAttrHelper;
using ClosedExecutable = NativeDialect::ClosedExecutableAttrHelper;
using Partition = NativeDialect::PartitionAttrHelper;
using PartitionManifest = NativeDialect::PartitionManifestAttrHelper;
using PartitionManifests = NativeDialect::PartitionManifestsAttrHelper;
using ElementDefaults = NativeDialect::ElementDefaultsAttrHelper;
using PhysicalPartitionManifest =
    NativeDialect::PhysicalPartitionManifestAttrHelper;
using Frame = NativeDialect::FrameAttrHelper;
using FrameInputs = NativeDialect::FrameInputsAttrHelper;
using FrameCheckpoint = NativeDialect::FrameCheckpointAttrHelper;
using EntryKind = NativeDialect::EntryKindAttrHelper;
using ScratchSize = NativeDialect::ScratchSizeAttrHelper;
using ObserverWidth = NativeDialect::ObserverWidthAttrHelper;
using ObserverFourState = NativeDialect::ObserverFourStateAttrHelper;
using SpawnCaptureOffsets = NativeDialect::SpawnCaptureOffsetsAttrHelper;
using SpawnCaptureSize = NativeDialect::SpawnCaptureSizeAttrHelper;
using MethodArgumentSizes = NativeDialect::MethodArgumentSizesAttrHelper;
using MethodArgumentRoots = NativeDialect::MethodArgumentRootsAttrHelper;
using TableWait = NativeDialect::TableWaitAttrHelper;
using Unmanaged = NativeDialect::UnmanagedAttrHelper;
using SharedCallback = NativeDialect::SharedCallbackAttrHelper;
using RuntimePublicationCertified =
    NativeDialect::RuntimePublicationCertifiedAttrHelper;
using OwnerReleaseInstrumented =
    NativeDialect::OwnerReleaseInstrumentedAttrHelper;
using TwoStateBlockUnknowns = NativeDialect::TwoStateBlockUnknownsAttrHelper;
using DriverId = NativeDialect::DriverIdAttrHelper;
using DriverLow = NativeDialect::DriverLowAttrHelper;
using NetId = NativeDialect::NetIdAttrHelper;
using ExactDriverRange = NativeDialect::ExactDriverRangeAttrHelper;
using WholeDriver = NativeDialect::WholeDriverAttrHelper;
using GuardedBulkDrive = NativeDialect::GuardedBulkDriveAttrHelper;
using CleanBulkDrive = NativeDialect::CleanBulkDriveAttrHelper;
using BulkCopySourceAssumeClean =
    NativeDialect::BulkCopySourceAssumeCleanAttrHelper;
using GuardedRefStore = NativeDialect::GuardedRefStoreAttrHelper;
using InBoundsHandleOffset = NativeDialect::InBoundsHandleOffsetAttrHelper;
using ManagedRootRangeRecord = NativeDialect::ManagedRootRangeRecordAttrHelper;
using ManagedRootRangePushCheck =
    NativeDialect::ManagedRootRangePushCheckAttrHelper;
using ManagedRootActivationPrologue =
    NativeDialect::ManagedRootActivationPrologueAttrHelper;
using TransferredReferences = NativeDialect::TransferredReferencesAttrHelper;
using NoTransitionRoots = NativeDialect::NoTransitionRootsAttrHelper;
using StringGlobal = NativeDialect::StringGlobalAttrHelper;
using ScanPrefixGlobal = NativeDialect::ScanPrefixGlobalAttrHelper;
using FileScanPrefixGlobal = NativeDialect::FileScanPrefixGlobalAttrHelper;
using AggregateBitstreamPlanGlobal =
    NativeDialect::AggregateBitstreamPlanGlobalAttrHelper;
using ContainerPatternPlanGlobal =
    NativeDialect::ContainerPatternPlanGlobalAttrHelper;
using BoxTypeId = NativeDialect::BoxTypeIdAttrHelper;
using VpiLocalId = NativeDialect::VpiLocalIdAttrHelper;
using VpiCallId = NativeDialect::VpiCallIdAttrHelper;
using ContainerPatternPlanSize =
    NativeDialect::ContainerPatternPlanSizeAttrHelper;

using discardable::get;
using discardable::has;
using discardable::mark;
using discardable::named;
using discardable::remove;
using discardable::set;

} // namespace obelisk::native

#endif // OBELISK_DIALECT_NATIVE_NATIVEDIALECT_H
