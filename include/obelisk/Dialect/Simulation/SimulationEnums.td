//===- SimulationEnums.td - Executable simulation enumerations -*- td -*-===//

#ifndef OBELISK_DIALECT_SIMULATION_SIMULATIONENUMS_TD
#define OBELISK_DIALECT_SIMULATION_SIMULATIONENUMS_TD

include "mlir/IR/EnumAttr.td"

def SimEntryRootInitializer
    : I32EnumAttrCase<"RootInitializer", 0, "root_initializer">;
def SimEntryInitial : I32EnumAttrCase<"Initial", 1, "initial">;
def SimEntryFinal : I32EnumAttrCase<"Final", 2, "final">;
def SimEntryAlways : I32EnumAttrCase<"Always", 3, "always">;
def SimEntryAlwaysComb : I32EnumAttrCase<"AlwaysComb", 4, "always_comb">;
def SimEntryAlwaysFF : I32EnumAttrCase<"AlwaysFF", 5, "always_ff">;
def SimEntryAlwaysLatch : I32EnumAttrCase<"AlwaysLatch", 6, "always_latch">;
def SimEntryContinuous : I32EnumAttrCase<"Continuous", 7, "continuous">;
def SimEntryFunction : I32EnumAttrCase<"Function", 8, "function">;
def SimEntryKind : I32EnumAttr<"EntryKind", "simulation code-unit kind", [
  SimEntryRootInitializer, SimEntryInitial, SimEntryFinal, SimEntryAlways,
  SimEntryAlwaysComb, SimEntryAlwaysFF, SimEntryAlwaysLatch, SimEntryContinuous,
  SimEntryFunction
]> {
  let cppNamespace = "::obelisk::sim";
}

def SimCaptureContext : I32EnumAttrCase<"Context", 0, "context">;
def SimCaptureFormal : I32EnumAttrCase<"Formal", 1, "formal">;
def SimCaptureValue : I32EnumAttrCase<"Value", 2, "value">;
def SimCaptureStorage : I32EnumAttrCase<"Storage", 3, "storage">;
def SimCaptureNet : I32EnumAttrCase<"Net", 4, "net">;
def SimCaptureDriver : I32EnumAttrCase<"Driver", 5, "driver">;
def SimCaptureEvent : I32EnumAttrCase<"Event", 6, "event">;
def SimCaptureKind : I32EnumAttr<"CaptureKind", "entry argument binding kind", [
  SimCaptureContext, SimCaptureFormal, SimCaptureValue, SimCaptureStorage,
  SimCaptureNet, SimCaptureDriver, SimCaptureEvent
]> {
  let cppNamespace = "::obelisk::sim";
}

def SimLifetimeAutomatic : I32EnumAttrCase<"Automatic", 0, "automatic">;
def SimLifetimeStatic : I32EnumAttrCase<"Static", 1, "static">;
def SimLifetimeDesign : I32EnumAttrCase<"Design", 2, "design">;
def SimLifetime : I32EnumAttr<"Lifetime", "simulation object lifetime", [
  SimLifetimeAutomatic, SimLifetimeStatic, SimLifetimeDesign
]> {
  let cppNamespace = "::obelisk::sim";
}

def SimEdgeChange : I32EnumAttrCase<"Change", 0, "change">;
def SimEdgePosedge : I32EnumAttrCase<"Posedge", 1, "posedge">;
def SimEdgeNegedge : I32EnumAttrCase<"Negedge", 2, "negedge">;
def SimEdgeBoth : I32EnumAttrCase<"Both", 3, "both">;
def SimEdgeKind : I32EnumAttr<"EdgeKind", "suspension edge kind", [
  SimEdgeChange, SimEdgePosedge, SimEdgeNegedge, SimEdgeBoth
]> {
  let cppNamespace = "::obelisk::sim";
}

def SimUnaryPlus : I32EnumAttrCase<"Plus", 0, "plus">;
def SimUnaryNegate : I32EnumAttrCase<"Negate", 1, "negate">;
def SimUnaryBitNot : I32EnumAttrCase<"BitNot", 2, "bit_not">;
def SimUnaryLogicalNot : I32EnumAttrCase<"LogicalNot", 3, "logical_not">;
def SimUnaryKind
    : I32EnumAttr<"UnaryKind", "normalized four-state unary operation", [
      SimUnaryPlus, SimUnaryNegate, SimUnaryBitNot, SimUnaryLogicalNot
    ]> {
  let cppNamespace = "::obelisk::sim";
}

