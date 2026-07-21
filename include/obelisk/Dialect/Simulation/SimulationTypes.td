//===- SimulationTypes.td - Executable simulation types -------*- td -*-===//

#ifndef OBELISK_DIALECT_SIMULATION_SIMULATIONTYPES_TD
#define OBELISK_DIALECT_SIMULATION_SIMULATIONTYPES_TD

include "obelisk/Dialect/Simulation/SimulationDialect.td"
include "mlir/IR/AttrTypeBase.td"
include "mlir/IR/OpBase.td"

class SimType<string name> : TypeDef<ObeliskSimulationDialect, name> {}

def SimLogicType : SimType<"Logic"> {
  let mnemonic = "logic";
  let summary = "fixed-width exact four-state packed value";
  let parameters = (ins "unsigned":$width);
  let assemblyFormat = "`<` $width `>`";
  let genVerifyDecl = 1;
}

def SimLogic1Type : ConfinedType<SimLogicType, [
  CPred<"::llvm::cast<::obelisk::sim::LogicType>($_self).getWidth() == 1">
], "one-bit exact four-state logic", "::obelisk::sim::LogicType">;

def SimContextType : SimType<"Context"> {
  let mnemonic = "context";
  let summary = "opaque simulator and design context";
}

def SimTimeType : SimType<"Time"> {
  let mnemonic = "time";
  let summary = "target-independent scaled simulation time";
}

def SimRefType : SimType<"Ref"> {
  let mnemonic = "ref";
  let summary = "typed mutable storage handle";
  let parameters = (ins "::mlir::Type":$elementType);
  let assemblyFormat = "`<` $elementType `>`";
  let genVerifyDecl = 1;
}

def SimNetType : SimType<"Net"> {
  let mnemonic = "net";
  let summary = "typed resolved net handle";
  let parameters = (ins "::mlir::Type":$elementType);
  let assemblyFormat = "`<` $elementType `>`";
  let genVerifyDecl = 1;
}

def SimDriverType : SimType<"Driver"> {
  let mnemonic = "driver";
  let summary = "typed static net-driver handle";
  let parameters = (ins "::mlir::Type":$elementType);
  let assemblyFormat = "`<` $elementType `>`";
  let genVerifyDecl = 1;
}

def SimEventType : SimType<"Event"> {
  let mnemonic = "event";
  let summary = "opaque scheduler event handle";
}

def SimProcessType : SimType<"Process"> {
  let mnemonic = "process";
  let summary = "opaque scheduler process handle";
}

#endif // OBELISK_DIALECT_SIMULATION_SIMULATIONTYPES_TD
