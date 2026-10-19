// RUN: obelisk-opt %s --test-obelisk-activation-memory='values=true' -o %t.analyzed 2>&1 | FileCheck %s --implicit-check-not="values valid=0"
// RUN: obelisk-opt %s -o %t.original
// RUN: diff %t.original %t.analyzed

// Integer spellings of packed slices must preserve the same lane dependencies
// as four-state operations. Otherwise complete overwrites retain false live-ins
// and acyclic combinational regions appear to have data dependence cycles.
module {
  simulation.design @integer_value_dependencies {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : i4 design
    simulation.storage.decl 2 in 0 : i4 design
    simulation.storage.decl 3 in 0 : i8 design
    simulation.storage.decl 4 in 0 : i4 design
    simulation.storage.decl 5 in 0 : !simulation.logic<8> design
    simulation.storage.decl 6 in 0 : !simulation.logic<4> design
    simulation.storage.decl 7 in 0 : !simulation.logic<4> design
    simulation.storage.decl 8 in 0 : !simulation.logic<8> design
    simulation.storage.decl 9 in 0 : !simulation.logic<4> design
    simulation.storage.decl 10 in 0 : i64 design
    simulation.storage.decl 11 in 0 : i8 design

    // Every old bit is replaced, despite the read-modify-write spelling.
    // CHECK-LABEL: activation @bits_insert_full valid=1 complete=1
    // CHECK: value-input s1[0,4) s2[0,4){{$}}
    // CHECK-NEXT: output s3[0,4) s1[0,4)
    // CHECK-NEXT: output s3[4,8) s2[0,4)
    simulation.code_unit.decl 1 in 0 function hierarchy "bits_insert_full"
    simulation.func @bits_insert_full(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %out = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      %low = simulation.context.storage %ctx[1] : !simulation.ref<i4>
      %high = simulation.context.storage %ctx[2] : !simulation.ref<i4>
      %old = simulation.ref.load %out : !simulation.ref<i8> -> i8
      %lo = simulation.ref.load %low : !simulation.ref<i4> -> i4
      %hi = simulation.ref.load %high : !simulation.ref<i4> -> i4
      %zero = arith.constant 0 : i64
      %four = arith.constant 4 : i64
      %first = simulation.bits.dyn_insert %lo into %old at %zero : (i8, i4, i64) -> i8
      %last = simulation.bits.dyn_insert %hi into %first at %four : (i8, i4, i64) -> i8
      simulation.ref.store %last to %out : i8, !simulation.ref<i8>
      simulation.return
    }

    // The equivalent four-state operation has identical logical dependencies.
    // CHECK-LABEL: activation @logic_insert_full valid=1 complete=1
    // CHECK: value-input s6[0,4) s7[0,4){{$}}
    // CHECK-NEXT: output s8[0,4) s6[0,4)
    // CHECK-NEXT: output s8[4,8) s7[0,4)
    simulation.code_unit.decl 2 in 0 function hierarchy "logic_insert_full"
    simulation.func @logic_insert_full(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %out = simulation.context.storage %ctx[8] : !simulation.ref<!simulation.logic<8>>
      %low = simulation.context.storage %ctx[6] : !simulation.ref<!simulation.logic<4>>
      %high = simulation.context.storage %ctx[7] : !simulation.ref<!simulation.logic<4>>
      %old = simulation.ref.load %out : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %lo = simulation.ref.load %low : !simulation.ref<!simulation.logic<4>> -> !simulation.logic<4>
      %hi = simulation.ref.load %high : !simulation.ref<!simulation.logic<4>> -> !simulation.logic<4>
      %first = simulation.logic.insert %lo into %old at 0 : (!simulation.logic<8>, !simulation.logic<4>) -> !simulation.logic<8>
      %last = simulation.logic.insert %hi into %first at 4 : (!simulation.logic<8>, !simulation.logic<4>) -> !simulation.logic<8>
      simulation.ref.store %last to %out : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.return
    }

    // Constant extraction may be spelled as an integer shift and truncation.
    // CHECK-LABEL: activation @bits_extract_high valid=1 complete=1
    // CHECK: value-input s0[4,8){{$}}
    // CHECK-NEXT: output s4[0,4) s0[4,8)
    simulation.code_unit.decl 3 in 0 function hierarchy "bits_extract_high"
    simulation.func @bits_extract_high(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %out = simulation.context.storage %ctx[4] : !simulation.ref<i4>
      %old = simulation.ref.load %input : !simulation.ref<i8> -> i8
      %four = arith.constant 4 : i8
      %shifted = arith.shrui %old, %four : i8
      %part = arith.trunci %shifted : i8 to i4
      simulation.ref.store %part to %out : i4, !simulation.ref<i4>
      simulation.return
    }

    // CHECK-LABEL: activation @logic_extract_high valid=1 complete=1
    // CHECK: value-input s5[4,8){{$}}
    // CHECK-NEXT: output s9[0,4) s5[4,8)
    simulation.code_unit.decl 4 in 0 function hierarchy "logic_extract_high"
    simulation.func @logic_extract_high(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %input = simulation.context.storage %ctx[5] : !simulation.ref<!simulation.logic<8>>
      %out = simulation.context.storage %ctx[9] : !simulation.ref<!simulation.logic<4>>
      %old = simulation.ref.load %input : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %part = simulation.logic.extract %old from 4 : !simulation.logic<8> -> !simulation.logic<4>
      simulation.ref.store %part to %out : !simulation.logic<4>, !simulation.ref<!simulation.logic<4>>
      simulation.return
    }

    // A concatenation's low half does not depend on the high input. This
    // exercises extension, left shift and bitwise OR without rewriting the IR.
    // CHECK-LABEL: activation @bits_concat_low valid=1 complete=1
    // CHECK: value-input s1[0,4){{$}}
    // CHECK-NEXT: output s4[0,4) s1[0,4)
    simulation.code_unit.decl 5 in 0 function hierarchy "bits_concat_low"
    simulation.func @bits_concat_low(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 5 : i64} {
      %low = simulation.context.storage %ctx[1] : !simulation.ref<i4>
      %high = simulation.context.storage %ctx[2] : !simulation.ref<i4>
      %out = simulation.context.storage %ctx[4] : !simulation.ref<i4>
      %lo = simulation.ref.load %low : !simulation.ref<i4> -> i4
      %hi = simulation.ref.load %high : !simulation.ref<i4> -> i4
      %lower = arith.extui %lo : i4 to i8
      %upper = arith.extui %hi : i4 to i8
      %four = arith.constant 4 : i8
      %shifted = arith.shli %upper, %four : i8
      %concat = arith.ori %lower, %shifted : i8
      %part = arith.trunci %concat : i8 to i4
      simulation.ref.store %part to %out : i4, !simulation.ref<i4>
      simulation.return
    }

    // The new upper lanes of zero extension have no input dependencies.
    // CHECK-LABEL: activation @zero_extension_high valid=1 complete=1
    // CHECK: value-input{{$}}
    // CHECK-NEXT: output s4[0,4){{$}}
    simulation.code_unit.decl 6 in 0 function hierarchy "zero_extension_high"
    simulation.func @zero_extension_high(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 6 : i64} {
      %input = simulation.context.storage %ctx[1] : !simulation.ref<i4>
      %out = simulation.context.storage %ctx[4] : !simulation.ref<i4>
      %lo = simulation.ref.load %input : !simulation.ref<i4> -> i4
      %extended = arith.extui %lo : i4 to i8
      %four = arith.constant 4 : i8
      %shifted = arith.shrui %extended, %four : i8
      %part = arith.trunci %shifted : i8 to i4
      simulation.ref.store %part to %out : i4, !simulation.ref<i4>
      simulation.return
    }

    // Each bitwise result lane depends only on the same operand lanes.
    // CHECK-LABEL: activation @bitwise_and_low valid=1 complete=1
    // CHECK: value-input s0[0,4) s11[0,4){{$}}
    // CHECK-NEXT: output s4[0,4) s0[0,4) s11[0,4)
    simulation.code_unit.decl 7 in 0 function hierarchy "bitwise_and_low"
    simulation.func @bitwise_and_low(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 7 : i64} {
      %left = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %right = simulation.context.storage %ctx[11] : !simulation.ref<i8>
      %out = simulation.context.storage %ctx[4] : !simulation.ref<i4>
      %a = simulation.ref.load %left : !simulation.ref<i8> -> i8
      %b = simulation.ref.load %right : !simulation.ref<i8> -> i8
      %combined = arith.andi %a, %b : i8
      %part = arith.trunci %combined : i8 to i4
      simulation.ref.store %part to %out : i4, !simulation.ref<i4>
      simulation.return
    }

    // CHECK-LABEL: activation @bitwise_or_low valid=1 complete=1
    // CHECK: value-input s0[0,4) s11[0,4){{$}}
    // CHECK-NEXT: output s4[0,4) s0[0,4) s11[0,4)
    simulation.code_unit.decl 8 in 0 function hierarchy "bitwise_or_low"
    simulation.func @bitwise_or_low(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 8 : i64} {
      %left = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %right = simulation.context.storage %ctx[11] : !simulation.ref<i8>
      %out = simulation.context.storage %ctx[4] : !simulation.ref<i4>
      %a = simulation.ref.load %left : !simulation.ref<i8> -> i8
      %b = simulation.ref.load %right : !simulation.ref<i8> -> i8
      %combined = arith.ori %a, %b : i8
      %part = arith.trunci %combined : i8 to i4
      simulation.ref.store %part to %out : i4, !simulation.ref<i4>
      simulation.return
    }

    // CHECK-LABEL: activation @bitwise_xor_low valid=1 complete=1
    // CHECK: value-input s0[0,4) s11[0,4){{$}}
    // CHECK-NEXT: output s4[0,4) s0[0,4) s11[0,4)
    simulation.code_unit.decl 9 in 0 function hierarchy "bitwise_xor_low"
    simulation.func @bitwise_xor_low(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 9 : i64} {
      %left = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %right = simulation.context.storage %ctx[11] : !simulation.ref<i8>
      %out = simulation.context.storage %ctx[4] : !simulation.ref<i4>
      %a = simulation.ref.load %left : !simulation.ref<i8> -> i8
      %b = simulation.ref.load %right : !simulation.ref<i8> -> i8
      %combined = arith.xori %a, %b : i8
      %part = arith.trunci %combined : i8 to i4
      simulation.ref.store %part to %out : i4, !simulation.ref<i4>
      simulation.return
    }

    // An unproved selector may preserve or replace any lane.
    // CHECK-LABEL: activation @insert_dynamic valid=1 complete=1
    // CHECK: value-input s0[0,8) s1[0,4) s10[0,64){{$}}
    simulation.code_unit.decl 10 in 0 function hierarchy "insert_dynamic"
    simulation.func @insert_dynamic(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 10 : i64} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %replacement = simulation.context.storage %ctx[1] : !simulation.ref<i4>
      %selector = simulation.context.storage %ctx[10] : !simulation.ref<i64>
      %out = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      %old = simulation.ref.load %input : !simulation.ref<i8> -> i8
      %new = simulation.ref.load %replacement : !simulation.ref<i4> -> i4
      %index = simulation.ref.load %selector : !simulation.ref<i64> -> i64
      %result = simulation.bits.dyn_insert %new into %old at %index : (i8, i4, i64) -> i8
      simulation.ref.store %result to %out : i8, !simulation.ref<i8>
      simulation.return
    }

    // Partly out-of-bounds replacement keeps the conservative fallback.
    // CHECK-LABEL: activation @insert_negative valid=1 complete=1
    // CHECK: value-input s0[0,8) s1[0,4){{$}}
    simulation.code_unit.decl 11 in 0 function hierarchy "insert_negative"
    simulation.func @insert_negative(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 11 : i64} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %replacement = simulation.context.storage %ctx[1] : !simulation.ref<i4>
      %out = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      %old = simulation.ref.load %input : !simulation.ref<i8> -> i8
      %new = simulation.ref.load %replacement : !simulation.ref<i4> -> i4
      %index = arith.constant -2 : i64
      %result = simulation.bits.dyn_insert %new into %old at %index : (i8, i4, i64) -> i8
      simulation.ref.store %result to %out : i8, !simulation.ref<i8>
      simulation.return
    }

    // An entirely out-of-bounds write cannot remove old-value dependencies.
    // CHECK-LABEL: activation @insert_outside valid=1 complete=1
    // CHECK: value-input s0[0,8)
    simulation.code_unit.decl 12 in 0 function hierarchy "insert_outside"
    simulation.func @insert_outside(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 12 : i64} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %replacement = simulation.context.storage %ctx[1] : !simulation.ref<i4>
      %out = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      %old = simulation.ref.load %input : !simulation.ref<i8> -> i8
      %new = simulation.ref.load %replacement : !simulation.ref<i4> -> i4
      %index = arith.constant 12 : i64
      %result = simulation.bits.dyn_insert %new into %old at %index : (i8, i4, i64) -> i8
      simulation.ref.store %result to %out : i8, !simulation.ref<i8>
      simulation.return
    }

    // Variable shifts retain both the data and shift amount as dependencies.
    // CHECK-LABEL: activation @shli_variable valid=1 complete=1
    // CHECK: value-input s0[0,8) s11[0,8){{$}}
    simulation.code_unit.decl 13 in 0 function hierarchy "shli_variable"
    simulation.func @shli_variable(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 13 : i64} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %amount = simulation.context.storage %ctx[11] : !simulation.ref<i8>
      %out = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      %old = simulation.ref.load %input : !simulation.ref<i8> -> i8
      %shift = simulation.ref.load %amount : !simulation.ref<i8> -> i8
      %result = arith.shli %old, %shift : i8
      simulation.ref.store %result to %out : i8, !simulation.ref<i8>
      simulation.return
    }

    // CHECK-LABEL: activation @shrui_variable valid=1 complete=1
    // CHECK: value-input s0[0,8) s11[0,8){{$}}
    simulation.code_unit.decl 14 in 0 function hierarchy "shrui_variable"
    simulation.func @shrui_variable(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 14 : i64} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %amount = simulation.context.storage %ctx[11] : !simulation.ref<i8>
      %out = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      %old = simulation.ref.load %input : !simulation.ref<i8> -> i8
      %shift = simulation.ref.load %amount : !simulation.ref<i8> -> i8
      %result = arith.shrui %old, %shift : i8
      simulation.ref.store %result to %out : i8, !simulation.ref<i8>
      simulation.return
    }

    // Oversized arith shifts are outside the precise in-range transfer rule.
    // CHECK-LABEL: activation @shli_oversized valid=1 complete=1
    // CHECK: value-input s0[0,8){{$}}
    simulation.code_unit.decl 15 in 0 function hierarchy "shli_oversized"
    simulation.func @shli_oversized(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 15 : i64} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %out = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      %old = simulation.ref.load %input : !simulation.ref<i8> -> i8
      %shift = arith.constant 8 : i8
      %result = arith.shli %old, %shift : i8
      simulation.ref.store %result to %out : i8, !simulation.ref<i8>
      simulation.return
    }

    // CHECK-LABEL: activation @shrui_oversized valid=1 complete=1
    // CHECK: value-input s0[0,8){{$}}
    simulation.code_unit.decl 16 in 0 function hierarchy "shrui_oversized"
    simulation.func @shrui_oversized(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 16 : i64} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %out = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      %old = simulation.ref.load %input : !simulation.ref<i8> -> i8
      %shift = arith.constant 8 : i8
      %result = arith.shrui %old, %shift : i8
      simulation.ref.store %result to %out : i8, !simulation.ref<i8>
      simulation.return
    }

    // Overflow flags make discarded input bits relevant to poison. The low
    // nibble is zero only when the whole flagged shift remains defined.
    // CHECK-LABEL: activation @shli_no_wrap valid=1 complete=1
    // CHECK: value-input s0[0,8){{$}}
    simulation.code_unit.decl 17 in 0 function hierarchy "shli_no_wrap"
    simulation.func @shli_no_wrap(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 17 : i64} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %out = simulation.context.storage %ctx[4] : !simulation.ref<i4>
      %old = simulation.ref.load %input : !simulation.ref<i8> -> i8
      %shift = arith.constant 4 : i8
      %shifted = arith.shli %old, %shift overflow<nsw, nuw> : i8
      %part = arith.trunci %shifted : i8 to i4
      simulation.ref.store %part to %out : i4, !simulation.ref<i4>
      simulation.return
    }

    // An exact right shift also depends on low bits that could cause poison.
    // CHECK-LABEL: activation @shrui_exact valid=1 complete=1
    // CHECK: value-input s0[0,8){{$}}
    simulation.code_unit.decl 18 in 0 function hierarchy "shrui_exact"
    simulation.func @shrui_exact(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 18 : i64} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %out = simulation.context.storage %ctx[4] : !simulation.ref<i4>
      %old = simulation.ref.load %input : !simulation.ref<i8> -> i8
      %shift = arith.constant 4 : i8
      %shifted = arith.shrui %old, %shift exact : i8
      %part = arith.trunci %shifted : i8 to i4
      simulation.ref.store %part to %out : i4, !simulation.ref<i4>
      simulation.return
    }

    // Flagged truncation also observes the discarded high bits: they must be
    // the appropriate zero/sign extension or the entire result is poison.
    // CHECK-LABEL: activation @trunci_no_wrap valid=1 complete=1
    // CHECK: value-input s0[0,8){{$}}
    simulation.code_unit.decl 19 in 0 function hierarchy "trunci_no_wrap"
    simulation.func @trunci_no_wrap(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 19 : i64} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %out = simulation.context.storage %ctx[4] : !simulation.ref<i4>
      %old = simulation.ref.load %input : !simulation.ref<i8> -> i8
      %part = arith.trunci %old overflow<nsw, nuw> : i8 to i4
      simulation.ref.store %part to %out : i4, !simulation.ref<i4>
      simulation.return
    }
  }
}
