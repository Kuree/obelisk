//===- SimulationDialect.td - Executable Obelisk simulation IR -*- td -*-===//

#ifndef OBELISK_DIALECT_SIMULATION_SIMULATIONDIALECT_TD
#define OBELISK_DIALECT_SIMULATION_SIMULATIONDIALECT_TD

include "mlir/IR/DialectBase.td"

def ObeliskSimulationDialect : Dialect {
  let name = "obelisk_sim";
  let cppNamespace = "::obelisk::sim";
  let dependentDialects = ["::mlir::arith::ArithDialect"];
  let summary = "Target-independent executable SystemVerilog simulation IR";
  let description = [{
    `obelisk_sim` is the SSA and explicit-effect boundary after elaborated
    SystemVerilog semantic IR.  Hierarchy and mutable design objects are
    identified by deterministic numeric descriptors; code is isolated into
    function-like SSACFG regions whose only symbolic references are direct
    sibling calls and process spawns.

    Packed builtin integers are exact two-state values and `logic` values are
    exact four-state values. Operations preserve the four-state plane unless
    their contract explicitly performs truth evaluation, conversion to a
    two-state result, or dynamic-selection fallback. Dynamic selections accept
    integer and four-state indices: an unknown index reads as X in the logic
    domain and zero in the bits domain, and suppresses writes and drives.
    Partially out-of-range selections preserve or update valid positions while
    reads fill invalid positions with X or zero according to the result domain
    and writes ignore invalid positions.
  }];
  let useDefaultAttributePrinterParser = 1;
  let useDefaultTypePrinterParser = 1;
  let usePropertiesForAttributes = 0;
  let hasConstantMaterializer = 1;
}

#endif // OBELISK_DIALECT_SIMULATION_SIMULATIONDIALECT_TD
