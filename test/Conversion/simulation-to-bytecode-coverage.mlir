// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' \
// RUN:   | FileCheck %s --check-prefix=ENCODE
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | FileCheck %s --check-prefix=LOWER

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @coverage {
    obelisk_sim.scope.decl 0 hierarchy "coverage"
    obelisk_sim.covergroup.decl @cg id 12 bins [1, 2]
    obelisk_sim.code_unit.decl 120 in 0 initial
      hierarchy "coverage.process"
    obelisk_sim.storage.decl 0 in 0
      : !obelisk_sim.covergroup_handle<@cg> design hierarchy "coverage.c"

    obelisk_sim.func @process(
        %ctx: !obelisk_sim.context
          {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 120 : i64, entry_kind = 1 : i32} {
      %null = obelisk_sim.covergroup.null
        : !obelisk_sim.covergroup_handle<@cg>
      %type_percentage, %type_covered, %type_total =
        obelisk_sim.covergroup.type_query %ctx from @cg
        : !obelisk_sim.context -> (f64, i32, i32)
      %created = obelisk_sim.covergroup.create %ctx from @cg
        : !obelisk_sim.context -> !obelisk_sim.covergroup_handle<@cg>
      %enabled = obelisk_sim.covergroup.sample_enabled %ctx, %created
        : (!obelisk_sim.context,
           !obelisk_sim.covergroup_handle<@cg>) -> i1
      obelisk_sim.covergroup.bin_hit %ctx, %created[1, 1]
        : !obelisk_sim.context, !obelisk_sim.covergroup_handle<@cg>
      obelisk_sim.covergroup.sample %ctx, %created[
          %enabled, %enabled, %enabled]
        : !obelisk_sim.context, !obelisk_sim.covergroup_handle<@cg>
      obelisk_sim.covergroup.stop %ctx, %created
        : !obelisk_sim.context, !obelisk_sim.covergroup_handle<@cg>
      obelisk_sim.covergroup.start %ctx, %created
        : !obelisk_sim.context, !obelisk_sim.covergroup_handle<@cg>
      %percentage, %covered, %total =
        obelisk_sim.covergroup.instance_query %ctx, %created
        : (!obelisk_sim.context,
           !obelisk_sim.covergroup_handle<@cg>) -> (f64, i32, i32)
      obelisk_sim.return
    }
  }
}

// The encoder accepts covergroup handles as 64-bit design state and encodes
// every coverage intrinsic into the process image.
// ENCODE: obelisk.bytecode.image = array<i8: 79, 66, 66, 67, 68, 83, 49, 0
// ENCODE: obelisk.execution.state_bits = 64 : i64
// ENCODE: obelisk_sim.covergroup.decl @cg id 12 bins [1, 2]
// ENCODE: obelisk.bytecode.function = 0 : i32
// ENCODE-SAME: obelisk.bytecode.scratch_alignment = 8 : i64
// ENCODE: obelisk_sim.covergroup.type_query
// ENCODE: obelisk_sim.covergroup.create
// ENCODE: obelisk_sim.covergroup.sample_enabled
// ENCODE: obelisk_sim.covergroup.bin_hit
// ENCODE: obelisk_sim.covergroup.sample
// ENCODE: obelisk_sim.covergroup.stop
// ENCODE: obelisk_sim.covergroup.start
// ENCODE: obelisk_sim.covergroup.instance_query

// Bytecode execution dispatches the same shared runtime ABI as native code.
// LOWER-DAG: llvm.func @obelisk_rt_v1_covergroup_create
// LOWER-DAG: llvm.func @obelisk_rt_v1_covergroup_sample_enabled
// LOWER-DAG: llvm.func @obelisk_rt_v1_covergroup_bin_hit
// LOWER-DAG: llvm.func @obelisk_rt_v1_covergroup_sample
// LOWER-DAG: llvm.func @obelisk_rt_v1_covergroup_set_enabled
// LOWER-DAG: llvm.func @obelisk_rt_v1_covergroup_instance_query
// LOWER-DAG: llvm.func @obelisk_rt_v1_covergroup_type_query
// LOWER: llvm.mlir.global internal constant @process.__obelisk_bytecode_entry
