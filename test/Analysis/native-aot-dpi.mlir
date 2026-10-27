// RUN: talos-opt %s --split-input-file -o /dev/null \
// RUN:   --pass-pipeline='builtin.module(simulation.design(talos-sim-build-compute-graph,talos-sim-verify-compute-graph),test-talos-native-aot-analysis)' \
// RUN:   2>&1 | FileCheck %s --check-prefix=CHECK --check-prefix=NATIVE
// RUN: talos-opt %s --split-input-file -o /dev/null \
// RUN:   --pass-pipeline='builtin.module(simulation.design(talos-sim-build-compute-graph{vpi=read},talos-sim-verify-compute-graph),test-talos-native-aot-analysis)' \
// RUN:   2>&1 | FileCheck %s --check-prefix=CHECK --check-prefix=NATIVE
// RUN: talos-opt %s --split-input-file -o /dev/null \
// RUN:   --pass-pipeline='builtin.module(simulation.design(talos-sim-build-compute-graph{vpi=full},talos-sim-verify-compute-graph),test-talos-native-aot-analysis)' \
// RUN:   2>&1 | FileCheck %s --check-prefix=CHECK --check-prefix=WRITABLE

// IEEE 1800-2023 35.5.3: a context import may call any export of the design.
// When no export reaches design state, suspends (35.5.1.5) or controls
// processes, the import touches only its own arguments: the process calling
// it stays native, and so does a task that calls it, which then never
// suspends (13.3). VPI reads change nothing; a VPI write (even from a
// noncontext import, whose VPI effects 35.5.3 calls unpredictable) must land
// at a scheduler boundary.

// NATIVE: native-aot eligible=true fully=true
// NATIVE-NEXT: actor 0 @root
// NATIVE-NEXT: actor 1 @host
// NATIVE-NOT: bytecode
// NATIVE-NOT: reason
// WRITABLE: native-aot eligible=true fully=false
// WRITABLE-NEXT: actor 0 @root
// WRITABLE-NEXT: bytecode @host bb0
// WRITABLE-NEXT: bytecode @poll_task bb0
module {
  simulation.design @constant_export {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "host"
    simulation.code_unit.decl 3 in 0 task hierarchy "poll_task"
    simulation.code_unit.decl 4 in 0 function hierarchy "period"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      %p = simulation.spawn @host(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func private @host(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 2 : i64, entry_kind = 1 : i32} {
      %c = simulation.dpi.call "start" id 1 scope 0 context %ctx : !simulation.context() {abi_signature = [], is_context = true, is_pure = false, is_task = false, source_column = 1 : i32, source_file = "host.sv", source_line = 1 : i32} : () -> !runtime.status
      simulation.task.call @poll_task(%ctx) arguments 1 to ^after : !simulation.context
    ^after:
      simulation.return
    }
    simulation.func private @poll_task(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 3 : i64, entry_kind = 12 : i32} {
      %p = simulation.dpi.call "poll" id 2 scope 0 context %ctx : !simulation.context() {abi_signature = [], is_context = true, is_pure = false, is_task = false, source_column = 1 : i32, source_file = "host.sv", source_line = 2 : i32} : () -> !runtime.status
      simulation.return
    }
    simulation.func @period(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i32 attributes {
        code_unit_id = 4 : i64, entry_kind = 8 : i32, dpi.export,
        dpi.c_identifier = "get_period", dpi.export_id = 7 : i32, dpi.scope_id = 0 : i64,
        dpi.logical_inputs = 0 : i32,
        dpi.abi_signature = [#dpi.abi<kind = int, direction = result, width = 32, fourState = false, isSigned = true>]} {
      %period = arith.constant 1000 : i32
      simulation.return %period : i32
    }
  }
}

// -----

// An export that reads design state may run inside either call, so both stay
// with the runtime scheduler.

// CHECK: native-aot eligible=true fully=false
// CHECK-NEXT: actor 0 @root
// CHECK-NEXT: bytecode @host bb0
// CHECK-NEXT: bytecode @poll_task bb0
// CHECK-DAG: reason DPI reentrancy is present
// CHECK-DAG: reason task, await, or join control is present
module {
  simulation.design @reading_export {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i32 design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "host"
    simulation.code_unit.decl 3 in 0 task hierarchy "poll_task"
    simulation.code_unit.decl 4 in 0 function hierarchy "period"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      %p = simulation.spawn @host(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func private @host(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 2 : i64, entry_kind = 1 : i32} {
      %c = simulation.dpi.call "start" id 1 scope 0 context %ctx : !simulation.context() {abi_signature = [], is_context = true, is_pure = false, is_task = false, source_column = 1 : i32, source_file = "host.sv", source_line = 1 : i32} : () -> !runtime.status
      simulation.task.call @poll_task(%ctx) arguments 1 to ^after : !simulation.context
    ^after:
      simulation.return
    }
    simulation.func private @poll_task(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 3 : i64, entry_kind = 12 : i32} {
      %p = simulation.dpi.call "poll" id 2 scope 0 context %ctx : !simulation.context() {abi_signature = [], is_context = true, is_pure = false, is_task = false, source_column = 1 : i32, source_file = "host.sv", source_line = 2 : i32} : () -> !runtime.status
      simulation.return
    }
    simulation.func @period(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i32 attributes {
        code_unit_id = 4 : i64, entry_kind = 8 : i32, dpi.export,
        dpi.c_identifier = "get_period", dpi.export_id = 7 : i32, dpi.scope_id = 0 : i64,
        dpi.logical_inputs = 0 : i32,
        dpi.abi_signature = [#dpi.abi<kind = int, direction = result, width = 32, fourState = false, isSigned = true>]} {
      %s = simulation.context.storage %ctx[0] : !simulation.ref<i32>
      %period = simulation.ref.load %s : !simulation.ref<i32> -> i32
      simulation.return %period : i32
    }
  }
}
