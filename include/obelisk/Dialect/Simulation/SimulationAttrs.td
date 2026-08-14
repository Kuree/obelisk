//===- SimulationAttrs.td - Late simulation analysis attributes -*- td -*-===//

#ifndef OBELISK_DIALECT_SIMULATION_SIMULATIONATTRS_TD
#define OBELISK_DIALECT_SIMULATION_SIMULATIONATTRS_TD

include "obelisk/Dialect/Simulation/SimulationDialect.td"
include "obelisk/Dialect/Simulation/SimulationEnums.td"
include "mlir/IR/AttrTypeBase.td"
include "mlir/IR/BuiltinAttributes.td"
include "mlir/IR/EnumAttr.td"

class SimAttr<string name, string attrMnemonic, list<Trait> traits = []>
    : AttrDef<ObeliskSimulationDialect, name, traits> {
  let mnemonic = attrMnemonic;
}

//===----------------------------------------------------------------------===//
// Prepared unit boundary
//===----------------------------------------------------------------------===//

def SimFrozenConstantAttr
    : SimAttr<"FrozenConstant", "frozen_constant", [TypedAttrInterface]> {
  let summary = "typed elaborated constant frozen into one simulation unit";
  let description = [{
    Carries the exact value of a parameter, localparam, enum value, specparam,
    or elaborated genvar after whole-design elaboration has completed. Packed
    values use the same two-plane `[value, unknown]` representation as
    `obelisk_sim.logic.constant`; real values use an f64 FloatAttr. Fixed
    unpacked arrays use an ArrayAttr of recursively typed frozen constants.
    The declared normalized type is retained so aggregate shape is not lost.
  }];
  let parameters = (ins
    AttributeSelfTypeParameter<"">:$type,
    "::mlir::Attribute":$value,
    "bool":$isSigned
  );
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimArgumentBindingAttr
    : SimAttr<"ArgumentBinding", "argument_binding"> {
  let summary = "source name bound to one prepared unit argument";
  let parameters = (ins
    "::mlir::StringAttr":$path,
    "uint64_t":$argument,
    EnumParameter<SimUnitArgumentKind>:$kind,
    "bool":$copyOut,
    OptionalParameter<"::mlir::IntegerAttr">:$lvalueNode
  );
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimLocalBindingAttr : SimAttr<"LocalBinding", "local_binding"> {
  let summary = "source name bound to activation-local storage";
  let parameters = (ins
    "::mlir::StringAttr":$path,
    "::mlir::Type":$type,
    "bool":$automatic,
    "bool":$patternVariable,
    "bool":$isReturn
  );
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimConstantBindingAttr : SimAttr<"ConstantBinding", "constant_binding"> {
  let summary = "source name bound to a typed elaborated constant";
  let parameters = (ins
    "::mlir::StringAttr":$path,
    SimFrozenConstantAttr:$value
  );
  // `qualified` is required for this nested typed attribute so its mnemonic
  // and trailing type survive textual IR round-trips.
  let assemblyFormat =
      "`<` `path` `=` $path `,` `value` `=` qualified($value) `>`";
  let genVerifyDecl = 1;
}

def SimFieldAttr : SimAttr<"Field", "field"> {
  let summary = "ordered aggregate field metadata";
  let parameters = (ins
    "::mlir::StringAttr":$name,
    "::mlir::Type":$type,
    "uint32_t":$ordinal,
    "uint64_t":$packedOffset
  );
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

// A constraint template is defined relative to one active object.  Keep its
// object traversal symbolic until a target-specific class layout is available:
// every path component and the terminal variable name a class-field
// declaration, never a byte offset.  An empty path denotes a variable on the
// origin object itself.
def SimRandomVariableReferenceAttr
    : SimAttr<"RandomVariableReference", "random_variable_reference"> {
  let summary = "object-relative symbolic random-variable reference";
  let description = [{
    Identifies one packed rand or randc field relative to the object owning a
    randomization constraint template. `path` follows strong rand class-handle
    fields in source order; `target` names the terminal packed random field.
    The owning class operation verifies the complete static type chain.
  }];
  let parameters = (ins
    OptionalArrayRefParameter<"::mlir::FlatSymbolRefAttr">:$path,
    "::mlir::FlatSymbolRefAttr":$target
  );
  let assemblyFormat =
      "`<` (`path` `=` `[` $path^ `]` `,`)? `target` `=` $target `>`";
  let genVerifyDecl = 1;
}

def SimRandomVariableReferenceArrayAttr
    : TypedArrayAttrBase<SimRandomVariableReferenceAttr,
                         "random-variable reference array"> {
  let constBuilderCall = "$_builder.getArrayAttr($0)";
}

def SimComputeEffectAttr : SimAttr<"ComputeEffect", "effect"> {
  let parameters = (ins
    EnumParameter<SimComputeEffectKind>:$effect,
    EnumParameter<SimComputeResourceKind>:$resource,
    EnumParameter<SimComputeTargetKind>:$target,
    "uint64_t":$descriptor,
    "uint32_t":$formal,
    "uint64_t":$low,
    "uint64_t":$width,
    "bool":$dynamic,
    "bool":$deferred,
    EnumParameter<SimComputeTriggerKind>:$trigger
  );
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

// Effect arrays appear both inside compute-graph attributes and directly on
// `obelisk_sim.func`. Constraining the element type declaratively means a
// malformed summary is rejected by the op verifier with a real diagnostic,
// rather than only by whichever pass happens to walk it.
def SimComputeEffectArrayAttr
    : TypedArrayAttrBase<SimComputeEffectAttr, "compute effect array"> {
  let constBuilderCall = "$_builder.getArrayAttr($0)";
}

def SimComputeFragmentAttr : SimAttr<"ComputeFragment", "fragment"> {
  let parameters = (ins
    "uint32_t":$id,
    "::mlir::FlatSymbolRefAttr":$function,
    "uint32_t":$block,
    EnumParameter<SimComputeRegionKind>:$region,
    EnumParameter<SimComputeActionKind>:$action,
    EnumParameter<SimComputeTierKind>:$tier,
    "uint64_t":$cost,
    "uint32_t":$lane,
    "bool":$twoState,
    "::mlir::ArrayAttr":$effects
  );
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimComputeNBACommitAttr : SimAttr<"ComputeNBACommit", "nba_commit"> {
  let parameters = (ins
    "uint32_t":$id,
    "::mlir::DenseI64ArrayAttr":$slots,
    "::mlir::DenseI64ArrayAttr":$accumulatorSites,
    "::mlir::DenseI64ArrayAttr":$frontierSites,
    SimComputeEffectAttr:$effect
  );
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimComputeEventCommitAttr : SimAttr<"ComputeEventCommit", "event_commit"> {
  let parameters = (ins
    "uint32_t":$id,
    "::mlir::DenseI64ArrayAttr":$sites,
    SimComputeEffectAttr:$effect
  );
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimComputeEdgeAttr : SimAttr<"ComputeEdge", "edge"> {
  let parameters = (ins
    "uint32_t":$source,
    "uint32_t":$target,
    EnumParameter<SimComputeEdgeKind>:$kind,
    OptionalParameter<"::obelisk::sim::ComputeEffectAttr">:$resource
  );
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimComputeGroupAttr : SimAttr<"ComputeGroup", "group"> {
  let parameters = (ins
    "::mlir::DenseI64ArrayAttr":$fragments,
    EnumParameter<SimComputeScheduleKind>:$schedule,
    "::mlir::ArrayAttr":$feedback
  );
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimComputeFusionAttr : SimAttr<"ComputeFusion", "fusion"> {
  let parameters = (ins
    "uint32_t":$id,
    "::mlir::DenseI64ArrayAttr":$fragments
  );
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimComputeKernelAttr : SimAttr<"ComputeKernel", "kernel"> {
  let parameters = (ins
    "uint32_t":$id,
    EnumParameter<SimComputeRegionKind>:$region,
    EnumParameter<SimComputeScheduleKind>:$schedule,
    "uint32_t":$lane,
    "uint64_t":$cost,
    "bool":$loweringReady,
    "::mlir::DenseI64ArrayAttr":$fragments
  );
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

// A physical trigger is independent of process identity.  Descriptor IDs are
// already canonical within a frozen design, so this tuple distinguishes exact
// packed ranges and edge senses without duplicating shared downstream cones.
def SimPhysicalTriggerAttr : SimAttr<"PhysicalTrigger", "physical_trigger"> {
  let parameters = (ins
    EnumParameter<SimComputeResourceKind>:$resource,
    "uint64_t":$descriptor,
    "uint64_t":$low,
    "uint64_t":$width,
    EnumParameter<SimComputeTriggerKind>:$edge
  );
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimTriggerGroupAttr : SimAttr<"TriggerGroup", "trigger_group"> {
  let parameters = (ins
    "uint32_t":$id,
    SimPhysicalTriggerAttr:$key
  );
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimInductiveRootAttr : SimAttr<"InductiveRoot", "inductive_root"> {
  let parameters = (ins
    EnumParameter<SimComputeResourceKind>:$resource,
    "uint64_t":$descriptor
  );
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

// Every final merged fragment has one owner and one stable ready bit.  Shared
// means that the owner is not a physical-trigger owner (multi-clock cones,
// convergence SCCs, and bytecode islands are examples).  Tier-1 candidates
// always have a four-state body; twoStateEligible requests a second body and
// one promotion-dirty latch for the complete owner closure.
def SimScheduledKernelAttr : SimAttr<"ScheduledKernel", "scheduled_kernel"> {
  let parameters = (ins
    "uint32_t":$id,
    "uint32_t":$owner,
    "uint32_t":$readyBit,
    EnumParameter<SimSchedulerTierKind>:$tier,
    EnumParameter<SimComputeRegionKind>:$region,
    EnumParameter<SimComputeScheduleKind>:$schedule,
    "bool":$shared,
    "bool":$loweringReady,
    "bool":$twoStateEligible,
    "::mlir::ArrayAttr":$promotionRoots,
    "::mlir::DenseI64ArrayAttr":$fragments
  );
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimSchedulerIngressAttr : SimAttr<"SchedulerIngress", "scheduler_ingress"> {
  let parameters = (ins
    "uint32_t":$trigger,
    "uint32_t":$owner,
    "uint32_t":$readyBit,
    "uint32_t":$fragment
  );
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

// A root with writers in more than one execution tier receives a separate
// barrier owner.  The owner tier is the least-static writer tier and therefore
// preserves ordering without cloning the root into multiple schedulers.
def SimScheduledRootAttr : SimAttr<"ScheduledRoot", "scheduled_root"> {
  let parameters = (ins
    EnumParameter<SimComputeResourceKind>:$resource,
    "uint64_t":$descriptor,
    "uint64_t":$low,
    "uint64_t":$width,
    "uint32_t":$owner,
    EnumParameter<SimSchedulerTierKind>:$tier
  );
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

// A physical clock key is deliberately independent of process identity.  The
// native backend resolves descriptor aliases to the same static-state ID when
// it materializes the revision-coupled runtime plan; this persistent form
// records the exact descriptor/range/edge proof made by the compute graph.
def SimClockKeyAttr : SimAttr<"ClockKey", "clock_key"> {
  let parameters = (ins
    EnumParameter<SimComputeResourceKind>:$resource,
    "uint64_t":$descriptor,
    "uint64_t":$low,
    "uint64_t":$width,
    EnumParameter<SimComputeTriggerKind>:$edge
  );
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimClockKernelAttr : SimAttr<"ClockKernel", "clock_kernel"> {
  let parameters = (ins
    "uint32_t":$id,
    SimClockKeyAttr:$key
  );
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

// `owner` names either a clock kernel (owner < clock count) or an exclusively
// owned shared/fallback kernel.  `bit` is stable within that owner and is the
// native ready-mask identity; original graph IDs remain in `fragments`.
def SimMergedFragmentAttr : SimAttr<"MergedFragment", "merged_fragment"> {
  let parameters = (ins
    "uint32_t":$id,
    "uint32_t":$owner,
    "uint32_t":$bit,
    "bool":$shared,
    "bool":$loweringReady,
    "::mlir::DenseI64ArrayAttr":$fragments
  );
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimKernelIngressAttr : SimAttr<"KernelIngress", "kernel_ingress"> {
  let parameters = (ins
    "uint32_t":$clock,
    "uint32_t":$owner,
    "uint32_t":$bit,
    "uint32_t":$fragment
  );
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimComputeRegionAttr : SimAttr<"ComputeRegion", "region"> {
  let parameters = (ins
    EnumParameter<SimComputeRegionKind>:$kind,
    "::mlir::ArrayAttr":$groups
  );
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimComputeGraphAttr : SimAttr<"ComputeGraph", "graph"> {
  let parameters = (ins
    "uint32_t":$version,
    EnumParameter<SimComputeVPIMode>:$vpi,
    "uint32_t":$workers,
    "::mlir::ArrayAttr":$nodes,
    "::mlir::ArrayAttr":$edges,
    "::mlir::ArrayAttr":$regions
  );
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimThreeTierScheduleAttr
    : SimAttr<"ThreeTierSchedule", "three_tier_schedule"> {
  let summary = "exclusive physical-trigger and fine-kernel ownership plan";
  let parameters = (ins
    "uint32_t":$version,
    SimComputeGraphAttr:$sourceGraph,
    "uint32_t":$ownerCount,
    "::mlir::ArrayAttr":$triggers,
    "::mlir::ArrayAttr":$kernels,
    "::mlir::ArrayAttr":$roots,
    "::mlir::ArrayAttr":$ingress
  );
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimClockKernelPlanAttr : SimAttr<"ClockKernelPlan", "clock_kernel_plan"> {
  let summary = "stable clock-keyed native-kernel ownership plan";
  let parameters = (ins
    "uint32_t":$version,
    SimComputeGraphAttr:$sourceGraph,
    "uint32_t":$ownerCount,
    "::mlir::ArrayAttr":$clocks,
    "::mlir::ArrayAttr":$mergedFragments,
    "::mlir::ArrayAttr":$ingress
  );
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimStaticStateRootAttr : SimAttr<"StaticStateRoot", "static_state_root"> {
  let summary = "one fixed storage root selected for static specialization";
  let parameters = (ins
    "uint64_t":$descriptor,
    "uint32_t":$width,
    "bool":$direct,
    "bool":$guarded,
    "bool":$nba
  );
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimStaticActorRootAttr
    : SimAttr<"StaticActorRoot", "static_actor_root"> {
  let summary = "one actor's dependency on a fixed packed root";
  let parameters = (ins
    "::mlir::FlatSymbolRefAttr":$function,
    "uint64_t":$descriptor,
    "bool":$read,
    "bool":$write
  );
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimStaticSpecializationAttr
    : SimAttr<"StaticSpecialization", "static_specialization"> {
  let summary = "revision-coupled direct-state and fixed-root NBA plan";
  let parameters = (ins
    "uint32_t":$version,
    "uint32_t":$maxPackedWidth,
    SimComputeGraphAttr:$sourceGraph,
    "::mlir::ArrayAttr":$roots,
    "::mlir::ArrayAttr":$actorRoots,
    "::mlir::DenseI64ArrayAttr":$nbaRoots
  );
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimStaticSuperstepAttr
    : SimAttr<"StaticSuperstep", "static_superstep"> {
  let summary = "verified-graph fixed-actor clean AOT superstep candidate";
  let parameters = (ins
    "uint32_t":$version,
    SimComputeGraphAttr:$sourceGraph,
    "::mlir::ArrayAttr":$actors
  );
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimFragmentABIAttr : SimAttr<"FragmentABI", "fragment_abi"> {
  let parameters = (ins
    "uint32_t":$version,
    "::mlir::DenseI64ArrayAttr":$fragments
  );
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimContinuationSiteAttr : SimAttr<"ContinuationSite", "continuation"> {
  let parameters = (ins "uint32_t":$id);
  let assemblyFormat = "`<` struct(params) `>`";
}

def SimDPIABIAttr : SimAttr<"DPIABI", "dpi_abi"> {
  let summary = "one logical value in a DPI-C import signature";
  let parameters = (ins
    EnumParameter<SimDPIABIKind>:$kind,
    EnumParameter<SimDPIArgumentDirection>:$direction,
    "uint32_t":$width,
    "bool":$fourState,
    "bool":$isSigned
  );
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimDPIABIArrayAttr
    : TypedArrayAttrBase<SimDPIABIAttr, "DPI ABI signature"> {
  let constBuilderCall = "$_builder.getArrayAttr($0)";
}

def SimTimingSiteAttr : SimAttr<"TimingSite", "timing_site"> {
  let parameters = (ins
    "uint64_t":$id,
    EnumParameter<SimComputeTimingKind>:$kind
  );
  let assemblyFormat = "`<` struct(params) `>`";
}

def SimNBASiteAttr : SimAttr<"NBASite", "nba_site"> {
  let parameters = (ins
    "uint64_t":$id,
    "uint32_t":$commit,
    EnumParameter<SimComputeNBAStorageKind>:$storage,
    OptionalParameter<"::obelisk::sim::TimingSiteAttr">:$timing
  );
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimEventSiteAttr : SimAttr<"EventSite", "event_site"> {
  let parameters = (ins
    "uint64_t":$id,
    "uint32_t":$commit,
    OptionalParameter<"::obelisk::sim::TimingSiteAttr">:$timing
  );
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

#endif // OBELISK_DIALECT_SIMULATION_SIMULATIONATTRS_TD
