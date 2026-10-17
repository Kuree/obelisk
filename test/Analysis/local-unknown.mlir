// RUN: obelisk-opt %s --split-input-file --test-obelisk-local-unknown -o /dev/null 2>&1 | FileCheck %s
// RUN: obelisk-opt %s --split-input-file --test-obelisk-local-unknown=checks=true -o /dev/null 2>&1 | FileCheck %s --check-prefix=CHECKS

// A read computed on is gated: the entry check covers its bits. A read that
// only moves into a store keeps its unknown plane, and so does a dynamically
// indexed memory element (IEEE 1800-2023 7.4.6) and function-private storage.
// The check covers only the gated bits of a root.

!c = !simulation.context
!l8 = !simulation.logic<8>
!l16 = !simulation.logic<16>
!r8 = !simulation.ref<!l8>
!r16 = !simulation.ref<!l16>
!memory = !simulation.unpacked_array<0 : 7 x !l8>
module {
  simulation.design @classes {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !l8 design
    simulation.storage.decl 1 in 0 : !l8 design
    simulation.storage.decl 2 in 0 : !l8 design
    simulation.storage.decl 3 in 0 : !l8 design
    simulation.storage.decl 4 in 0 : !memory design
    simulation.storage.decl 5 in 0 : !l16 design
    simulation.code_unit.decl 1 in 0 function hierarchy "top.f"
    simulation.func @classes(%ctx: !c {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %a = simulation.context.storage %ctx[0] : !r8
      %b = simulation.context.storage %ctx[1] : !r8
      %m = simulation.context.storage %ctx[2] : !r8
      %d = simulation.context.storage %ctx[3] : !r8
      %mem = simulation.context.storage %ctx[4] : !simulation.ref<!memory>
      %wide = simulation.context.storage %ctx[5] : !r16
      %x = simulation.ref.load %a {test.read = "computed"} : !r8 -> !l8
      %sum = simulation.logic.binary add %x, %x : !l8
      simulation.ref.store %sum to %b : !l8, !r8
      %y = simulation.ref.load %m {test.read = "moved"} : !r8 -> !l8
      simulation.ref.store %y to %d : !l8, !r8
      %slot = simulation.ref.array_element %mem[%x] : (!simulation.ref<!memory>, !l8) -> !r8
      %e = simulation.ref.load %slot {test.read = "element"} : !r8 -> !l8
      %t = simulation.logic.binary xor %e, %sum : !l8
      simulation.ref.store %t to %d : !l8, !r8
      %high = simulation.ref.extract %wide from 8 : !r16 -> !r8
      %h = simulation.ref.load %high {test.read = "slice"} : !r8 -> !l8
      %p = simulation.ref.alloc %h : !l8 -> !r8
      %q = simulation.ref.load %p {test.read = "private"} : !r8 -> !l8
      %u = simulation.logic.binary or %q, %h : !l8
      simulation.ref.store %u to %b : !l8, !r8
      simulation.return
    }
  }
}

// CHECK-LABEL: local-unknown @classes benefit={{[0-9]+}} gate-words=2
// CHECK-NEXT: gate 0: [0,8)
// CHECK-NEXT: gate 5: [8,16)
// CHECK-NEXT: read computed: gated
// CHECK-NEXT: read moved: moved
// CHECK-NEXT: read element: dynamic
// CHECK-NEXT: read slice: gated
// CHECK-NEXT: read private: local

// -----

// Writes are attributed per path. A read before the activation stores X/Z
// into its root sees entry state; a read the X/Z store may precede does not.
// A store of a value known under the check (here derived from gated reads)
// leaves later reads gated, and their bits need no check: every path stored
// them. The division may yield X for a zero divisor
// (IEEE 1800-2023 11.4.2), so its store counts as an X/Z write.

!c = !simulation.context
!l8 = !simulation.logic<8>
!r8 = !simulation.ref<!l8>
module {
  simulation.design @paths {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !l8 design
    simulation.storage.decl 1 in 0 : !l8 design
    simulation.storage.decl 2 in 0 : !l8 design
    simulation.storage.decl 3 in 0 : !l8 design
    simulation.code_unit.decl 1 in 0 function hierarchy "top.f"
    simulation.func @paths(%ctx: !c {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %q = simulation.context.storage %ctx[0] : !r8
      %src = simulation.context.storage %ctx[1] : !r8
      %k = simulation.context.storage %ctx[2] : !r8
      %out = simulation.context.storage %ctx[3] : !r8
      %before = simulation.ref.load %q {test.read = "before"} : !r8 -> !l8
      %v = simulation.ref.load %src {test.read = "source"} : !r8 -> !l8
      %c = simulation.logic.is_true %before : !l8
      cf.cond_br %c, ^write, ^join
    ^write:
      %w = simulation.ref.load %src {test.read = "moved"} : !r8 -> !l8
      simulation.ref.store %w to %q : !l8, !r8
      %ratio = simulation.logic.binary udiv %before, %v : !l8
      simulation.ref.store %ratio to %out : !l8, !r8
      cf.br ^join
    ^join:
      %after = simulation.ref.load %q {test.read = "after"} : !r8 -> !l8
      %sum = simulation.logic.binary add %before, %v : !l8
      simulation.ref.store %sum to %k : !l8, !r8
      %known = simulation.ref.load %k {test.read = "known"} : !r8 -> !l8
      %o = simulation.ref.load %out {test.read = "quotient"} : !r8 -> !l8
      %s = simulation.logic.binary add %after, %known : !l8
      %s2 = simulation.logic.binary add %s, %o : !l8
      simulation.ref.store %s2 to %k : !l8, !r8
      simulation.return
    }
  }
}

// CHECK-LABEL: local-unknown @paths
// CHECK-NEXT: gate 0: [0,8)
// CHECK-NEXT: gate 1: [0,8)
// CHECK-NEXT: read before: gated
// CHECK-NEXT: read source: gated
// CHECK-NEXT: read moved: moved
// CHECK-NEXT: read after: written
// CHECK-NEXT: read known: gated
// CHECK-NEXT: read quotient: written

// -----

// Callees are summarized first. A pure callee returns known results for
// known arguments, so storing one leaves later reads gated; after a call to a
// callee that writes design storage every read may observe X/Z it stored.

!c = !simulation.context
!l8 = !simulation.logic<8>
!r8 = !simulation.ref<!l8>
module {
  simulation.design @calls {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !l8 design
    simulation.storage.decl 1 in 0 : !l8 design
    simulation.storage.decl 2 in 0 : !l8 design
    simulation.code_unit.decl 1 in 0 function hierarchy "top.f"
    simulation.code_unit.decl 2 in 0 function hierarchy "top.square"
    simulation.code_unit.decl 3 in 0 function hierarchy "top.writer"
    simulation.func @square(%ctx: !c {simulation.capture_kind = 0 : i32},
                            %v: !l8 {simulation.capture_kind = 1 : i32}) -> !l8
        attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %p = simulation.logic.binary mul %v, %v : !l8
      simulation.return %p : !l8
    }
    simulation.func @writer(%ctx: !c {simulation.capture_kind = 0 : i32},
                            %v: !l8 {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %b = simulation.context.storage %ctx[1] : !r8
      simulation.ref.store %v to %b : !l8, !r8
      simulation.return
    }
    simulation.func @calls(%ctx: !c {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %a = simulation.context.storage %ctx[0] : !r8
      %b = simulation.context.storage %ctx[1] : !r8
      %t = simulation.context.storage %ctx[2] : !r8
      %x = simulation.ref.load %a {test.read = "first"} : !r8 -> !l8
      %y = simulation.call @square(%ctx, %x) : (!c, !l8) -> !l8
      simulation.ref.store %y to %t : !l8, !r8
      %s = simulation.ref.load %t {test.read = "stored"} : !r8 -> !l8
      %w = simulation.logic.binary add %s, %x : !l8
      simulation.call @writer(%ctx, %w) : (!c, !l8) -> ()
      %z = simulation.ref.load %a {test.read = "after"} : !r8 -> !l8
      %u = simulation.logic.binary add %z, %z : !l8
      simulation.ref.store %u to %b : !l8, !r8
      simulation.return
    }
  }
}

// CHECK-LABEL: local-unknown @calls
// CHECK-NEXT: gate 0: [0,8)
// CHECK-NEXT: read first: gated
// CHECK-NEXT: read stored: gated
// CHECK-NEXT: read after: written

// -----

// A read the check cannot cover is tested where it loads when enough
// unknown-plane work depends on it. The copy treats a checked read as known,
// so a store derived from it leaves later reads of its root gated; a read
// with little dependent work stays an island.

!c = !simulation.context
!l8 = !simulation.logic<8>
!r8 = !simulation.ref<!l8>
!memory = !simulation.unpacked_array<0 : 7 x !l8>
module {
  simulation.design @checked {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !l8 design
    simulation.storage.decl 1 in 0 : !l8 design
    simulation.storage.decl 2 in 0 : !l8 design
    simulation.storage.decl 3 in 0 : !memory design
    simulation.code_unit.decl 1 in 0 function hierarchy "top.f"
    simulation.func @checked(%ctx: !c {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %a = simulation.context.storage %ctx[0] : !r8
      %b = simulation.context.storage %ctx[1] : !r8
      %d = simulation.context.storage %ctx[2] : !r8
      %mem = simulation.context.storage %ctx[3] : !simulation.ref<!memory>
      %x = simulation.ref.load %a {test.read = "index"} : !r8 -> !l8
      %slot = simulation.ref.array_element %mem[%x] : (!simulation.ref<!memory>, !l8) -> !r8
      %e = simulation.ref.load %slot {test.read = "element"} : !r8 -> !l8
      %s1 = simulation.logic.binary add %e, %x : !l8
      %s2 = simulation.logic.binary mul %s1, %s1 : !l8
      %s3 = simulation.logic.binary xor %s2, %e : !l8
      %s4 = simulation.logic.binary sub %s3, %x : !l8
      simulation.ref.store %s4 to %b : !l8, !r8
      %y = simulation.ref.load %b {test.read = "stored"} : !r8 -> !l8
      %f = simulation.ref.load %slot {test.read = "small"} : !r8 -> !l8
      %u = simulation.logic.binary add %y, %f : !l8
      simulation.ref.store %u to %d : !l8, !r8
      simulation.return
    }
  }
}

// CHECK-LABEL: local-unknown @checked
// CHECK: read index: gated
// CHECK-NEXT: read element: dynamic
// CHECK-NEXT: read stored: written
// CHECK-NEXT: read small: dynamic

// CHECKS-LABEL: local-unknown @checked
// CHECKS-NEXT: gate 0: [0,8)
// CHECKS-NEXT: read index: gated
// CHECKS-NEXT: read element: checked
// CHECKS-NEXT: read stored: gated
// CHECKS-NEXT: read small: dynamic

// -----

// A dynamic packed selection reads X whenever it is invalid, whatever its
// root holds (IEEE 1800-2023 7.4.6, 11.5.1). It is gated over its hull only
// when it is valid for every known value of its index and the copy proves
// that index known; otherwise it stays an island.

!c = !simulation.context
!l3 = !simulation.logic<3>
!l4 = !simulation.logic<4>
!l8 = !simulation.logic<8>
!l16 = !simulation.logic<16>
!r3 = !simulation.ref<!l3>
!r8 = !simulation.ref<!l8>
!r16 = !simulation.ref<!l16>
!memory = !simulation.unpacked_array<0 : 7 x !l3>
module {
  simulation.design @selections {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !l16 design
    simulation.storage.decl 1 in 0 : !l16 design
    simulation.storage.decl 2 in 0 : !l16 design
    simulation.storage.decl 3 in 0 : !l3 design
    simulation.storage.decl 4 in 0 : !l8 design
    simulation.storage.decl 5 in 0 : !memory design
    simulation.storage.decl 6 in 0 : !l8 design
    simulation.code_unit.decl 1 in 0 function hierarchy "top.f"
    simulation.func @selections(%ctx: !c {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %valid = simulation.context.storage %ctx[0] : !r16
      %invalid = simulation.context.storage %ctx[1] : !r16
      %island = simulation.context.storage %ctx[2] : !r16
      %i = simulation.context.storage %ctx[3] : !r3
      %j = simulation.context.storage %ctx[4] : !r8
      %mem = simulation.context.storage %ctx[5] : !simulation.ref<!memory>
      %out = simulation.context.storage %ctx[6] : !r8
      // valid[index +: 8] with index in [0, 7].
      %short = simulation.ref.load %i {test.read = "index"} : !r3 -> !l3
      %index = simulation.logic.resize %short signed = false : !l3 -> !l4
      %a = simulation.ref.dyn_extract %valid from %index : (!r16, !l4) -> !r8
      %x = simulation.ref.load %a {test.read = "valid"} : !r8 -> !l8
      // invalid[wide +: 8] may select bits past the root.
      %wide = simulation.ref.load %j {test.read = "wide-index"} : !r8 -> !l8
      %b = simulation.ref.dyn_extract %invalid from %wide : (!r16, !l8) -> !r8
      %y = simulation.ref.load %b {test.read = "invalid"} : !r8 -> !l8
      // island[mem[index] +: 8]: the index is a memory element.
      %slot = simulation.ref.array_element %mem[%short] : (!simulation.ref<!memory>, !l3) -> !r3
      %e = simulation.ref.load %slot {test.read = "element"} : !r3 -> !l3
      %far = simulation.logic.resize %e signed = false : !l3 -> !l4
      %c = simulation.ref.dyn_extract %island from %far : (!r16, !l4) -> !r8
      %z = simulation.ref.load %c {test.read = "island-index"} : !r8 -> !l8
      %s = simulation.logic.binary add %x, %y : !l8
      %t = simulation.logic.binary mul %s, %z : !l8
      simulation.ref.store %t to %out : !l8, !r8
      simulation.return
    }
  }
}

// CHECK-LABEL: local-unknown @selections
// CHECK-NEXT: gate 0: [0,16)
// CHECK-NEXT: gate 3: [0,3)
// CHECK-NEXT: gate 4: [0,8)
// CHECK-NEXT: read index: gated
// CHECK-NEXT: read valid: gated
// CHECK-NEXT: read wide-index: gated
// CHECK-NEXT: read invalid: dynamic
// CHECK-NEXT: read element: dynamic
// CHECK-NEXT: read island-index: dynamic
