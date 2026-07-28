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
def SimEntryPortInput : I32EnumAttrCase<"PortInput", 9, "port_input">;
def SimEntryPortOutput : I32EnumAttrCase<"PortOutput", 10, "port_output">;
def SimEntryPortInitialize
    : I32EnumAttrCase<"PortInitialize", 11, "port_initialize">;
def SimEntryTask : I32EnumAttrCase<"Task", 12, "task">;
def SimEntryFork : I32EnumAttrCase<"Fork", 13, "fork">;
def SimEntryObserver : I32EnumAttrCase<"Observer", 14, "observer">;
def SimEntryKind : I32EnumAttr<"EntryKind", "simulation code-unit kind", [
  SimEntryRootInitializer, SimEntryInitial, SimEntryFinal, SimEntryAlways,
  SimEntryAlwaysComb, SimEntryAlwaysFF, SimEntryAlwaysLatch, SimEntryContinuous,
  SimEntryFunction, SimEntryPortInput, SimEntryPortOutput,
  SimEntryPortInitialize, SimEntryTask, SimEntryFork, SimEntryObserver
]> {
  let cppNamespace = "::obelisk::sim";
}

def SimNativeSchedulerAuto
    : I32EnumAttrCase<"Auto", 0, "auto">;
def SimNativeSchedulerGeneric
    : I32EnumAttrCase<"Generic", 1, "generic">;
def SimNativeSchedulerAOT
    : I32EnumAttrCase<"AOT", 2, "aot">;
def SimNativeSchedulerMode
    : I32EnumAttr<"NativeSchedulerMode", "native scheduling policy", [
        SimNativeSchedulerAuto, SimNativeSchedulerGeneric,
        SimNativeSchedulerAOT
      ]> {
  let cppNamespace = "::obelisk::sim";
}

// IEEE 1800-2023 event regions in their normative scheduling order. These
// remain distinct from ComputeRegionKind: the latter is a coarse graph
// partition and is not an executable scheduler queue.
def SimRegionPreponed
    : I32EnumAttrCase<"Preponed", 0, "preponed">;
def SimRegionPreActive
    : I32EnumAttrCase<"PreActive", 1, "pre_active">;
def SimRegionActive
    : I32EnumAttrCase<"Active", 2, "active">;
def SimRegionInactive
    : I32EnumAttrCase<"Inactive", 3, "inactive">;
def SimRegionPreNBA
    : I32EnumAttrCase<"PreNBA", 4, "pre_nba">;
def SimRegionNBA
    : I32EnumAttrCase<"NBA", 5, "nba">;
def SimRegionPostNBA
    : I32EnumAttrCase<"PostNBA", 6, "post_nba">;
def SimRegionPreObserved
    : I32EnumAttrCase<"PreObserved", 7, "pre_observed">;
def SimRegionObserved
    : I32EnumAttrCase<"Observed", 8, "observed">;
def SimRegionPostObserved
    : I32EnumAttrCase<"PostObserved", 9, "post_observed">;
def SimRegionReactive
    : I32EnumAttrCase<"Reactive", 10, "reactive">;
def SimRegionReInactive
    : I32EnumAttrCase<"ReInactive", 11, "re_inactive">;
def SimRegionPreReNBA
    : I32EnumAttrCase<"PreReNBA", 12, "pre_re_nba">;
def SimRegionReNBA
    : I32EnumAttrCase<"ReNBA", 13, "re_nba">;
def SimRegionPostReNBA
    : I32EnumAttrCase<"PostReNBA", 14, "post_re_nba">;
def SimRegionPrePostponed
    : I32EnumAttrCase<"PrePostponed", 15, "pre_postponed">;
def SimRegionPostponed
    : I32EnumAttrCase<"Postponed", 16, "postponed">;
def SimEventRegion
    : I32EnumAttr<"EventRegion", "IEEE executable event-region queue", [
        SimRegionPreponed, SimRegionPreActive, SimRegionActive,
        SimRegionInactive, SimRegionPreNBA, SimRegionNBA, SimRegionPostNBA,
        SimRegionPreObserved, SimRegionObserved, SimRegionPostObserved,
        SimRegionReactive, SimRegionReInactive, SimRegionPreReNBA,
        SimRegionReNBA, SimRegionPostReNBA, SimRegionPrePostponed,
        SimRegionPostponed
      ]> {
  let cppNamespace = "::obelisk::sim";
}

def SimDomainDesign
    : I32EnumAttrCase<"Design", 0, "design">;
def SimDomainProgram
    : I32EnumAttrCase<"Program", 1, "program">;
def SimExecutionDomain
    : I32EnumAttr<"ExecutionDomain", "procedural scheduling domain", [
        SimDomainDesign, SimDomainProgram
      ]> {
  let cppNamespace = "::obelisk::sim";
}

def SimDPIABIBit : I32EnumAttrCase<"Bit", 0, "bit">;
def SimDPIABILogic : I32EnumAttrCase<"Logic", 1, "logic">;
def SimDPIABIByte : I32EnumAttrCase<"Byte", 2, "byte">;
def SimDPIABIShortInt : I32EnumAttrCase<"ShortInt", 3, "shortint">;
def SimDPIABIInt : I32EnumAttrCase<"Int", 4, "int">;
def SimDPIABILongInt : I32EnumAttrCase<"LongInt", 5, "longint">;
def SimDPIABIBitVector : I32EnumAttrCase<"BitVector", 6, "bit_vector">;
def SimDPIABILogicVector
    : I32EnumAttrCase<"LogicVector", 7, "logic_vector">;
