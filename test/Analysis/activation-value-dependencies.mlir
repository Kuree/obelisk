// RUN: obelisk-opt %s --test-obelisk-activation-memory='values=true' -o %t.analyzed 2>&1 | FileCheck %s --implicit-check-not="values valid=0"
// RUN: obelisk-opt %s -o %t.original
// RUN: diff %t.original %t.analyzed

!elt = !simulation.packed_struct<[#simulation.field<name = "high", type = !simulation.logic<4>, ordinal = 0, packedOffset = 8>, #simulation.field<name = "low", type = !simulation.logic<8>, ordinal = 1, packedOffset = 0>]>
!arr = !simulation.unpacked_array<-1 : -2 x !elt>
module {
  simulation.design @value_dependencies {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !arr design
    simulation.storage.decl 1 in 0 : !elt design
    simulation.storage.decl 2 in 0 : !simulation.logic<4> design
    simulation.storage.decl 3 in 0 : !simulation.logic<8> design
    simulation.storage.decl 4 in 0 : i32 design
    simulation.storage.decl 5 in 0 : !simulation.logic<4> design

    // Declared descending negative indices remain logical array indices.
    // CHECK: activation @complete_snapshot valid=1 complete=1
    // CHECK-NEXT: live-in s0[0,24) s2[0,4) s3[0,8)
    // CHECK-NEXT: must-write s0[0,24)
    // CHECK: value-input s2[0,4) s3[0,8)
    simulation.code_unit.decl 1 in 0 function hierarchy "complete_snapshot"
    simulation.func @complete_snapshot(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %out = simulation.context.storage %ctx[0] : !simulation.ref<!arr>
      %high = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<4>>
      %low = simulation.context.storage %ctx[3] : !simulation.ref<!simulation.logic<8>>
      %old = simulation.ref.load %out : !simulation.ref<!arr> -> !arr
      %first = arith.constant -1 : i32
      %end = arith.constant -3 : i32
      %one = arith.constant 1 : i32
      cf.br ^loop(%first, %old : i32, !arr)
    ^loop(%i: i32, %array: !arr):
      %more = arith.cmpi sgt, %i, %end : i32
      cf.cond_br %more, ^update, ^done
    ^update:
      %element = simulation.array.extract_dynamic %array[%i] : (!arr, i32) -> !elt
      %hi = simulation.ref.load %high : !simulation.ref<!simulation.logic<4>> -> !simulation.logic<4>
      %with_high = simulation.aggregate.insert %hi into %element[0] : (!elt, !simulation.logic<4>) -> !elt
      %lo = simulation.ref.load %low : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %with_low = simulation.aggregate.insert %lo into %with_high[1] : (!elt, !simulation.logic<8>) -> !elt
      %updated = simulation.array.insert_dynamic %with_low into %array[%i] : (!arr, !elt, i32) -> !arr
      %next = arith.subi %i, %one : i32
      cf.br ^loop(%next, %updated : i32, !arr)
    ^done:
      simulation.ref.store %array to %out : !arr, !simulation.ref<!arr>
      simulation.return
    }

    // Partial replacement leaves only the untouched low fields incoming.
    // CHECK: activation @partial_snapshot valid=1 complete=1
    // CHECK-NEXT: live-in s0[0,24) s2[0,4)
    // CHECK-NEXT: must-write s0[0,24)
    // CHECK: value-input s0[0,8) s0[12,20) s2[0,4)
    simulation.code_unit.decl 2 in 0 function hierarchy "partial_snapshot"
    simulation.func @partial_snapshot(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %out = simulation.context.storage %ctx[0] : !simulation.ref<!arr>
      %high = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<4>>
      %low = simulation.context.storage %ctx[3] : !simulation.ref<!simulation.logic<8>>
      %old = simulation.ref.load %out : !simulation.ref<!arr> -> !arr
      %first = arith.constant -1 : i32
      %end = arith.constant -3 : i32
      %one = arith.constant 1 : i32
      cf.br ^loop(%first, %old : i32, !arr)
    ^loop(%i: i32, %array: !arr):
      %more = arith.cmpi sgt, %i, %end : i32
      cf.cond_br %more, ^update, ^done
    ^update:
      %element = simulation.array.extract_dynamic %array[%i] : (!arr, i32) -> !elt
      %hi = simulation.ref.load %high : !simulation.ref<!simulation.logic<4>> -> !simulation.logic<4>
      %with_high = simulation.aggregate.insert %hi into %element[0] : (!elt, !simulation.logic<4>) -> !elt
      %updated = simulation.array.insert_dynamic %with_high into %array[%i] : (!arr, !elt, i32) -> !arr
      %next = arith.subi %i, %one : i32
      cf.br ^loop(%next, %updated : i32, !arr)
    ^done:
      simulation.ref.store %array to %out : !arr, !simulation.ref<!arr>
      simulation.return
    }

    // A publication before the last overwrite still exposes the old low bits.
    // CHECK: activation @intermediate_publication valid=1 complete=1
    // CHECK: value-input s1[0,8) s2[0,4) s3[0,8)
    simulation.code_unit.decl 3 in 0 function hierarchy "intermediate_publication"
    simulation.func @intermediate_publication(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %out = simulation.context.storage %ctx[1] : !simulation.ref<!elt>
      %high = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<4>>
      %low = simulation.context.storage %ctx[3] : !simulation.ref<!simulation.logic<8>>
      %old = simulation.ref.load %out : !simulation.ref<!elt> -> !elt
      %hi = simulation.ref.load %high : !simulation.ref<!simulation.logic<4>> -> !simulation.logic<4>
      %lo = simulation.ref.load %low : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %first = simulation.aggregate.insert %hi into %old[0] : (!elt, !simulation.logic<4>) -> !elt
      simulation.ref.store %first to %out : !elt, !simulation.ref<!elt>
      %last = simulation.aggregate.insert %lo into %first[1] : (!elt, !simulation.logic<8>) -> !elt
      simulation.ref.store %last to %out : !elt, !simulation.ref<!elt>
      simulation.return
    }

    // Control use exposes the selected old bits even when all data is replaced.
    // CHECK: activation @control_dependence valid=1 complete=1
    // CHECK: value-input s1[8,12)
    simulation.code_unit.decl 4 in 0 function hierarchy "control_dependence"
    simulation.func @control_dependence(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %out = simulation.context.storage %ctx[1] : !simulation.ref<!elt>
      %old = simulation.ref.load %out : !simulation.ref<!elt> -> !elt
      %high = simulation.aggregate.extract %old[0] : (!elt) -> !simulation.logic<4>
      %condition = simulation.logic.is_true %high : !simulation.logic<4>
      %fresh = simulation.aggregate.default : !elt
      cf.cond_br %condition, ^a, ^b
    ^a:
      simulation.ref.store %fresh to %out : !elt, !simulation.ref<!elt>
      simulation.return
    ^b:
      simulation.ref.store %fresh to %out : !elt, !simulation.ref<!elt>
      simulation.return
    }

    // An unproved selector cannot kill old lanes; invalid selectors are no-ops.
    // CHECK: activation @unknown_selector valid=1 complete=1
    // CHECK: value-input s0[0,24) s1[0,12) s4[0,32)
    simulation.code_unit.decl 5 in 0 function hierarchy "unknown_selector"
    simulation.func @unknown_selector(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 5 : i64} {
      %out = simulation.context.storage %ctx[0] : !simulation.ref<!arr>
      %replacement = simulation.context.storage %ctx[1] : !simulation.ref<!elt>
      %old = simulation.ref.load %out : !simulation.ref<!arr> -> !arr
      %value = simulation.ref.load %replacement : !simulation.ref<!elt> -> !elt
      %selector = simulation.context.storage %ctx[4] : !simulation.ref<i32>
      %index = simulation.ref.load %selector : !simulation.ref<i32> -> i32
      %updated = simulation.array.insert_dynamic %value into %old[%index] : (!arr, !elt, i32) -> !arr
      simulation.ref.store %updated to %out : !arr, !simulation.ref<!arr>
      simulation.return
    }

    // An unproved selector cannot kill old lanes; invalid selectors are no-ops.
    // CHECK: activation @invalid_selector valid=1 complete=1
    // CHECK: value-input s0[0,24)
    simulation.code_unit.decl 6 in 0 function hierarchy "invalid_selector"
    simulation.func @invalid_selector(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 6 : i64} {
      %out = simulation.context.storage %ctx[0] : !simulation.ref<!arr>
      %replacement = simulation.context.storage %ctx[1] : !simulation.ref<!elt>
      %old = simulation.ref.load %out : !simulation.ref<!arr> -> !arr
      %value = simulation.ref.load %replacement : !simulation.ref<!elt> -> !elt
      %index = arith.constant 18446744073709551616 : i128
      %updated = simulation.array.insert_dynamic %value into %old[%index] : (!arr, !elt, i128) -> !arr
      simulation.ref.store %updated to %out : !arr, !simulation.ref<!arr>
      simulation.return
    }

    // An unproved selector cannot kill old lanes; invalid selectors are no-ops.
    // CHECK: activation @x_selector valid=1 complete=1
    // CHECK: value-input s0[0,24)
    simulation.code_unit.decl 7 in 0 function hierarchy "x_selector"
    simulation.func @x_selector(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 7 : i64} {
      %out = simulation.context.storage %ctx[0] : !simulation.ref<!arr>
      %replacement = simulation.context.storage %ctx[1] : !simulation.ref<!elt>
      %old = simulation.ref.load %out : !simulation.ref<!arr> -> !arr
      %value = simulation.ref.load %replacement : !simulation.ref<!elt> -> !elt
      %index = simulation.logic.constant 0 : i32, -1 : i32 : !simulation.logic<32>
      %updated = simulation.array.insert_dynamic %value into %old[%index] : (!arr, !elt, !simulation.logic<32>) -> !arr
      simulation.ref.store %updated to %out : !arr, !simulation.ref<!arr>
      simulation.return
    }

    // A dead snapshot is not an activation input, but the analysis is read-only.
    // CHECK: activation @dead_snapshot valid=1 complete=1
    // CHECK-NEXT: live-in s1[0,12)
    // CHECK: value-input{{$}}
    simulation.code_unit.decl 8 in 0 function hierarchy "dead_snapshot"
    simulation.func @dead_snapshot(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 8 : i64} {
      %out = simulation.context.storage %ctx[1] : !simulation.ref<!elt>
      %old = simulation.ref.load %out : !simulation.ref<!elt> -> !elt
      %fresh = simulation.aggregate.default : !elt
      simulation.ref.store %fresh to %out : !elt, !simulation.ref<!elt>
      simulation.return
    }

    // Unknown CFG paths join lane influences by union. Only one path replaces
    // the low field, so that field's old bits remain a possible publication.
    // CHECK: activation @conditional_snapshot valid=1 complete=1
    // CHECK: value-input s1[0,8) s2[0,4) s3[0,8) s4[0,32)
    simulation.code_unit.decl 9 in 0 function hierarchy "conditional_snapshot"
    simulation.func @conditional_snapshot(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 9 : i64} {
      %out = simulation.context.storage %ctx[1] : !simulation.ref<!elt>
      %high = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<4>>
      %low = simulation.context.storage %ctx[3] : !simulation.ref<!simulation.logic<8>>
      %flag = simulation.context.storage %ctx[4] : !simulation.ref<i32>
      %old = simulation.ref.load %out : !simulation.ref<!elt> -> !elt
      %hi = simulation.ref.load %high : !simulation.ref<!simulation.logic<4>> -> !simulation.logic<4>
      %lo = simulation.ref.load %low : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %condition = simulation.ref.load %flag : !simulation.ref<i32> -> i32
      %zero = arith.constant 0 : i32
      %test = arith.cmpi ne, %condition, %zero : i32
      %partial = simulation.aggregate.insert %hi into %old[0] : (!elt, !simulation.logic<4>) -> !elt
      cf.cond_br %test, ^replace, ^done(%partial : !elt)
    ^replace:
      %full = simulation.aggregate.insert %lo into %partial[1] : (!elt, !simulation.logic<8>) -> !elt
      cf.br ^done(%full : !elt)
    ^done(%result: !elt):
      simulation.ref.store %result to %out : !elt, !simulation.ref<!elt>
      simulation.return
    }

    // Four-state insertion and two-state truncation project the same logical
    // lanes. The old upper bits cannot affect the published lower four bits.
    // CHECK: activation @logic_projection valid=1 complete=1
    // CHECK: value-input s2[0,4)
    simulation.code_unit.decl 10 in 0 function hierarchy "logic_projection"
    simulation.func @logic_projection(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 10 : i64} {
      %high = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<4>>
      %low = simulation.context.storage %ctx[3] : !simulation.ref<!simulation.logic<8>>
      %out = simulation.context.storage %ctx[5] : !simulation.ref<!simulation.logic<4>>
      %hi = simulation.ref.load %high : !simulation.ref<!simulation.logic<4>> -> !simulation.logic<4>
      %old = simulation.ref.load %low : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %updated = simulation.logic.insert %hi into %old at 0 : (!simulation.logic<8>, !simulation.logic<4>) -> !simulation.logic<8>
      %bits = simulation.logic.to_bits %updated : !simulation.logic<8> -> i8
      %part = arith.trunci %bits : i8 to i4
      %logic = simulation.logic.from_bits %part : i4 -> !simulation.logic<4>
      simulation.ref.store %logic to %out : !simulation.logic<4>, !simulation.ref<!simulation.logic<4>>
      simulation.return
    }

    // An invalid array read yields the element default, independent of the
    // array contents. Preserve that distinction from an unknown runtime index.
    // CHECK: activation @invalid_extract valid=1 complete=1
    // CHECK: value-input{{$}}
    simulation.code_unit.decl 11 in 0 function hierarchy "invalid_extract"
    simulation.func @invalid_extract(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 11 : i64} {
      %array = simulation.context.storage %ctx[0] : !simulation.ref<!arr>
      %out = simulation.context.storage %ctx[1] : !simulation.ref<!elt>
      %old = simulation.ref.load %array : !simulation.ref<!arr> -> !arr
      %index = simulation.logic.constant -1 : i32, -1 : i32 : !simulation.logic<32>
      %value = simulation.array.extract_dynamic %old[%index] : (!arr, !simulation.logic<32>) -> !elt
      simulation.ref.store %value to %out : !elt, !simulation.ref<!elt>
      simulation.return
    }

    // Address selection is observable even when the stored value is constant.
    // CHECK: activation @address_dependence valid=1 complete=0
    // CHECK: value-input s4[0,32)
    simulation.code_unit.decl 12 in 0 function hierarchy "address_dependence"
    simulation.func @address_dependence(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 12 : i64} {
      %out = simulation.context.storage %ctx[0] : !simulation.ref<!arr>
      %selector = simulation.context.storage %ctx[4] : !simulation.ref<i32>
      %index = simulation.ref.load %selector : !simulation.ref<i32> -> i32
      %ref = simulation.ref.array_element %out[%index] : (!simulation.ref<!arr>, i32) -> !simulation.ref<!elt>
      %fresh = simulation.aggregate.default : !elt
      simulation.ref.store %fresh to %ref : !elt, !simulation.ref<!elt>
      simulation.return
    }

    // Stored incoming values stay exposed even if a later reload has a must-def.
    // A later overwrite does not erase an earlier publication's dependencies.
    // CHECK: activation @stored_value valid=1 complete=1
    // CHECK: value-input s3[0,4)
    simulation.code_unit.decl 13 in 0 function hierarchy "stored_value"
    simulation.func @stored_value(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 13 : i64} {
      %in = simulation.context.storage %ctx[3] : !simulation.ref<!simulation.logic<8>>
      %out = simulation.context.storage %ctx[5] : !simulation.ref<!simulation.logic<4>>
      %v = simulation.ref.load %in : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %low = simulation.logic.extract %v from 0 : !simulation.logic<8> -> !simulation.logic<4>
      simulation.ref.store %low to %out : !simulation.logic<4>, !simulation.ref<!simulation.logic<4>>
      %reload = simulation.ref.load %out : !simulation.ref<!simulation.logic<4>> -> !simulation.logic<4>
      %fresh = simulation.logic.constant 0 : i4, 0 : i4 : !simulation.logic<4>
      simulation.ref.store %fresh to %out : !simulation.logic<4>, !simulation.ref<!simulation.logic<4>>
      simulation.return
    }

    // Snapshot block arguments simultaneously: swapping arguments on a backedge
    // must not accidentally substitute the first updated argument for the next.
    // CHECK: activation @swapped_arguments valid=1 complete=1
    // CHECK: value-input s2[0,4)
    simulation.code_unit.decl 14 in 0 function hierarchy "swapped_arguments"
    simulation.func @swapped_arguments(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 14 : i64} {
      %in = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<4>>
      %out = simulation.context.storage %ctx[5] : !simulation.ref<!simulation.logic<4>>
      %v = simulation.ref.load %in : !simulation.ref<!simulation.logic<4>> -> !simulation.logic<4>
      %fresh = simulation.logic.constant 0 : i4, 0 : i4 : !simulation.logic<4>
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      cf.br ^loop(%zero, %fresh, %v : i32, !simulation.logic<4>, !simulation.logic<4>)
    ^loop(%i: i32, %a: !simulation.logic<4>, %b: !simulation.logic<4>):
      %more = arith.cmpi slt, %i, %one : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %next = arith.addi %i, %one : i32
      cf.br ^loop(%next, %b, %a : i32, !simulation.logic<4>, !simulation.logic<4>)
    ^exit:
      simulation.ref.store %a to %out : !simulation.logic<4>, !simulation.ref<!simulation.logic<4>>
      simulation.return
    }
  }
}