def SimReductionAnd : I32EnumAttrCase<"And", 0, "and">;
def SimReductionOr : I32EnumAttrCase<"Or", 1, "or">;
def SimReductionXor : I32EnumAttrCase<"Xor", 2, "xor">;
def SimReductionNand : I32EnumAttrCase<"Nand", 3, "nand">;
def SimReductionNor : I32EnumAttrCase<"Nor", 4, "nor">;
def SimReductionXnor : I32EnumAttrCase<"Xnor", 5, "xnor">;
def SimReductionKind
    : I32EnumAttr<"ReductionKind", "normalized four-state reduction", [
      SimReductionAnd, SimReductionOr, SimReductionXor, SimReductionNand,
      SimReductionNor, SimReductionXnor
    ]> {
  let cppNamespace = "::obelisk::sim";
}

def SimJoinAll : I32EnumAttrCase<"All", 0, "all">;
def SimJoinAny : I32EnumAttrCase<"Any", 1, "any">;
def SimJoinKind : I32EnumAttr<"JoinKind", "process join mode", [
  SimJoinAll, SimJoinAny
]> {
  let cppNamespace = "::obelisk::sim";
}

def SimBinaryAdd : I32EnumAttrCase<"Add", 0, "add">;
def SimBinarySub : I32EnumAttrCase<"Sub", 1, "sub">;
def SimBinaryMul : I32EnumAttrCase<"Mul", 2, "mul">;
def SimBinaryUDiv : I32EnumAttrCase<"UDiv", 3, "udiv">;
def SimBinarySDiv : I32EnumAttrCase<"SDiv", 4, "sdiv">;
def SimBinaryUMod : I32EnumAttrCase<"UMod", 5, "umod">;
def SimBinarySMod : I32EnumAttrCase<"SMod", 6, "smod">;
def SimBinaryAnd : I32EnumAttrCase<"And", 7, "and">;
def SimBinaryOr : I32EnumAttrCase<"Or", 8, "or">;
def SimBinaryXor : I32EnumAttrCase<"Xor", 9, "xor">;
def SimBinaryXnor : I32EnumAttrCase<"Xnor", 10, "xnor">;
def SimBinaryKind
    : I32EnumAttr<"BinaryKind", "normalized four-state binary operation", [
      SimBinaryAdd, SimBinarySub, SimBinaryMul, SimBinaryUDiv, SimBinarySDiv,
      SimBinaryUMod, SimBinarySMod, SimBinaryAnd, SimBinaryOr, SimBinaryXor,
      SimBinaryXnor
    ]> {
  let cppNamespace = "::obelisk::sim";
}

def SimLogicalAnd : I32EnumAttrCase<"And", 0, "and">;
def SimLogicalOr : I32EnumAttrCase<"Or", 1, "or">;
def SimLogicalKind : I32EnumAttr<"LogicalKind", "four-state logical operation",
                                 [SimLogicalAnd, SimLogicalOr]> {
  let cppNamespace = "::obelisk::sim";
}

def SimShiftLeft : I32EnumAttrCase<"Left", 0, "left">;
def SimShiftRight : I32EnumAttrCase<"Right", 1, "right">;
def SimShiftRightArith : I32EnumAttrCase<"RightArith", 2, "right_arith">;
def SimShiftKind
    : I32EnumAttr<"ShiftKind", "four-state shift operation",
                  [SimShiftLeft, SimShiftRight, SimShiftRightArith]> {
  let cppNamespace = "::obelisk::sim";
}

