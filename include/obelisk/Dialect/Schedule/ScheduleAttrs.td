// Scheduling attributes shared by planning, optimization, and lowering.
#ifndef OBELISK_DIALECT_SCHEDULE_ATTRS_TD
#define OBELISK_DIALECT_SCHEDULE_ATTRS_TD
include "obelisk/Dialect/Schedule/ScheduleDialect.td"
include "obelisk/Dialect/Schedule/ScheduleEnums.td"
include "mlir/IR/AttrTypeBase.td"
include "mlir/IR/BuiltinAttributes.td"
include "mlir/IR/EnumAttr.td"
class ScheduleAttr<string name, string mnemonicValue, list<Trait> traits = []>
    : AttrDef<ScheduleDialect, name, traits> {
  let mnemonic = mnemonicValue;
}
def SimComputeEffectAttr : ScheduleAttr<"ComputeEffect", "effect"> {
  let parameters = (ins EnumParameter<SimComputeEffectKind>:$effect,
      EnumParameter<SimComputeResourceKind>:$resource,
      EnumParameter<SimComputeTargetKind>:$target, "uint64_t":$descriptor,
      "uint32_t":$formal, "uint64_t":$low, "uint64_t":$width, "bool":$dynamic,
      "bool":$deferred, EnumParameter<SimComputeTriggerKind>:$trigger);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

// Effect arrays appear both inside compute-graph attributes and directly on
// `simulation.func`. Constraining the element type declaratively means a
// malformed summary is rejected by the op verifier with a real diagnostic,
// rather than only by whichever pass happens to walk it.
def SimComputeEffectArrayAttr
    : TypedArrayAttrBase<SimComputeEffectAttr, "compute effect array"> {
  let constBuilderCall = "$_builder.getArrayAttr($0)";
}

def SimComputeFragmentAttr : ScheduleAttr<"ComputeFragment", "fragment"> {
  let parameters = (ins "uint32_t":$id, "::mlir::FlatSymbolRefAttr":$function,
      "uint32_t":$block, EnumParameter<SimComputeRegionKind>:$region,
      EnumParameter<SimComputeActionKind>:$action,
      EnumParameter<SimComputeTierKind>:$tier, "uint64_t":$cost,
      "uint32_t":$lane, "bool":$twoState, "::mlir::ArrayAttr":$effects);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimComputeNBACommitAttr : ScheduleAttr<"ComputeNBACommit", "nba_commit"> {
  let parameters = (ins "uint32_t":$id, "::mlir::DenseI64ArrayAttr":$slots,
      "::mlir::DenseI64ArrayAttr":$accumulatorSites,
      "::mlir::DenseI64ArrayAttr":$frontierSites, SimComputeEffectAttr:$effect);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimComputeEventCommitAttr : ScheduleAttr<"ComputeEventCommit", "event_commit"> {
  let parameters = (ins "uint32_t":$id, "::mlir::DenseI64ArrayAttr":$sites,
      SimComputeEffectAttr:$effect);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimComputeEdgeAttr : ScheduleAttr<"ComputeEdge", "edge"> {
  let parameters = (ins "uint32_t":$source, "uint32_t":$target,
      EnumParameter<SimComputeEdgeKind>:$kind,
      OptionalParameter<"::obelisk::schedule::ComputeEffectAttr">:$resource);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimComputeGroupAttr : ScheduleAttr<"ComputeGroup", "group"> {
  let parameters = (ins "::mlir::DenseI64ArrayAttr":$fragments,
      EnumParameter<SimComputeScheduleKind>:$schedule,
      "::mlir::ArrayAttr":$feedback);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimComputeFusionAttr : ScheduleAttr<"ComputeFusion", "fusion"> {
  let parameters = (ins "uint32_t":$id, "::mlir::DenseI64ArrayAttr":$fragments);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimComputeKernelAttr : ScheduleAttr<"ComputeKernel", "kernel"> {
  let parameters = (ins "uint32_t":$id,
      EnumParameter<SimComputeRegionKind>:$region,
      EnumParameter<SimComputeScheduleKind>:$schedule, "uint32_t":$lane,
      "uint64_t":$cost, "bool":$loweringReady,
      "::mlir::DenseI64ArrayAttr":$fragments);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

// A physical trigger is independent of process identity.  Descriptor IDs are
// already canonical within a frozen design, so this tuple distinguishes exact
// packed ranges and edge senses without duplicating shared downstream cones.
def SimPhysicalTriggerAttr : ScheduleAttr<"PhysicalTrigger", "physical_trigger"> {
  let parameters = (ins EnumParameter<SimComputeResourceKind>:$resource,
      "uint64_t":$descriptor, "uint64_t":$low, "uint64_t":$width,
      EnumParameter<SimComputeTriggerKind>:$edge);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimTriggerGroupAttr : ScheduleAttr<"TriggerGroup", "trigger_group"> {
  let parameters = (ins "uint32_t":$id, SimPhysicalTriggerAttr:$key);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimInductiveRootAttr : ScheduleAttr<"InductiveRoot", "inductive_root"> {
  let parameters = (ins EnumParameter<SimComputeResourceKind>:$resource,
      "uint64_t":$descriptor);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

// Every final merged fragment has one owner and one stable ready bit.  Shared
// means that the owner is not a physical-trigger owner (multi-clock cones,
// convergence SCCs, and bytecode islands are examples).  Tier-1 candidates
// always have a four-state body; twoStateEligible requests a second body and
// one promotion-dirty latch for the complete owner closure.
def SimScheduledKernelAttr : ScheduleAttr<"ScheduledKernel", "scheduled_kernel"> {
  let parameters = (ins "uint32_t":$id, "uint32_t":$owner, "uint32_t":$readyBit,
      EnumParameter<SimSchedulerTierKind>:$tier,
      EnumParameter<SimComputeRegionKind>:$region,
      EnumParameter<SimComputeScheduleKind>:$schedule, "bool":$shared,
      "bool":$loweringReady, "bool":$twoStateEligible,
      "::mlir::ArrayAttr":$promotionRoots,
      "::mlir::DenseI64ArrayAttr":$fragments);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimSchedulerIngressAttr : ScheduleAttr<"SchedulerIngress", "scheduler_ingress"> {
  let parameters = (ins "uint32_t":$trigger, "uint32_t":$owner,
      "uint32_t":$readyBit, "uint32_t":$fragment);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

// A root with writers in more than one execution tier receives a separate
// barrier owner.  The owner tier is the least-static writer tier and therefore
// preserves ordering without cloning the root into multiple schedulers.
def SimScheduledRootAttr : ScheduleAttr<"ScheduledRoot", "scheduled_root"> {
  let parameters = (ins EnumParameter<SimComputeResourceKind>:$resource,
      "uint64_t":$descriptor, "uint64_t":$low, "uint64_t":$width,
      "uint32_t":$owner, EnumParameter<SimSchedulerTierKind>:$tier);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

// A physical clock key is deliberately independent of process identity.  The
// native backend resolves descriptor aliases to the same static-state ID when
// it materializes the revision-coupled runtime plan; this persistent form
// records the exact descriptor/range/edge proof made by the compute graph.
def SimClockKeyAttr : ScheduleAttr<"ClockKey", "clock_key"> {
  let parameters = (ins EnumParameter<SimComputeResourceKind>:$resource,
      "uint64_t":$descriptor, "uint64_t":$low, "uint64_t":$width,
      EnumParameter<SimComputeTriggerKind>:$edge);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimClockKernelAttr : ScheduleAttr<"ClockKernel", "clock_kernel"> {
  let parameters = (ins "uint32_t":$id, SimClockKeyAttr:$key);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

// `owner` names either a clock kernel (owner < clock count) or an exclusively
// owned shared/fallback kernel.  `bit` is stable within that owner and is the
// native ready-mask identity; original graph IDs remain in `fragments`.
def SimMergedFragmentAttr : ScheduleAttr<"MergedFragment", "merged_fragment"> {
  let parameters = (ins "uint32_t":$id, "uint32_t":$owner, "uint32_t":$bit,
      "bool":$shared, "bool":$loweringReady,
      "::mlir::DenseI64ArrayAttr":$fragments);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimKernelIngressAttr : ScheduleAttr<"KernelIngress", "kernel_ingress"> {
  let parameters = (ins "uint32_t":$clock, "uint32_t":$owner, "uint32_t":$bit,
      "uint32_t":$fragment);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimComputeRegionAttr : ScheduleAttr<"ComputeRegion", "region"> {
  let parameters = (ins EnumParameter<SimComputeRegionKind>:$kind,
      "::mlir::ArrayAttr":$groups);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimComputeGraphAttr : ScheduleAttr<"ComputeGraph", "graph"> {
  // Root-initializer spawn blocks implicitly order startup entries before
  // initial entries in the same event region (excluding starts_without_waiting).
  // Schedule construction and rank refinement derive these phase constraints
  // from the executable IR; `edges` does not store their Cartesian product.
  let parameters = (ins "uint32_t":$version,
      EnumParameter<SimComputeVPIMode>:$vpi, "uint32_t":$workers,
      "::mlir::ArrayAttr":$nodes, "::mlir::ArrayAttr":$edges,
      "::mlir::ArrayAttr":$regions);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimThreeTierScheduleAttr
    : ScheduleAttr<"ThreeTierSchedule", "three_tier_schedule"> {
  let summary = "exclusive physical-trigger and fine-kernel ownership plan";
  let parameters = (ins "uint32_t":$version, SimComputeGraphAttr:$sourceGraph,
      "uint32_t":$ownerCount, "::mlir::ArrayAttr":$triggers,
      "::mlir::ArrayAttr":$kernels, "::mlir::ArrayAttr":$roots,
      "::mlir::ArrayAttr":$ingress);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimClockKernelPlanAttr : ScheduleAttr<"ClockKernelPlan", "clock_kernel_plan"> {
  let summary = "stable clock-keyed native-kernel ownership plan";
  let parameters = (ins "uint32_t":$version, SimComputeGraphAttr:$sourceGraph,
      "uint32_t":$ownerCount, "::mlir::ArrayAttr":$clocks,
      "::mlir::ArrayAttr":$mergedFragments, "::mlir::ArrayAttr":$ingress);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimStaticStateRootAttr : ScheduleAttr<"StaticStateRoot", "static_state_root"> {
  let summary = "one fixed storage root selected for static specialization";
  let parameters = (ins "uint64_t":$descriptor, "uint32_t":$width,
      "bool":$direct, "bool":$guarded, "bool":$nba);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimStaticActorRootAttr : ScheduleAttr<"StaticActorRoot", "static_actor_root"> {
  let summary = "one actor's dependency on a fixed packed root";
  let parameters = (ins "::mlir::FlatSymbolRefAttr":$function,
      "uint64_t":$descriptor, "bool":$read, "bool":$write);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimStaticSpecializationAttr
    : ScheduleAttr<"StaticSpecialization", "static_specialization"> {
  let summary = "revision-coupled direct-state and fixed-root NBA plan";
  let parameters = (ins "uint32_t":$version, "uint32_t":$maxPackedWidth,
      SimComputeGraphAttr:$sourceGraph, "::mlir::ArrayAttr":$roots,
      "::mlir::ArrayAttr":$actorRoots, "::mlir::DenseI64ArrayAttr":$nbaRoots);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimStaticSuperstepAttr : ScheduleAttr<"StaticSuperstep", "static_superstep"> {
  let summary = "verified-graph fixed-actor clean AOT superstep candidate";
  let parameters = (ins "uint32_t":$version, SimComputeGraphAttr:$sourceGraph,
      "::mlir::ArrayAttr":$actors);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimFragmentABIAttr : ScheduleAttr<"FragmentABI", "fragment_abi"> {
  let parameters = (ins "uint32_t":$version,
      "::mlir::DenseI64ArrayAttr":$fragments);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimContinuationSiteAttr : ScheduleAttr<"ContinuationSite", "continuation"> {
  let parameters = (ins "uint32_t":$id);
  let assemblyFormat = "`<` struct(params) `>`";
}

def SimTimingSiteAttr : ScheduleAttr<"TimingSite", "timing_site"> {
  let parameters = (ins "uint64_t":$id,
      EnumParameter<SimComputeTimingKind>:$kind);
  let assemblyFormat = "`<` struct(params) `>`";
}

def SimNBASiteAttr : ScheduleAttr<"NBASite", "nba_site"> {
  let parameters = (ins "uint64_t":$id, "uint32_t":$commit,
      EnumParameter<SimComputeNBAStorageKind>:$storage,
      OptionalParameter<"::obelisk::schedule::TimingSiteAttr">:$timing);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimEventSiteAttr : ScheduleAttr<"EventSite", "event_site"> {
  let parameters = (ins "uint64_t":$id, "uint32_t":$commit,
      OptionalParameter<"::obelisk::schedule::TimingSiteAttr">:$timing);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}


// Physical plans are formed after actor and storage layout. Their units and
// identities match the existing runtime ABI; source clock recognition remains
// in SimulationToSchedule.
def ScheduleCoveragePoints : ArrayRefParameter<"uint64_t"> {
  let parser = [{ ::obelisk::schedule::parseCoveragePoints($_parser) }];
  let printer = [{
    $_printer << '[';
    ::llvm::interleaveComma($_self, $_printer);
    $_printer << ']';
  }];
}

def SchedulePeriodicClockAttr : ScheduleAttr<"PeriodicClock", "periodic_clock"> {
  let parameters = (ins "uint32_t":$actorSlot, "uint32_t":$continuation,
      "uint32_t":$staticState, "uint64_t":$bitOffset, "uint64_t":$halfPeriod,
      ScheduleCoveragePoints:$coveragePoints);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}
def SchedulePeriodicAliasAttr : ScheduleAttr<"PeriodicAlias", "periodic_alias"> {
  let parameters = (ins "uint32_t":$sourceStaticState,
      "uint32_t":$forwardingActorSlot, "uint32_t":$forwardingContinuation,
      "uint32_t":$targetStaticState, "uint64_t":$sourceBitOffset,
      "uint64_t":$targetBitOffset, "uint64_t":$driverBitOffset);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

include "obelisk/Dialect/Schedule/ScheduleRecords.td"
#endif
