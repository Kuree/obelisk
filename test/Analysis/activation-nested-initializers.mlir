// RUN: obelisk-opt %s --test-obelisk-activation-regions -o %t.analyzed 2>&1 | FileCheck %s
// RUN: obelisk-opt %s -o %t.original
// RUN: diff %t.original %t.analyzed
// RUN: obelisk-opt %s --test-obelisk-activation-memory -o /dev/null 2>&1 | FileCheck %s --check-prefix=MEM
// IEEE 1800-2023 12.7.1: each entry initializes the inner induction from
// the enclosing iteration. The finite proof must include zero-trip entries
// and every stride residue, and respect fixed-width overflow (11.6).
!array = !simulation.unpacked_array<0 : 7 x i8>
module {
  simulation.design @initializers {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !array design
    simulation.storage.decl 1 in 0 : !array design
    simulation.storage.decl 2 in 0 : i8 design
    // CHECK-LABEL: regions @increasing_init valid=1
    // MEM-LABEL: activation @increasing_init valid=1 complete=1
    // MEM-NEXT: live-in s0[0,32)
    // MEM-NEXT: must-write s1[0,32)
    // MEM-NEXT: may-write s1[0,32)
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "increasing_init"
    simulation.func @increasing_init(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %src = simulation.context.storage %ctx[0] : !simulation.ref<!array>
      %dst = simulation.context.storage %ctx[1] : !simulation.ref<!array>
      %control = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %start = arith.constant 0 : i8
      %limit = arith.constant 4 : i8
      %one = arith.constant 1 : i8
      %innerLimit = arith.constant 4 : i8
      %step = arith.constant 1 : i8
      %zero = arith.constant 0 : i8
      cf.br ^outer(%start : i8)
    ^outer(%i: i8):
      %more = arith.cmpi slt, %i, %limit : i8
      cf.cond_br %more, ^enter, ^wait
    ^enter:
      %initial = arith.subi %i, %one : i8
      cf.br ^inner(%i : i8)
    ^inner(%k: i8):
      %again = arith.cmpi slt, %k, %innerLimit : i8
      cf.cond_br %again, ^body, ^advance
    ^body:
      %s = simulation.ref.array_element %src[%k] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %d = simulation.ref.array_element %dst[%k] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %v = simulation.ref.load %s : !simulation.ref<i8> -> i8
      simulation.ref.store %v to %d : i8, !simulation.ref<i8>
      %stop = arith.cmpi eq, %v, %zero : i8
      cf.br ^latch
    ^latch:
      %knext = arith.addi %k, %step : i8
      cf.br ^inner(%knext : i8)
    ^advance:
      %next = arith.addi %i, %one : i8
      cf.br ^outer(%next : i8)
    ^wait:
      simulation.suspend.change %src to ^resume : !simulation.ref<!array>
    ^resume:
      cf.br ^outer(%start : i8)
    }
    // CHECK-LABEL: regions @descending_init valid=1
    // MEM-LABEL: activation @descending_init valid=1 complete=1
    // MEM-NEXT: live-in s0[0,24)
    // MEM-NEXT: must-write s1[0,24)
    // MEM-NEXT: may-write s1[0,24)
    simulation.code_unit.decl 2 in 0 always_comb hierarchy "descending_init"
    simulation.func @descending_init(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
      %src = simulation.context.storage %ctx[0] : !simulation.ref<!array>
      %dst = simulation.context.storage %ctx[1] : !simulation.ref<!array>
      %control = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %start = arith.constant 0 : i8
      %limit = arith.constant 4 : i8
      %one = arith.constant 1 : i8
      %innerLimit = arith.constant 0 : i8
      %step = arith.constant 1 : i8
      %zero = arith.constant 0 : i8
      cf.br ^outer(%start : i8)
    ^outer(%i: i8):
      %more = arith.cmpi slt, %i, %limit : i8
      cf.cond_br %more, ^enter, ^wait
    ^enter:
      %initial = arith.subi %i, %one : i8
      cf.br ^inner(%initial : i8)
    ^inner(%k: i8):
      %again = arith.cmpi sge, %k, %innerLimit : i8
      cf.cond_br %again, ^body, ^advance
    ^body:
      %s = simulation.ref.array_element %src[%k] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %d = simulation.ref.array_element %dst[%k] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %v = simulation.ref.load %s : !simulation.ref<i8> -> i8
      simulation.ref.store %v to %d : i8, !simulation.ref<i8>
      %stop = arith.cmpi eq, %v, %zero : i8
      cf.br ^latch
    ^latch:
      %knext = arith.subi %k, %step : i8
      cf.br ^inner(%knext : i8)
    ^advance:
      %next = arith.addi %i, %one : i8
      cf.br ^outer(%next : i8)
    ^wait:
      simulation.suspend.change %src to ^resume : !simulation.ref<!array>
    ^resume:
      cf.br ^outer(%start : i8)
    }
    // CHECK-LABEL: regions @descending_break valid=1
    simulation.code_unit.decl 3 in 0 always_comb hierarchy "descending_break"
    simulation.func @descending_break(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 3 : i64} {
      %src = simulation.context.storage %ctx[0] : !simulation.ref<!array>
      %dst = simulation.context.storage %ctx[1] : !simulation.ref<!array>
      %control = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %start = arith.constant 0 : i8
      %limit = arith.constant 4 : i8
      %one = arith.constant 1 : i8
      %innerLimit = arith.constant 0 : i8
      %step = arith.constant 1 : i8
      %zero = arith.constant 0 : i8
      cf.br ^outer(%start : i8)
    ^outer(%i: i8):
      %more = arith.cmpi slt, %i, %limit : i8
      cf.cond_br %more, ^enter, ^wait
    ^enter:
      %initial = arith.subi %i, %one : i8
      cf.br ^inner(%initial : i8)
    ^inner(%k: i8):
      %again = arith.cmpi sge, %k, %innerLimit : i8
      cf.cond_br %again, ^body, ^advance
    ^body:
      %s = simulation.ref.array_element %src[%k] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %d = simulation.ref.array_element %dst[%k] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %v = simulation.ref.load %s : !simulation.ref<i8> -> i8
      simulation.ref.store %v to %d : i8, !simulation.ref<i8>
      %stop = arith.cmpi eq, %v, %zero : i8
      cf.cond_br %stop, ^advance, ^latch
    ^latch:
      %knext = arith.subi %k, %step : i8
      simulation.ref.store %knext to %control : i8, !simulation.ref<i8>
      cf.br ^inner(%knext : i8)
    ^advance:
      %next = arith.addi %i, %one : i8
      cf.br ^outer(%next : i8)
    ^wait:
      simulation.suspend.change %src to ^resume : !simulation.ref<!array>
    ^resume:
      cf.br ^outer(%start : i8)
    }
    // CHECK-LABEL: regions @reversed_init valid=1
    // MEM-LABEL: activation @reversed_init valid=1 complete=1
    // MEM-NEXT: live-in s0[0,32)
    // MEM-NEXT: must-write s1[0,32)
    // MEM-NEXT: may-write s1[0,32)
    simulation.code_unit.decl 4 in 0 always_comb hierarchy "reversed_init"
    simulation.func @reversed_init(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 4 : i64} {
      %src = simulation.context.storage %ctx[0] : !simulation.ref<!array>
      %dst = simulation.context.storage %ctx[1] : !simulation.ref<!array>
      %control = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %start = arith.constant 0 : i8
      %limit = arith.constant 4 : i8
      %one = arith.constant 1 : i8
      %innerLimit = arith.constant 4 : i8
      %step = arith.constant 1 : i8
      %zero = arith.constant 0 : i8
      cf.br ^outer(%start : i8)
    ^outer(%i: i8):
      %more = arith.cmpi slt, %i, %limit : i8
      cf.cond_br %more, ^enter, ^wait
    ^enter:
      %initial = arith.subi %i, %one : i8
      cf.br ^inner(%i : i8)
    ^inner(%k: i8):
      %again = arith.cmpi sgt, %innerLimit, %k : i8
      cf.cond_br %again, ^body, ^advance
    ^body:
      %s = simulation.ref.array_element %src[%k] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %d = simulation.ref.array_element %dst[%k] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %v = simulation.ref.load %s : !simulation.ref<i8> -> i8
      simulation.ref.store %v to %d : i8, !simulation.ref<i8>
      %stop = arith.cmpi eq, %v, %zero : i8
      cf.br ^latch
    ^latch:
      %knext = arith.addi %k, %step : i8
      cf.br ^inner(%knext : i8)
    ^advance:
      %next = arith.addi %i, %one : i8
      cf.br ^outer(%next : i8)
    ^wait:
      simulation.suspend.change %src to ^resume : !simulation.ref<!array>
    ^resume:
      cf.br ^outer(%start : i8)
    }
    // CHECK-LABEL: regions @unsigned_init valid=1
    // MEM-LABEL: activation @unsigned_init valid=1 complete=1
    // MEM-NEXT: live-in s0[0,32)
    // MEM-NEXT: must-write s1[0,32)
    // MEM-NEXT: may-write s1[0,32)
    simulation.code_unit.decl 5 in 0 always_comb hierarchy "unsigned_init"
    simulation.func @unsigned_init(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 5 : i64} {
      %src = simulation.context.storage %ctx[0] : !simulation.ref<!array>
      %dst = simulation.context.storage %ctx[1] : !simulation.ref<!array>
      %control = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %start = arith.constant 0 : i8
      %limit = arith.constant 4 : i8
      %one = arith.constant 1 : i8
      %innerLimit = arith.constant 4 : i8
      %step = arith.constant 1 : i8
      %zero = arith.constant 0 : i8
      cf.br ^outer(%start : i8)
    ^outer(%i: i8):
      %more = arith.cmpi slt, %i, %limit : i8
      cf.cond_br %more, ^enter, ^wait
    ^enter:
      %initial = arith.subi %i, %one : i8
      cf.br ^inner(%i : i8)
    ^inner(%k: i8):
      %again = arith.cmpi ult, %k, %innerLimit : i8
      cf.cond_br %again, ^body, ^advance
    ^body:
      %s = simulation.ref.array_element %src[%k] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %d = simulation.ref.array_element %dst[%k] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %v = simulation.ref.load %s : !simulation.ref<i8> -> i8
      simulation.ref.store %v to %d : i8, !simulation.ref<i8>
      %stop = arith.cmpi eq, %v, %zero : i8
      cf.br ^latch
    ^latch:
      %knext = arith.addi %k, %step : i8
      cf.br ^inner(%knext : i8)
    ^advance:
      %next = arith.addi %i, %one : i8
      cf.br ^outer(%next : i8)
    ^wait:
      simulation.suspend.change %src to ^resume : !simulation.ref<!array>
    ^resume:
      cf.br ^outer(%start : i8)
    }
    // CHECK-LABEL: regions @partly_zero_trip valid=1
    // MEM-LABEL: activation @partly_zero_trip valid=1 complete=1
    // MEM-NEXT: live-in s0[0,16)
    // MEM-NEXT: must-write s1[0,16)
    // MEM-NEXT: may-write s1[0,16)
    simulation.code_unit.decl 6 in 0 always_comb hierarchy "partly_zero_trip"
    simulation.func @partly_zero_trip(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 6 : i64} {
      %src = simulation.context.storage %ctx[0] : !simulation.ref<!array>
      %dst = simulation.context.storage %ctx[1] : !simulation.ref<!array>
      %control = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %start = arith.constant 0 : i8
      %limit = arith.constant 4 : i8
      %one = arith.constant 1 : i8
      %innerLimit = arith.constant 2 : i8
      %step = arith.constant 1 : i8
      %zero = arith.constant 0 : i8
      cf.br ^outer(%start : i8)
    ^outer(%i: i8):
      %more = arith.cmpi slt, %i, %limit : i8
      cf.cond_br %more, ^enter, ^wait
    ^enter:
      %initial = arith.subi %i, %one : i8
      cf.br ^inner(%i : i8)
    ^inner(%k: i8):
      %again = arith.cmpi slt, %k, %innerLimit : i8
      cf.cond_br %again, ^body, ^advance
    ^body:
      %s = simulation.ref.array_element %src[%k] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %d = simulation.ref.array_element %dst[%k] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %v = simulation.ref.load %s : !simulation.ref<i8> -> i8
      simulation.ref.store %v to %d : i8, !simulation.ref<i8>
      %stop = arith.cmpi eq, %v, %zero : i8
      cf.br ^latch
    ^latch:
      %knext = arith.addi %k, %step : i8
      cf.br ^inner(%knext : i8)
    ^advance:
      %next = arith.addi %i, %one : i8
      cf.br ^outer(%next : i8)
    ^wait:
      simulation.suspend.change %src to ^resume : !simulation.ref<!array>
    ^resume:
      cf.br ^outer(%start : i8)
    }
    // CHECK-LABEL: regions @all_zero_trip valid=1
    simulation.code_unit.decl 7 in 0 always_comb hierarchy "all_zero_trip"
    simulation.func @all_zero_trip(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 7 : i64} {
      %src = simulation.context.storage %ctx[0] : !simulation.ref<!array>
      %dst = simulation.context.storage %ctx[1] : !simulation.ref<!array>
      %control = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %start = arith.constant 0 : i8
      %limit = arith.constant 4 : i8
      %one = arith.constant 1 : i8
      %innerLimit = arith.constant 0 : i8
      %step = arith.constant 1 : i8
      %zero = arith.constant 0 : i8
      cf.br ^outer(%start : i8)
    ^outer(%i: i8):
      %more = arith.cmpi slt, %i, %limit : i8
      cf.cond_br %more, ^enter, ^wait
    ^enter:
      %initial = arith.subi %i, %one : i8
      cf.br ^inner(%i : i8)
    ^inner(%k: i8):
      %again = arith.cmpi slt, %k, %innerLimit : i8
      cf.cond_br %again, ^body, ^advance
    ^body:
      %s = simulation.ref.array_element %src[%k] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %d = simulation.ref.array_element %dst[%k] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %v = simulation.ref.load %s : !simulation.ref<i8> -> i8
      simulation.ref.store %v to %d : i8, !simulation.ref<i8>
      %stop = arith.cmpi eq, %v, %zero : i8
      cf.br ^latch
    ^latch:
      %knext = arith.addi %k, %step : i8
      cf.br ^inner(%knext : i8)
    ^advance:
      %next = arith.addi %i, %one : i8
      cf.br ^outer(%next : i8)
    ^wait:
      simulation.suspend.change %src to ^resume : !simulation.ref<!array>
    ^resume:
      cf.br ^outer(%start : i8)
    }
    // CHECK-LABEL: regions @stride_residues valid=1
    // MEM-LABEL: activation @stride_residues valid=1 complete=1
    // MEM-NEXT: live-in s0[0,48)
    // MEM-NEXT: must-write s1[0,48)
    // MEM-NEXT: may-write s1[0,48)
    simulation.code_unit.decl 8 in 0 always_comb hierarchy "stride_residues"
    simulation.func @stride_residues(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 8 : i64} {
      %src = simulation.context.storage %ctx[0] : !simulation.ref<!array>
      %dst = simulation.context.storage %ctx[1] : !simulation.ref<!array>
      %control = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %start = arith.constant 0 : i8
      %limit = arith.constant 3 : i8
      %one = arith.constant 1 : i8
      %innerLimit = arith.constant 6 : i8
      %step = arith.constant 2 : i8
      %zero = arith.constant 0 : i8
      cf.br ^outer(%start : i8)
    ^outer(%i: i8):
      %more = arith.cmpi slt, %i, %limit : i8
      cf.cond_br %more, ^enter, ^wait
    ^enter:
      %initial = arith.subi %i, %one : i8
      cf.br ^inner(%i : i8)
    ^inner(%k: i8):
      %again = arith.cmpi slt, %k, %innerLimit : i8
      cf.cond_br %again, ^body, ^advance
    ^body:
      %s = simulation.ref.array_element %src[%k] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %d = simulation.ref.array_element %dst[%k] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %v = simulation.ref.load %s : !simulation.ref<i8> -> i8
      simulation.ref.store %v to %d : i8, !simulation.ref<i8>
      %stop = arith.cmpi eq, %v, %zero : i8
      cf.br ^latch
    ^latch:
      %knext = arith.addi %k, %step : i8
      cf.br ^inner(%knext : i8)
    ^advance:
      %next = arith.addi %i, %one : i8
      cf.br ^outer(%next : i8)
    ^wait:
      simulation.suspend.change %src to ^resume : !simulation.ref<!array>
    ^resume:
      cf.br ^outer(%start : i8)
    }
    // CHECK-LABEL: regions @initializer_wrap valid=0 reason=termination
    simulation.code_unit.decl 9 in 0 always_comb hierarchy "initializer_wrap"
    simulation.func @initializer_wrap(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 9 : i64} {
      %src = simulation.context.storage %ctx[0] : !simulation.ref<!array>
      %dst = simulation.context.storage %ctx[1] : !simulation.ref<!array>
      %control = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %start = arith.constant 0 : i8
      %limit = arith.constant 3 : i8
      %one = arith.constant 1 : i8
      %innerLimit = arith.constant 127 : i8
      %step = arith.constant 2 : i8
      %zero = arith.constant 0 : i8
      cf.br ^outer(%start : i8)
    ^outer(%i: i8):
      %more = arith.cmpi slt, %i, %limit : i8
      cf.cond_br %more, ^enter, ^wait
    ^enter:
      %initial = arith.subi %i, %one : i8
      cf.br ^inner(%i : i8)
    ^inner(%k: i8):
      %again = arith.cmpi slt, %k, %innerLimit : i8
      cf.cond_br %again, ^body, ^advance
    ^body:
      %s = simulation.ref.array_element %src[%k] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %d = simulation.ref.array_element %dst[%k] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %v = simulation.ref.load %s : !simulation.ref<i8> -> i8
      simulation.ref.store %v to %d : i8, !simulation.ref<i8>
      %stop = arith.cmpi eq, %v, %zero : i8
      cf.br ^latch
    ^latch:
      %knext = arith.addi %k, %step : i8
      cf.br ^inner(%knext : i8)
    ^advance:
      %next = arith.addi %i, %one : i8
      cf.br ^outer(%next : i8)
    ^wait:
      simulation.suspend.change %src to ^resume : !simulation.ref<!array>
    ^resume:
      cf.br ^outer(%start : i8)
    }
    // CHECK-LABEL: regions @unsigned_underflow valid=0 reason=termination
    simulation.code_unit.decl 10 in 0 always_comb hierarchy "unsigned_underflow"
    simulation.func @unsigned_underflow(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 10 : i64} {
      %src = simulation.context.storage %ctx[0] : !simulation.ref<!array>
      %dst = simulation.context.storage %ctx[1] : !simulation.ref<!array>
      %control = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %start = arith.constant 0 : i8
      %limit = arith.constant 3 : i8
      %one = arith.constant 1 : i8
      %innerLimit = arith.constant 0 : i8
      %step = arith.constant 1 : i8
      %zero = arith.constant 0 : i8
      cf.br ^outer(%start : i8)
    ^outer(%i: i8):
      %more = arith.cmpi slt, %i, %limit : i8
      cf.cond_br %more, ^enter, ^wait
    ^enter:
      %initial = arith.subi %i, %one : i8
      cf.br ^inner(%i : i8)
    ^inner(%k: i8):
      %again = arith.cmpi uge, %k, %innerLimit : i8
      cf.cond_br %again, ^body, ^advance
    ^body:
      %s = simulation.ref.array_element %src[%k] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %d = simulation.ref.array_element %dst[%k] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %v = simulation.ref.load %s : !simulation.ref<i8> -> i8
      simulation.ref.store %v to %d : i8, !simulation.ref<i8>
      %stop = arith.cmpi eq, %v, %zero : i8
      cf.br ^latch
    ^latch:
      %knext = arith.subi %k, %step : i8
      cf.br ^inner(%knext : i8)
    ^advance:
      %next = arith.addi %i, %one : i8
      cf.br ^outer(%next : i8)
    ^wait:
      simulation.suspend.change %src to ^resume : !simulation.ref<!array>
    ^resume:
      cf.br ^outer(%start : i8)
    }
    // CHECK-LABEL: regions @loaded_initializer valid=0 reason=termination
    simulation.code_unit.decl 11 in 0 always_comb hierarchy "loaded_initializer"
    simulation.func @loaded_initializer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 11 : i64} {
      %src = simulation.context.storage %ctx[0] : !simulation.ref<!array>
      %dst = simulation.context.storage %ctx[1] : !simulation.ref<!array>
      %control = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %start = arith.constant 0 : i8
      %limit = arith.constant 4 : i8
      %one = arith.constant 1 : i8
      %innerLimit = arith.constant 4 : i8
      %step = arith.constant 1 : i8
      %zero = arith.constant 0 : i8
      cf.br ^outer(%start : i8)
    ^outer(%i: i8):
      %more = arith.cmpi slt, %i, %limit : i8
      cf.cond_br %more, ^enter, ^wait
    ^enter:
      %initial = arith.subi %i, %one : i8
      %loaded = simulation.ref.load %control : !simulation.ref<i8> -> i8
      cf.br ^inner(%loaded : i8)
    ^inner(%k: i8):
      %again = arith.cmpi slt, %k, %innerLimit : i8
      cf.cond_br %again, ^body, ^advance
    ^body:
      %s = simulation.ref.array_element %src[%k] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %d = simulation.ref.array_element %dst[%k] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %v = simulation.ref.load %s : !simulation.ref<i8> -> i8
      simulation.ref.store %v to %d : i8, !simulation.ref<i8>
      %stop = arith.cmpi eq, %v, %zero : i8
      cf.br ^latch
    ^latch:
      %knext = arith.addi %k, %step : i8
      cf.br ^inner(%knext : i8)
    ^advance:
      %next = arith.addi %i, %one : i8
      cf.br ^outer(%next : i8)
    ^wait:
      simulation.suspend.change %src to ^resume : !simulation.ref<!array>
    ^resume:
      cf.br ^outer(%start : i8)
    }
    // CHECK-LABEL: regions @zero_stride_init valid=0 reason=termination
    simulation.code_unit.decl 12 in 0 always_comb hierarchy "zero_stride_init"
    simulation.func @zero_stride_init(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 12 : i64} {
      %src = simulation.context.storage %ctx[0] : !simulation.ref<!array>
      %dst = simulation.context.storage %ctx[1] : !simulation.ref<!array>
      %control = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %start = arith.constant 0 : i8
      %limit = arith.constant 4 : i8
      %one = arith.constant 1 : i8
      %innerLimit = arith.constant 4 : i8
      %step = arith.constant 0 : i8
      %zero = arith.constant 0 : i8
      cf.br ^outer(%start : i8)
    ^outer(%i: i8):
      %more = arith.cmpi slt, %i, %limit : i8
      cf.cond_br %more, ^enter, ^wait
    ^enter:
      %initial = arith.subi %i, %one : i8
      cf.br ^inner(%i : i8)
    ^inner(%k: i8):
      %again = arith.cmpi slt, %k, %innerLimit : i8
      cf.cond_br %again, ^body, ^advance
    ^body:
      %s = simulation.ref.array_element %src[%k] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %d = simulation.ref.array_element %dst[%k] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %v = simulation.ref.load %s : !simulation.ref<i8> -> i8
      simulation.ref.store %v to %d : i8, !simulation.ref<i8>
      %stop = arith.cmpi eq, %v, %zero : i8
      cf.br ^latch
    ^latch:
      %knext = arith.addi %k, %step : i8
      cf.br ^inner(%knext : i8)
    ^advance:
      %next = arith.addi %i, %one : i8
      cf.br ^outer(%next : i8)
    ^wait:
      simulation.suspend.change %src to ^resume : !simulation.ref<!array>
    ^resume:
      cf.br ^outer(%start : i8)
    }
    // CHECK-LABEL: regions @varying_equality valid=0 reason=termination
    simulation.code_unit.decl 13 in 0 always_comb hierarchy "varying_equality"
    simulation.func @varying_equality(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 13 : i64} {
      %src = simulation.context.storage %ctx[0] : !simulation.ref<!array>
      %dst = simulation.context.storage %ctx[1] : !simulation.ref<!array>
      %control = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %start = arith.constant 0 : i8
      %limit = arith.constant 4 : i8
      %one = arith.constant 1 : i8
      %innerLimit = arith.constant 4 : i8
      %step = arith.constant 2 : i8
      %zero = arith.constant 0 : i8
      cf.br ^outer(%start : i8)
    ^outer(%i: i8):
      %more = arith.cmpi slt, %i, %limit : i8
      cf.cond_br %more, ^enter, ^wait
    ^enter:
      %initial = arith.subi %i, %one : i8
      cf.br ^inner(%i : i8)
    ^inner(%k: i8):
      %again = arith.cmpi ne, %k, %innerLimit : i8
      cf.cond_br %again, ^body, ^advance
    ^body:
      %s = simulation.ref.array_element %src[%k] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %d = simulation.ref.array_element %dst[%k] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %v = simulation.ref.load %s : !simulation.ref<i8> -> i8
      simulation.ref.store %v to %d : i8, !simulation.ref<i8>
      %stop = arith.cmpi eq, %v, %zero : i8
      cf.br ^latch
    ^latch:
      %knext = arith.addi %k, %step : i8
      cf.br ^inner(%knext : i8)
    ^advance:
      %next = arith.addi %i, %one : i8
      cf.br ^outer(%next : i8)
    ^wait:
      simulation.suspend.change %src to ^resume : !simulation.ref<!array>
    ^resume:
      cf.br ^outer(%start : i8)
    }
  }
}
