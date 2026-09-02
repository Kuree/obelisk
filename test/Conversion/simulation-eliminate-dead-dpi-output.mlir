// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-eliminate-dead-captures))' | FileCheck %s
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-eliminate-dead-captures),obelisk-sim-materialize-dpi-exports,convert-obelisk-sim-processes-to-llvm-coroutines)' | FileCheck %s --check-prefix=NATIVE
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-eliminate-dead-captures),obelisk-sim-materialize-dpi-exports,obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off require-bytecode=true},convert-obelisk-sim-processes-to-llvm-coroutines)' | FileCheck %s --check-prefix=BYTECODE

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  obelisk_sim.has_dpi_exports
} {
  obelisk_sim.design @dead_dpi_output {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.storage.decl 0 in 0 : i32 design
    obelisk_sim.code_unit.decl 1 in 0 task hierarchy "top.exported"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "top.caller"
    obelisk_sim.code_unit.decl 3 in 0 root_initializer hierarchy "top.root"

    // The exported C ABI keeps its output and copy-out entries.  Only the
    // unused internal output copy-in is removed from the task activation.
    // CHECK-LABEL: obelisk_sim.func nested @exported(
    // CHECK-SAME: %arg0: !obelisk_sim.context
    // CHECK-SAME: %arg1: !obelisk_sim.ref<i32>
    // CHECK-SAME: obelisk_sim.dpi_abi_signature = [
    // CHECK-SAME: direction = output
    // CHECK-SAME: direction = output
    // CHECK-SAME: obelisk_sim.dpi_elided_inputs = array<i64: 0>
    // CHECK-SAME: obelisk_sim.dpi_logical_inputs = 1 : i32
    // CHECK-NOT: obelisk_sim.bindings
    obelisk_sim.func nested @exported(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %unused_copy_in: i32 {obelisk_sim.capture_kind = 1 : i32},
        %destination: !obelisk_sim.ref<i32>
            {obelisk_sim.capture_kind = 1 : i32})
        attributes {
          code_unit_id = 1 : i64, entry_kind = 12 : i32,
          obelisk_sim.dpi_export,
          obelisk_sim.dpi_c_identifier = "exported_c",
          obelisk_sim.dpi_export_id = 1 : i32,
          obelisk_sim.dpi_scope_id = 0 : i64,
          obelisk_sim.dpi_logical_inputs = 1 : i32,
          obelisk_sim.bindings = [
            #obelisk_sim.argument_binding<path = "top.exported.value",
                argument = 1, kind = formal_local, copyOut = true,
                copyIn = false>,
            #obelisk_sim.argument_binding<path = "top.exported.value",
                argument = 2, kind = copy_out_destination, copyOut = false>
          ],
          obelisk_sim.dpi_abi_signature = [
            #obelisk_sim.dpi_abi<kind = int, direction = output, width = 32,
                                  fourState = false, isSigned = true>,
            #obelisk_sim.dpi_abi<kind = int, direction = output, width = 32,
                                  fourState = false, isSigned = true>
          ]
        } {
      %answer = arith.constant 42 : i32
      obelisk_sim.ref.store %answer to %destination :
          i32, !obelisk_sim.ref<i32>
      obelisk_sim.return
    }

    // CHECK-LABEL: obelisk_sim.func private @caller(
    // CHECK: obelisk_sim.task.call @exported(%arg0, %arg1) arguments 2
    obelisk_sim.func private @caller(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %destination: !obelisk_sim.ref<i32>
            {obelisk_sim.capture_kind = 1 : i32})
        attributes {code_unit_id = 2 : i64, entry_kind = 1 : i32} {
      %zero = arith.constant 0 : i32
      obelisk_sim.task.call @exported(%ctx, %zero, %destination)
          arguments 3 to ^done : !obelisk_sim.context, i32,
          !obelisk_sim.ref<i32>
    ^done:
      obelisk_sim.return
    }

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 3 : i64, entry_kind = 0 : i32} {
      %destination = obelisk_sim.context.storage %ctx[0] :
          !obelisk_sim.ref<i32>
      %caller = obelisk_sim.spawn @caller(%ctx, %destination) :
          !obelisk_sim.context, !obelisk_sim.ref<i32> -> !obelisk_sim.process
      obelisk_sim.return
    }
  }
}

// NATIVE-LABEL: llvm.func internal @exported.__obelisk_dpi_export_bridge.__obelisk_activate_checked(
// NATIVE-SAME: %{{.*}}: !llvm.ptr, %{{.*}}: i64, %{{.*}}: !llvm.ptr) -> i32
// NATIVE: llvm.call @exported.__obelisk_dpi_export_bridge.__obelisk_activate_checked({{.*}}) : (!llvm.ptr, i64, !llvm.ptr) -> i32

// BYTECODE-LABEL: llvm.func @exported.__obelisk_dpi_export_bridge.__obelisk_dpi_export(
// BYTECODE: llvm.mlir.constant(-127 : i8) : i8
// BYTECODE: llvm.call @obelisk_rt_v1_dpi_export_task_bytecode_run
