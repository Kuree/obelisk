// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph),test-obelisk-dpi-effects)' -o /dev/null 2>&1 | FileCheck %s

// IEEE 1800-2023 35.5.1.3: an import that is not context reads and writes no
// SystemVerilog data other than its arguments. A context import may call any
// export, in any scope svSetScope selects (35.5.3); it does not enable VPI.
// With VPI off, a context call reaches the design only through exports.

// An export that computes from constants keeps every import transparent.
// CHECK-LABEL: design @constant_export exports transparent
// CHECK-NEXT: call start: transparent
// CHECK-NEXT: call poll: transparent
// An export that reads design state makes context calls opaque only.
// CHECK-LABEL: design @reading_export exports opaque
// CHECK-NEXT: call start: may access design
// CHECK-NEXT: call poll: transparent
// An exported task may suspend the importing thread (35.5.1.5).
// CHECK-LABEL: design @waiting_export exports opaque
// CHECK-NEXT: call start: may access design
// CHECK-NEXT: call poll: transparent
module {
  simulation.design @constant_export {
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
  simulation.design @reading_export {
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
      %s = simulation.context.storage %ctx[0] : !simulation.ref<i32>
      %period = simulation.ref.load %s : !simulation.ref<i32> -> i32
      simulation.return %period : i32
    }
  }
  simulation.design @waiting_export {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i32 design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "host"
    simulation.code_unit.decl 3 in 0 task hierarchy "period"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      %p = simulation.spawn @host(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func private @host(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 2 : i64, entry_kind = 1 : i32} {
      %c = simulation.dpi.call "start" id 1 scope 0 context %ctx : !simulation.context() {abi_signature = [], is_context = true, is_pure = false, is_task = false, source_column = 1 : i32, source_file = "host.sv", source_line = 1 : i32} : () -> !runtime.status
      %p = simulation.dpi.call "poll" id 2 scope 0 context %ctx : !simulation.context() {abi_signature = [], is_context = false, is_pure = false, is_task = false, source_column = 1 : i32, source_file = "host.sv", source_line = 2 : i32} : () -> !runtime.status
      simulation.return
    }
    simulation.func @period(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {
        code_unit_id = 3 : i64, entry_kind = 12 : i32, dpi.export,
        dpi.c_identifier = "get_period", dpi.export_id = 7 : i32, dpi.scope_id = 0 : i64,
        dpi.logical_inputs = 0 : i32,
        dpi.abi_signature = []} {
      %delay = simulation.time.constant 5
      simulation.suspend.delay %delay to ^done
    ^done:
      simulation.return
    }
  }
}
