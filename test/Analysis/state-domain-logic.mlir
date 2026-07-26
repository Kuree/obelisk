// RUN: obelisk-opt %s -o /dev/null --pass-pipeline='builtin.module(test-obelisk-sim-state-domain)' 2> %t.threaded
// RUN: obelisk-opt %s -o /dev/null --mlir-disable-threading --pass-pipeline='builtin.module(test-obelisk-sim-state-domain)' 2> %t.single
// RUN: diff %t.threaded %t.single
// RUN: FileCheck %s < %t.threaded

module {
  obelisk_sim.design @logic_rules {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.logic_rules.rules.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<8> design

    obelisk_sim.func @rules(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %net: !obelisk_sim.net<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 4 : i32, obelisk_sim.descriptor_id = 0 : i64},
        %dynamic_bits: i8 {obelisk_sim.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %bits = arith.constant 3 : i8
      %index = arith.constant 2 : i8
      %outside = arith.constant 7 : i8
      %logic_index = obelisk_sim.logic.constant 2 : i8, 0 : i8 : !obelisk_sim.logic<8>
      %known = obelisk_sim.logic.from_bits %bits : i8 -> !obelisk_sim.logic<8>
      %runtime_divisor = obelisk_sim.logic.from_bits %dynamic_bits : i8 -> !obelisk_sim.logic<8>
      %zero = obelisk_sim.logic.constant 0 : i8, 0 : i8 : !obelisk_sim.logic<8>
      %one = obelisk_sim.logic.constant 1 : i8, 0 : i8 : !obelisk_sim.logic<8>
      %ones = obelisk_sim.logic.constant -1 : i8, 0 : i8 : !obelisk_sim.logic<8>
      %xz = obelisk_sim.logic.constant 0 : i8, -1 : i8 : !obelisk_sim.logic<8>
      %resized = obelisk_sim.logic.resize %known signed = false : !obelisk_sim.logic<8> -> !obelisk_sim.logic<16>
      %resized_xz = obelisk_sim.logic.resize %xz signed = false : !obelisk_sim.logic<8> -> !obelisk_sim.logic<16>
      %unary = obelisk_sim.logic.unary bit_not %known : (!obelisk_sim.logic<8>) -> !obelisk_sim.logic<8>
      %unary_xz = obelisk_sim.logic.unary bit_not %xz : (!obelisk_sim.logic<8>) -> !obelisk_sim.logic<8>
      %reduced = obelisk_sim.logic.reduction xor %known : !obelisk_sim.logic<8> -> !obelisk_sim.logic<1>
      %reduced_xz = obelisk_sim.logic.reduction xor %xz : !obelisk_sim.logic<8> -> !obelisk_sim.logic<1>
      %added = obelisk_sim.logic.binary add %known, %one : !obelisk_sim.logic<8>
      %added_xz = obelisk_sim.logic.binary add %known, %xz : !obelisk_sim.logic<8>
      %div = obelisk_sim.logic.binary udiv %known, %one : !obelisk_sim.logic<8>
      %signed_div = obelisk_sim.logic.binary sdiv %known, %one : !obelisk_sim.logic<8>
      %mod = obelisk_sim.logic.binary umod %known, %one : !obelisk_sim.logic<8>
      %signed_mod = obelisk_sim.logic.binary smod %known, %one : !obelisk_sim.logic<8>
      %bad_div = obelisk_sim.logic.binary udiv %known, %zero : !obelisk_sim.logic<8>
      %runtime_div = obelisk_sim.logic.binary udiv %known, %runtime_divisor : !obelisk_sim.logic<8>
      %unknown_div = obelisk_sim.logic.binary udiv %known, %xz : !obelisk_sim.logic<8>
      %absorbed_and = obelisk_sim.logic.binary and %xz, %zero : !obelisk_sim.logic<8>
      %absorbed_or = obelisk_sim.logic.binary or %xz, %ones : !obelisk_sim.logic<8>
      %logical = obelisk_sim.logic.logical and %known, %one : (!obelisk_sim.logic<8>, !obelisk_sim.logic<8>) -> !obelisk_sim.logic<1>
      %logical_xz = obelisk_sim.logic.logical and %known, %xz : (!obelisk_sim.logic<8>, !obelisk_sim.logic<8>) -> !obelisk_sim.logic<1>
      %absorbed_logical = obelisk_sim.logic.logical and %xz, %zero : (!obelisk_sim.logic<8>, !obelisk_sim.logic<8>) -> !obelisk_sim.logic<1>
      %absorbed_logical_or = obelisk_sim.logic.logical or %xz, %ones : (!obelisk_sim.logic<8>, !obelisk_sim.logic<8>) -> !obelisk_sim.logic<1>
      %shifted = obelisk_sim.logic.shift left %known by %index : (!obelisk_sim.logic<8>, i8) -> !obelisk_sim.logic<8>
      %shifted_xz = obelisk_sim.logic.shift left %known by %xz : (!obelisk_sim.logic<8>, !obelisk_sim.logic<8>) -> !obelisk_sim.logic<8>
      %compare = obelisk_sim.logic.compare eq %known, %one : (!obelisk_sim.logic<8>, !obelisk_sim.logic<8>) -> !obelisk_sim.logic<1>
      %compare_xz = obelisk_sim.logic.compare eq %known, %xz : (!obelisk_sim.logic<8>, !obelisk_sim.logic<8>) -> !obelisk_sim.logic<1>
      %case_compare = obelisk_sim.logic.compare case_eq %xz, %known : (!obelisk_sim.logic<8>, !obelisk_sim.logic<8>) -> i1
      %case_compare_ne = obelisk_sim.logic.compare case_ne %xz, %known : (!obelisk_sim.logic<8>, !obelisk_sim.logic<8>) -> i1
      %concat = obelisk_sim.logic.concat %known, %one : (!obelisk_sim.logic<8>, !obelisk_sim.logic<8>) -> !obelisk_sim.logic<16>
      %concat_xz = obelisk_sim.logic.concat %known, %xz : (!obelisk_sim.logic<8>, !obelisk_sim.logic<8>) -> !obelisk_sim.logic<16>
      %replicated = obelisk_sim.logic.replicate %known times 2 : !obelisk_sim.logic<8> -> !obelisk_sim.logic<16>
      %replicated_xz = obelisk_sim.logic.replicate %xz times 2 : !obelisk_sim.logic<8> -> !obelisk_sim.logic<16>
      %extract = obelisk_sim.logic.extract %known from 2 : !obelisk_sim.logic<8> -> !obelisk_sim.logic<4>
      %extract_xz = obelisk_sim.logic.extract %xz from 2 : !obelisk_sim.logic<8> -> !obelisk_sim.logic<4>
      %dynamic = obelisk_sim.logic.dyn_extract %known from %index : (!obelisk_sim.logic<8>, i8) -> !obelisk_sim.logic<4>
      %logic_dynamic = obelisk_sim.logic.dyn_extract %known from %logic_index : (!obelisk_sim.logic<8>, !obelisk_sim.logic<8>) -> !obelisk_sim.logic<4>
      %bad_dynamic = obelisk_sim.logic.dyn_extract %known from %outside : (!obelisk_sim.logic<8>, i8) -> !obelisk_sim.logic<4>
      %unknown_dynamic = obelisk_sim.logic.dyn_extract %known from %xz : (!obelisk_sim.logic<8>, !obelisk_sim.logic<8>) -> !obelisk_sim.logic<4>
      %runtime_dynamic = obelisk_sim.logic.dyn_extract %known from %runtime_divisor : (!obelisk_sim.logic<8>, !obelisk_sim.logic<8>) -> !obelisk_sim.logic<4>
      %dynamic_xz = obelisk_sim.logic.dyn_extract %xz from %index : (!obelisk_sim.logic<8>, i8) -> !obelisk_sim.logic<4>
      %inserted = obelisk_sim.logic.insert %extract into %known at 2 : (!obelisk_sim.logic<8>, !obelisk_sim.logic<4>) -> !obelisk_sim.logic<8>
      %inserted_xz = obelisk_sim.logic.insert %extract into %xz at 2 : (!obelisk_sim.logic<8>, !obelisk_sim.logic<4>) -> !obelisk_sim.logic<8>
      %unsupported = builtin.unrealized_conversion_cast %known : !obelisk_sim.logic<8> to !obelisk_sim.logic<8>
      %local = obelisk_sim.ref.alloc %known : !obelisk_sim.logic<8> -> !obelisk_sim.ref<!obelisk_sim.logic<8>>
      %loaded = obelisk_sim.ref.load %local : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      %net_value = obelisk_sim.net.read %net : !obelisk_sim.net<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      %mux_known = obelisk_sim.logic.mux %reduced ? %known : %one : (!obelisk_sim.logic<1>, !obelisk_sim.logic<8>, !obelisk_sim.logic<8>) -> !obelisk_sim.logic<8>
      %mux_xz = obelisk_sim.logic.mux %reduced_xz ? %known : %one : (!obelisk_sim.logic<1>, !obelisk_sim.logic<8>, !obelisk_sim.logic<8>) -> !obelisk_sim.logic<8>
      obelisk_sim.return
    }
  }
}

