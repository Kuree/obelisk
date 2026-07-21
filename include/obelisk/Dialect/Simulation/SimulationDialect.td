//===- SimulationDialect.td - Executable Obelisk simulation IR -*- td -*-===//

#ifndef OBELISK_DIALECT_SIMULATION_SIMULATIONDIALECT_TD
#define OBELISK_DIALECT_SIMULATION_SIMULATIONDIALECT_TD

include "mlir/IR/DialectBase.td"

def ObeliskSimulationDialect : Dialect {
  let name = "obelisk_sim";
  let cppNamespace = "::obelisk::sim";
  let summary = "Target-independent executable SystemVerilog simulation IR";
  let description = [{
    `obelisk_sim` is the SSA and explicit-effect boundary after elaborated
    SystemVerilog semantic IR.  Hierarchy and mutable design objects are
    identified by deterministic numeric descriptors; code is isolated into
    function-like SSACFG regions whose only symbolic references are direct
    sibling calls and process spawns.
  }];
  let useDefaultAttributePrinterParser = 0;
  let useDefaultTypePrinterParser = 1;
  let usePropertiesForAttributes = 0;
  let hasConstantMaterializer = 1;
}

#endif // OBELISK_DIALECT_SIMULATION_SIMULATIONDIALECT_TD
