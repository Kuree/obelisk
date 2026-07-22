// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' | FileCheck %s

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @imports {
    obelisk_sim.code_unit.decl 9000001 in 0 function hierarchy "test.imports.caller.9000001"
    obelisk_sim.scope.decl 0 hierarchy "top"

    obelisk_sim.func private @external_logic(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: !obelisk_sim.logic<129> {obelisk_sim.capture_kind = 1 : i32})
        -> !obelisk_sim.logic<129> attributes {entry_kind = 8 : i32}

    obelisk_sim.func @caller(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: !obelisk_sim.logic<129> {obelisk_sim.capture_kind = 1 : i32})
        -> !obelisk_sim.logic<129> attributes {entry_kind = 8 : i32, code_unit_id = 9000001 : i64} {
      %result = obelisk_sim.call @external_logic(%ctx, %value)
          : (!obelisk_sim.context, !obelisk_sim.logic<129>)
          -> !obelisk_sim.logic<129>
      obelisk_sim.return %result : !obelisk_sim.logic<129>
    }
  }
}

// CHECK: obelisk.bytecode.image = array<i8: 79, 66, 66, 67, 68, 83, 49, 0
// CHECK: obelisk.bytecode.function = 0 : i32
// CHECK: obelisk.bytecode.scratch_size = 176 : i64
// CHECK: obelisk_sim.call @external_logic
