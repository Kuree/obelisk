//===- NativeDialect.td - Native backend metadata ----------*- tablegen -*-===//

#ifndef OBELISK_DIALECT_NATIVE_NATIVEDIALECT_TD
#define OBELISK_DIALECT_NATIVE_NATIVEDIALECT_TD

include "mlir/IR/DialectBase.td"

def NativeDialect : Dialect {
  let name = "native";
  let cppNamespace = "::obelisk::native";
  let summary = "facts the native code generator carries between its passes";
  let description = [{
    `native` names the facts that native lowering, scheduling and the driver
    exchange through the IR: compilation options on the module, partition
    inventories for separate object emission, coroutine frame layouts and
    their replay certificates, and per-operation lowering certificates. It
    has no operations; its attributes annotate operations of other dialects
    and are consumed before LLVM translation.
  }];
  let useDefaultAttributePrinterParser = 1;
  let usePropertiesForAttributes = 0;
  let hasOperationAttrVerify = 1;
  let discardableAttrs = (ins
    // Module: compilation options.
    "::mlir::IntegerAttr":$optimization_level,
    "::mlir::IntegerAttr":$max_state_domain_functions,
    // Print native planning and timing diagnostics to stderr.
    "::mlir::UnitAttr":$debug_timing,
    // The driver lowers a complete executable and may omit unreferenced
    // implementation helpers. Object and IR emission keep public helpers.
    "::mlir::UnitAttr":$closed_executable,

    // Stable semantic partition of a function or global, independent of the
    // worker count, so object emission and caches share one boundary.
    "::mlir::StringAttr":$partition,
    // Design: `#native.partition` inventory planned before lowering.
    "::mlir::ArrayAttr":$partition_manifest,
    // Module: `#native.design_partitions`, preserved once designs are erased.
    "::mlir::ArrayAttr":$partition_manifests,
    // Module: `#native.element_default` images the runtime learns before any
    // process runs, preserved once designs are erased.
    "::mlir::ArrayAttr":$element_defaults,
    // Module: link-complete `#native.partition` inventory of LLVM symbols and
    // their cross-partition references, which object emission consumes.
    "::mlir::ArrayAttr":$physical_partition_manifest,

    // Coroutine frame a ramp function allocates.
    "::obelisk::native::FrameAttr":$frame,
    // Immutable canonical-frame certificate of a function, and the module
    // inventory of replayable functions. Valid only at the frames boundary,
    // before derived native specialization.
    "::obelisk::native::FrameInputsAttr":$frame_inputs,
    "::obelisk::native::FrameCheckpointAttr":$frame_checkpoint,

    // Lowered function entry.
    "::mlir::IntegerAttr":$entry_kind,
    "::mlir::IntegerAttr":$scratch_size,
    "::mlir::IntegerAttr":$observer_width,
    "::mlir::BoolAttr":$observer_four_state,
    "::mlir::DenseI64ArrayAttr":$spawn_capture_offsets,
    "::mlir::IntegerAttr":$spawn_capture_size,
    "::mlir::DenseI64ArrayAttr":$method_argument_sizes,
    "::mlir::DenseI64ArrayAttr":$method_argument_roots,
    "::mlir::IntegerAttr":$table_wait,
    "::mlir::UnitAttr":$unmanaged,
    "::mlir::UnitAttr":$shared_callback,
    "::mlir::UnitAttr":$runtime_publication_certified,
    "::mlir::UnitAttr":$owner_release_instrumented,
    // Per block, the argument indices whose unknown planes are zero.
    "::mlir::ArrayAttr":$two_state_block_unknowns,

    // Driver lowering certificates.
    "::mlir::IntegerAttr":$driver_id,
    "::mlir::IntegerAttr":$driver_low,
    "::mlir::IntegerAttr":$net_id,
    "::mlir::UnitAttr":$exact_driver_range,
    "::mlir::UnitAttr":$whole_driver,
    "::mlir::UnitAttr":$guarded_bulk_drive,
    "::mlir::UnitAttr":$clean_bulk_drive,

    // State access and managed-root certificates.
    "::mlir::UnitAttr":$bulk_copy_source_assume_clean,
    "::mlir::UnitAttr":$guarded_ref_store,
    "::mlir::UnitAttr":$in_bounds_handle_offset,
    "::mlir::UnitAttr":$managed_root_range_record,
    "::mlir::UnitAttr":$managed_root_range_push_check,
    // Ends the prologue that reacquires an activation's root record at a
    // resume target; coroutine lowering runs it only on scheduler resumption.
    "::mlir::UnitAttr":$managed_root_activation_prologue,
    "::mlir::DenseI64ArrayAttr":$transferred_references,
    "::mlir::DenseI32ArrayAttr":$no_transition_roots,

    // Globals generated for runtime tables.
    "::mlir::StringAttr":$string_global,
    "::mlir::StringAttr":$scan_prefix_global,
    "::mlir::StringAttr":$file_scan_prefix_global,
    "::mlir::StringAttr":$aggregate_bitstream_plan_global,
    "::mlir::StringAttr":$container_pattern_plan_global,
    "::mlir::IntegerAttr":$container_pattern_plan_size,
    "::mlir::IntegerAttr":$box_type_id,
    "::mlir::IntegerAttr":$vpi_local_id,
    "::mlir::IntegerAttr":$vpi_call_id
  );
}

#endif // OBELISK_DIALECT_NATIVE_NATIVEDIALECT_TD
