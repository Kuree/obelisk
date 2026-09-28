// Typed scheduling records shared by planning and native lowering.
def ScheduleSourceOwnerAttr : ScheduleAttr<"SourceOwner", "source_owner"> {
  let parameters = (ins "::mlir::IntegerAttr":$codeUnit, "::mlir::IntegerAttr":$continuation);
  let assemblyFormat = "`<` struct(params) `>`";
}
def ScheduleClockedSamplePlanAttr : ScheduleAttr<"ClockedSamplePlan", "clocked_sample_plan"> {
  let parameters = (ins "::mlir::StringAttr":$key, "::mlir::IntegerAttr":$id, "::mlir::StringAttr":$hierarchy);
  let assemblyFormat = "`<` struct(params) `>`";
}
def ScheduleCheckpointRouteAttr : ScheduleAttr<"CheckpointRoute", "checkpoint_route"> {
  let parameters = (ins "::mlir::FlatSymbolRefAttr":$fourState, "::mlir::IntegerAttr":$actor, "::mlir::IntegerAttr":$continuation);
  let assemblyFormat = "`<` struct(params) `>`";
}
def SchedulePathProbeRouteAttr : ScheduleAttr<"PathProbeRoute", "path_probe_route"> {
  let parameters = (ins "::mlir::FlatSymbolRefAttr":$twoState, "::mlir::FlatSymbolRefAttr":$probe, "::mlir::FlatSymbolRefAttr":$checkpointProbe);
  let assemblyFormat = "`<` struct(params) `>`";
}
def ScheduleKernelProofDependencyAttr : ScheduleAttr<"KernelProofDependency", "kernel_proof_dependency"> {
  let parameters = (ins "::mlir::IntegerAttr":$latch, "::mlir::IntegerAttr":$pendingBit, "::mlir::DenseI64ArrayAttr":$ranges);
  let assemblyFormat = "`<` struct(params) `>`";
}
def ScheduleNBAProofDependencyAttr : ScheduleAttr<"NBAProofDependency", "nba_proof_dependency"> {
  let parameters = (ins "::mlir::IntegerAttr":$bit, "::mlir::DenseI64ArrayAttr":$ranges);
  let assemblyFormat = "`<` struct(params) `>`";
}
def ScheduleRouteProofDependencyAttr : ScheduleAttr<"RouteProofDependency", "route_proof_dependency"> {
  let parameters = (ins "::mlir::DenseI64ArrayAttr":$ranges, "::mlir::IntegerAttr":$pendingBit);
  let assemblyFormat = "`<` struct(params) `>`";
}
