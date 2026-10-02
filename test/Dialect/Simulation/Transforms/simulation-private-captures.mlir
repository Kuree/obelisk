// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-simplify-bodies{vpi=off},simulation.func(canonicalize,mem2reg,canonicalize)))' | FileCheck %s
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-simplify-bodies{vpi=read},simulation.func(canonicalize,mem2reg,canonicalize)))' | FileCheck %s --check-prefix=READ
module {
  simulation.design @captures {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !simulation.logic<8> design
    simulation.code_unit.decl 1 in 0 initial hierarchy "writer"
    simulation.code_unit.decl 2 in 0 root_initializer hierarchy "spawn"
    // A matched capture is reference transport. Flush before suspension and
    // reload on resume: another instance may have changed canonical state.
    // CHECK-LABEL: simulation.func @writer
    // CHECK-NOT: simulation.ref.alloc
    // CHECK: simulation.ref.store
    // CHECK-NOT: simulation.ref.store
    // CHECK: simulation.suspend.delay
    // CHECK: simulation.ref.load
    // CHECK: simulation.logic.binary xor
    // CHECK-NOT: simulation.ref.store
    // CHECK: simulation.ref.store
    // CHECK-NOT: simulation.ref.store
    // CHECK: simulation.return
    // READ-LABEL: simulation.func @writer
    // READ: simulation.ref.store
    // READ: simulation.ref.store
    // READ: simulation.suspend.delay
    // READ: simulation.ref.store
    // READ: simulation.ref.store
    simulation.func @writer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %root: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %input: !simulation.logic<8> {simulation.capture_kind = 2 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %mask = simulation.logic.constant 1 : i8, 0 : i8 : !simulation.logic<8>
      %a = simulation.logic.binary xor %input, %mask : !simulation.logic<8>
      simulation.ref.store %input to %root : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.ref.store %a to %root : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^resume
    ^resume:
      %old = simulation.ref.load %root : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %b = simulation.logic.binary xor %old, %input : !simulation.logic<8>
      simulation.ref.store %b to %root : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      %c = simulation.logic.binary xor %b, %mask : !simulation.logic<8>
      simulation.ref.store %c to %root : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.return
    }
    simulation.func @spawn(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 2 : i64} {
      %input = simulation.logic.constant 0 : i8, 255 : i8 : !simulation.logic<8>
      %root = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<8>>
      %process = simulation.spawn @writer(%ctx, %root, %input) : !simulation.context, !simulation.ref<!simulation.logic<8>>, !simulation.logic<8> -> !simulation.process
      simulation.return
    }
  }
}
