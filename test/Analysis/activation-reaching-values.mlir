// RUN: obelisk-opt %s --test-obelisk-activation-memory='values=true' -o %t.analyzed 2>&1 | FileCheck %s --implicit-check-not="values valid=0"
// RUN: obelisk-opt %s -o %t.original
// RUN: diff %t.original %t.analyzed
module {
  simulation.design @reaching {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i16 design
    simulation.storage.decl 1 in 0 : i16 design
    simulation.storage.decl 2 in 0 : i16 design
    simulation.storage.decl 3 in 0 : i32 design
    simulation.storage.decl 4 in 0 : i1 design
    simulation.storage.decl 5 in 0 : i16 design
    simulation.storage.decl 6 in 0 : !simulation.unpacked_array<0 : 1 x i8> design

    // Loads follow their reaching store, retaining its input dependence.
    // CHECK-LABEL: activation @store_load valid=1
    // CHECK: output s1[0,16) s0[0,16){{$}}
    // CHECK: output s2[0,16) s0[0,16){{$}}
    simulation.code_unit.decl 1 in 0 function hierarchy "store_load"
    simulation.func @store_load(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %r0 = simulation.context.storage %ctx[0] : !simulation.ref<i16>
      %r1 = simulation.context.storage %ctx[1] : !simulation.ref<i16>
      %r2 = simulation.context.storage %ctx[2] : !simulation.ref<i16>
      %r3 = simulation.context.storage %ctx[3] : !simulation.ref<i32>
      %r4 = simulation.context.storage %ctx[4] : !simulation.ref<i1>
      %r5 = simulation.context.storage %ctx[5] : !simulation.ref<i16>
      %r6 = simulation.context.storage %ctx[6] : !simulation.ref<!simulation.unpacked_array<0 : 1 x i8>>
      %x = simulation.ref.load %r0 : !simulation.ref<i16> -> i16
      simulation.ref.store %x to %r1 : i16, !simulation.ref<i16>
      %y = simulation.ref.load %r1 : !simulation.ref<i16> -> i16
      simulation.ref.store %y to %r2 : i16, !simulation.ref<i16>
      simulation.return
    }

    // A strong update kills earlier definitions without hiding their publications.
    // CHECK-LABEL: activation @overwrite valid=1
    // CHECK: output s1[0,16) s0[0,16){{$}}
    // CHECK: output s1[0,16){{$}}
    // CHECK: output s2[0,16){{$}}
    simulation.code_unit.decl 2 in 0 function hierarchy "overwrite"
    simulation.func @overwrite(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %r0 = simulation.context.storage %ctx[0] : !simulation.ref<i16>
      %r1 = simulation.context.storage %ctx[1] : !simulation.ref<i16>
      %r2 = simulation.context.storage %ctx[2] : !simulation.ref<i16>
      %r3 = simulation.context.storage %ctx[3] : !simulation.ref<i32>
      %r4 = simulation.context.storage %ctx[4] : !simulation.ref<i1>
      %r5 = simulation.context.storage %ctx[5] : !simulation.ref<i16>
      %r6 = simulation.context.storage %ctx[6] : !simulation.ref<!simulation.unpacked_array<0 : 1 x i8>>
      %x = simulation.ref.load %r0 : !simulation.ref<i16> -> i16
      %zero = arith.constant 0 : i16
      simulation.ref.store %x to %r1 : i16, !simulation.ref<i16>
      simulation.ref.store %zero to %r1 : i16, !simulation.ref<i16>
      %y = simulation.ref.load %r1 : !simulation.ref<i16> -> i16
      simulation.ref.store %y to %r2 : i16, !simulation.ref<i16>
      simulation.return
    }

    // A partial write kills only its lanes; untouched high bits remain incoming.
    // CHECK-LABEL: activation @partial valid=1
    // CHECK: output s2[0,8) s0[0,8){{$}}
    // CHECK: output s2[8,16) s1[8,16){{$}}
    simulation.code_unit.decl 3 in 0 function hierarchy "partial"
    simulation.func @partial(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %r0 = simulation.context.storage %ctx[0] : !simulation.ref<i16>
      %r1 = simulation.context.storage %ctx[1] : !simulation.ref<i16>
      %r2 = simulation.context.storage %ctx[2] : !simulation.ref<i16>
      %r3 = simulation.context.storage %ctx[3] : !simulation.ref<i32>
      %r4 = simulation.context.storage %ctx[4] : !simulation.ref<i1>
      %r5 = simulation.context.storage %ctx[5] : !simulation.ref<i16>
      %r6 = simulation.context.storage %ctx[6] : !simulation.ref<!simulation.unpacked_array<0 : 1 x i8>>
      %x = simulation.ref.load %r0 : !simulation.ref<i16> -> i16
      %low = arith.trunci %x : i16 to i8
      %ref = simulation.ref.extract %r1 from 0 : !simulation.ref<i16> -> !simulation.ref<i8>
      simulation.ref.store %low to %ref : i8, !simulation.ref<i8>
      %y = simulation.ref.load %r1 : !simulation.ref<i16> -> i16
      simulation.ref.store %y to %r2 : i16, !simulation.ref<i16>
      simulation.return
    }

    // The no-write predecessor contributes incoming memory and the path predicate.
    // CHECK-LABEL: activation @conditional_memory valid=1
    // CHECK: output s1[0,16) s0[0,16) s4[0,1){{$}}
    // CHECK: output s2[0,16) s0[0,16) s1[0,16) s4[0,1){{$}}
    simulation.code_unit.decl 4 in 0 function hierarchy "conditional_memory"
    simulation.func @conditional_memory(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %r0 = simulation.context.storage %ctx[0] : !simulation.ref<i16>
      %r1 = simulation.context.storage %ctx[1] : !simulation.ref<i16>
      %r2 = simulation.context.storage %ctx[2] : !simulation.ref<i16>
      %r3 = simulation.context.storage %ctx[3] : !simulation.ref<i32>
      %r4 = simulation.context.storage %ctx[4] : !simulation.ref<i1>
      %r5 = simulation.context.storage %ctx[5] : !simulation.ref<i16>
      %r6 = simulation.context.storage %ctx[6] : !simulation.ref<!simulation.unpacked_array<0 : 1 x i8>>
      %x = simulation.ref.load %r0 : !simulation.ref<i16> -> i16
      %c = simulation.ref.load %r4 : !simulation.ref<i1> -> i1
      cf.cond_br %c, ^yes, ^join
      ^yes:
      simulation.ref.store %x to %r1 : i16, !simulation.ref<i16>
      cf.br ^join
      ^join:
      %y = simulation.ref.load %r1 : !simulation.ref<i16> -> i16
      simulation.ref.store %y to %r2 : i16, !simulation.ref<i16>
      simulation.return
    }

    // Control dependence ends at the post-dominator; later independent stores stay independent.
    // CHECK-LABEL: activation @control_scope valid=1
    // CHECK: output s5[0,16) s0[0,16) s4[0,1){{$}}
    // CHECK: output s2[0,16){{$}}
    simulation.code_unit.decl 5 in 0 function hierarchy "control_scope"
    simulation.func @control_scope(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 5 : i64} {
      %r0 = simulation.context.storage %ctx[0] : !simulation.ref<i16>
      %r1 = simulation.context.storage %ctx[1] : !simulation.ref<i16>
      %r2 = simulation.context.storage %ctx[2] : !simulation.ref<i16>
      %r3 = simulation.context.storage %ctx[3] : !simulation.ref<i32>
      %r4 = simulation.context.storage %ctx[4] : !simulation.ref<i1>
      %r5 = simulation.context.storage %ctx[5] : !simulation.ref<i16>
      %r6 = simulation.context.storage %ctx[6] : !simulation.ref<!simulation.unpacked_array<0 : 1 x i8>>
      %x = simulation.ref.load %r0 : !simulation.ref<i16> -> i16
      %c = simulation.ref.load %r4 : !simulation.ref<i1> -> i1
      %zero = arith.constant 0 : i16
      cf.cond_br %c, ^yes, ^join
      ^yes:
      simulation.ref.store %x to %r5 : i16, !simulation.ref<i16>
      cf.br ^join
      ^join:
      simulation.ref.store %zero to %r2 : i16, !simulation.ref<i16>
      simulation.return
    }

    // A direct branch-to-merge edge still controls the selected SSA block argument.
    // CHECK-LABEL: activation @phi_control valid=1
    // CHECK: output s2[0,16) s0[0,16) s4[0,1){{$}}
    simulation.code_unit.decl 6 in 0 function hierarchy "phi_control"
    simulation.func @phi_control(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 6 : i64} {
      %r0 = simulation.context.storage %ctx[0] : !simulation.ref<i16>
      %r1 = simulation.context.storage %ctx[1] : !simulation.ref<i16>
      %r2 = simulation.context.storage %ctx[2] : !simulation.ref<i16>
      %r3 = simulation.context.storage %ctx[3] : !simulation.ref<i32>
      %r4 = simulation.context.storage %ctx[4] : !simulation.ref<i1>
      %r5 = simulation.context.storage %ctx[5] : !simulation.ref<i16>
      %r6 = simulation.context.storage %ctx[6] : !simulation.ref<!simulation.unpacked_array<0 : 1 x i8>>
      %x = simulation.ref.load %r0 : !simulation.ref<i16> -> i16
      %c = simulation.ref.load %r4 : !simulation.ref<i1> -> i1
      %zero = arith.constant 0 : i16
      cf.cond_br %c, ^yes, ^join(%zero : i16)
      ^yes:
      cf.br ^join(%x : i16)
      ^join(%v: i16):
      simulation.ref.store %v to %r2 : i16, !simulation.ref<i16>
      simulation.return
    }

    // Nested predicates are implicit SSA uses even if the IR only uses them in terminators.
    // CHECK-LABEL: activation @nested_control valid=1
    // CHECK: output s1[0,16) s3[0,32) s4[0,1){{$}}
    // CHECK: output s2[0,16) s1[0,16) s3[0,32) s4[0,1){{$}}
    simulation.code_unit.decl 7 in 0 function hierarchy "nested_control"
    simulation.func @nested_control(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 7 : i64} {
      %r0 = simulation.context.storage %ctx[0] : !simulation.ref<i16>
      %r1 = simulation.context.storage %ctx[1] : !simulation.ref<i16>
      %r2 = simulation.context.storage %ctx[2] : !simulation.ref<i16>
      %r3 = simulation.context.storage %ctx[3] : !simulation.ref<i32>
      %r4 = simulation.context.storage %ctx[4] : !simulation.ref<i1>
      %r5 = simulation.context.storage %ctx[5] : !simulation.ref<i16>
      %r6 = simulation.context.storage %ctx[6] : !simulation.ref<!simulation.unpacked_array<0 : 1 x i8>>
      %c = simulation.ref.load %r4 : !simulation.ref<i1> -> i1
      %index = simulation.ref.load %r3 : !simulation.ref<i32> -> i32
      %z32 = arith.constant 0 : i32
      %zero = arith.constant 0 : i16
      cf.cond_br %c, ^outer, ^join
      ^outer:
      %d = arith.cmpi ne, %index, %z32 : i32
      cf.cond_br %d, ^inner, ^join
      ^inner:
      simulation.ref.store %zero to %r1 : i16, !simulation.ref<i16>
      cf.br ^join
      ^join:
      %v = simulation.ref.load %r1 : !simulation.ref<i16> -> i16
      simulation.ref.store %v to %r2 : i16, !simulation.ref<i16>
      simulation.return
    }

    // A bounded memory accumulation starts from its initializer, not from prior activation state.
    // CHECK-LABEL: activation @accumulator valid=1
    // CHECK: value-input s0[0,16){{$}}
    // CHECK: output s2[0,16) s0[0,16){{$}}
    simulation.code_unit.decl 8 in 0 function hierarchy "accumulator"
    simulation.func @accumulator(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 8 : i64} {
      %r0 = simulation.context.storage %ctx[0] : !simulation.ref<i16>
      %r1 = simulation.context.storage %ctx[1] : !simulation.ref<i16>
      %r2 = simulation.context.storage %ctx[2] : !simulation.ref<i16>
      %r3 = simulation.context.storage %ctx[3] : !simulation.ref<i32>
      %r4 = simulation.context.storage %ctx[4] : !simulation.ref<i1>
      %r5 = simulation.context.storage %ctx[5] : !simulation.ref<i16>
      %r6 = simulation.context.storage %ctx[6] : !simulation.ref<!simulation.unpacked_array<0 : 1 x i8>>
      %zero = arith.constant 0 : i16
      %i0 = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      %two = arith.constant 2 : i32
      %eight = arith.constant 8 : i32
      simulation.ref.store %zero to %r1 : i16, !simulation.ref<i16>
      cf.br ^header(%i0 : i32)
      ^header(%i: i32):
      %more = arith.cmpi slt, %i, %two : i32
      cf.cond_br %more, ^body, ^done
      ^body:
      %offset = arith.muli %i, %eight : i32
      %ref = simulation.ref.dyn_extract %r0 from %offset : (!simulation.ref<i16>, i32) -> !simulation.ref<i8>
      %x = simulation.ref.load %ref : !simulation.ref<i8> -> i8
      %wide = arith.extui %x : i8 to i16
      %old = simulation.ref.load %r1 : !simulation.ref<i16> -> i16
      %next = arith.addi %old, %wide : i16
      simulation.ref.store %next to %r1 : i16, !simulation.ref<i16>
      %inc = arith.addi %i, %one : i32
      cf.br ^header(%inc : i32)
      ^done:
      %v = simulation.ref.load %r1 : !simulation.ref<i16> -> i16
      simulation.ref.store %v to %r2 : i16, !simulation.ref<i16>
      simulation.return
    }

    // A later store cannot rewrite the provenance of an already loaded SSA snapshot.
    // CHECK-LABEL: activation @old_snapshot valid=1
    // CHECK: output s1[0,16){{$}}
    // CHECK: output s2[0,16) s1[0,16){{$}}
    simulation.code_unit.decl 9 in 0 function hierarchy "old_snapshot"
    simulation.func @old_snapshot(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 9 : i64} {
      %r0 = simulation.context.storage %ctx[0] : !simulation.ref<i16>
      %r1 = simulation.context.storage %ctx[1] : !simulation.ref<i16>
      %r2 = simulation.context.storage %ctx[2] : !simulation.ref<i16>
      %r3 = simulation.context.storage %ctx[3] : !simulation.ref<i32>
      %r4 = simulation.context.storage %ctx[4] : !simulation.ref<i1>
      %r5 = simulation.context.storage %ctx[5] : !simulation.ref<i16>
      %r6 = simulation.context.storage %ctx[6] : !simulation.ref<!simulation.unpacked_array<0 : 1 x i8>>
      %old = simulation.ref.load %r1 : !simulation.ref<i16> -> i16
      %zero = arith.constant 0 : i16
      simulation.ref.store %zero to %r1 : i16, !simulation.ref<i16>
      simulation.ref.store %old to %r2 : i16, !simulation.ref<i16>
      simulation.return
    }

    // An unknown selector needs a weak update: it cannot kill every possible destination lane.
    // CHECK-LABEL: activation @weak_update valid=1
    // CHECK: output s6[0,16) s0[0,8) s3[0,32){{$}}
    // CHECK: output s6[0,16) s0[0,8) s3[0,32) s6[0,16){{$}}
    simulation.code_unit.decl 10 in 0 function hierarchy "weak_update"
    simulation.func @weak_update(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 10 : i64} {
      %r0 = simulation.context.storage %ctx[0] : !simulation.ref<i16>
      %r1 = simulation.context.storage %ctx[1] : !simulation.ref<i16>
      %r2 = simulation.context.storage %ctx[2] : !simulation.ref<i16>
      %r3 = simulation.context.storage %ctx[3] : !simulation.ref<i32>
      %r4 = simulation.context.storage %ctx[4] : !simulation.ref<i1>
      %r5 = simulation.context.storage %ctx[5] : !simulation.ref<i16>
      %r6 = simulation.context.storage %ctx[6] : !simulation.ref<!simulation.unpacked_array<0 : 1 x i8>>
      %x = simulation.ref.load %r0 : !simulation.ref<i16> -> i16
      %low = arith.trunci %x : i16 to i8
      %index = simulation.ref.load %r3 : !simulation.ref<i32> -> i32
      %ref = simulation.ref.array_element %r6[%index] : (!simulation.ref<!simulation.unpacked_array<0 : 1 x i8>>, i32) -> !simulation.ref<i8>
      simulation.ref.store %low to %ref : i8, !simulation.ref<i8>
      %v = simulation.ref.load %r6 : !simulation.ref<!simulation.unpacked_array<0 : 1 x i8>> -> !simulation.unpacked_array<0 : 1 x i8>
      simulation.ref.store %v to %r6 : !simulation.unpacked_array<0 : 1 x i8>, !simulation.ref<!simulation.unpacked_array<0 : 1 x i8>>
      simulation.return
    }

    // Merged alias offsets retain selector dependence and untouched incoming lanes.
    // CHECK-LABEL: activation @alias_phi valid=1
    // CHECK: output s2[0,16) s1[0,16) s4[0,1){{$}}
    simulation.code_unit.decl 11 in 0 function hierarchy "alias_phi"
    simulation.func @alias_phi(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 11 : i64} {
      %r0 = simulation.context.storage %ctx[0] : !simulation.ref<i16>
      %r1 = simulation.context.storage %ctx[1] : !simulation.ref<i16>
      %r2 = simulation.context.storage %ctx[2] : !simulation.ref<i16>
      %r3 = simulation.context.storage %ctx[3] : !simulation.ref<i32>
      %r4 = simulation.context.storage %ctx[4] : !simulation.ref<i1>
      %r5 = simulation.context.storage %ctx[5] : !simulation.ref<i16>
      %r6 = simulation.context.storage %ctx[6] : !simulation.ref<!simulation.unpacked_array<0 : 1 x i8>>
      %low = simulation.ref.extract %r1 from 0 : !simulation.ref<i16> -> !simulation.ref<i8>
      %high = simulation.ref.extract %r1 from 8 : !simulation.ref<i16> -> !simulation.ref<i8>
      %c = simulation.ref.load %r4 : !simulation.ref<i1> -> i1
      %zero = arith.constant 0 : i8
      cf.cond_br %c, ^join(%low : !simulation.ref<i8>), ^join(%high : !simulation.ref<i8>)
      ^join(%ref: !simulation.ref<i8>):
      simulation.ref.store %zero to %ref : i8, !simulation.ref<i8>
      %v = simulation.ref.load %r1 : !simulation.ref<i16> -> i16
      simulation.ref.store %v to %r2 : i16, !simulation.ref<i16>
      simulation.return
    }
  }
}
