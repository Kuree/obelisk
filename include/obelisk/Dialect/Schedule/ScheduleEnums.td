#ifndef OBELISK_DIALECT_SCHEDULE_ENUMS_TD
#define OBELISK_DIALECT_SCHEDULE_ENUMS_TD
include "mlir/IR/EnumAttr.td"
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
  let cppNamespace = "::obelisk::schedule";
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
  let cppNamespace = "::obelisk::schedule";
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
  let cppNamespace = "::obelisk::schedule";
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
  let cppNamespace = "::obelisk::schedule";
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
  let cppNamespace = "::obelisk::schedule";
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
  let cppNamespace = "::obelisk::schedule";
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
  let cppNamespace = "::obelisk::schedule";
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
def SimComputeActionProcessControl
    : I32EnumAttrCase<"ProcessControl", 12, "process_control">;
def SimComputeActionSuspendMailbox
    : I32EnumAttrCase<"SuspendMailbox", 13, "suspend_mailbox">;
def SimComputeActionSuspendSemaphore
    : I32EnumAttrCase<"SuspendSemaphore", 14, "suspend_semaphore">;
def SimComputeActionKind
    : I32EnumAttr<"ComputeActionKind", "fragment terminal action", [
        SimComputeActionContinue, SimComputeActionSuspendDelay,
        SimComputeActionSuspendChange, SimComputeActionSuspendEdge,
        SimComputeActionSuspendAny, SimComputeActionSuspendEvent,
        SimComputeActionSuspendAwait, SimComputeActionSuspendJoin,
        SimComputeActionTerminate, SimComputeActionTaskCall,
        SimComputeActionSuspendChildren, SimComputeActionSuspendObserve,
        SimComputeActionProcessControl, SimComputeActionSuspendMailbox,
        SimComputeActionSuspendSemaphore
      ]> {
  let cppNamespace = "::obelisk::schedule";
}


def SimComputeTierNative : I32EnumAttrCase<"Native", 0, "native">;
def SimComputeTierBytecode : I32EnumAttrCase<"Bytecode", 1, "bytecode">;
def SimComputeTierGenerated : I32EnumAttrCase<"Generated", 2, "generated">;
def SimComputeTierKind
    : I32EnumAttr<"ComputeTierKind", "fragment implementation tier", [
        SimComputeTierNative, SimComputeTierBytecode, SimComputeTierGenerated
      ]> {
  let cppNamespace = "::obelisk::schedule";
}

// Execution ownership is separate from implementation provenance.  A native
// fragment may belong to a coarse clock kernel (Tier 1), a fine generated
// convergence kernel (Tier 2), or a bytecode island (Tier 3) when surrounding
// semantics require the generic scheduler.
def SimSchedulerTier1 : I32EnumAttrCase<"Tier1", 1, "tier1">;
def SimSchedulerTier2 : I32EnumAttrCase<"Tier2", 2, "tier2">;
def SimSchedulerTier3 : I32EnumAttrCase<"Tier3", 3, "tier3">;
def SimSchedulerTierKind
    : I32EnumAttr<"SchedulerTierKind", "generated scheduler ownership tier", [
        SimSchedulerTier1, SimSchedulerTier2, SimSchedulerTier3
      ]> {
  let cppNamespace = "::obelisk::schedule";
}

def SimComputeVPIOff : I32EnumAttrCase<"Off", 0, "off">;
def SimComputeVPIRead : I32EnumAttrCase<"Read", 1, "read">;
def SimComputeVPIFull : I32EnumAttrCase<"Full", 2, "full">;
def SimComputeVPIMode
    : I32EnumAttr<"ComputeVPIMode", "VPI compilation mode", [
        SimComputeVPIOff, SimComputeVPIRead, SimComputeVPIFull
      ]> {
  let cppNamespace = "::obelisk::schedule";
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
  let cppNamespace = "::obelisk::schedule";
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
  let cppNamespace = "::obelisk::schedule";
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
  let cppNamespace = "::obelisk::schedule";
}


def SimNativeSchedulerAuto
    : I32EnumAttrCase<"Auto", 0, "auto">;
def SimNativeSchedulerGeneric
    : I32EnumAttrCase<"Generic", 1, "generic">;
def SimNativeSchedulerAOT
    : I32EnumAttrCase<"AOT", 2, "aot">;
def SimNativeSchedulerEval
    : I32EnumAttrCase<"Eval", 3, "eval">;
def SimNativeSchedulerMode
    : I32EnumAttr<"NativeSchedulerMode", "native scheduling policy", [
        SimNativeSchedulerAuto, SimNativeSchedulerGeneric,
        SimNativeSchedulerAOT, SimNativeSchedulerEval
      ]> {
  let cppNamespace = "::obelisk::schedule";
}

#endif