def SimCmpEq : I32EnumAttrCase<"Eq", 0, "eq">;
def SimCmpNe : I32EnumAttrCase<"Ne", 1, "ne">;
def SimCmpCaseEq : I32EnumAttrCase<"CaseEq", 2, "case_eq">;
def SimCmpCaseNe : I32EnumAttrCase<"CaseNe", 3, "case_ne">;
def SimCmpULT : I32EnumAttrCase<"ULT", 4, "ult">;
def SimCmpULE : I32EnumAttrCase<"ULE", 5, "ule">;
def SimCmpUGT : I32EnumAttrCase<"UGT", 6, "ugt">;
def SimCmpUGE : I32EnumAttrCase<"UGE", 7, "uge">;
def SimCmpSLT : I32EnumAttrCase<"SLT", 8, "slt">;
def SimCmpSLE : I32EnumAttrCase<"SLE", 9, "sle">;
def SimCmpSGT : I32EnumAttrCase<"SGT", 10, "sgt">;
def SimCmpSGE : I32EnumAttrCase<"SGE", 11, "sge">;
def SimCompareKind
    : I32EnumAttr<"CompareKind", "normalized comparison operation", [
      SimCmpEq, SimCmpNe, SimCmpCaseEq, SimCmpCaseNe, SimCmpULT, SimCmpULE,
      SimCmpUGT, SimCmpUGE, SimCmpSLT, SimCmpSLE, SimCmpSGT, SimCmpSGE
    ]> {
  let cppNamespace = "::obelisk::sim";
}

// Late compute-graph metadata uses dialect enums rather than stringly-typed
// dictionary fields. These values are part of the persisted analysis ABI.
def SimComputeResourceUnknown : I32EnumAttrCase<"Unknown", 0, "unknown">;
def SimComputeResourceStorage : I32EnumAttrCase<"Storage", 1, "storage">;
def SimComputeResourceNet : I32EnumAttrCase<"Net", 2, "net">;
def SimComputeResourceEvent : I32EnumAttrCase<"Event", 3, "event">;
def SimComputeResourceLocal : I32EnumAttrCase<"Local", 4, "local">;
def SimComputeResourceKind
    : I32EnumAttr<"ComputeResourceKind", "compute-graph resource kind", [
        SimComputeResourceUnknown, SimComputeResourceStorage,
        SimComputeResourceNet, SimComputeResourceEvent, SimComputeResourceLocal
      ]> {
  let cppNamespace = "::obelisk::sim";
}

def SimComputeTargetUnknown : I32EnumAttrCase<"Unknown", 0, "unknown">;
def SimComputeTargetDescriptor
    : I32EnumAttrCase<"Descriptor", 1, "descriptor">;
def SimComputeTargetFormal : I32EnumAttrCase<"Formal", 2, "formal">;
def SimComputeTargetLocal : I32EnumAttrCase<"Local", 3, "local">;
def SimComputeTargetKind
    : I32EnumAttr<"ComputeTargetKind", "compute-effect target kind", [
        SimComputeTargetUnknown, SimComputeTargetDescriptor,
        SimComputeTargetFormal, SimComputeTargetLocal
      ]> {
  let cppNamespace = "::obelisk::sim";
}

def SimComputeEffectRead : I32EnumAttrCase<"Read", 0, "read">;
def SimComputeEffectWrite : I32EnumAttrCase<"Write", 1, "write">;
def SimComputeEffectDrive : I32EnumAttrCase<"Drive", 2, "drive">;
def SimComputeEffectWatch : I32EnumAttrCase<"Watch", 3, "watch">;
def SimComputeEffectNBA : I32EnumAttrCase<"NBA", 4, "nba">;
def SimComputeEffectTrigger : I32EnumAttrCase<"Trigger", 5, "trigger">;
def SimComputeEffectKind
    : I32EnumAttr<"ComputeEffectKind", "compute-graph effect kind", [
        SimComputeEffectRead, SimComputeEffectWrite, SimComputeEffectDrive,
        SimComputeEffectWatch, SimComputeEffectNBA, SimComputeEffectTrigger
      ]> {
  let cppNamespace = "::obelisk::sim";
}

def SimComputeTriggerNone : I32EnumAttrCase<"None", 0, "none">;
def SimComputeTriggerChange : I32EnumAttrCase<"Change", 1, "change">;
def SimComputeTriggerPosedge : I32EnumAttrCase<"Posedge", 2, "posedge">;
def SimComputeTriggerNegedge : I32EnumAttrCase<"Negedge", 3, "negedge">;
def SimComputeTriggerBoth : I32EnumAttrCase<"Both", 4, "both">;
def SimComputeTriggerEvent : I32EnumAttrCase<"Event", 5, "event">;
def SimComputeTriggerKind
    : I32EnumAttr<"ComputeTriggerKind", "compute-graph trigger kind", [
        SimComputeTriggerNone, SimComputeTriggerChange, SimComputeTriggerPosedge,
        SimComputeTriggerNegedge, SimComputeTriggerBoth, SimComputeTriggerEvent
      ]> {
  let cppNamespace = "::obelisk::sim";
}

