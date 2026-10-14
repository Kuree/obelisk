//===- SimulationDialect.td - Executable Obelisk simulation IR -*- td -*-===//

#ifndef OBELISK_DIALECT_SIMULATION_SIMULATIONDIALECT_TD
#define OBELISK_DIALECT_SIMULATION_SIMULATIONDIALECT_TD

include "mlir/IR/DialectBase.td"

def ObeliskSimulationDialect : Dialect {
  let name = "simulation";
  let cppNamespace = "::obelisk::sim";
  let dependentDialects = ["::obelisk::schedule::ScheduleDialect", "::mlir::arith::ArithDialect",
                           "::mlir::math::MathDialect",
                           "::obelisk::runtime::ObeliskRuntimeDialect",
                           "::obelisk::cov::CoverageDialect",
                           "::obelisk::dpi::DPIDialect",
                           "::obelisk::native::NativeDialect"];
  let summary = "Target-independent executable SystemVerilog simulation IR";
  let description = [{
    `simulation` is the SSA and explicit-effect boundary after elaborated
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
  let hasOperationAttrVerify = 1;
  let hasRegionArgAttrVerify = 1;
  // Most `simulation.*` metadata predates declared attributes; these are
  // declared and verified.
  let discardableAttrs = (ins
    // Integer range certificates: per function argument, per call or
    // function result (a `#simulation.integer_range` or unit), and symbolic
    // return summaries (a `#simulation.integer_expression` or unit).
    "::mlir::Attribute":$integer_range,
    "::mlir::ArrayAttr":$result_ranges,
    "::mlir::ArrayAttr":$result_exprs,
    // Transient no-X/Z certificate for one semantic logic result.
    "::mlir::UnitAttr":$known_logic_result,
    // Module: compiled code roots managed values only at GC polls and at
    // calls that may reach one.
    "::mlir::UnitAttr":$gc_polled,
    // Function: the inliner's cached callee-invariant `InlineLegality`.
    "::mlir::IntegerAttr":$inline_legality,
    // Module: driver pulse controls for path delays (IEEE 1800-2023 30.7).
    "::mlir::IntegerAttr":$pulse_reject_percent,
    "::mlir::IntegerAttr":$pulse_error_percent,
    "::mlir::BoolAttr":$pulse_on_detect,
    "::mlir::BoolAttr":$pulse_show_cancelled
  );
}

#endif // OBELISK_DIALECT_SIMULATION_SIMULATIONDIALECT_TD
