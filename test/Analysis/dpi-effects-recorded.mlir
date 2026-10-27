// RUN: talos-opt %s --pass-pipeline='builtin.module(test-talos-dpi-effects)' -o /dev/null 2>&1 | FileCheck %s
// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(talos-sim-build-compute-graph))' | FileCheck %s --check-prefix=RECORD

// Export transparency is judged from compute-graph effect summaries, which
// later rewrites may drop. Building the graph records the answer on the
// design; without summaries, only that record keeps a context import
// transparent (IEEE 1800-2023 35.5.3).

// CHECK-LABEL: design @recorded exports transparent
// CHECK-LABEL: design @unrecorded exports opaque
// RECORD-LABEL: simulation.design @recorded
// RECORD-SAME: dpi.transparent_exports = true
// RECORD-LABEL: simulation.design @unrecorded
// RECORD-SAME: dpi.transparent_exports = true
module {
  simulation.design @recorded attributes {dpi.transparent_exports = true} {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i32 design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "host"
    simulation.code_unit.decl 3 in 0 function hierarchy "period"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      %p = simulation.spawn @host(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func private @host(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 2 : i64, entry_kind = 1 : i32} {
      %c = simulation.dpi.call "start" id 1 scope 0 context %ctx : !simulation.context() {abi_signature = [], is_context = true, is_pure = false, is_task = false, source_column = 1 : i32, source_file = "host.sv", source_line = 1 : i32} : () -> !runtime.status
      %p = simulation.dpi.call "poll" id 2 scope 0 context %ctx : !simulation.context() {abi_signature = [], is_context = false, is_pure = false, is_task = false, source_column = 1 : i32, source_file = "host.sv", source_line = 2 : i32} : () -> !runtime.status
      simulation.return
    }
    simulation.func @period(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i32 attributes {
        code_unit_id = 3 : i64, entry_kind = 8 : i32, dpi.export,
        dpi.c_identifier = "get_period", dpi.export_id = 7 : i32, dpi.scope_id = 0 : i64,
        dpi.logical_inputs = 0 : i32,
        dpi.abi_signature = [#dpi.abi<kind = int, direction = result, width = 32, fourState = false, isSigned = true>]} {
      %period = arith.constant 1000 : i32
      simulation.return %period : i32
    }
  }
  simulation.design @unrecorded {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i32 design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "host"
    simulation.code_unit.decl 3 in 0 function hierarchy "period"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      %p = simulation.spawn @host(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func private @host(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 2 : i64, entry_kind = 1 : i32} {
      %c = simulation.dpi.call "start" id 1 scope 0 context %ctx : !simulation.context() {abi_signature = [], is_context = true, is_pure = false, is_task = false, source_column = 1 : i32, source_file = "host.sv", source_line = 1 : i32} : () -> !runtime.status
      %p = simulation.dpi.call "poll" id 2 scope 0 context %ctx : !simulation.context() {abi_signature = [], is_context = false, is_pure = false, is_task = false, source_column = 1 : i32, source_file = "host.sv", source_line = 2 : i32} : () -> !runtime.status
      simulation.return
    }
    simulation.func @period(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i32 attributes {
        code_unit_id = 3 : i64, entry_kind = 8 : i32, dpi.export,
        dpi.c_identifier = "get_period", dpi.export_id = 7 : i32, dpi.scope_id = 0 : i64,
        dpi.logical_inputs = 0 : i32,
        dpi.abi_signature = [#dpi.abi<kind = int, direction = result, width = 32, fourState = false, isSigned = true>]} {
      %period = arith.constant 1000 : i32
      simulation.return %period : i32
    }
  }
}
