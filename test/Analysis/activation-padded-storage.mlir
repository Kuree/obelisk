// RUN: obelisk-opt %s '--test-obelisk-activation-regions=reaching-values=true inputs=true' -o %t.analyzed 2>&1 | FileCheck %s --check-prefix=CHECK
// RUN: obelisk-opt %s '--test-obelisk-activation-regions=inputs=true' -o /dev/null 2>&1 | FileCheck %s --check-prefix=ALL
// RUN: obelisk-opt %s -o %t.original
// RUN: diff %t.original %t.analyzed
// Native byte padding does not contain retained SystemVerilog values.
// Both physical offsets and the unwritten logical element remain significant.
// An array copy in each iteration publishes the next element's old value
// until that element is assigned. Only the final reaching versions are free
// of the incoming scratch state. These facts alone do not authorize hiding
// the intermediate updates (IEEE 1800-2023 4.3, 4.6(a)).
!array = !simulation.unpacked_array<0 : 1 x i7, #simulation.storage_layout<size = 2, alignment = 1, stride = 1, offsets = []>>
!record = !simulation.unpacked_struct<[#simulation.field<name = "flag", type = i3, ordinal = 0, packedOffset = 0>, #simulation.field<name = "data", type = !array, ordinal = 1, packedOffset = 0>], #simulation.storage_layout<size = 3, alignment = 1, stride = 0, offsets = [0, 1]>>
!nested = !simulation.unpacked_array<0 : 1 x !record, #simulation.storage_layout<size = 6, alignment = 1, stride = 3, offsets = []>>
module {
  simulation.design @padded_final_versions {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !array design
    simulation.storage.decl 1 in 0 : !array design
    simulation.storage.decl 2 in 0 : !array design
    simulation.storage.decl 3 in 0 : i1 design
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "whole_copy"
    // CHECK-LABEL: regions @whole_copy valid=1
    // ALL-LABEL: regions @whole_copy valid=1
    // CHECK: values valid=1
    // CHECK-NEXT: value-input s0[0,7) s0[8,15) s1[8,15)
    // CHECK-NEXT: final-input s0[0,7) s0[8,15)
    simulation.func @whole_copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<!array>
      %scratch = simulation.context.storage %ctx[1] : !simulation.ref<!array>
      %output = simulation.context.storage %ctx[2] : !simulation.ref<!array>
      %zero = arith.constant 0 : i8
      %two = arith.constant 2 : i8
      %one = arith.constant 1 : i8
      cf.br ^head(%zero : i8)
    ^head(%i: i8):
      %more = arith.cmpi slt, %i, %two : i8
      cf.cond_br %more, ^body, ^wait
    ^body:
      %source = simulation.ref.array_element %input[%i] : (!simulation.ref<!array>, i8) -> !simulation.ref<i7>
      %dest = simulation.ref.array_element %scratch[%i] : (!simulation.ref<!array>, i8) -> !simulation.ref<i7>
      %v = simulation.ref.load %source : !simulation.ref<i7> -> i7
      simulation.ref.store %v to %dest : i7, !simulation.ref<i7>
      %all = simulation.ref.load %scratch : !simulation.ref<!array> -> !array
      simulation.ref.store %all to %output : !array, !simulation.ref<!array>
      %next = arith.addi %i, %one : i8
      cf.br ^head(%next : i8)
    ^wait:
      simulation.suspend.change %input to ^resume : !simulation.ref<!array>
    ^resume:
      cf.br ^head(%zero : i8)
    }
    simulation.storage.decl 4 in 0 : !array design
    simulation.code_unit.decl 2 in 0 always_comb hierarchy "complete_fields"
    // CHECK-LABEL: regions @complete_fields valid=1
    // ALL-LABEL: regions @complete_fields valid=1
    // ALL: must-write s4[0,7) s4[8,15){{$}}
    // ALL-NEXT: may-write s4[0,7) s4[8,15)
    simulation.func @complete_fields(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<!array>
      %output = simulation.context.storage %ctx[4] : !simulation.ref<!array>
      %condition = simulation.context.storage %ctx[3] : !simulation.ref<i1>
      %first = simulation.ref.subelement %output[[0]] : !simulation.ref<!array> -> !simulation.ref<i7>
      %second = simulation.ref.subelement %output[[1]] : !simulation.ref<!array> -> !simulation.ref<i7>
      %one = arith.constant 1 : i7
      cf.br ^body
    ^body:
      %choose = simulation.ref.load %condition : !simulation.ref<i1> -> i1
      cf.cond_br %choose, ^whole, ^fields
    ^whole:
      %all = simulation.ref.load %input : !simulation.ref<!array> -> !array
      simulation.ref.store %all to %output : !array, !simulation.ref<!array>
      cf.br ^wait
    ^fields:
      simulation.ref.store %one to %first : i7, !simulation.ref<i7>
      simulation.ref.store %one to %second : i7, !simulation.ref<i7>
      cf.br ^wait
    ^wait:
      simulation.suspend.any %input, %condition edges [0, 0] to ^body : !simulation.ref<!array>, !simulation.ref<i1>
    }
    simulation.storage.decl 5 in 0 : !array design
    simulation.code_unit.decl 3 in 0 always_comb hierarchy "missing_field"
    // CHECK-LABEL: regions @missing_field valid=1
    // ALL-LABEL: regions @missing_field valid=1
    // ALL: must-write s5[0,7){{$}}
    // ALL-NEXT: may-write s5[0,7) s5[8,15)
    simulation.func @missing_field(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 3 : i64} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<!array>
      %output = simulation.context.storage %ctx[5] : !simulation.ref<!array>
      %condition = simulation.context.storage %ctx[3] : !simulation.ref<i1>
      %first = simulation.ref.subelement %output[[0]] : !simulation.ref<!array> -> !simulation.ref<i7>
      %second = simulation.ref.subelement %output[[1]] : !simulation.ref<!array> -> !simulation.ref<i7>
      %one = arith.constant 1 : i7
      cf.br ^body
    ^body:
      %choose = simulation.ref.load %condition : !simulation.ref<i1> -> i1
      cf.cond_br %choose, ^whole, ^fields
    ^whole:
      %all = simulation.ref.load %input : !simulation.ref<!array> -> !array
      simulation.ref.store %all to %output : !array, !simulation.ref<!array>
      cf.br ^wait
    ^fields:
      simulation.ref.store %one to %first : i7, !simulation.ref<i7>
      cf.br ^wait
    ^wait:
      simulation.suspend.any %input, %condition edges [0, 0] to ^body : !simulation.ref<!array>, !simulation.ref<i1>
    }
    simulation.storage.decl 6 in 0 : i8 design
    simulation.storage.decl 7 in 0 : !array design
    simulation.code_unit.decl 4 in 0 always_comb hierarchy "dynamic_update"
    // A dynamic selected-element range includes the root's holes, but those
    // bits do not introduce incoming state after every element is initialized.
    // CHECK-LABEL: regions @dynamic_update valid=1
    // ALL-LABEL: regions @dynamic_update valid=1
    // CHECK: value-input s6[0,8){{$}}
    // CHECK-NEXT: final-input s6[0,8){{$}}
    // ALL: must-write s7[0,7) s7[8,15)
    // ALL-NEXT: may-write s7[0,7) s7[8,15)
    simulation.func @dynamic_update(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 4 : i64} {
      %index = simulation.context.storage %ctx[6] : !simulation.ref<i8>
      %output = simulation.context.storage %ctx[7] : !simulation.ref<!array>
      %first = simulation.ref.subelement %output[[0]] : !simulation.ref<!array> -> !simulation.ref<i7>
      %second = simulation.ref.subelement %output[[1]] : !simulation.ref<!array> -> !simulation.ref<i7>
      %one = arith.constant 1 : i7
      cf.br ^body
    ^body:
      simulation.ref.store %one to %first : i7, !simulation.ref<i7>
      simulation.ref.store %one to %second : i7, !simulation.ref<i7>
      %i = simulation.ref.load %index : !simulation.ref<i8> -> i8
      %selected = simulation.ref.array_element %output[%i] : (!simulation.ref<!array>, i8) -> !simulation.ref<i7>
      %old = simulation.ref.load %selected : !simulation.ref<i7> -> i7
      %next = arith.addi %old, %one : i7
      simulation.ref.store %next to %selected : i7, !simulation.ref<i7>
      simulation.suspend.change %index to ^body : !simulation.ref<i8>
    }
    simulation.storage.decl 8 in 0 : !nested design
    simulation.storage.decl 9 in 0 : !nested design
    simulation.code_unit.decl 5 in 0 always_comb hierarchy "nested_copy"
    // CHECK-LABEL: regions @nested_copy valid=1
    // ALL-LABEL: regions @nested_copy valid=1
    // CHECK: value-input s8[0,3) s8[8,15) s8[16,23) s8[24,27) s8[32,39) s8[40,47){{$}}
    // ALL: must-write s9[0,3) s9[8,15) s9[16,23) s9[24,27) s9[32,39) s9[40,47)
    simulation.func @nested_copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 5 : i64} {
      %input = simulation.context.storage %ctx[8] : !simulation.ref<!nested>
      %output = simulation.context.storage %ctx[9] : !simulation.ref<!nested>
      cf.br ^body
    ^body:
      %v = simulation.ref.load %input : !simulation.ref<!nested> -> !nested
      simulation.ref.store %v to %output : !nested, !simulation.ref<!nested>
      simulation.suspend.change %input to ^body : !simulation.ref<!nested>
    }
  }
}
