// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(talos-sim-eliminate-dead-boundaries))' | FileCheck %s

#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module {
  simulation.design @locals {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i32 design
    simulation.code_unit.decl 1 in 0 function hierarchy "top.target"
    simulation.code_unit.decl 2 in 0 function hierarchy "top.caller"
    simulation.vpi_local.decl @local id 19 in 1 scope 0 ordinal 0 hierarchy "top.target.value" debug "value" : i32 {vpi_type = #word}

    // Removing the preceding dead argument renumbers the formal binding while
    // preserving its exact source declaration.
    // CHECK-LABEL: simulation.func private @target(
    // CHECK-SAME: %arg1: i32
    // CHECK-SAME: %arg2: !simulation.ref<i32>
    // CHECK-SAME: simulation.bindings = [#simulation.argument_binding<path = "top.target.value", argument = 1, kind = formal_local, copyOut = false, vpiLocal = @local>]
    simulation.func private @target(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %dead: i32 {simulation.capture_kind = 1 : i32},
        %value: i32 {simulation.capture_kind = 1 : i32},
        %output: !simulation.ref<i32> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64,
          simulation.bindings = [#simulation.argument_binding<path = "top.target.value", argument = 2, kind = formal_local, copyOut = false, vpiLocal = @local>]} {
      simulation.ref.store %value to %output : i32, !simulation.ref<i32>
      simulation.return
    }
    simulation.func @caller(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %output: !simulation.ref<i32> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %dead = arith.constant 0 : i32
      %value = arith.constant 23 : i32
      simulation.call @target(%ctx, %dead, %value, %output)
          : (!simulation.context, i32, i32, !simulation.ref<i32>) -> ()
      simulation.return
    }
  }
}
