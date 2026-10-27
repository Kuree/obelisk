// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(talos-sim-eliminate-dead-captures))' | FileCheck %s
// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(talos-sim-eliminate-dead-captures),talos-sim-materialize-dpi-exports,convert-talos-sim-processes-to-llvm-coroutines)' | FileCheck %s --check-prefix=NATIVE
// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(talos-sim-eliminate-dead-captures),talos-sim-materialize-dpi-exports,simulation.design(talos-sim-build-compute-graph,talos-sim-verify-compute-graph),encode-talos-sim-to-bytecode{vpi=off require-bytecode=true},convert-talos-sim-processes-to-llvm-coroutines)' | FileCheck %s --check-prefix=BYTECODE

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  dpi.has_exports
} {
  simulation.design @dead_dpi_output {
    simulation.scope.decl 0 hierarchy "top"
    simulation.storage.decl 0 in 0 : i32 design
    simulation.code_unit.decl 1 in 0 task hierarchy "top.exported"
    simulation.code_unit.decl 2 in 0 initial hierarchy "top.caller"
    simulation.code_unit.decl 3 in 0 root_initializer hierarchy "top.root"

    // The exported C ABI keeps its output and copy-out entries.  Only the
    // unused internal output copy-in is removed from the task activation.
    // CHECK-LABEL: simulation.func nested @exported(
    // CHECK-SAME: %arg0: !simulation.context
    // CHECK-SAME: %arg1: !simulation.ref<i32>
    // CHECK-SAME: dpi.abi_signature = [
    // CHECK-SAME: direction = output
    // CHECK-SAME: direction = output
    // CHECK-SAME: dpi.elided_inputs = array<i64: 0>
    // CHECK-SAME: dpi.logical_inputs = 1 : i32
    // CHECK-SAME: simulation.bindings = []
    simulation.func nested @exported(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %unused_copy_in: i32 {simulation.capture_kind = 1 : i32},
        %destination: !simulation.ref<i32>
            {simulation.capture_kind = 1 : i32})
        attributes {
          code_unit_id = 1 : i64, entry_kind = 12 : i32,
          dpi.export,
          dpi.c_identifier = "exported_c",
          dpi.export_id = 1 : i32,
          dpi.scope_id = 0 : i64,
          dpi.logical_inputs = 1 : i32,
          simulation.bindings = [
            #simulation.argument_binding<path = "top.exported.value",
                argument = 1, kind = formal_local, copyOut = true,
                copyIn = false>,
            #simulation.argument_binding<path = "top.exported.value",
                argument = 2, kind = copy_out_destination, copyOut = false>
          ],
          dpi.abi_signature = [
            #dpi.abi<kind = int, direction = output, width = 32,
                                  fourState = false, isSigned = true>,
            #dpi.abi<kind = int, direction = output, width = 32,
                                  fourState = false, isSigned = true>
          ]
        } {
      %answer = arith.constant 42 : i32
      simulation.ref.store %answer to %destination :
          i32, !simulation.ref<i32>
      simulation.return
    }

    // CHECK-LABEL: simulation.func private @caller(
    // CHECK: simulation.task.call @exported(%arg0, %arg1) arguments 2
    simulation.func private @caller(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %destination: !simulation.ref<i32>
            {simulation.capture_kind = 1 : i32})
        attributes {code_unit_id = 2 : i64, entry_kind = 1 : i32} {
      %zero = arith.constant 0 : i32
      simulation.task.call @exported(%ctx, %zero, %destination)
          arguments 3 to ^done : !simulation.context, i32,
          !simulation.ref<i32>
    ^done:
      simulation.return
    }

    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 3 : i64, entry_kind = 0 : i32} {
      %destination = simulation.context.storage %ctx[0] :
          !simulation.ref<i32>
      %caller = simulation.spawn @caller(%ctx, %destination) :
          !simulation.context, !simulation.ref<i32> -> !simulation.process
      simulation.return
    }
  }
}

// NATIVE-LABEL: llvm.func internal @exported.__talos_dpi_export_bridge.__talos_activate_checked(
// NATIVE-SAME: %{{.*}}: !llvm.ptr, %{{.*}}: i64, %{{.*}}: !llvm.ptr) -> i32
// NATIVE: llvm.call @exported.__talos_dpi_export_bridge.__talos_activate_checked({{.*}}) : (!llvm.ptr, i64, !llvm.ptr) -> i32

// BYTECODE-LABEL: llvm.func @exported.__talos_dpi_export_bridge.__talos_dpi_export(
// BYTECODE: llvm.mlir.constant(-127 : i8) : i8
// BYTECODE: llvm.call @talos_rt_v1_dpi_export_task_bytecode_run
