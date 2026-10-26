//===- SimulationOps.h - Executable simulation operations -------*- C++ -*-===//

#ifndef OBELISK_DIALECT_SIMULATION_SIMULATIONOPS_H
#define OBELISK_DIALECT_SIMULATION_SIMULATIONOPS_H

#include "obelisk/Dialect/Coverage/CoverageDialect.h"
#include "obelisk/Dialect/DPI/DPIAttrs.h"
#include "obelisk/Dialect/Native/NativeDialect.h"
#include "obelisk/Dialect/Runtime/RuntimeTypes.h"
#include "obelisk/Dialect/Schedule/ScheduleAttrs.h"
#include "obelisk/Dialect/Schedule/ScheduleFieldEnums.h"
#include "obelisk/Dialect/Simulation/SimulationAttrs.h"
#include "obelisk/Dialect/Simulation/SimulationDialect.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationTypes.h"

#include "mlir/IR/BuiltinTypes.h"
#include "mlir/Interfaces/CallInterfaces.h"
#include "mlir/Interfaces/CastInterfaces.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "mlir/Interfaces/FunctionInterfaces.h"
#include "mlir/Interfaces/InferIntRangeInterface.h"
#include "mlir/Interfaces/InferTypeOpInterface.h"
#include "mlir/Interfaces/MemorySlotInterfaces.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "mlir/Interfaces/ViewLikeInterface.h"

#include "obelisk/Dialect/Simulation/SimulationResources.h"
#include "obelisk/Dialect/Simulation/SimulationTraits.h"

#define GET_OP_CLASSES
#include "obelisk/Dialect/Simulation/SimulationOps.h.inc"

namespace obelisk::sim {

/// Return the element type of a dynamic array, queue, or associative array.
/// Returns a null type for every other input.
::mlir::Type getContainerElement(::mlir::Type type);

/// Unconditional semantic legality for inlining a simulation call.  These
/// rules are shared by every MLIR inlining client; profitability and growth
/// policy remain properties of the Obelisk-owned pass.
enum class InlineLegality {
  Legal,
  NotDefinedFunction,
  LateMetadata,
  Recursive,
  UnknownMetadata,
  UnfrozenDisplayScope,
  UnknownBoundaryMetadata,
  VPIFrameActivation,
};

bool hasLateInlineMetadata(SimDesignOp design);
InlineLegality getInlineCalleeLegality(SimFuncOp callee, bool calleeIsRecursive,
                                       bool designHasLateMetadata);
InlineLegality getInlineLegality(SimCallOp call, SimFuncOp callee);
/// Variant for clients that already computed call-graph SCC membership.
InlineLegality getInlineLegality(SimCallOp call, SimFuncOp callee,
                                 bool calleeIsRecursive);
/// Variant for clients that already cached callee-invariant legality.
InlineLegality getInlineLegality(SimCallOp call, SimFuncOp callee,
                                 InlineLegality calleeLegality);
::llvm::StringRef getInlineLegalityReason(InlineLegality legality);

/// Normalize direct instance-method calls to the ordinary zero-time call ABI.
/// This exposes their call edges to MLIR's call graph and inliner while class
/// receiver dispatch is still explicit in the IR.
::mlir::LogicalResult normalizeClassDirectCall(SimClassDirectCallOp call);
::mlir::LogicalResult normalizeClassDirectCalls(::mlir::Operation *root,
                                                uint64_t *count = nullptr);

/// Validate the transient source-name bindings frozen onto a prepared
/// simulation function. This is shared by the operation verifier and passes
/// that must remain defensive when verification between passes is disabled.
::mlir::LogicalResult verifyUnitBindings(SimFuncOp function);

/// Return the source path carried by any supported unit-binding attribute.
/// Returns an empty string for an unrelated attribute.
::llvm::StringRef getUnitBindingPath(::mlir::Attribute binding);

/// Materialize one verified elaborated constant as normalized simulation SSA.
/// Packed aggregate shape is restored after creating its scalar value planes.
::mlir::FailureOr<::mlir::Value>
materializeFrozenConstant(::mlir::OpBuilder &builder, ::mlir::Location location,
                          FrozenConstantAttr constant);

/// Whether materializeKnownZero supports `type`: packed scalars and fixed
/// unpacked arrays and structs of them, but not other state such as strings,
/// handles and unpacked unions.
bool hasKnownZero(::mlir::Type type);

/// Materialize a value of `type` whose every bit is a known zero. Fails
/// unless hasKnownZero(type).
::mlir::FailureOr<::mlir::Value>
materializeKnownZero(::mlir::OpBuilder &builder, ::mlir::Location location,
                     ::mlir::Type type);

/// Whether an operation transfers execution to a scheduler continuation.
bool isSuspensionOp(::mlir::Operation *operation);

/// Whether a suspension always ends the activation and returns control to the
/// scheduler before the process resumes. Task calls, joins, and blocking
/// mailbox or semaphore operations may continue without a scheduler pass.
bool returnsToScheduler(::mlir::Operation *operation);

/// Whether a spawn starts the evaluator of a pending deferred assertion
/// report: it carries the ticket of a deferred enqueue (IEEE 1800-2023
/// 16.4.1).
bool isDeferredReportSpawn(SimSpawnOp spawn);

/// Whether an operation is a cold exit to the runtime from generated
/// combinational evaluation: a status, termination, or output effect, a read
/// of runtime-owned sampled history, or a pending deferred assertion report.
/// A generated eval closure takes a checkpoint before its block instead of
/// calling the runtime.
bool isColdRuntimeCheckpoint(::mlir::Operation *operation);

/// Whether a process is startup infrastructure that must reach its first
/// suspension or termination before ordinary initial actors begin.
bool isStartupEntryKind(EntryKind kind);

/// Whether a source-visible code unit has a corresponding VPI object. Other
/// entry kinds are compiler/runtime infrastructure and are intrinsically
/// internal even when they do not carry the optional `internal` marker.
bool isVPIVisibleEntryKind(EntryKind kind);

/// Marks a process of a startup entry kind that nonetheless begins by running
/// rather than by waiting, so the prepare pass spawns it among the initial
/// procedures instead of ahead of them. IEEE 1800-2017 9.2.2.1 leaves an
/// always procedure with no timing control no suspension to reach.
inline constexpr auto startupWithoutSuspensionAttrName =
    ::obelisk::schedule::Field::StartsWithoutWaiting;

/// Marks a settling process whose root spawn belongs to a feature-local
/// computed-event startup chain and must retain that frozen position.
inline constexpr auto computedEventStartupAttrName =
    ::obelisk::schedule::Field::ComputedEventStartup;

/// Number of ordinary wait entries required by a suspension operation.
/// Computed-observer waits use their own variable-sized record.
uint32_t getWaitEntryCount(::mlir::Operation *operation);

} // namespace obelisk::sim

#endif // OBELISK_DIALECT_SIMULATION_SIMULATIONOPS_H
