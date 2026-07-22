//===- RuntimeDialect.td - Typed Obelisk runtime ABI ---------*- tablegen -*-===//

#ifndef OBELISK_DIALECT_RUNTIME_RUNTIMEDIALECT_TD
#define OBELISK_DIALECT_RUNTIME_RUNTIMEDIALECT_TD

include "mlir/IR/DialectBase.td"

def ObeliskRuntimeDialect : Dialect {
  let name = "obelisk_rt";
  let cppNamespace = "::obelisk::runtime";
  let summary = "typed compiler boundary for the Obelisk runtime C ABI";
  let description = [{
    `obelisk_rt` preserves runtime concepts and ownership until the final
    native-call or bytecode encoding step. Its operations mirror the Runtime.h
    schema from the same build; C out-parameters are represented as additional
    SSA results. Cross-revision compatibility is deliberately unsupported.
  }];
  let useDefaultAttributePrinterParser = 0;
  let useDefaultTypePrinterParser = 1;
  let usePropertiesForAttributes = 0;
  let hasConstantMaterializer = 1;
}

#endif // OBELISK_DIALECT_RUNTIME_RUNTIMEDIALECT_TD
