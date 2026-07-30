// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @native_body_patterns {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "native_body_patterns.identity"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "native_body_patterns.child"
    obelisk_sim.code_unit.decl 3 in 0 initial hierarchy "native_body_patterns.caller"

    obelisk_sim.func private @identity(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32},
        %value: i8 {obelisk_sim.capture_kind = 2 : i32}) -> i8
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      obelisk_sim.return %value : i8
    }

    obelisk_sim.func @child(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 2 : i64, entry_kind = 1 : i32} {
      obelisk_sim.return
    }

    obelisk_sim.func @caller(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32},
        %value: i8 {obelisk_sim.capture_kind = 2 : i32})
        attributes {code_unit_id = 3 : i64, entry_kind = 1 : i32} {
      %result = obelisk_sim.call @identity(%ctx, %value) :
          (!obelisk_sim.context, i8) -> i8
      %spawned = obelisk_sim.spawn @child(%ctx) :
          !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.return
    }
  }
}

// CHECK-LABEL: llvm.func @identity(
// CHECK: llvm.return %{{.*}} : i8
// CHECK-LABEL: llvm.func @child(
// CHECK: llvm.return %{{.*}} : i32
// CHECK-LABEL: llvm.func @caller(
// CHECK: llvm.call @identity
// CHECK: llvm.call @child.__obelisk_spawn
// CHECK: llvm.return %{{.*}} : i32
// CHECK-NOT: obelisk_sim.call
// CHECK-NOT: obelisk_sim.spawn
// CHECK-NOT: obelisk_sim.return
