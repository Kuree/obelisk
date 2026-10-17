// RUN: obelisk-opt %s --split-input-file --pass-pipeline='builtin.module(schedule-version-known-activations{minimum-benefit=1 checked-reads=true})' | FileCheck %s
// RUN: obelisk-opt %s --split-input-file --pass-pipeline='builtin.module(schedule-version-known-activations{minimum-benefit=1 segment-size=10})' | FileCheck %s --check-prefix=SEGMENTS

// One check of exactly the gated bits selects a copy of the activation. The
// copy reads the gated location's value plane only; the moved read and the
// memory element keep both planes, and every store is unchanged. The original
// blocks keep their positions, and the entry's operations move behind the
// check.

!c = !simulation.context
!l8 = !simulation.logic<8>
!l16 = !simulation.logic<16>
!r8 = !simulation.ref<!l8>
!r16 = !simulation.ref<!l16>
!memory = !simulation.unpacked_array<0 : 7 x !l8>
module attributes {native.optimization_level = 3 : i32} {
  simulation.design @activation {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !l16 design
    simulation.storage.decl 1 in 0 : !l8 design
    simulation.storage.decl 2 in 0 : !l8 design
    simulation.storage.decl 3 in 0 : !memory design
    simulation.code_unit.decl 1 in 0 function hierarchy "top.f"
    // CHECK-LABEL: simulation.func @activation
    // CHECK-SAME: schedule.native.known_activation
    // CHECK-SAME: schedule.native.known_versioned
    // CHECK-NEXT: %[[ROOT:.*]] = simulation.context.storage %arg0[0]
    // CHECK-NEXT: %[[OK:.*]] = schedule.known.bits %[[ROOT]] [array<i64: 8, 8>] :
    // CHECK-NEXT: cf.cond_br %[[OK]] weights([1048575, 1]), ^[[COPY:bb[0-9]+]], ^[[BODY:bb[0-9]+]]
    // CHECK: ^bb1:
    // CHECK-NEXT: simulation.logic.binary mul
    // CHECK: simulation.return
    // CHECK: ^[[BODY]]:
    // CHECK: %[[X:.*]] = simulation.ref.load %{{.*}} : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
    // CHECK-NEXT: simulation.logic.is_true %[[X]]
    // CHECK: ^[[COPY]]:
    // CHECK-NOT: known_read
    // CHECK: %[[K:.*]] = simulation.ref.load %{{.*}} {schedule.native.known_read}
    // CHECK-NEXT: simulation.logic.is_true %[[K]]
    // CHECK-NOT: known_read
    // CHECK: }
    simulation.func @activation(%ctx: !c {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %wide = simulation.context.storage %ctx[0] : !r16
      %m = simulation.context.storage %ctx[1] : !r8
      %d = simulation.context.storage %ctx[2] : !r8
      %mem = simulation.context.storage %ctx[3] : !simulation.ref<!memory>
      %high = simulation.ref.extract %wide from 8 : !r16 -> !r8
      %x = simulation.ref.load %high : !r8 -> !l8
      %c = simulation.logic.is_true %x : !l8
      cf.cond_br %c, ^taken, ^join
    ^taken:
      %p = simulation.logic.binary mul %x, %x : !l8
      %slot = simulation.ref.array_element %mem[%x] : (!simulation.ref<!memory>, !l8) -> !r8
      %e = simulation.ref.load %slot : !r8 -> !l8
      %s = simulation.logic.binary add %p, %e : !l8
      simulation.ref.store %s to %d : !l8, !r8
      cf.br ^join
    ^join:
      %y = simulation.ref.load %m : !r8 -> !l8
      simulation.ref.store %y to %d : !l8, !r8
      simulation.return
    }
  }
}

// -----

// An activation whose reads are all islands has nothing to check.

!c = !simulation.context
!l8 = !simulation.logic<8>
!r8 = !simulation.ref<!l8>
module attributes {native.optimization_level = 3 : i32} {
  simulation.design @islands {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !l8 design
    simulation.storage.decl 1 in 0 : !l8 design
    simulation.code_unit.decl 1 in 0 function hierarchy "top.f"
    // CHECK-LABEL: simulation.func @islands
    // CHECK-NOT: schedule.native.known_versioned
    // CHECK-NOT: schedule.known.bits
    // CHECK: simulation.return
    simulation.func @islands(%ctx: !c {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %a = simulation.context.storage %ctx[0] : !r8
      %b = simulation.context.storage %ctx[1] : !r8
      %y = simulation.ref.load %a : !r8 -> !l8
      simulation.ref.store %y to %b : !l8, !r8
      simulation.return
    }
  }
}

// -----

// The copy tests the memory element where it loads and, on X/Z, continues
// in the original right after the same read, carrying the values the
// original defined before it as block arguments. The private allocation both
// copies use moves to the entry, keeping a store of its initializer, and the
// output reference is recomputed after the merge.

!c = !simulation.context
!l8 = !simulation.logic<8>
!r8 = !simulation.ref<!l8>
!memory = !simulation.unpacked_array<0 : 7 x !l8>
module attributes {native.optimization_level = 3 : i32} {
  simulation.design @transfer {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !l8 design
    simulation.storage.decl 1 in 0 : !l8 design
    simulation.storage.decl 2 in 0 : !memory design
    simulation.code_unit.decl 1 in 0 function hierarchy "top.f"
    // CHECK-LABEL: simulation.func @transfer
    // CHECK-NEXT: %[[DEFAULT:.*]] = simulation.logic.constant 0 : i8, -1 : i8
    // CHECK-NEXT: %[[TMP:.*]] = simulation.ref.alloc %[[DEFAULT]]
    // CHECK: schedule.known.bits
    // CHECK-NEXT: cf.cond_br %{{.*}}, ^[[COPY:bb[0-9]+]], ^[[BODY:bb[0-9]+]]
    // CHECK: ^[[BODY]]:
    // CHECK: %[[P:.*]] = simulation.logic.binary mul
    // CHECK-NEXT: simulation.ref.store %[[P]] to %[[TMP]]
    // CHECK: %[[E:.*]] = simulation.ref.load
    // CHECK-NEXT: cf.br ^[[REST:bb[0-9]+]](%[[P]], %[[E]] :
    // CHECK: ^[[COPY]]:
    // CHECK: %[[P2:.*]] = simulation.logic.binary mul
    // CHECK: %[[E2:.*]] = simulation.ref.load %{{.*}} : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
    // CHECK-NEXT: %[[OK:.*]] = schedule.known.value %[[E2]]
    // CHECK-NEXT: cf.cond_br %[[OK]] weights([1048575, 1]), ^[[MORE:bb[0-9]+]], ^[[LEAVE:bb[0-9]+]]
    // CHECK: ^[[MORE]]:
    // CHECK-NEXT: %[[BITS:.*]] = simulation.logic.to_bits %[[E2]]
    // CHECK-NEXT: %[[KNOWN:.*]] = simulation.logic.from_bits %[[BITS]]
    // CHECK-NEXT: simulation.logic.binary add %[[KNOWN]], %[[P2]]
    // CHECK: ^[[REST]](%[[MP:.*]]: !simulation.logic<8>, %[[ME:.*]]: !simulation.logic<8>):
    // CHECK-NEXT: simulation.logic.binary add %[[ME]], %[[MP]]
    // CHECK: %[[OUT:.*]] = simulation.context.storage %arg0[1]
    // CHECK-NEXT: simulation.ref.store %{{.*}} to %[[OUT]]
    // CHECK: ^[[LEAVE]]:
    // CHECK-NEXT: cf.br ^[[REST]](%[[P2]], %[[E2]] :
    simulation.func @transfer(%ctx: !c {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %a = simulation.context.storage %ctx[0] : !r8
      %d = simulation.context.storage %ctx[1] : !r8
      %mem = simulation.context.storage %ctx[2] : !simulation.ref<!memory>
      %x = simulation.ref.load %a : !r8 -> !l8
      %p = simulation.logic.binary mul %x, %x : !l8
      %tmp = simulation.ref.alloc %p : !l8 -> !r8
      %slot = simulation.ref.array_element %mem[%x] : (!simulation.ref<!memory>, !l8) -> !r8
      %e = simulation.ref.load %slot : !r8 -> !l8
      %s1 = simulation.logic.binary add %e, %p : !l8
      %s2 = simulation.logic.binary mul %s1, %s1 : !l8
      %s3 = simulation.logic.binary xor %s2, %e : !l8
      %s4 = simulation.logic.binary sub %s3, %p : !l8
      %q = simulation.ref.load %tmp : !r8 -> !l8
      %s5 = simulation.logic.binary add %s4, %q : !l8
      %s6 = simulation.logic.binary mul %s5, %s4 : !l8
      simulation.ref.store %s6 to %d : !l8, !r8
      simulation.return
    }
  }
}

// -----

// Either copy of the first segment may have filled the private slot, so the
// second segment's check tests it before the copy relies on it. The root the
// slot's value overwrites needs no check of its own.
!c = !simulation.context
!l8 = !simulation.logic<8>
!l16 = !simulation.logic<16>
!r8 = !simulation.ref<!l8>
!r16 = !simulation.ref<!l16>
module attributes {native.optimization_level = 3 : i32} {
  simulation.design @slots {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !l8 design
    simulation.storage.decl 1 in 0 : !l8 design
    simulation.storage.decl 2 in 0 : !l16 design
    simulation.storage.decl 3 in 0 : !l16 design
    simulation.code_unit.decl 1 in 0 function hierarchy "top.f"
    // SEGMENTS-LABEL: simulation.func @slots
    // SEGMENTS: %[[BITS:.*]] = schedule.known.bits %{{.*}} [array<i64: 0, 16>] : !simulation.ref<!simulation.logic<16>>{{$}}
    // SEGMENTS-NEXT: %[[SLOT:.*]] = simulation.ref.load %[[TMP:.*]] :
    // SEGMENTS-NEXT: %[[FILLED:.*]] = schedule.known.value %[[SLOT]]
    // SEGMENTS-NEXT: %[[OK:.*]] = arith.andi %[[BITS]], %[[FILLED]]
    // SEGMENTS-NEXT: cf.cond_br %[[OK]] weights([1048575, 1]), ^[[COPY:bb[0-9]+]], ^
    // SEGMENTS: ^[[COPY]]:
    // SEGMENTS-NEXT: %[[Q:.*]] = simulation.ref.load %[[TMP]] :
    // SEGMENTS-NEXT: simulation.ref.store %[[Q]] to %[[B:.*]] :
    // SEGMENTS-NEXT: simulation.ref.load %[[B]] {schedule.native.known_read}
    // SEGMENTS-NEXT: simulation.ref.load %{{.*}} {schedule.native.known_read}
    simulation.func @slots(%ctx: !c {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %a = simulation.context.storage %ctx[0] : !r8
      %b = simulation.context.storage %ctx[1] : !r8
      %c = simulation.context.storage %ctx[2] : !r16
      %d = simulation.context.storage %ctx[3] : !r16
      %x = simulation.ref.load %a : !r8 -> !l8
      %p1 = simulation.logic.binary mul %x, %x : !l8
      %p2 = simulation.logic.binary add %p1, %x : !l8
      %p3 = simulation.logic.binary mul %p2, %p1 : !l8
      %p4 = simulation.logic.binary sub %p3, %x : !l8
      %tmp = simulation.ref.alloc %p4 : !l8 -> !r8
      %q = simulation.ref.load %tmp : !r8 -> !l8
      simulation.ref.store %q to %b : !l8, !r8
      %y = simulation.ref.load %b : !r8 -> !l8
      %z = simulation.ref.load %c : !r16 -> !l16
      %u = simulation.logic.binary add %y, %y : !l8
      simulation.ref.store %u to %a : !l8, !r8
      %v = simulation.logic.binary mul %z, %z : !l16
      %w = simulation.logic.binary add %v, %z : !l16
      %s = simulation.logic.binary mul %w, %z : !l16
      %t = simulation.logic.binary sub %s, %v : !l16
      %o = simulation.logic.binary add %t, %w : !l16
      simulation.ref.store %o to %d : !l16, !r16
      simulation.return
    }
  }
}

// -----

// The first segment's copy leaves the root it checked known, since nothing
// stores X/Z to it. After that copy ran, the second segment's check tests
// only the new root; after the original ran it tests both.

!c = !simulation.context
!l8 = !simulation.logic<8>
!l16 = !simulation.logic<16>
!r8 = !simulation.ref<!l8>
!r16 = !simulation.ref<!l16>
module attributes {native.optimization_level = 3 : i32} {
  simulation.design @inherit {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !l8 design
    simulation.storage.decl 1 in 0 : !l8 design
    simulation.storage.decl 2 in 0 : !l16 design
    simulation.storage.decl 3 in 0 : !l16 design
    simulation.code_unit.decl 1 in 0 function hierarchy "top.f"
    // SEGMENTS-LABEL: simulation.func @inherit
    // SEGMENTS: %[[FALSE:.*]] = arith.constant false
    // SEGMENTS-NEXT: cf.br ^[[NEXT:bb[0-9]+]](%[[FALSE]] : i1)
    // SEGMENTS: ^[[NEXT]](%[[RAN:.*]]: i1):
    // SEGMENTS: %[[FRESH:.*]] = schedule.known.bits %{{.*}} [array<i64: 0, 16>] : !simulation.ref<!simulation.logic<16>>{{$}}
    // SEGMENTS-NEXT: cf.cond_br %[[RAN]] weights([1048575, 1]), ^[[DECIDE:bb[0-9]+]](%[[FRESH]] : i1), ^[[INHERIT:bb[0-9]+]]
    // SEGMENTS: %[[TRUE:.*]] = arith.constant true
    // SEGMENTS-NEXT: cf.br ^[[NEXT]](%[[TRUE]] : i1)
    // SEGMENTS: ^[[INHERIT]]:
    // SEGMENTS-NEXT: %[[A:.*]] = simulation.context.storage %arg0[0]
    // SEGMENTS-NEXT: %[[OLD:.*]] = schedule.known.bits %[[A]] [array<i64: 0, 8>]
    // SEGMENTS-NEXT: %[[BOTH:.*]] = arith.andi %[[FRESH]], %[[OLD]]
    // SEGMENTS-NEXT: cf.br ^[[DECIDE]](%[[BOTH]] : i1)
    // SEGMENTS: ^[[DECIDE]](%[[OK:.*]]: i1):
    // SEGMENTS-NEXT: cf.cond_br %[[OK]] weights([1048575, 1]), ^
    simulation.func @inherit(%ctx: !c {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %a = simulation.context.storage %ctx[0] : !r8
      %b = simulation.context.storage %ctx[1] : !r8
      %c = simulation.context.storage %ctx[2] : !r16
      %d = simulation.context.storage %ctx[3] : !r16
      %x = simulation.ref.load %a : !r8 -> !l8
      %p1 = simulation.logic.binary mul %x, %x : !l8
      %p2 = simulation.logic.binary add %p1, %x : !l8
      %p3 = simulation.logic.binary mul %p2, %p1 : !l8
      %p4 = simulation.logic.binary sub %p3, %x : !l8
      simulation.ref.store %p4 to %b : !l8, !r8
      %y = simulation.ref.load %a : !r8 -> !l8
      %z = simulation.ref.load %c : !r16 -> !l16
      %yw = simulation.logic.resize %y signed = false : !l8 -> !l16
      %v = simulation.logic.binary mul %z, %yw : !l16
      %w = simulation.logic.binary add %v, %z : !l16
      %s = simulation.logic.binary mul %w, %yw : !l16
      %t = simulation.logic.binary sub %s, %v : !l16
      %o = simulation.logic.binary add %t, %w : !l16
      simulation.ref.store %o to %d : !l16, !r16
      simulation.return
    }
  }
}

// -----

// A value from the previous segment comes from whichever copy ran there. The
// second segment's check tests the values only the copy proves known: the
// gated read and the product computed from it. The memory element is an
// island in both copies, so the check leaves it alone and both copies keep
// its unknown plane.

!c = !simulation.context
!l8 = !simulation.logic<8>
!r8 = !simulation.ref<!l8>
!memory = !simulation.unpacked_array<0 : 7 x !l8>
module attributes {native.optimization_level = 3 : i32} {
  simulation.design @crossing {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !l8 design
    simulation.storage.decl 1 in 0 : !l8 design
    simulation.storage.decl 2 in 0 : !memory design
    simulation.storage.decl 3 in 0 : !l8 design
    simulation.code_unit.decl 1 in 0 function hierarchy "top.f"
    // SEGMENTS-LABEL: simulation.func @crossing
    // SEGMENTS: ^[[NEXT:bb[0-9]+]](%[[X:[0-9]+]]: !simulation.logic<8>, %[[E:[0-9]+]]: !simulation.logic<8>, %[[P:[0-9]+]]: !simulation.logic<8>, %{{[0-9]+}}: i1):
    // SEGMENTS: %[[BITS:.*]] = schedule.known.bits
    // SEGMENTS-NEXT: %[[KP:.*]] = schedule.known.value %[[P]]
    // SEGMENTS-NEXT: %[[BOTH:.*]] = arith.andi %[[BITS]], %[[KP]]
    // SEGMENTS-NEXT: %[[KX:.*]] = schedule.known.value %[[X]]
    // SEGMENTS-NEXT: %[[OK:.*]] = arith.andi %[[BOTH]], %[[KX]]
    // SEGMENTS-NEXT: cf.cond_br %[[OK]] weights([1048575, 1]), ^[[COPY:bb[0-9]+]], ^
    // SEGMENTS: ^[[COPY]]:
    // SEGMENTS: simulation.logic.binary xor %{{.*}}, %[[E]]
    simulation.func @crossing(%ctx: !c {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %a = simulation.context.storage %ctx[0] : !r8
      %b = simulation.context.storage %ctx[1] : !r8
      %mem = simulation.context.storage %ctx[2] : !simulation.ref<!memory>
      %d = simulation.context.storage %ctx[3] : !r8
      %x = simulation.ref.load %a : !r8 -> !l8
      %slot = simulation.ref.array_element %mem[%x] : (!simulation.ref<!memory>, !l8) -> !r8
      %e = simulation.ref.load %slot : !r8 -> !l8
      %p1 = simulation.logic.binary mul %x, %x : !l8
      %p2 = simulation.logic.binary add %p1, %x : !l8
      %p3 = simulation.logic.binary mul %p2, %p1 : !l8
      %p4 = simulation.logic.binary sub %p3, %x : !l8
      simulation.ref.store %p4 to %d : !l8, !r8
      %y = simulation.ref.load %b : !r8 -> !l8
      %q1 = simulation.logic.binary mul %y, %p4 : !l8
      %q2 = simulation.logic.binary add %q1, %y : !l8
      %q3 = simulation.logic.binary mul %q2, %q1 : !l8
      %q4 = simulation.logic.binary sub %q3, %y : !l8
      %q5 = simulation.logic.binary xor %q4, %e : !l8
      simulation.ref.store %q5 to %d : !l8, !r8
      simulation.return
    }
  }
}

// -----

// The first segment's copy proves the union known, but an unpacked union
// has no fixed bit image a check could test, so the segment that uses it
// keeps only its original.

!c = !simulation.context
!l8 = !simulation.logic<8>
!r8 = !simulation.ref<!l8>
!u = !simulation.unpacked_union<fields = [#simulation.field<name = "a", type = !l8, ordinal = 0, packedOffset = 0>, #simulation.field<name = "b", type = !simulation.logic<4>, ordinal = 1, packedOffset = 0>], isTagged = false>
!ru = !simulation.ref<!u>
module attributes {native.optimization_level = 3 : i32} {
  simulation.design @untestable {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !l8 design
    simulation.storage.decl 1 in 0 : !l8 design
    simulation.storage.decl 2 in 0 : !u design
    simulation.storage.decl 3 in 0 : !l8 design
    simulation.code_unit.decl 1 in 0 function hierarchy "top.f"
    // SEGMENTS-LABEL: simulation.func @untestable
    // SEGMENTS-SAME: schedule.native.known_versioned
    // SEGMENTS: schedule.known.bits %{{.*}}, %{{.*}} [array<i64: 0, 8>, array<i64: 0, 8>]
    // SEGMENTS-NOT: schedule.known.
    simulation.func @untestable(%ctx: !c {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %a = simulation.context.storage %ctx[0] : !r8
      %b = simulation.context.storage %ctx[1] : !r8
      %us = simulation.context.storage %ctx[2] : !ru
      %d = simulation.context.storage %ctx[3] : !r8
      %u = simulation.ref.load %us : !ru -> !u
      %x = simulation.ref.load %a : !r8 -> !l8
      %p1 = simulation.logic.binary mul %x, %x : !l8
      %p2 = simulation.logic.binary add %p1, %x : !l8
      %p3 = simulation.logic.binary mul %p2, %p1 : !l8
      %p4 = simulation.logic.binary sub %p3, %x : !l8
      %p5 = simulation.logic.binary mul %p4, %p3 : !l8
      simulation.ref.store %p5 to %d : !l8, !r8
      %y = simulation.ref.load %b : !r8 -> !l8
      %f = simulation.union.extract %u[0] : (!u) -> !l8
      %q1 = simulation.logic.binary mul %y, %f : !l8
      %q2 = simulation.logic.binary add %q1, %y : !l8
      %q3 = simulation.logic.binary mul %q2, %q1 : !l8
      %q4 = simulation.logic.binary sub %q3, %y : !l8
      simulation.ref.store %q4 to %d : !l8, !r8
      simulation.return
    }
  }
}

// -----

// The block that never executes also enters the second segment, so the
// first segment's copies merge in a dedicated exit. That exit forwards
// whether the copy ran to the second segment's check, which tests the
// inherited root only after the original ran; the other edge passes false.

!c = !simulation.context
!l8 = !simulation.logic<8>
!l16 = !simulation.logic<16>
!r8 = !simulation.ref<!l8>
!r16 = !simulation.ref<!l16>
module attributes {native.optimization_level = 3 : i32} {
  simulation.design @dedicated {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !l8 design
    simulation.storage.decl 1 in 0 : !l8 design
    simulation.storage.decl 2 in 0 : !l16 design
    simulation.storage.decl 3 in 0 : !l16 design
    simulation.code_unit.decl 1 in 0 function hierarchy "top.f"
    // SEGMENTS-LABEL: simulation.func @dedicated
    // SEGMENTS: ^bb1:  // no predecessors
    // SEGMENTS-NEXT: %[[FALSE:.*]] = arith.constant false
    // SEGMENTS-NEXT: cf.br ^[[NEXT:bb[0-9]+]](%[[FALSE]] : i1)
    // SEGMENTS-NEXT: ^[[NEXT]](%[[RAN:.*]]: i1):
    // SEGMENTS: %[[FRESH:.*]] = schedule.known.bits %{{.*}} [array<i64: 0, 16>]
    // SEGMENTS-NEXT: cf.cond_br %[[RAN]] weights([1048575, 1]), ^{{bb[0-9]+}}(%[[FRESH]] : i1), ^
    // SEGMENTS: ^{{bb[0-9]+}}(%[[COPY:.*]]: i1):
    // SEGMENTS: cf.br ^[[NEXT]](%[[COPY]] : i1)
    simulation.func @dedicated(%ctx: !c {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %a = simulation.context.storage %ctx[0] : !r8
      %b = simulation.context.storage %ctx[1] : !r8
      %c = simulation.context.storage %ctx[2] : !r16
      %d = simulation.context.storage %ctx[3] : !r16
      %x = simulation.ref.load %a : !r8 -> !l8
      %p1 = simulation.logic.binary mul %x, %x : !l8
      %p2 = simulation.logic.binary add %p1, %x : !l8
      %p3 = simulation.logic.binary mul %p2, %p1 : !l8
      %p4 = simulation.logic.binary sub %p3, %x : !l8
      simulation.ref.store %p4 to %b : !l8, !r8
      cf.br ^next
    ^dead:
      cf.br ^next
    ^next:
      %y = simulation.ref.load %a : !r8 -> !l8
      %z = simulation.ref.load %c : !r16 -> !l16
      %yw = simulation.logic.resize %y signed = false : !l8 -> !l16
      %v = simulation.logic.binary mul %z, %yw : !l16
      %w = simulation.logic.binary add %v, %z : !l16
      %s = simulation.logic.binary mul %w, %yw : !l16
      %t = simulation.logic.binary sub %s, %v : !l16
      %o = simulation.logic.binary add %t, %w : !l16
      simulation.ref.store %o to %d : !l16, !r16
      simulation.return
    }
  }
}
