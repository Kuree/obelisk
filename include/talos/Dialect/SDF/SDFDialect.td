//===- SDFDialect.td - Transient Standard Delay Format IR -*- tablegen -*-===//

#ifndef TALOS_DIALECT_SDF_SDFDIALECT_TD
#define TALOS_DIALECT_SDF_SDFDIALECT_TD

include "mlir/IR/DialectBase.td"

def TalosSDFDialect : Dialect {
  let name = "sdf";
  let cppNamespace = "::talos::sdf";
  let summary = "transient normalized IEEE 1800 Clause 32 data";
  let description = [{
    `sdf` is a frontend-only interchange form for parsed Standard
    Delay Format. It deliberately has no executable semantics or lowering to
    Simulation IR: the SystemVerilog frontend must resolve every record into
    elaborated Clause 30/31 metadata and erase this dialect before semantic
    simulation IR is returned.
  }];
  let useDefaultAttributePrinterParser = 1;
  let usePropertiesForAttributes = 0;
}

#endif // TALOS_DIALECT_SDF_SDFDIALECT_TD
