// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-simplify-bodies{vpi=off},simulation.func(canonicalize,mem2reg,canonicalize)))' | FileCheck %s
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-simplify-bodies{vpi=read}))' | FileCheck %s --check-prefix=READ
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-simplify-bodies{vpi=off},simulation.func(canonicalize,mem2reg,canonicalize,obelisk-sim-thread-suspension)),test-obelisk-activation-regions)' -o /dev/null 2>&1 | FileCheck %s --check-prefix=REGION
// REGION: regions @writer valid=1
// RUN: sed 's/!simulation.unpacked_array<0 : 1 x !record>/!simulation.packed_array<1 : 0 x !record>/g' %s > %t.packed.mlir
// RUN: obelisk-opt %t.packed.mlir --pass-pipeline='builtin.module(simulation.design(obelisk-sim-simplify-bodies{vpi=off},simulation.func(canonicalize,sroa,mem2reg,canonicalize,obelisk-sim-thread-suspension)),test-obelisk-activation-regions)' -o /dev/null 2>&1 | FileCheck %s --check-prefix=REGION
// IEEE 1800-2023 6.8 Table 6-7: unwritten four-state lanes remain X;
// two-state lanes remain zero. Private fields need no copy-in or flush when
// every mutable field is defined before reading in each activation.
!record = !simulation.packed_struct<[
  #simulation.field<name = "a", type = i8, ordinal = 0, packedOffset = 16>,
  #simulation.field<name = "b", type = i8, ordinal = 1, packedOffset = 8>,
  #simulation.field<name = "c", type = !simulation.logic<8>, ordinal = 2, packedOffset = 0>
]>
!array = !simulation.unpacked_array<0 : 1 x !record>
module {
  simulation.design @private_invariants {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !array design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.storage.decl 2 in 0 : !array design
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "writer"
    // CHECK-LABEL: simulation.func @writer
    // CHECK: simulation.aggregate.default
    // CHECK-NOT: simulation.ref.load %arg1
    // CHECK-NOT: simulation.ref.store {{.*}} to %arg1 : !simulation.unpacked_array
    // CHECK: simulation.suspend.change
    // READ-LABEL: simulation.func @writer
    // READ-NOT: simulation.ref.alloc
    // READ: simulation.ref.load %arg1
    simulation.func @writer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %root: !simulation.ref<!array> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %in: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %out: !simulation.ref<!array> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      cf.br ^body
    ^body:
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      %two = arith.constant 2 : i32
      %value = simulation.ref.load %in : !simulation.ref<i8> -> i8
      cf.br ^loop(%zero : i32)
    ^loop(%i: i32):
      %more = arith.cmpi slt, %i, %two : i32
      cf.cond_br %more, ^store, ^copy
    ^store:
      %element = simulation.ref.array_element %root[%i] : (!simulation.ref<!array>, i32) -> !simulation.ref<!record>
      %a = simulation.ref.subelement %element[[0]] : !simulation.ref<!record> -> !simulation.ref<i8>
      %b = simulation.ref.subelement %element[[1]] : !simulation.ref<!record> -> !simulation.ref<i8>
      simulation.ref.store %value to %a : i8, !simulation.ref<i8>
      simulation.ref.store %value to %b : i8, !simulation.ref<i8>
      %next = arith.addi %i, %one : i32
      cf.br ^loop(%next : i32)
    ^copy:
      %result = simulation.ref.load %root : !simulation.ref<!array> -> !array
      simulation.ref.store %result to %out : !array, !simulation.ref<!array>
      simulation.suspend.change %in to ^body : !simulation.ref<i8>
    }
  }
}
