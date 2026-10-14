//===- RuntimeDialect.td - Typed Obelisk runtime ABI ---------*- tablegen -*-===//

#ifndef OBELISK_DIALECT_RUNTIME_RUNTIMEDIALECT_TD
#define OBELISK_DIALECT_RUNTIME_RUNTIMEDIALECT_TD

include "mlir/IR/DialectBase.td"

def ObeliskRuntimeDialect : Dialect {
  let name = "runtime";
  let cppNamespace = "::obelisk::runtime";
  let summary = "typed compiler boundary for the Obelisk runtime C ABI";
  let description = [{
    `runtime` preserves runtime concepts and ownership until the final
    native-call or bytecode encoding step. Its operations mirror the Runtime.h
    schema from the same build; C out-parameters are represented as additional
    SSA results. Cross-revision compatibility is deliberately unsupported.
  }];
  let useDefaultAttributePrinterParser = 1;
  let useDefaultTypePrinterParser = 1;
  let usePropertiesForAttributes = 0;
  let hasConstantMaterializer = 1;
  let hasOperationAttrVerify = 1;
  // The executable image a compilation hands to the runtime, attached to
  // the module, and each simulation function's bytecode encoding.
  let discardableAttrs = (ins
    "::mlir::DenseI8ArrayAttr":$bytecode_image,
    "::mlir::DenseI8ArrayAttr":$design_database,
    "::obelisk::runtime::ExecutionFlagsAttr":$execution_flags,
    // Bits of design state the execution descriptor allocates.
    "::mlir::IntegerAttr":$state_bits,
    // Offset/width pairs of state the runtime samples in the preponed region.
    "::mlir::DenseI64ArrayAttr":$sampled_ranges,
    // The embedded execution descriptor has been emitted.
    "::mlir::UnitAttr":$materialized,
    // Canonical pointer-free class schema/group/site blob.
    "::mlir::DenseI8ArrayAttr":$class_bitstream_blob,
    // Schema-only coverage database embedded in the v1 execution extension.
    // Runtime run data is never written back into it.
    "::mlir::DenseI8ArrayAttr":$coverage_schema_blob,
    "::obelisk::runtime::FeaturesAttr":$features,
    "::mlir::IntegerAttr":$bytecode_function,
    "::mlir::IntegerAttr":$bytecode_scratch_size,
    "::mlir::IntegerAttr":$bytecode_scratch_alignment,
    "::mlir::IntegerAttr":$bytecode_two_state_logic_registers,
    // Runtime-to-LLVM preparation ran on the module, and the byte globals
    // it materialized for an operation.
    "::mlir::UnitAttr":$llvm_prepared,
    "::mlir::ArrayAttr":$llvm_byte_globals
  );
}

#endif // OBELISK_DIALECT_RUNTIME_RUNTIMEDIALECT_TD
