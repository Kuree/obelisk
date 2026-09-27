// RUN: obelisk-opt %s -o /dev/null --pass-pipeline='builtin.module(test-obelisk-sim-state-domain)' 2> %t.threaded
// RUN: obelisk-opt %s -o /dev/null --mlir-disable-threading --pass-pipeline='builtin.module(test-obelisk-sim-state-domain)' 2> %t.single
// RUN: diff %t.threaded %t.single
// RUN: FileCheck %s < %t.threaded

module {
  simulation.design @logic_rules {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.logic_rules.rules.9000001"
    simulation.scope.decl 0
    simulation.net.decl 0 in 0 : !simulation.logic<8> design

    simulation.func @rules(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %net: !simulation.net<!simulation.logic<8>> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 0 : i64},
        %dynamic_bits: i8 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %bits = arith.constant 3 : i8
      %index = arith.constant 2 : i8
      %outside = arith.constant 7 : i8
      %logic_index = simulation.logic.constant 2 : i8, 0 : i8 : !simulation.logic<8>
      %known = simulation.logic.from_bits %bits : i8 -> !simulation.logic<8>
      %runtime_divisor = simulation.logic.from_bits %dynamic_bits : i8 -> !simulation.logic<8>
      %zero = simulation.logic.constant 0 : i8, 0 : i8 : !simulation.logic<8>
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !simulation.logic<8>
      %ones = simulation.logic.constant -1 : i8, 0 : i8 : !simulation.logic<8>
      %xz = simulation.logic.constant 0 : i8, -1 : i8 : !simulation.logic<8>
      %resized = simulation.logic.resize %known signed = false : !simulation.logic<8> -> !simulation.logic<16>
      %resized_xz = simulation.logic.resize %xz signed = false : !simulation.logic<8> -> !simulation.logic<16>
      %unary = simulation.logic.unary bit_not %known : (!simulation.logic<8>) -> !simulation.logic<8>
      %unary_xz = simulation.logic.unary bit_not %xz : (!simulation.logic<8>) -> !simulation.logic<8>
      %reduced = simulation.logic.reduction xor %known : !simulation.logic<8> -> !simulation.logic<1>
      %reduced_xz = simulation.logic.reduction xor %xz : !simulation.logic<8> -> !simulation.logic<1>
      %added = simulation.logic.binary add %known, %one : !simulation.logic<8>
      %added_xz = simulation.logic.binary add %known, %xz : !simulation.logic<8>
      %div = simulation.logic.binary udiv %known, %one : !simulation.logic<8>
      %signed_div = simulation.logic.binary sdiv %known, %one : !simulation.logic<8>
      %mod = simulation.logic.binary umod %known, %one : !simulation.logic<8>
      %signed_mod = simulation.logic.binary smod %known, %one : !simulation.logic<8>
      %bad_div = simulation.logic.binary udiv %known, %zero : !simulation.logic<8>
      %runtime_div = simulation.logic.binary udiv %known, %runtime_divisor : !simulation.logic<8>
      %unknown_div = simulation.logic.binary udiv %known, %xz : !simulation.logic<8>
      %absorbed_and = simulation.logic.binary and %xz, %zero : !simulation.logic<8>
      %absorbed_or = simulation.logic.binary or %xz, %ones : !simulation.logic<8>
      %logical = simulation.logic.logical and %known, %one : (!simulation.logic<8>, !simulation.logic<8>) -> !simulation.logic<1>
      %logical_xz = simulation.logic.logical and %known, %xz : (!simulation.logic<8>, !simulation.logic<8>) -> !simulation.logic<1>
      %absorbed_logical = simulation.logic.logical and %xz, %zero : (!simulation.logic<8>, !simulation.logic<8>) -> !simulation.logic<1>
      %absorbed_logical_or = simulation.logic.logical or %xz, %ones : (!simulation.logic<8>, !simulation.logic<8>) -> !simulation.logic<1>
      %shifted = simulation.logic.shift left %known by %index : (!simulation.logic<8>, i8) -> !simulation.logic<8>
      %shifted_xz = simulation.logic.shift left %known by %xz : (!simulation.logic<8>, !simulation.logic<8>) -> !simulation.logic<8>
      %compare = simulation.logic.compare eq %known, %one : (!simulation.logic<8>, !simulation.logic<8>) -> !simulation.logic<1>
      %compare_xz = simulation.logic.compare eq %known, %xz : (!simulation.logic<8>, !simulation.logic<8>) -> !simulation.logic<1>
      %case_compare = simulation.logic.compare case_eq %xz, %known : (!simulation.logic<8>, !simulation.logic<8>) -> i1
      %case_compare_ne = simulation.logic.compare case_ne %xz, %known : (!simulation.logic<8>, !simulation.logic<8>) -> i1
      %concat = simulation.logic.concat %known, %one : (!simulation.logic<8>, !simulation.logic<8>) -> !simulation.logic<16>
      %concat_xz = simulation.logic.concat %known, %xz : (!simulation.logic<8>, !simulation.logic<8>) -> !simulation.logic<16>
      %replicated = simulation.logic.replicate %known times 2 : !simulation.logic<8> -> !simulation.logic<16>
      %replicated_xz = simulation.logic.replicate %xz times 2 : !simulation.logic<8> -> !simulation.logic<16>
      %extract = simulation.logic.extract %known from 2 : !simulation.logic<8> -> !simulation.logic<4>
      %extract_xz = simulation.logic.extract %xz from 2 : !simulation.logic<8> -> !simulation.logic<4>
      %dynamic = simulation.logic.dyn_extract %known from %index : (!simulation.logic<8>, i8) -> !simulation.logic<4>
      %logic_dynamic = simulation.logic.dyn_extract %known from %logic_index : (!simulation.logic<8>, !simulation.logic<8>) -> !simulation.logic<4>
      %bad_dynamic = simulation.logic.dyn_extract %known from %outside : (!simulation.logic<8>, i8) -> !simulation.logic<4>
      %unknown_dynamic = simulation.logic.dyn_extract %known from %xz : (!simulation.logic<8>, !simulation.logic<8>) -> !simulation.logic<4>
      %runtime_dynamic = simulation.logic.dyn_extract %known from %runtime_divisor : (!simulation.logic<8>, !simulation.logic<8>) -> !simulation.logic<4>
      %dynamic_xz = simulation.logic.dyn_extract %xz from %index : (!simulation.logic<8>, i8) -> !simulation.logic<4>
      %inserted = simulation.logic.insert %extract into %known at 2 : (!simulation.logic<8>, !simulation.logic<4>) -> !simulation.logic<8>
      %inserted_xz = simulation.logic.insert %extract into %xz at 2 : (!simulation.logic<8>, !simulation.logic<4>) -> !simulation.logic<8>
      %unsupported = builtin.unrealized_conversion_cast %known : !simulation.logic<8> to !simulation.logic<8>
      %local = simulation.ref.alloc %known : !simulation.logic<8> -> !simulation.ref<!simulation.logic<8>>
      %loaded = simulation.ref.load %local : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %net_value = simulation.net.read %net : !simulation.net<!simulation.logic<8>> -> !simulation.logic<8>
      %mux_known = simulation.logic.mux %reduced ? %known : %one : (!simulation.logic<1>, !simulation.logic<8>, !simulation.logic<8>) -> !simulation.logic<8>
      %mux_xz = simulation.logic.mux %reduced_xz ? %known : %one : (!simulation.logic<1>, !simulation.logic<8>, !simulation.logic<8>) -> !simulation.logic<8>
      simulation.return
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
