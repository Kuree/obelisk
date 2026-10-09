// RUN: obelisk-opt %s --test-obelisk-activation-regions -o %t.analyzed 2>&1 | FileCheck %s
// RUN: obelisk-opt %s -o %t.original
// RUN: diff %t.original %t.analyzed

!pair = !simulation.unpacked_array<0 : 1 x i8>
module {
  simulation.design @private_state {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.storage.decl 2 in 0 : i8 design
    simulation.storage.decl 3 in 0 : i1 design
    simulation.storage.decl 4 in 0 : !pair design
    simulation.code_unit.decl 1 in 0 function hierarchy "twice"
    simulation.code_unit.decl 2 in 0 function hierarchy "peek"
    simulation.code_unit.decl 3 in 0 always_comb hierarchy "pure_call"
    simulation.code_unit.decl 4 in 0 always_comb hierarchy "captured_read"
    simulation.code_unit.decl 5 in 0 always_comb hierarchy "local_slot"
    simulation.code_unit.decl 6 in 0 always_comb hierarchy "merged_reference"
    simulation.code_unit.decl 7 in 0 always_comb hierarchy "flushed_slot"

    // A function of its arguments only, with a private slot of its own.
    simulation.func @twice(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %x: i8 {simulation.capture_kind = 2 : i32}) -> i8 attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %slot = simulation.ref.alloc %x : i8 -> !simulation.ref<i8>
      %v = simulation.ref.load %slot : !simulation.ref<i8> -> i8
      %y = arith.addi %v, %v : i8
      simulation.return %y : i8
    }
    // A read-only capture contributes a design read at the call site.
    simulation.func @peek(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i8 attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %ref = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %v = simulation.ref.load %ref : !simulation.ref<i8> -> i8
      simulation.return %v : i8
    }

    // A call of a pure function is a computation of its operands.
    // CHECK-LABEL: regions @pure_call valid=1
    // CHECK-NEXT: node simulation.ref.load read:s0[0,8)
    // CHECK-NEXT: node simulation.ref.store write:s1[0,8)
    simulation.func @pure_call(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %in: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %out: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 3 : i64} {
      cf.br ^body
    ^body:
      %x = simulation.ref.load %in : !simulation.ref<i8> -> i8
      %y = simulation.call @twice(%ctx, %x) : (!simulation.context, i8) -> i8
      simulation.ref.store %y to %out : i8, !simulation.ref<i8>
      simulation.suspend.change %in to ^body : !simulation.ref<i8>
    }

    // CHECK-LABEL: regions @captured_read valid=1
    // CHECK-NEXT: node simulation.call read:s0[0,8)
    // CHECK-NEXT: node simulation.ref.store write:s1[0,8)
    simulation.func @captured_read(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %in: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %out: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 4 : i64} {
      cf.br ^body
    ^body:
      %y = simulation.call @peek(%ctx) : (!simulation.context) -> i8
      simulation.ref.store %y to %out : i8, !simulation.ref<i8>
      simulation.suspend.change %in to ^body : !simulation.ref<i8>
    }

    // A slot allocated by the activation is private state: its accesses are
    // neither design reads nor writes.
    // CHECK-LABEL: regions @local_slot valid=1
    // CHECK-NEXT: node simulation.ref.load read:s0[0,8)
    // CHECK-NEXT: node simulation.ref.store write:s1[0,8)
    // CHECK-NOT: node
    simulation.func @local_slot(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %in: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %out: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 5 : i64} {
      cf.br ^body
    ^body:
      %zero = arith.constant 0 : i8
      %slot = simulation.ref.alloc %zero : i8 -> !simulation.ref<i8>
      %x = simulation.ref.load %in : !simulation.ref<i8> -> i8
      simulation.ref.store %x to %slot : i8, !simulation.ref<i8>
      %y = simulation.ref.load %slot : !simulation.ref<i8> -> i8
      simulation.ref.store %y to %out : i8, !simulation.ref<i8>
      simulation.suspend.change %in to ^body : !simulation.ref<i8>
    }

    // A reference merged at a join, which canonicalization turns into a
    // select, may access either root.
    // CHECK-LABEL: regions @merged_reference valid=1
    // CHECK-NEXT: node simulation.ref.load read:s3[0,1)
    // CHECK-NEXT: node simulation.ref.load read:s0[0,8) read:s2[0,8)
    // CHECK-NEXT: node simulation.ref.store write:s1[0,8)
    simulation.func @merged_reference(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %a: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %b: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %sel: !simulation.ref<i1> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64}, %out: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 6 : i64} {
      cf.br ^body
    ^body:
      %s = simulation.ref.load %sel : !simulation.ref<i1> -> i1
      cf.cond_br %s, ^pick(%a : !simulation.ref<i8>), ^pick(%b : !simulation.ref<i8>)
    ^pick(%ref: !simulation.ref<i8>):
      %v = simulation.ref.load %ref : !simulation.ref<i8> -> i8
      simulation.ref.store %v to %out : i8, !simulation.ref<i8>
      simulation.suspend.any %a, %b, %sel edges [0, 0, 0] to ^body : !simulation.ref<i8>, !simulation.ref<i8>, !simulation.ref<i1>
    }

    // A process-lifetime copy of an exclusive root, flushed at the wait and
    // refreshed after it, equals a fresh copy of the root in every activation.
    // CHECK-LABEL: regions @flushed_slot valid=1
    // CHECK-NEXT: node simulation.ref.load read:s0[0,8)
    // CHECK-NEXT: node simulation.ref.load read:s4[0,16)
    // CHECK-NEXT: node simulation.ref.store write:s1[0,8)
    // CHECK-NEXT: node simulation.ref.store write:s4[0,16)
    simulation.func @flushed_slot(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %in: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %root: !simulation.ref<!pair> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}, %out: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 7 : i64} {
      %initial = simulation.ref.load %root : !simulation.ref<!pair> -> !pair
      %slot = simulation.ref.alloc %initial {simulation.flushed_private_slot} : !pair -> !simulation.ref<!pair>
      cf.br ^body(%slot : !simulation.ref<!pair>)
    ^body(%local: !simulation.ref<!pair>):
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      %x = simulation.ref.load %in : !simulation.ref<i8> -> i8
      %e0 = simulation.ref.array_element %local[%zero] : (!simulation.ref<!pair>, i32) -> !simulation.ref<i8>
      simulation.ref.store %x to %e0 : i8, !simulation.ref<i8>
      %e1 = simulation.ref.array_element %local[%one] : (!simulation.ref<!pair>, i32) -> !simulation.ref<i8>
      simulation.ref.store %x to %e1 : i8, !simulation.ref<i8>
      %y = simulation.ref.load %e1 : !simulation.ref<i8> -> i8
      simulation.ref.store %y to %out : i8, !simulation.ref<i8>
      %flush = simulation.ref.load %local : !simulation.ref<!pair> -> !pair
      simulation.ref.store %flush to %root : !pair, !simulation.ref<!pair>
      simulation.suspend.change %in to ^resume(%local : !simulation.ref<!pair>) : !simulation.ref<i8>
    ^resume(%carried: !simulation.ref<!pair>):
      %refresh = simulation.ref.load %root : !simulation.ref<!pair> -> !pair
      simulation.ref.store %refresh to %carried : !pair, !simulation.ref<!pair>
      cf.br ^body(%carried : !simulation.ref<!pair>)
    }
    simulation.code_unit.decl 8 in 0 function hierarchy "read_formal"
    simulation.func @read_formal(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %ref: !simulation.ref<i8> {simulation.capture_kind = 1 : i32}) -> i8 attributes {entry_kind = 8 : i32, code_unit_id = 8 : i64} {
      %v = simulation.ref.load %ref : !simulation.ref<i8> -> i8
      simulation.return %v : i8
    }
    simulation.code_unit.decl 9 in 0 function hierarchy "write_formal"
    simulation.func @write_formal(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %ref: !simulation.ref<i8> {simulation.capture_kind = 1 : i32}) -> i8 attributes {entry_kind = 8 : i32, code_unit_id = 9 : i64} {
      %v = arith.constant 1 : i8
      simulation.ref.store %v to %ref : i8, !simulation.ref<i8>
      simulation.return %v : i8
    }
    simulation.code_unit.decl 10 in 0 function hierarchy "unbounded"
    simulation.func @unbounded(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %ref: !simulation.ref<i8> {simulation.capture_kind = 1 : i32}) -> i8 attributes {entry_kind = 8 : i32, code_unit_id = 10 : i64} {
      cf.br ^loop
    ^loop:
      cf.br ^loop
    }
    // CHECK-LABEL: regions @formal_read valid=1
    // CHECK-NEXT: node simulation.call read:s0[0,8)
    // CHECK-NEXT: node simulation.ref.store write:s1[0,8)
    simulation.code_unit.decl 11 in 0 always_comb hierarchy "formal_read"
    simulation.func @formal_read(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %in: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %out: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 11 : i64} {
      cf.br ^body
    ^body:
      %v = simulation.call @read_formal(%ctx, %in) : (!simulation.context, !simulation.ref<i8>) -> i8
      simulation.ref.store %v to %out : i8, !simulation.ref<i8>
      simulation.suspend.change %in to ^body : !simulation.ref<i8>
    }
    // CHECK-LABEL: regions @call_store_rejected valid=0 reason=memory effects
    simulation.code_unit.decl 12 in 0 always_comb hierarchy "call_store_rejected"
    simulation.func @call_store_rejected(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %in: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %out: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 12 : i64} {
      cf.br ^body
    ^body:
      %v = simulation.call @write_formal(%ctx, %in) : (!simulation.context, !simulation.ref<i8>) -> i8
      simulation.ref.store %v to %out : i8, !simulation.ref<i8>
      simulation.suspend.change %in to ^body : !simulation.ref<i8>
    }
    // CHECK-LABEL: regions @nonterminating_call valid=0 reason=memory effects
    simulation.code_unit.decl 13 in 0 always_comb hierarchy "nonterminating_call"
    simulation.func @nonterminating_call(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %in: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %out: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 13 : i64} {
      cf.br ^body
    ^body:
      %v = simulation.call @unbounded(%ctx, %in) : (!simulation.context, !simulation.ref<i8>) -> i8
      simulation.ref.store %v to %out : i8, !simulation.ref<i8>
      simulation.suspend.change %in to ^body : !simulation.ref<i8>
    }
    // Default initializers interleaved with allocations have the same meaning
    // when recreating several scratch slots at a suspension boundary.
    // CHECK-LABEL: regions @default_slots valid=1
    simulation.code_unit.decl 14 in 0 always_comb hierarchy "default_slots"
    simulation.func @default_slots(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 14 : i64} {
      %in = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %out = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %a = simulation.aggregate.default : !pair
      %x = simulation.ref.alloc %a {simulation.flushed_private_slot, simulation.activation_default_slot} : !pair -> !simulation.ref<!pair>
      %b = simulation.aggregate.default : !simulation.unpacked_array<0 : 2 x i8>
      %y = simulation.ref.alloc %b {simulation.flushed_private_slot, simulation.activation_default_slot} : !simulation.unpacked_array<0 : 2 x i8> -> !simulation.ref<!simulation.unpacked_array<0 : 2 x i8>>
      cf.br ^body
    ^body:
      %zero = arith.constant 0 : i32
      %xr = simulation.ref.array_element %x[%zero] : (!simulation.ref<!pair>, i32) -> !simulation.ref<i8>
      %yr = simulation.ref.array_element %y[%zero] : (!simulation.ref<!simulation.unpacked_array<0 : 2 x i8>>, i32) -> !simulation.ref<i8>
      %v = simulation.ref.load %in : !simulation.ref<i8> -> i8
      simulation.ref.store %v to %xr : i8, !simulation.ref<i8>
      simulation.ref.store %v to %yr : i8, !simulation.ref<i8>
      %xv = simulation.ref.load %xr : !simulation.ref<i8> -> i8
      %yv = simulation.ref.load %yr : !simulation.ref<i8> -> i8
      %sum = arith.addi %xv, %yv : i8
      simulation.ref.store %sum to %out : i8, !simulation.ref<i8>
      simulation.suspend.change %in to ^resume : !simulation.ref<i8>
    ^resume:
      simulation.ref.store %a to %x : !pair, !simulation.ref<!pair>
      simulation.ref.store %b to %y : !simulation.unpacked_array<0 : 2 x i8>, !simulation.ref<!simulation.unpacked_array<0 : 2 x i8>>
      cf.br ^body
    }
  }
}
