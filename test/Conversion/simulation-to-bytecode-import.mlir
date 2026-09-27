// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' | FileCheck %s

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @imports {
    simulation.code_unit.decl 9000001 in 0 function hierarchy "test.imports.caller.9000001"
    simulation.scope.decl 0 hierarchy "top"

    simulation.func private @external_logic(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: !simulation.logic<129> {simulation.capture_kind = 1 : i32})
        -> !simulation.logic<129> attributes {entry_kind = 8 : i32}

    simulation.func @caller(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: !simulation.logic<129> {simulation.capture_kind = 1 : i32})
        -> !simulation.logic<129> attributes {entry_kind = 8 : i32, code_unit_id = 9000001 : i64} {
      %result = simulation.call @external_logic(%ctx, %value)
          : (!simulation.context, !simulation.logic<129>)
          -> !simulation.logic<129>
      simulation.return %result : !simulation.logic<129>
    }
  }
}

// CHECK: obelisk.bytecode.image = array<i8: 79, 66, 66, 67, 68, 83, 49, 0
// CHECK: obelisk.bytecode.function = 0 : i32
// CHECK: obelisk.bytecode.scratch_size = 176 : i64
// CHECK: simulation.call @external_logic
