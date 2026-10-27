// RUN: talos-opt %s '--test-talos-activation-regions=inputs=true' -o %t.analyzed 2>&1 | FileCheck %s
// RUN: talos-opt %s -o %t.original
// RUN: diff %t.original %t.analyzed
// RUN: talos-opt %s '--test-talos-activation-regions=compare-input-dependencies=true' -o /dev/null 2>&1 | FileCheck %s --check-prefix=COMPARE

// The input-only dependency proof agrees with full publication analysis.
// COMPARE: inputs @ascending agree
// COMPARE: inputs @descending agree
// COMPARE: inputs @modular_ne agree

// IEEE 1800-2023 11.6 and 12.7: a known packed counter remains known under
// constant add/sub and resizing, but its narrow storage width controls wrap.
!counter = !simulation.packed_array<1 : 0 x !simulation.logic<1>>
!array = !simulation.unpacked_array<0 : 3 x i8>
module { simulation.design @packed_induction {
  simulation.scope.decl 0
  simulation.storage.decl 0 in 0 : !array design
  simulation.storage.decl 1 in 0 : i8 design
  // CHECK-LABEL: regions @ascending valid=1
  // CHECK: must-write s0[0,24)
  // CHECK-NEXT: may-write s0[0,24)
  simulation.code_unit.decl 1 in 0 always_comb hierarchy "ascending"
  simulation.func @ascending(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
    %array = simulation.context.storage %ctx[0] : !simulation.ref<!array>
    %input = simulation.context.storage %ctx[1] : !simulation.ref<i8>
    %start = simulation.logic.constant 0 : i2, 0 : i2 : !simulation.logic<2>
    %initial = simulation.packed.unflatten %start : (!simulation.logic<2>) -> !counter
    %bound = simulation.logic.constant 2 : i4, 0 : i4 : !simulation.logic<4>
    %step = simulation.logic.constant 1 : i4, 0 : i4 : !simulation.logic<4>
    cf.br ^head(%initial : !counter)
  ^head(%i: !counter):
    %bits = simulation.packed.flatten %i : (!counter) -> !simulation.logic<2>
    %wide = simulation.logic.resize %bits signed = false : !simulation.logic<2> -> !simulation.logic<4>
    %more = simulation.logic.compare ule %wide, %bound : (!simulation.logic<4>, !simulation.logic<4>) -> !simulation.logic<1>
    %test = simulation.logic.is_true %more : !simulation.logic<1>
    cf.cond_br %test, ^body, ^wait
  ^body:
    %cell = simulation.ref.array_element %array[%wide] : (!simulation.ref<!array>, !simulation.logic<4>) -> !simulation.ref<i8>
    %value = simulation.ref.load %input : !simulation.ref<i8> -> i8
    simulation.ref.store %value to %cell : i8, !simulation.ref<i8>
    %nextWide = simulation.logic.binary add %wide, %step : !simulation.logic<4>
    %nextBits = simulation.logic.resize %nextWide signed = false : !simulation.logic<4> -> !simulation.logic<2>
    %next = simulation.packed.unflatten %nextBits : (!simulation.logic<2>) -> !counter
    cf.br ^head(%next : !counter)
  ^wait:
    simulation.suspend.change %input to ^resume : !simulation.ref<i8>
  ^resume:
    cf.br ^head(%initial : !counter)
  }
  // CHECK-LABEL: regions @descending valid=1
  // CHECK: must-write s0[8,24)
  // CHECK-NEXT: may-write s0[8,24)
  simulation.code_unit.decl 2 in 0 always_comb hierarchy "descending"
  simulation.func @descending(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
    %array = simulation.context.storage %ctx[0] : !simulation.ref<!array>
    %input = simulation.context.storage %ctx[1] : !simulation.ref<i8>
    %start = simulation.logic.constant 2 : i2, 0 : i2 : !simulation.logic<2>
    %initial = simulation.packed.unflatten %start : (!simulation.logic<2>) -> !counter
    %bound = simulation.logic.constant 0 : i4, 0 : i4 : !simulation.logic<4>
    %step = simulation.logic.constant 1 : i4, 0 : i4 : !simulation.logic<4>
    cf.br ^head(%initial : !counter)
  ^head(%i: !counter):
    %bits = simulation.packed.flatten %i : (!counter) -> !simulation.logic<2>
    %wide = simulation.logic.resize %bits signed = false : !simulation.logic<2> -> !simulation.logic<4>
    %more = simulation.logic.compare ugt %wide, %bound : (!simulation.logic<4>, !simulation.logic<4>) -> !simulation.logic<1>
    %test = simulation.logic.is_true %more : !simulation.logic<1>
    cf.cond_br %test, ^body, ^wait
  ^body:
    %cell = simulation.ref.array_element %array[%wide] : (!simulation.ref<!array>, !simulation.logic<4>) -> !simulation.ref<i8>
    %value = simulation.ref.load %input : !simulation.ref<i8> -> i8
    simulation.ref.store %value to %cell : i8, !simulation.ref<i8>
    %nextWide = simulation.logic.binary sub %wide, %step : !simulation.logic<4>
    %nextBits = simulation.logic.resize %nextWide signed = false : !simulation.logic<4> -> !simulation.logic<2>
    %next = simulation.packed.unflatten %nextBits : (!simulation.logic<2>) -> !counter
    cf.br ^head(%next : !counter)
  ^wait:
    simulation.suspend.change %input to ^resume : !simulation.ref<i8>
  ^resume:
    cf.br ^head(%initial : !counter)
  }
  // CHECK-LABEL: regions @modular_ne valid=1
  simulation.code_unit.decl 3 in 0 always_comb hierarchy "modular_ne"
  simulation.func @modular_ne(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 3 : i64} {
    %array = simulation.context.storage %ctx[0] : !simulation.ref<!array>
    %input = simulation.context.storage %ctx[1] : !simulation.ref<i8>
    %start = simulation.logic.constant 3 : i2, 0 : i2 : !simulation.logic<2>
    %initial = simulation.packed.unflatten %start : (!simulation.logic<2>) -> !counter
    %bound = simulation.logic.constant 1 : i4, 0 : i4 : !simulation.logic<4>
    %step = simulation.logic.constant 1 : i4, 0 : i4 : !simulation.logic<4>
    cf.br ^head(%initial : !counter)
  ^head(%i: !counter):
    %bits = simulation.packed.flatten %i : (!counter) -> !simulation.logic<2>
    %wide = simulation.logic.resize %bits signed = false : !simulation.logic<2> -> !simulation.logic<4>
    %more = simulation.logic.compare ne %wide, %bound : (!simulation.logic<4>, !simulation.logic<4>) -> !simulation.logic<1>
    %test = simulation.logic.is_true %more : !simulation.logic<1>
    cf.cond_br %test, ^body, ^wait
  ^body:
    %cell = simulation.ref.array_element %array[%wide] : (!simulation.ref<!array>, !simulation.logic<4>) -> !simulation.ref<i8>
    %value = simulation.ref.load %input : !simulation.ref<i8> -> i8
    simulation.ref.store %value to %cell : i8, !simulation.ref<i8>
    %nextWide = simulation.logic.binary add %wide, %step : !simulation.logic<4>
    %nextBits = simulation.logic.resize %nextWide signed = false : !simulation.logic<4> -> !simulation.logic<2>
    %next = simulation.packed.unflatten %nextBits : (!simulation.logic<2>) -> !counter
    cf.br ^head(%next : !counter)
  ^wait:
    simulation.suspend.change %input to ^resume : !simulation.ref<i8>
  ^resume:
    cf.br ^head(%initial : !counter)
  }
  // CHECK-LABEL: regions @wrap valid=0 reason=termination
  simulation.code_unit.decl 4 in 0 always_comb hierarchy "wrap"
  simulation.func @wrap(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 4 : i64} {
    %array = simulation.context.storage %ctx[0] : !simulation.ref<!array>
    %input = simulation.context.storage %ctx[1] : !simulation.ref<i8>
    %start = simulation.logic.constant 0 : i2, 0 : i2 : !simulation.logic<2>
    %initial = simulation.packed.unflatten %start : (!simulation.logic<2>) -> !counter
    %bound = simulation.logic.constant 3 : i4, 0 : i4 : !simulation.logic<4>
    %step = simulation.logic.constant 1 : i4, 0 : i4 : !simulation.logic<4>
    cf.br ^head(%initial : !counter)
  ^head(%i: !counter):
    %bits = simulation.packed.flatten %i : (!counter) -> !simulation.logic<2>
    %wide = simulation.logic.resize %bits signed = false : !simulation.logic<2> -> !simulation.logic<4>
    %more = simulation.logic.compare ule %wide, %bound : (!simulation.logic<4>, !simulation.logic<4>) -> !simulation.logic<1>
    %test = simulation.logic.is_true %more : !simulation.logic<1>
    cf.cond_br %test, ^body, ^wait
  ^body:
    %cell = simulation.ref.array_element %array[%wide] : (!simulation.ref<!array>, !simulation.logic<4>) -> !simulation.ref<i8>
    %value = simulation.ref.load %input : !simulation.ref<i8> -> i8
    simulation.ref.store %value to %cell : i8, !simulation.ref<i8>
    %nextWide = simulation.logic.binary add %wide, %step : !simulation.logic<4>
    %nextBits = simulation.logic.resize %nextWide signed = false : !simulation.logic<4> -> !simulation.logic<2>
    %next = simulation.packed.unflatten %nextBits : (!simulation.logic<2>) -> !counter
    cf.br ^head(%next : !counter)
  ^wait:
    simulation.suspend.change %input to ^resume : !simulation.ref<i8>
  ^resume:
    cf.br ^head(%initial : !counter)
  }
  // CHECK-LABEL: regions @unknown_init valid=0 reason=termination
  simulation.code_unit.decl 5 in 0 always_comb hierarchy "unknown_init"
  simulation.func @unknown_init(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 5 : i64} {
    %array = simulation.context.storage %ctx[0] : !simulation.ref<!array>
    %input = simulation.context.storage %ctx[1] : !simulation.ref<i8>
    %start = simulation.logic.constant 0 : i2, 1 : i2 : !simulation.logic<2>
    %initial = simulation.packed.unflatten %start : (!simulation.logic<2>) -> !counter
    %bound = simulation.logic.constant 2 : i4, 0 : i4 : !simulation.logic<4>
    %step = simulation.logic.constant 1 : i4, 0 : i4 : !simulation.logic<4>
    cf.br ^head(%initial : !counter)
  ^head(%i: !counter):
    %bits = simulation.packed.flatten %i : (!counter) -> !simulation.logic<2>
    %wide = simulation.logic.resize %bits signed = false : !simulation.logic<2> -> !simulation.logic<4>
    %more = simulation.logic.compare ule %wide, %bound : (!simulation.logic<4>, !simulation.logic<4>) -> !simulation.logic<1>
    %test = simulation.logic.is_true %more : !simulation.logic<1>
    cf.cond_br %test, ^body, ^wait
  ^body:
    %cell = simulation.ref.array_element %array[%wide] : (!simulation.ref<!array>, !simulation.logic<4>) -> !simulation.ref<i8>
    %value = simulation.ref.load %input : !simulation.ref<i8> -> i8
    simulation.ref.store %value to %cell : i8, !simulation.ref<i8>
    %nextWide = simulation.logic.binary add %wide, %step : !simulation.logic<4>
    %nextBits = simulation.logic.resize %nextWide signed = false : !simulation.logic<4> -> !simulation.logic<2>
    %next = simulation.packed.unflatten %nextBits : (!simulation.logic<2>) -> !counter
    cf.br ^head(%next : !counter)
  ^wait:
    simulation.suspend.change %input to ^resume : !simulation.ref<i8>
  ^resume:
    cf.br ^head(%initial : !counter)
  }
  // CHECK-LABEL: regions @unknown_bound valid=0 reason=termination
  simulation.code_unit.decl 6 in 0 always_comb hierarchy "unknown_bound"
  simulation.func @unknown_bound(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 6 : i64} {
    %array = simulation.context.storage %ctx[0] : !simulation.ref<!array>
    %input = simulation.context.storage %ctx[1] : !simulation.ref<i8>
    %start = simulation.logic.constant 0 : i2, 0 : i2 : !simulation.logic<2>
    %initial = simulation.packed.unflatten %start : (!simulation.logic<2>) -> !counter
    %bound = simulation.logic.constant 2 : i4, 1 : i4 : !simulation.logic<4>
    %step = simulation.logic.constant 1 : i4, 0 : i4 : !simulation.logic<4>
    cf.br ^head(%initial : !counter)
  ^head(%i: !counter):
    %bits = simulation.packed.flatten %i : (!counter) -> !simulation.logic<2>
    %wide = simulation.logic.resize %bits signed = false : !simulation.logic<2> -> !simulation.logic<4>
    %more = simulation.logic.compare ule %wide, %bound : (!simulation.logic<4>, !simulation.logic<4>) -> !simulation.logic<1>
    %test = simulation.logic.is_true %more : !simulation.logic<1>
    cf.cond_br %test, ^body, ^wait
  ^body:
    %cell = simulation.ref.array_element %array[%wide] : (!simulation.ref<!array>, !simulation.logic<4>) -> !simulation.ref<i8>
    %value = simulation.ref.load %input : !simulation.ref<i8> -> i8
    simulation.ref.store %value to %cell : i8, !simulation.ref<i8>
    %nextWide = simulation.logic.binary add %wide, %step : !simulation.logic<4>
    %nextBits = simulation.logic.resize %nextWide signed = false : !simulation.logic<4> -> !simulation.logic<2>
    %next = simulation.packed.unflatten %nextBits : (!simulation.logic<2>) -> !counter
    cf.br ^head(%next : !counter)
  ^wait:
    simulation.suspend.change %input to ^resume : !simulation.ref<i8>
  ^resume:
    cf.br ^head(%initial : !counter)
  }
  // CHECK-LABEL: regions @unknown_stride valid=0 reason=termination
  simulation.code_unit.decl 7 in 0 always_comb hierarchy "unknown_stride"
  simulation.func @unknown_stride(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 7 : i64} {
    %array = simulation.context.storage %ctx[0] : !simulation.ref<!array>
    %input = simulation.context.storage %ctx[1] : !simulation.ref<i8>
    %start = simulation.logic.constant 0 : i2, 0 : i2 : !simulation.logic<2>
    %initial = simulation.packed.unflatten %start : (!simulation.logic<2>) -> !counter
    %bound = simulation.logic.constant 2 : i4, 0 : i4 : !simulation.logic<4>
    %step = simulation.logic.constant 1 : i4, 1 : i4 : !simulation.logic<4>
    cf.br ^head(%initial : !counter)
  ^head(%i: !counter):
    %bits = simulation.packed.flatten %i : (!counter) -> !simulation.logic<2>
    %wide = simulation.logic.resize %bits signed = false : !simulation.logic<2> -> !simulation.logic<4>
    %more = simulation.logic.compare ule %wide, %bound : (!simulation.logic<4>, !simulation.logic<4>) -> !simulation.logic<1>
    %test = simulation.logic.is_true %more : !simulation.logic<1>
    cf.cond_br %test, ^body, ^wait
  ^body:
    %cell = simulation.ref.array_element %array[%wide] : (!simulation.ref<!array>, !simulation.logic<4>) -> !simulation.ref<i8>
    %value = simulation.ref.load %input : !simulation.ref<i8> -> i8
    simulation.ref.store %value to %cell : i8, !simulation.ref<i8>
    %nextWide = simulation.logic.binary add %wide, %step : !simulation.logic<4>
    %nextBits = simulation.logic.resize %nextWide signed = false : !simulation.logic<4> -> !simulation.logic<2>
    %next = simulation.packed.unflatten %nextBits : (!simulation.logic<2>) -> !counter
    cf.br ^head(%next : !counter)
  ^wait:
    simulation.suspend.change %input to ^resume : !simulation.ref<i8>
  ^resume:
    cf.br ^head(%initial : !counter)
  }
  // CHECK-LABEL: regions @zero_stride valid=0 reason=termination
  simulation.code_unit.decl 8 in 0 always_comb hierarchy "zero_stride"
  simulation.func @zero_stride(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 8 : i64} {
    %array = simulation.context.storage %ctx[0] : !simulation.ref<!array>
    %input = simulation.context.storage %ctx[1] : !simulation.ref<i8>
    %start = simulation.logic.constant 0 : i2, 0 : i2 : !simulation.logic<2>
    %initial = simulation.packed.unflatten %start : (!simulation.logic<2>) -> !counter
    %bound = simulation.logic.constant 2 : i4, 0 : i4 : !simulation.logic<4>
    %step = simulation.logic.constant 0 : i4, 0 : i4 : !simulation.logic<4>
    cf.br ^head(%initial : !counter)
  ^head(%i: !counter):
    %bits = simulation.packed.flatten %i : (!counter) -> !simulation.logic<2>
    %wide = simulation.logic.resize %bits signed = false : !simulation.logic<2> -> !simulation.logic<4>
    %more = simulation.logic.compare ule %wide, %bound : (!simulation.logic<4>, !simulation.logic<4>) -> !simulation.logic<1>
    %test = simulation.logic.is_true %more : !simulation.logic<1>
    cf.cond_br %test, ^body, ^wait
  ^body:
    %cell = simulation.ref.array_element %array[%wide] : (!simulation.ref<!array>, !simulation.logic<4>) -> !simulation.ref<i8>
    %value = simulation.ref.load %input : !simulation.ref<i8> -> i8
    simulation.ref.store %value to %cell : i8, !simulation.ref<i8>
    %nextWide = simulation.logic.binary add %wide, %step : !simulation.logic<4>
    %nextBits = simulation.logic.resize %nextWide signed = false : !simulation.logic<4> -> !simulation.logic<2>
    %next = simulation.packed.unflatten %nextBits : (!simulation.logic<2>) -> !counter
    cf.br ^head(%next : !counter)
  ^wait:
    simulation.suspend.change %input to ^resume : !simulation.ref<i8>
  ^resume:
    cf.br ^head(%initial : !counter)
  }
  // CHECK-LABEL: regions @wide_bound valid=0 reason=termination
  simulation.code_unit.decl 9 in 0 always_comb hierarchy "wide_bound"
  simulation.func @wide_bound(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 9 : i64} {
    %array = simulation.context.storage %ctx[0] : !simulation.ref<!array>
    %input = simulation.context.storage %ctx[1] : !simulation.ref<i8>
    %start = simulation.logic.constant 0 : i2, 0 : i2 : !simulation.logic<2>
    %initial = simulation.packed.unflatten %start : (!simulation.logic<2>) -> !counter
    %bound = simulation.logic.constant 7 : i4, 0 : i4 : !simulation.logic<4>
    %step = simulation.logic.constant 1 : i4, 0 : i4 : !simulation.logic<4>
    cf.br ^head(%initial : !counter)
  ^head(%i: !counter):
    %bits = simulation.packed.flatten %i : (!counter) -> !simulation.logic<2>
    %wide = simulation.logic.resize %bits signed = false : !simulation.logic<2> -> !simulation.logic<4>
    %more = simulation.logic.compare ule %wide, %bound : (!simulation.logic<4>, !simulation.logic<4>) -> !simulation.logic<1>
    %test = simulation.logic.is_true %more : !simulation.logic<1>
    cf.cond_br %test, ^body, ^wait
  ^body:
    %cell = simulation.ref.array_element %array[%wide] : (!simulation.ref<!array>, !simulation.logic<4>) -> !simulation.ref<i8>
    %value = simulation.ref.load %input : !simulation.ref<i8> -> i8
    simulation.ref.store %value to %cell : i8, !simulation.ref<i8>
    %nextWide = simulation.logic.binary add %wide, %step : !simulation.logic<4>
    %nextBits = simulation.logic.resize %nextWide signed = false : !simulation.logic<4> -> !simulation.logic<2>
    %next = simulation.packed.unflatten %nextBits : (!simulation.logic<2>) -> !counter
    cf.br ^head(%next : !counter)
  ^wait:
    simulation.suspend.change %input to ^resume : !simulation.ref<i8>
  ^resume:
    cf.br ^head(%initial : !counter)
  }
  // CHECK-LABEL: regions @lossy_test valid=0 reason=termination
  simulation.code_unit.decl 10 in 0 always_comb hierarchy "lossy_test"
  simulation.func @lossy_test(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 10 : i64} {
    %array = simulation.context.storage %ctx[0] : !simulation.ref<!array>
    %input = simulation.context.storage %ctx[1] : !simulation.ref<i8>
    %start = simulation.logic.constant 0 : i2, 0 : i2 : !simulation.logic<2>
    %initial = simulation.packed.unflatten %start : (!simulation.logic<2>) -> !counter
    %bound = simulation.logic.constant 1 : i1, 0 : i1 : !simulation.logic<1>
    %step = simulation.logic.constant 1 : i1, 0 : i1 : !simulation.logic<1>
    cf.br ^head(%initial : !counter)
  ^head(%i: !counter):
    %bits = simulation.packed.flatten %i : (!counter) -> !simulation.logic<2>
    %wide = simulation.logic.resize %bits signed = false : !simulation.logic<2> -> !simulation.logic<1>
    %more = simulation.logic.compare ule %wide, %bound : (!simulation.logic<1>, !simulation.logic<1>) -> !simulation.logic<1>
    %test = simulation.logic.is_true %more : !simulation.logic<1>
    cf.cond_br %test, ^body, ^wait
  ^body:
    %cell = simulation.ref.array_element %array[%wide] : (!simulation.ref<!array>, !simulation.logic<1>) -> !simulation.ref<i8>
    %value = simulation.ref.load %input : !simulation.ref<i8> -> i8
    simulation.ref.store %value to %cell : i8, !simulation.ref<i8>
    %nextWide = simulation.logic.binary add %wide, %step : !simulation.logic<1>
    %nextBits = simulation.logic.resize %nextWide signed = false : !simulation.logic<1> -> !simulation.logic<2>
    %next = simulation.packed.unflatten %nextBits : (!simulation.logic<2>) -> !counter
    cf.br ^head(%next : !counter)
  ^wait:
    simulation.suspend.change %input to ^resume : !simulation.ref<i8>
  ^resume:
    cf.br ^head(%initial : !counter)
  }
  // CHECK-LABEL: regions @signed_test valid=0 reason=termination
  simulation.code_unit.decl 11 in 0 always_comb hierarchy "signed_test"
  simulation.func @signed_test(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 11 : i64} {
    %array = simulation.context.storage %ctx[0] : !simulation.ref<!array>
    %input = simulation.context.storage %ctx[1] : !simulation.ref<i8>
    %start = simulation.logic.constant 0 : i2, 0 : i2 : !simulation.logic<2>
    %initial = simulation.packed.unflatten %start : (!simulation.logic<2>) -> !counter
    %bound = simulation.logic.constant 2 : i4, 0 : i4 : !simulation.logic<4>
    %step = simulation.logic.constant 1 : i4, 0 : i4 : !simulation.logic<4>
    cf.br ^head(%initial : !counter)
  ^head(%i: !counter):
    %bits = simulation.packed.flatten %i : (!counter) -> !simulation.logic<2>
    %wide = simulation.logic.resize %bits signed = false : !simulation.logic<2> -> !simulation.logic<4>
    %more = simulation.logic.compare slt %wide, %bound : (!simulation.logic<4>, !simulation.logic<4>) -> !simulation.logic<1>
    %test = simulation.logic.is_true %more : !simulation.logic<1>
    cf.cond_br %test, ^body, ^wait
  ^body:
    %cell = simulation.ref.array_element %array[%wide] : (!simulation.ref<!array>, !simulation.logic<4>) -> !simulation.ref<i8>
    %value = simulation.ref.load %input : !simulation.ref<i8> -> i8
    simulation.ref.store %value to %cell : i8, !simulation.ref<i8>
    %nextWide = simulation.logic.binary add %wide, %step : !simulation.logic<4>
    %nextBits = simulation.logic.resize %nextWide signed = false : !simulation.logic<4> -> !simulation.logic<2>
    %next = simulation.packed.unflatten %nextBits : (!simulation.logic<2>) -> !counter
    cf.br ^head(%next : !counter)
  ^wait:
    simulation.suspend.change %input to ^resume : !simulation.ref<i8>
  ^resume:
    cf.br ^head(%initial : !counter)
  }
} }
