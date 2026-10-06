// RUN: obelisk-opt %s --test-obelisk-activation-memory -o /dev/null 2>&1 | FileCheck %s
// RUN: obelisk-opt %s --test-obelisk-activation-memory='maximum-partitions=0' -o /dev/null 2>&1 | FileCheck %s --check-prefix=LIMIT

module {
  simulation.design @edges {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i16 design
    simulation.storage.decl 1 in 0 : i1 design
    simulation.storage.decl 2 in 0 : !simulation.unpacked_array<-1 : -3 x i8> design
    simulation.storage.decl 3 in 0 : !simulation.unpacked_array<0 : 0 x i8> design

    // A joined handle's hull is a may-write, not a definite write of both bytes.
    // CHECK: activation @joined_reference valid=1 complete=0
    // CHECK-NEXT: live-in s1[0,1)
    // CHECK-NEXT: must-write{{$}}
    // CHECK-NEXT: may-write s0[0,16)
    // LIMIT: activation @joined_reference valid=0 complete=0
    simulation.code_unit.decl 1 in 0 function hierarchy "joined_reference"
    simulation.func @joined_reference(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %out = simulation.context.storage %ctx[0] : !simulation.ref<i16>
      %flag = simulation.context.storage %ctx[1] : !simulation.ref<i1>
      %a = simulation.ref.extract %out from 0 : !simulation.ref<i16> -> !simulation.ref<i8>
      %b = simulation.ref.extract %out from 8 : !simulation.ref<i16> -> !simulation.ref<i8>
      %value = arith.constant 0 : i8
      %test = simulation.ref.load %flag : !simulation.ref<i1> -> i1
      cf.cond_br %test, ^join(%a : !simulation.ref<i8>), ^join(%b : !simulation.ref<i8>)
    ^join(%ref: !simulation.ref<i8>):
      simulation.ref.store %value to %ref : i8, !simulation.ref<i8>
      simulation.return
    }

    // A zero-trip loop writes nothing. In particular it cannot claim coverage
    // just because a syntactic store exists in the body.
    // CHECK: activation @zero_trip valid=1 complete=1
    // CHECK-NEXT: live-in{{$}}
    // CHECK-NEXT: must-write{{$}}
    // CHECK-NEXT: may-write{{$}}
    // LIMIT: activation @zero_trip valid=0 complete=0
    simulation.code_unit.decl 2 in 0 function hierarchy "zero_trip"
    simulation.func @zero_trip(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %out = simulation.context.storage %ctx[0] : !simulation.ref<i16>
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      %value = arith.constant 0 : i16
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %test = arith.cmpi slt, %i, %zero : i32
      cf.cond_br %test, ^body, ^exit
    ^body:
      simulation.ref.store %value to %out : i16, !simulation.ref<i16>
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }

    // Negative declaration bounds and descending induction select all three
    // physical elements, independent of their source index spelling.
    // CHECK: activation @descending valid=1 complete=1
    // CHECK-NEXT: live-in{{$}}
    // CHECK-NEXT: must-write s2[0,24)
    // CHECK-NEXT: may-write s2[0,24)
    // CHECK-NEXT: repeated{{$}}
    // LIMIT: activation @descending valid=0 complete=0
    simulation.code_unit.decl 3 in 0 function hierarchy "descending"
    simulation.func @descending(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %out = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.unpacked_array<-1 : -3 x i8>>
      %start = arith.constant -1 : i32
      %limit = arith.constant -3 : i32
      %one = arith.constant 1 : i32
      %value = arith.constant 0 : i8
      cf.br ^head(%start : i32)
    ^head(%i: i32):
      %test = arith.cmpi sge, %i, %limit : i32
      cf.cond_br %test, ^body, ^exit
    ^body:
      %dst = simulation.ref.array_element %out[%i] : (!simulation.ref<!simulation.unpacked_array<-1 : -3 x i8>>, i32) -> !simulation.ref<i8>
      simulation.ref.store %value to %dst : i8, !simulation.ref<i8>
      %next = arith.subi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }

    // Even a singleton array is not definitely written through an X index.
    // CHECK: activation @unknown_index valid=1 complete=0
    // CHECK-NEXT: live-in{{$}}
    // CHECK-NEXT: must-write{{$}}
    // CHECK-NEXT: may-write s3[0,8)
    // LIMIT: activation @unknown_index valid=0 complete=0
    simulation.code_unit.decl 4 in 0 function hierarchy "unknown_index"
    simulation.func @unknown_index(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %out = simulation.context.storage %ctx[3] : !simulation.ref<!simulation.unpacked_array<0 : 0 x i8>>
      %x = simulation.logic.constant 0 : i65, 1 : i65 : !simulation.logic<65>
      %value = arith.constant 0 : i8
      %dst = simulation.ref.array_element %out[%x] : (!simulation.ref<!simulation.unpacked_array<0 : 0 x i8>>, !simulation.logic<65>) -> !simulation.ref<i8>
      simulation.ref.store %value to %dst : i8, !simulation.ref<i8>
      simulation.return
    }

    // Do not truncate an out-of-range wide index to a valid low word.
    // CHECK: activation @wide_invalid_index valid=1 complete=0
    // CHECK-NEXT: live-in{{$}}
    // CHECK-NEXT: must-write{{$}}
    // CHECK-NEXT: may-write s3[0,8)
    // LIMIT: activation @wide_invalid_index valid=0 complete=0
    simulation.code_unit.decl 5 in 0 function hierarchy "wide_invalid_index"
    simulation.func @wide_invalid_index(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 5 : i64} {
      %out = simulation.context.storage %ctx[3] : !simulation.ref<!simulation.unpacked_array<0 : 0 x i8>>
      %index = arith.constant 18446744073709551616 : i128
      %value = arith.constant 0 : i8
      %dst = simulation.ref.array_element %out[%index] : (!simulation.ref<!simulation.unpacked_array<0 : 0 x i8>>, i128) -> !simulation.ref<i8>
      simulation.ref.store %value to %dst : i8, !simulation.ref<i8>
      simulation.return
    }
  }
}