// CHECK-LABEL: state-domain @logic_rules
// CHECK-LABEL: func @rules
// CHECK-NEXT:   bb0.op3.result0: two-state (logic-constant)
// CHECK-NEXT:   bb0.op4.result0: two-state (logic-from-bits)
// CHECK-NEXT:   bb0.op5.result0: two-state (logic-from-bits)
// CHECK-NEXT:   bb0.op6.result0: two-state (logic-constant)
// CHECK-NEXT:   bb0.op7.result0: two-state (logic-constant)
// CHECK-NEXT:   bb0.op8.result0: two-state (logic-constant)
// CHECK-NEXT:   bb0.op9.result0: may-four-state (unknown-constant)
// CHECK-NEXT:   bb0.op10.result0: two-state (logic-resize)
// CHECK-NEXT:   bb0.op11.result0: may-four-state (logic-resize)
// CHECK-NEXT:   bb0.op12.result0: two-state (logic-unary)
// CHECK-NEXT:   bb0.op13.result0: may-four-state (logic-unary)
// CHECK-NEXT:   bb0.op14.result0: two-state (logic-reduction)
// CHECK-NEXT:   bb0.op15.result0: may-four-state (logic-reduction)
// CHECK-NEXT:   bb0.op16.result0: two-state (logic-binary)
// CHECK-NEXT:   bb0.op17.result0: may-four-state (logic-binary)
// CHECK-NEXT:   bb0.op18.result0: two-state (logic-binary)
// CHECK-NEXT:   bb0.op19.result0: two-state (logic-binary)
// CHECK-NEXT:   bb0.op20.result0: two-state (logic-binary)
// CHECK-NEXT:   bb0.op21.result0: two-state (logic-binary)
// CHECK-NEXT:   bb0.op22.result0: may-four-state (division-divisor)
// CHECK-NEXT:   bb0.op23.result0: may-four-state (division-divisor)
// CHECK-NEXT:   bb0.op24.result0: may-four-state (logic-binary)
// CHECK-NEXT:   bb0.op25.result0: two-state (absorbing-constant)
// CHECK-NEXT:   bb0.op26.result0: two-state (absorbing-constant)
// CHECK-NEXT:   bb0.op27.result0: two-state (logic-logical)
// CHECK-NEXT:   bb0.op28.result0: may-four-state (logic-logical)
// CHECK-NEXT:   bb0.op29.result0: two-state (absorbing-constant)
// CHECK-NEXT:   bb0.op30.result0: two-state (absorbing-constant)
// CHECK-NEXT:   bb0.op31.result0: two-state (logic-shift)
// CHECK-NEXT:   bb0.op32.result0: may-four-state (logic-shift)
// CHECK-NEXT:   bb0.op33.result0: two-state (logic-compare)
// CHECK-NEXT:   bb0.op34.result0: may-four-state (logic-compare)
// CHECK-NEXT:   bb0.op35.result0: two-state (case-comparison)
// CHECK-NEXT:   bb0.op36.result0: two-state (case-comparison)
// CHECK-NEXT:   bb0.op37.result0: two-state (logic-concat)
// CHECK-NEXT:   bb0.op38.result0: may-four-state (logic-concat)
// CHECK-NEXT:   bb0.op39.result0: two-state (logic-replicate)
// CHECK-NEXT:   bb0.op40.result0: may-four-state (logic-replicate)
// CHECK-NEXT:   bb0.op41.result0: two-state (logic-extract)
// CHECK-NEXT:   bb0.op42.result0: may-four-state (logic-extract)
// CHECK-NEXT:   bb0.op43.result0: two-state (dynamic-extract)
// CHECK-NEXT:   bb0.op44.result0: two-state (dynamic-extract)
// CHECK-NEXT:   bb0.op45.result0: may-four-state (dynamic-extract-index)
// CHECK-NEXT:   bb0.op46.result0: may-four-state (dynamic-extract-index)
// CHECK-NEXT:   bb0.op47.result0: may-four-state (dynamic-extract-index)
// CHECK-NEXT:   bb0.op48.result0: may-four-state (dynamic-extract)
// CHECK-NEXT:   bb0.op49.result0: two-state (logic-insert)
// CHECK-NEXT:   bb0.op50.result0: may-four-state (logic-insert)
// CHECK-NEXT:   bb0.op51.result0: may-four-state (unsupported-producer)
// CHECK-NEXT:   bb0.op53.result0: may-four-state (ref-load)
// CHECK-NEXT:   bb0.op54.result0: may-four-state (net-read)
// CHECK-NEXT:   bb0.op55.result0: two-state (logic-mux)
// CHECK-NEXT:   bb0.op56.result0: may-four-state (logic-mux)