def SimDPIABIKind
    : I32EnumAttr<"DPIABIKind", "DPI-C integral ABI category", [
        SimDPIABIBit, SimDPIABILogic, SimDPIABIByte, SimDPIABIShortInt,
        SimDPIABIInt, SimDPIABILongInt, SimDPIABIBitVector,
        SimDPIABILogicVector
      ]> {
  let cppNamespace = "::obelisk::sim";
}

def SimDPIInput : I32EnumAttrCase<"Input", 0, "input">;
def SimDPIOutput : I32EnumAttrCase<"Output", 1, "output">;
def SimDPIInOut : I32EnumAttrCase<"InOut", 2, "inout">;
def SimDPIResult : I32EnumAttrCase<"Result", 3, "result">;
def SimDPIArgumentDirection
    : I32EnumAttr<"DPIArgumentDirection", "DPI-C ABI value role", [
        SimDPIInput, SimDPIOutput, SimDPIInOut, SimDPIResult
      ]> {
  let cppNamespace = "::obelisk::sim";
}

def SimNetWire : I32EnumAttrCase<"Wire", 0, "wire">;
def SimNetTri : I32EnumAttrCase<"Tri", 1, "tri">;
def SimNetUWire : I32EnumAttrCase<"UWire", 2, "uwire">;
def SimNetResolutionKind
    : I32EnumAttr<"NetResolutionKind", "supported net resolution kind", [
        SimNetWire, SimNetTri, SimNetUWire
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

// Transient binding roles used while the prepare pass freezes each isolated
// code unit. These are deliberately distinct from CaptureKind: the latter is
// stable ABI metadata on a function argument, while these roles describe how
// unit lowering reconstructs source-level names from that ABI.
def SimUnitArgumentDirect
    : I32EnumAttrCase<"Direct", 0, "direct">;
def SimUnitArgumentLValueOnly
    : I32EnumAttrCase<"LValueOnly", 1, "lvalue_only">;
def SimUnitArgumentFormalLocal
    : I32EnumAttrCase<"FormalLocal", 2, "formal_local">;
def SimUnitArgumentCopyOutDestination
    : I32EnumAttrCase<"CopyOutDestination", 3, "copy_out_destination">;
def SimUnitArgumentKind
    : I32EnumAttr<"UnitArgumentKind",
                  "frozen unit argument binding role", [
        SimUnitArgumentDirect, SimUnitArgumentLValueOnly,
        SimUnitArgumentFormalLocal, SimUnitArgumentCopyOutDestination
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
def SimCmpWildEq : I32EnumAttrCase<"WildEq", 12, "wild_eq">;
def SimCmpWildNe : I32EnumAttrCase<"WildNe", 13, "wild_ne">;
def SimCmpCaseZEq : I32EnumAttrCase<"CaseZEq", 14, "casez_eq">;
def SimCmpCaseXZEq : I32EnumAttrCase<"CaseXZEq", 15, "casexz_eq">;
def SimCompareKind
    : I32EnumAttr<"CompareKind", "normalized comparison operation", [
      SimCmpEq, SimCmpNe, SimCmpCaseEq, SimCmpCaseNe, SimCmpULT, SimCmpULE,
      SimCmpUGT, SimCmpUGE, SimCmpSLT, SimCmpSLE, SimCmpSGT, SimCmpSGE,
      SimCmpWildEq, SimCmpWildNe, SimCmpCaseZEq, SimCmpCaseXZEq
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
def SimComputeActionTaskCall
    : I32EnumAttrCase<"TaskCall", 9, "task_call">;
def SimComputeActionSuspendChildren
    : I32EnumAttrCase<"SuspendChildren", 10, "suspend_children">;
def SimComputeActionSuspendObserve
    : I32EnumAttrCase<"SuspendObserve", 11, "suspend_observe">;
def SimComputeActionKind
    : I32EnumAttr<"ComputeActionKind", "fragment terminal action", [
        SimComputeActionContinue, SimComputeActionSuspendDelay,
        SimComputeActionSuspendChange, SimComputeActionSuspendEdge,
        SimComputeActionSuspendAny, SimComputeActionSuspendEvent,
        SimComputeActionSuspendAwait, SimComputeActionSuspendJoin,
        SimComputeActionTerminate, SimComputeActionTaskCall,
        SimComputeActionSuspendChildren, SimComputeActionSuspendObserve
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
def SimComputeTimingDelayedEvent
    : I32EnumAttrCase<"DelayedEvent", 3, "delayed_event">;
def SimComputeTimingKind
    : I32EnumAttr<"ComputeTimingKind", "compiled timing-site kind", [
        SimComputeTimingCalendar, SimComputeTimingDeadlineSlot,
        SimComputeTimingDelayedNBA, SimComputeTimingDelayedEvent
      ]> {
  let cppNamespace = "::obelisk::sim";
}

// A finite static journal is a further policy the design anticipates, but it
// needs a proven multiplicity bound that no analysis produces yet. It is left
// out rather than declared and never selected.
def SimComputeNBAStorageFixedSlot
    : I32EnumAttrCase<"FixedSlot", 0, "fixed_slot">;
def SimComputeNBAStorageRootAccumulator
    : I32EnumAttrCase<"RootAccumulator", 1, "root_accumulator">;
def SimComputeNBAStorageDynamicFrontier
    : I32EnumAttrCase<"DynamicFrontier", 2, "dynamic_frontier">;
def SimComputeNBAStorageKind
    : I32EnumAttr<"ComputeNBAStorageKind", "generated NBA staging policy", [
        SimComputeNBAStorageFixedSlot, SimComputeNBAStorageRootAccumulator,
        SimComputeNBAStorageDynamicFrontier
      ]> {
  let cppNamespace = "::obelisk::sim";
}

#endif // OBELISK_DIALECT_SIMULATION_SIMULATIONENUMS_TD