def SimComputeRegionActive : I32EnumAttrCase<"Active", 0, "active">;
def SimComputeRegionNBA : I32EnumAttrCase<"NBA", 1, "nba">;
def SimComputeRegionObserved : I32EnumAttrCase<"Observed", 2, "observed">;
def SimComputeRegionReactive : I32EnumAttrCase<"Reactive", 3, "reactive">;
def SimComputeRegionPostponed : I32EnumAttrCase<"Postponed", 4, "postponed">;
def SimComputeRegionKind
    : I32EnumAttr<"ComputeRegionKind", "simulation event-region kind", [
        SimComputeRegionActive, SimComputeRegionNBA, SimComputeRegionObserved,
        SimComputeRegionReactive, SimComputeRegionPostponed
      ]> {
  let cppNamespace = "::obelisk::sim";
}

def SimComputeScheduleAcyclic
    : I32EnumAttrCase<"Acyclic", 0, "acyclic">;
def SimComputeScheduleControlLoop
    : I32EnumAttrCase<"ControlLoop", 1, "control_loop">;
def SimComputeScheduleConvergence
    : I32EnumAttrCase<"Convergence", 2, "convergence">;
def SimComputeScheduleKind
    : I32EnumAttr<"ComputeScheduleKind", "compute-group execution mode", [
        SimComputeScheduleAcyclic, SimComputeScheduleControlLoop,
        SimComputeScheduleConvergence
      ]> {
  let cppNamespace = "::obelisk::sim";
}

def SimComputeEdgeProcessOrder
    : I32EnumAttrCase<"ProcessOrder", 0, "process_order">;
def SimComputeEdgeResume : I32EnumAttrCase<"Resume", 1, "resume">;
def SimComputeEdgeSpawn : I32EnumAttrCase<"Spawn", 2, "spawn">;
def SimComputeEdgeSensitivity
    : I32EnumAttrCase<"Sensitivity", 3, "sensitivity">;
def SimComputeEdgeNBAStage : I32EnumAttrCase<"NBAStage", 4, "nba_stage">;
def SimComputeEdgeNBAActivate
    : I32EnumAttrCase<"NBAActivate", 5, "nba_activate">;
def SimComputeEdgeConflict : I32EnumAttrCase<"Conflict", 6, "conflict">;
def SimComputeEdgeDeferredStage
    : I32EnumAttrCase<"DeferredStage", 7, "deferred_stage">;
def SimComputeEdgeDeferredActivate
    : I32EnumAttrCase<"DeferredActivate", 8, "deferred_activate">;
def SimComputeEdgeKind
    : I32EnumAttr<"ComputeEdgeKind", "compute-graph edge kind", [
        SimComputeEdgeProcessOrder, SimComputeEdgeResume, SimComputeEdgeSpawn,
        SimComputeEdgeSensitivity, SimComputeEdgeNBAStage,
        SimComputeEdgeNBAActivate, SimComputeEdgeConflict,
        SimComputeEdgeDeferredStage, SimComputeEdgeDeferredActivate
      ]> {
  let cppNamespace = "::obelisk::sim";
}

def SimComputeActionContinue : I32EnumAttrCase<"Continue", 0, "continue">;
def SimComputeActionSuspendDelay
    : I32EnumAttrCase<"SuspendDelay", 1, "suspend_delay">;
def SimComputeActionSuspendChange
    : I32EnumAttrCase<"SuspendChange", 2, "suspend_change">;
def SimComputeActionSuspendEdge
    : I32EnumAttrCase<"SuspendEdge", 3, "suspend_edge">;
def SimComputeActionSuspendAny
    : I32EnumAttrCase<"SuspendAny", 4, "suspend_any">;
def SimComputeActionSuspendEvent
    : I32EnumAttrCase<"SuspendEvent", 5, "suspend_event">;
def SimComputeActionSuspendAwait
    : I32EnumAttrCase<"SuspendAwait", 6, "suspend_await">;
