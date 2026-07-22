//===- SimulationAttrs.td - Late simulation analysis attributes -*- td -*-===//

#ifndef OBELISK_DIALECT_SIMULATION_SIMULATIONATTRS_TD
#define OBELISK_DIALECT_SIMULATION_SIMULATIONATTRS_TD

include "obelisk/Dialect/Simulation/SimulationDialect.td"
include "obelisk/Dialect/Simulation/SimulationEnums.td"
include "mlir/IR/AttrTypeBase.td"
include "mlir/IR/BuiltinAttributes.td"
include "mlir/IR/EnumAttr.td"

class SimAttr<string name, string attrMnemonic>
    : AttrDef<ObeliskSimulationDialect, name> {
  let mnemonic = attrMnemonic;
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
  let parameters = (ins "uint64_t":$id, "uint32_t":$commit);
  let assemblyFormat = "`<` struct(params) `>`";
}

#endif // OBELISK_DIALECT_SIMULATION_SIMULATIONATTRS_TD
