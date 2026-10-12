// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s

// A read whose location the executing path proved known loads only the value
// plane; the same read without the proof also loads the unknown plane.

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @known_read_lowering {
    simulation.code_unit.decl 80 in 0 function hierarchy "top.known"
    simulation.code_unit.decl 81 in 0 function hierarchy "top.canonical"
    simulation.code_unit.decl 82 in 0 initial hierarchy "top.root"
    simulation.scope.decl 0 hierarchy "top"
    simulation.storage.decl 0 in 0 : !simulation.logic<16> design
        hierarchy "top.source"

    simulation.func @known(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        -> i16 attributes {code_unit_id = 80 : i64, entry_kind = 8 : i32} {
      %source = simulation.context.storage %ctx[0] :
          !simulation.ref<!simulation.logic<16>>
      %value = simulation.ref.load %source {schedule.native.known_read} :
          !simulation.ref<!simulation.logic<16>> -> !simulation.logic<16>
      %bits = simulation.logic.to_bits %value : !simulation.logic<16> -> i16
      simulation.return %bits : i16
    }

    simulation.func @canonical(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        -> i16 attributes {code_unit_id = 81 : i64, entry_kind = 8 : i32} {
      %source = simulation.context.storage %ctx[0] :
          !simulation.ref<!simulation.logic<16>>
      %value = simulation.ref.load %source :
          !simulation.ref<!simulation.logic<16>> -> !simulation.logic<16>
      %bits = simulation.logic.to_bits %value : !simulation.logic<16> -> i16
      simulation.return %bits : i16
    }

    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 82 : i64, entry_kind = 1 : i32} {
      %a = simulation.call @known(%ctx) : (!simulation.context) -> i16
      %b = simulation.call @canonical(%ctx) : (!simulation.context) -> i16
      simulation.return
    }
  }
}

// CHECK-LABEL: llvm.func @known(
// CHECK-NOT: __obelisk_state_unknown
// CHECK: llvm.return
// CHECK-LABEL: llvm.func @canonical(
// CHECK: __obelisk_state_unknown
// CHECK: llvm.return