def SimComputeActionSuspendJoin
    : I32EnumAttrCase<"SuspendJoin", 7, "suspend_join">;
def SimComputeActionTerminate : I32EnumAttrCase<"Terminate", 8, "terminate">;
def SimComputeActionKind
    : I32EnumAttr<"ComputeActionKind", "fragment terminal action", [
        SimComputeActionContinue, SimComputeActionSuspendDelay,
        SimComputeActionSuspendChange, SimComputeActionSuspendEdge,
        SimComputeActionSuspendAny, SimComputeActionSuspendEvent,
        SimComputeActionSuspendAwait, SimComputeActionSuspendJoin,
        SimComputeActionTerminate
      ]> {
  let cppNamespace = "::obelisk::sim";
}

def SimComputeTierNative : I32EnumAttrCase<"Native", 0, "native">;
def SimComputeTierBytecode : I32EnumAttrCase<"Bytecode", 1, "bytecode">;
def SimComputeTierGenerated : I32EnumAttrCase<"Generated", 2, "generated">;
def SimComputeTierKind
    : I32EnumAttr<"ComputeTierKind", "fragment implementation tier", [
        SimComputeTierNative, SimComputeTierBytecode, SimComputeTierGenerated
      ]> {
  let cppNamespace = "::obelisk::sim";
}

def SimComputeVPIOff : I32EnumAttrCase<"Off", 0, "off">;
def SimComputeVPIRead : I32EnumAttrCase<"Read", 1, "read">;
def SimComputeVPIFull : I32EnumAttrCase<"Full", 2, "full">;
def SimComputeVPIMode
    : I32EnumAttr<"ComputeVPIMode", "VPI compilation mode", [
        SimComputeVPIOff, SimComputeVPIRead, SimComputeVPIFull
      ]> {
  let cppNamespace = "::obelisk::sim";
}

def SimComputeObservabilityInvisible
    : I32EnumAttrCase<"Invisible", 0, "invisible">;
def SimComputeObservabilitySafePoint
    : I32EnumAttrCase<"SafePoint", 1, "safe_point">;
def SimComputeObservabilityExternallyWritable
    : I32EnumAttrCase<"ExternallyWritable", 2, "externally_writable">;
def SimComputeObservabilityKind
    : I32EnumAttr<"ComputeObservabilityKind", "VPI state observability", [
        SimComputeObservabilityInvisible, SimComputeObservabilitySafePoint,
        SimComputeObservabilityExternallyWritable
      ]> {
  let cppNamespace = "::obelisk::sim";
}

def SimComputeTimingCalendar : I32EnumAttrCase<"Calendar", 0, "calendar">;
def SimComputeTimingDeadlineSlot
    : I32EnumAttrCase<"DeadlineSlot", 1, "deadline_slot">;
def SimComputeTimingDelayedNBA
    : I32EnumAttrCase<"DelayedNBA", 2, "delayed_nba">;
def SimComputeTimingKind
    : I32EnumAttr<"ComputeTimingKind", "compiled timing-site kind", [
        SimComputeTimingCalendar, SimComputeTimingDeadlineSlot,
        SimComputeTimingDelayedNBA
      ]> {
  let cppNamespace = "::obelisk::sim";
}

def SimComputeNBAStorageFixedSlot
    : I32EnumAttrCase<"FixedSlot", 0, "fixed_slot">;
def SimComputeNBAStorageRootAccumulator
    : I32EnumAttrCase<"RootAccumulator", 1, "root_accumulator">;
def SimComputeNBAStorageStaticJournal
    : I32EnumAttrCase<"StaticJournal", 2, "static_journal">;
def SimComputeNBAStorageDynamicFrontier
    : I32EnumAttrCase<"DynamicFrontier", 3, "dynamic_frontier">;
def SimComputeNBAStorageKind
    : I32EnumAttr<"ComputeNBAStorageKind", "generated NBA staging policy", [
        SimComputeNBAStorageFixedSlot, SimComputeNBAStorageRootAccumulator,
        SimComputeNBAStorageStaticJournal,
        SimComputeNBAStorageDynamicFrontier
      ]> {
  let cppNamespace = "::obelisk::sim";
}

#endif // OBELISK_DIALECT_SIMULATION_SIMULATIONENUMS_TD
