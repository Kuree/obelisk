// REQUIRES: z3
// RUN: obelisk-opt %s --test-obelisk-scoped-integer-ranges -o %t.threaded 2>&1 | FileCheck %s
// RUN: obelisk-opt %s --mlir-disable-threading --test-obelisk-scoped-integer-ranges -o %t.serial 2>&1 | FileCheck %s
// RUN: diff %t.threaded %t.serial

!array = !simulation.unpacked_array<0 : 15 x i8>
module {
  simulation.design @bitvector_proofs {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !array design
    simulation.code_unit.decl 1 in 0 function hierarchy "bit_identity"
    simulation.func @bit_identity(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %seed: i8 {simulation.capture_kind = 2 : i32}, %mask: i8 {simulation.capture_kind = 2 : i32}, %xz: !simulation.logic<8> {simulation.capture_kind = 2 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %root = simulation.context.storage %ctx[0] : !simulation.ref<!array>
      %ones = arith.constant -1 : i8
      %one = arith.constant 1 : i8
      %inverse = arith.xori %mask, %ones : i8
      %low = arith.andi %seed, %mask : i8
      %high = arith.andi %seed, %inverse : i8
      %combined = arith.ori %low, %high : i8
      %difference = arith.subi %combined, %seed : i8
      %a = simulation.ref.array_element %root[%difference] {test.scoped = "smt-identity", test.prove_bounds = array<i64: 0, 15>, test.repeat_proof} : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %wrapped = arith.addi %seed, %one : i8
      %b = simulation.ref.array_element %root[%wrapped] {test.scoped = "smt-invalid", test.prove_bounds = array<i64: 0, 15>} : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %unknown = simulation.logic.binary sub %xz, %xz : !simulation.logic<8>
      %c = simulation.ref.array_element %root[%unknown] {test.scoped = "smt-xz", test.prove_bounds = array<i64: 0, 15>} : (!simulation.ref<!array>, !simulation.logic<8>) -> !simulation.ref<i8>
      %zero = arith.constant 0 : i8
      %positive = arith.cmpi sgt, %seed, %zero : i8
      %clamped = arith.select %positive, %seed, %zero : i8
      %d = simulation.ref.array_element %root[%clamped] {test.scoped = "smt-select", test.prove_bounds = array<i64: 0, 127>} : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %unsigned = arith.cmpi ugt, %seed, %zero : i8
      %unclamped = arith.select %unsigned, %seed, %zero : i8
      %e = simulation.ref.array_element %root[%unclamped] {test.scoped = "smt-unsigned-select", test.prove_bounds = array<i64: 0, 127>} : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      simulation.return
    }
    // CHECK: smt-identity umin=0 umax=255 proven=1
    // CHECK: smt-invalid umin=0 umax=255 proven=0
    // CHECK: smt-xz unknown proven=0
    // CHECK: smt-select {{.*}} proven=1
    // CHECK: smt-unsigned-select {{.*}} proven=0
  }
}
