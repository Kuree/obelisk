// RUN: obelisk-opt %s --split-input-file --pass-pipeline='builtin.module(schedule-version-known-activations{minimum-benefit=1 checked-reads=true})' | FileCheck %s
// RUN: obelisk-opt %s --split-input-file --pass-pipeline='builtin.module(schedule-version-known-activations{minimum-benefit=1 segment-size=10})' | FileCheck %s --check-prefix=SEGMENTS

// One check of the roots the activation reads selects a copy of it. The copy
// reads those roots' value planes only. The memory element, whose index may
// select past the array, keeps both planes, and so does every store that may
// follow the store derived from it. The original blocks keep their
// positions, and the entry's operations move behind the check.

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
    // CHECK-NEXT: %[[WIDE:.*]] = simulation.context.storage %arg0[0]
    // CHECK-NEXT: %[[M:.*]] = simulation.context.storage %arg0[1]
    // CHECK-NEXT: %[[OK:.*]] = schedule.known.roots %[[WIDE]], %[[M]] :
    // CHECK-NEXT: cf.cond_br %[[OK]] weights([1048575, 1]), ^[[COPY:bb[0-9]+]], ^[[BODY:bb[0-9]+]]
    // CHECK: ^bb1:
    // CHECK-NEXT: simulation.logic.binary mul
    // CHECK: simulation.return
    // CHECK: ^[[BODY]]:
    // CHECK: %[[X:.*]] = simulation.ref.load %{{.*}} : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
    // CHECK-NEXT: simulation.logic.is_true %[[X]]
    // CHECK: ^[[COPY]]:
    // CHECK-NOT: known_
    // CHECK: %[[K:.*]] = simulation.ref.load %{{.*}} {schedule.native.known_read}
    // CHECK-NEXT: simulation.logic.is_true %[[K]]
    // CHECK: %[[Y:.*]] = simulation.ref.load %{{.*}} {schedule.native.known_read}
    // CHECK-NEXT: simulation.ref.store %[[Y]] to %{{.*}} : !simulation.logic<8>
    // CHECK-NOT: known_
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

// An activation that saves less than its check costs keeps only its
// original.

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
    // CHECK-NOT: schedule.known.roots
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
// original defined before it as block arguments. Its store of the result is
// known. The private allocation both copies use moves to the entry, keeping
// a store of its initializer, and the output reference is recomputed after
// the merge.

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
    // CHECK: schedule.known.roots
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
    // CHECK: simulation.ref.store %{{.*}} to %{{.*}} {schedule.native.known_store}
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

// A memory element selected by an index that is valid whenever it is known
// is gated, and a known value stored to that element writes only its value
// plane: an invalid index writes nothing (IEEE 1800-2023 7.4.5). Once the
// activation may have stored X/Z into a root, a later store to it keeps both
// planes, and so does the possibly X/Z quotient (11.4.2).

!c = !simulation.context
!l3 = !simulation.logic<3>
!l4 = !simulation.logic<4>
!l8 = !simulation.logic<8>
!r3 = !simulation.ref<!l3>
!r8 = !simulation.ref<!l8>
!memory = !simulation.unpacked_array<0 : 7 x !l8>
module attributes {native.optimization_level = 3 : i32} {
  simulation.design @stores {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !l3 design
    simulation.storage.decl 1 in 0 : !memory design
    simulation.storage.decl 2 in 0 : !l8 design
    simulation.code_unit.decl 1 in 0 function hierarchy "top.f"
    // CHECK-LABEL: simulation.func @stores
    // CHECK: %[[OK:.*]] = schedule.known.roots %{{.*}}, %{{.*}} : !simulation.ref<!simulation.logic<3>>, !simulation.ref<!simulation.unpacked_array<0 : 7 x !simulation.logic<8>>>{{$}}
    // CHECK-NEXT: cf.cond_br %[[OK]] weights([1048575, 1]), ^[[COPY:bb[0-9]+]], ^
    // CHECK: ^[[COPY]]:
    // CHECK: simulation.ref.load %{{.*}} {schedule.native.known_read} : !simulation.ref<!simulation.logic<3>>
    // CHECK: simulation.ref.load %{{.*}} {schedule.native.known_read} : !simulation.ref<!simulation.logic<8>>
    // CHECK: simulation.ref.store %{{.*}} to %{{.*}} {schedule.native.known_store}
    // CHECK-NEXT: simulation.logic.binary udiv
    // CHECK-NEXT: simulation.ref.store %{{.*}} to %{{.*}} : !simulation.logic<8>
    // CHECK-NEXT: simulation.ref.store %{{.*}} to %{{.*}} : !simulation.logic<8>
    simulation.func @stores(%ctx: !c {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %i = simulation.context.storage %ctx[0] : !r3
      %mem = simulation.context.storage %ctx[1] : !simulation.ref<!memory>
      %out = simulation.context.storage %ctx[2] : !r8
      %short = simulation.ref.load %i : !r3 -> !l3
      %index = simulation.logic.resize %short signed = false : !l3 -> !l4
      %slot = simulation.ref.array_element %mem[%index] :
          (!simulation.ref<!memory>, !l4) -> !r8
      %e = simulation.ref.load %slot : !r8 -> !l8
      %p1 = simulation.logic.binary mul %e, %e : !l8
      %p2 = simulation.logic.binary add %p1, %e : !l8
      %p3 = simulation.logic.binary mul %p2, %p1 : !l8
      simulation.ref.store %p3 to %slot : !l8, !r8
      %r = simulation.logic.binary udiv %p3, %e : !l8
      simulation.ref.store %r to %out : !l8, !r8
      simulation.ref.store %p2 to %out : !l8, !r8
      simulation.return
    }
  }
}

// -----

// A root's steady pattern (`simulation.steady_unknown`, here a[7:4]) holds
// X/Z when the check passes. A read of the root before any write is
// value-only and loads the pattern as a constant X. A store reaching pattern
// bits keeps both planes even with a known value, and a later read keeps
// both too: the store may have made pattern bits known.

!c = !simulation.context
!l8 = !simulation.logic<8>
!r8 = !simulation.ref<!l8>
module attributes {native.optimization_level = 3 : i32} {
  simulation.design @steady {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !l8 design {simulation.steady_unknown = array<i64: 4, 4>}
    simulation.storage.decl 1 in 0 : !l8 design
    simulation.storage.decl 2 in 0 : !l8 design
    simulation.code_unit.decl 1 in 0 function hierarchy "top.f"
    // CHECK-LABEL: simulation.func @steady
    // CHECK: %[[OK:.*]] = schedule.known.roots
    // CHECK-NEXT: cf.cond_br %[[OK]] weights([1048575, 1]), ^[[COPY:bb[0-9]+]], ^
    // CHECK: ^[[COPY]]:
    // CHECK: simulation.ref.load %{{.*}} {schedule.native.known_read}
    // CHECK-NEXT: simulation.ref.load %{{.*}} {schedule.native.known_read}
    // CHECK: simulation.ref.store %{{.*}} to %{{.*}} : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>{{$}}
    // CHECK-NEXT: simulation.ref.load %{{.*}} : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>{{$}}
    simulation.func @steady(%ctx: !c {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %a = simulation.context.storage %ctx[0] : !r8
      %in = simulation.context.storage %ctx[1] : !r8
      %out = simulation.context.storage %ctx[2] : !r8
      %old = simulation.ref.load %a : !r8 -> !l8
      %x = simulation.ref.load %in : !r8 -> !l8
      %p1 = simulation.logic.binary mul %x, %x : !l8
      %p2 = simulation.logic.binary add %p1, %x : !l8
      %p3 = simulation.logic.binary mul %p2, %p1 : !l8
      simulation.ref.store %p3 to %a : !l8, !r8
      %new = simulation.ref.load %a : !r8 -> !l8
      %sum = simulation.logic.binary add %new, %old : !l8
      simulation.ref.store %sum to %out : !l8, !r8
      simulation.return
    }
  }
}

// -----

// Either copy of the first segment may have filled the private slot, so the
// second segment's check tests it before the copy relies on it. The copy
// stores the slot's value to a root as a known store, so the check tests that
// root with the ones the segment reads.
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
    // SEGMENTS: %[[ROOTS:.*]] = schedule.known.roots %{{.*}}, %{{.*}}, %{{.*}} : !simulation.ref<!simulation.logic<8>>, !simulation.ref<!simulation.logic<8>>, !simulation.ref<!simulation.logic<16>>{{$}}
    // SEGMENTS-NEXT: %[[SLOT:.*]] = simulation.ref.load %[[TMP:.*]] :
    // SEGMENTS-NEXT: %[[FILLED:.*]] = schedule.known.value %[[SLOT]]
    // SEGMENTS-NEXT: %[[OK:.*]] = arith.andi %[[ROOTS]], %[[FILLED]]
    // SEGMENTS-NEXT: cf.cond_br %[[OK]] weights([1048575, 1]), ^[[COPY:bb[0-9]+]], ^
    // SEGMENTS: ^[[COPY]]:
    // SEGMENTS-NEXT: %[[Q:.*]] = simulation.ref.load %[[TMP]] :
    // SEGMENTS-NEXT: %[[QB:.*]] = simulation.logic.to_bits %[[Q]]
    // SEGMENTS-NEXT: %[[QK:.*]] = simulation.logic.from_bits %[[QB]]
    // SEGMENTS-NEXT: simulation.ref.store %[[QK]] to %[[B:.*]] {schedule.native.known_store}
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

// A value from the previous segment comes from whichever copy ran there. The
// second segment's check tests the values only the copy proves known: the
// gated read and the product computed from it. The memory element's index
// may select past the array, so the check leaves the element alone and both
// copies keep its unknown plane.

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
    // SEGMENTS: ^[[NEXT:bb[0-9]+]](%[[X:[0-9]+]]: !simulation.logic<8>, %[[E:[0-9]+]]: !simulation.logic<8>, %[[P:[0-9]+]]: !simulation.logic<8>):
    // SEGMENTS: %[[ROOTS:.*]] = schedule.known.roots
    // SEGMENTS-NEXT: %[[KP:.*]] = schedule.known.value %[[P]]
    // SEGMENTS-NEXT: %[[BOTH:.*]] = arith.andi %[[ROOTS]], %[[KP]]
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
    // SEGMENTS: schedule.known.roots %{{.*}}, %{{.*}} :
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

// A callee that stores only through its formals writes nothing else: the
// slot it fills holds a known value when its arguments are known, so the
// read of that slot and the store derived from it are value-only in the
// copy. A callee that stores X/Z through a formal taints its slot, and the
// store derived from that one keeps both planes.

!c = !simulation.context
!l8 = !simulation.logic<8>
!r8 = !simulation.ref<!l8>
module attributes {native.optimization_level = 3 : i32} {
  simulation.design @callees {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !l8 design
    simulation.storage.decl 1 in 0 : !l8 design
    simulation.storage.decl 2 in 0 : !l8 design
    simulation.code_unit.decl 1 in 0 function hierarchy "top.f"
    simulation.code_unit.decl 2 in 0 function hierarchy "top.fill"
    simulation.code_unit.decl 3 in 0 function hierarchy "top.taint"
    simulation.func private @fill(
        %ctx: !c {simulation.capture_kind = 0 : i32},
        %out: !r8 {simulation.capture_kind = 1 : i32},
        %v: !l8 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %p = simulation.logic.binary mul %v, %v : !l8
      simulation.ref.store %p to %out : !l8, !r8
      simulation.return
    }
    simulation.func private @taint(
        %ctx: !c {simulation.capture_kind = 0 : i32},
        %out: !r8 {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %x = simulation.logic.constant 0 : i8, -1 : i8 : !l8
      simulation.ref.store %x to %out : !l8, !r8
      simulation.return
    }
    // CHECK-LABEL: simulation.func @caller
    // CHECK: %[[OK:.*]] = schedule.known.roots %{{.*}}, %{{.*}} : !simulation.ref<!simulation.logic<8>>, !simulation.ref<!simulation.logic<8>>{{$}}
    // CHECK-NEXT: cf.cond_br %[[OK]] weights([1048575, 1]), ^[[COPY:bb[0-9]+]], ^
    // CHECK: ^[[COPY]]:
    // CHECK: simulation.call @fill
    // CHECK-NEXT: %[[Q:.*]] = simulation.ref.load
    // CHECK: simulation.ref.store %{{.*}} to %{{.*}} {schedule.native.known_store}
    // CHECK: simulation.call @taint
    // CHECK-NEXT: %[[R:.*]] = simulation.ref.load
    // CHECK: simulation.ref.store %{{.*}} to %{{.*}} : !simulation.logic<8>
    // CHECK-NEXT: simulation.return
    simulation.func @caller(%ctx: !c {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %a = simulation.context.storage %ctx[0] : !r8
      %b = simulation.context.storage %ctx[1] : !r8
      %d = simulation.context.storage %ctx[2] : !r8
      %x = simulation.ref.load %a : !r8 -> !l8
      %zero = simulation.logic.constant 0 : i8, 0 : i8 : !l8
      %tmp = simulation.ref.alloc %zero : !l8 -> !r8
      simulation.call @fill(%ctx, %tmp, %x) : (!c, !r8, !l8) -> ()
      %q = simulation.ref.load %tmp : !r8 -> !l8
      %p1 = simulation.logic.binary mul %q, %q : !l8
      %p2 = simulation.logic.binary add %p1, %q : !l8
      %p3 = simulation.logic.binary mul %p2, %p1 : !l8
      simulation.ref.store %p3 to %b : !l8, !r8
      %other = simulation.ref.alloc %zero : !l8 -> !r8
      simulation.call @taint(%ctx, %other) : (!c, !r8) -> ()
      %r = simulation.ref.load %other : !r8 -> !l8
      %s1 = simulation.logic.binary add %r, %p3 : !l8
      %s2 = simulation.logic.binary mul %s1, %p2 : !l8
      simulation.ref.store %s2 to %d : !l8, !r8
      simulation.return
    }
  }
}

// -----

// Knownness is tracked per bit: a struct built from its default holds X/Z
// only in the field nothing assigns (IEEE 1800-2023 6.8, Table 6-7). The
// field the copy assigns from a gated read is known, so the store of a
// value derived from it is value-only after a cast to its two-state view,
// while the store derived from the unassigned field keeps both planes.

!c = !simulation.context
!l8 = !simulation.logic<8>
!r8 = !simulation.ref<!l8>
!pair = !simulation.unpacked_struct<[#simulation.field<name = "a", type = !l8, ordinal = 0, packedOffset = 0>, #simulation.field<name = "b", type = !l8, ordinal = 1, packedOffset = 0>]>
module attributes {native.optimization_level = 3 : i32} {
  simulation.design @fields {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !l8 design
    simulation.storage.decl 1 in 0 : !l8 design
    simulation.storage.decl 2 in 0 : !l8 design
    simulation.code_unit.decl 1 in 0 function hierarchy "top.f"
    // CHECK-LABEL: simulation.func @fields
    // CHECK: %[[OK:.*]] = schedule.known.roots %{{.*}}, %{{.*}} : !simulation.ref<!simulation.logic<8>>, !simulation.ref<!simulation.logic<8>>{{$}}
    // CHECK-NEXT: cf.cond_br %[[OK]] weights([1048575, 1]), ^[[COPY:bb[0-9]+]], ^
    // CHECK: ^[[COPY]]:
    // CHECK: %[[A:.*]] = simulation.aggregate.extract %{{.*}}[0]
    // CHECK-NEXT: %[[AB:.*]] = simulation.logic.to_bits %[[A]]
    // CHECK-NEXT: %[[AK:.*]] = simulation.logic.from_bits %[[AB]]
    // CHECK-NEXT: %[[B:.*]] = simulation.aggregate.extract %{{.*}}[1]
    // CHECK-NOT: simulation.logic.to_bits
    // CHECK: simulation.ref.store %{{.*}} to %{{.*}} {schedule.native.known_store}
    // CHECK: simulation.ref.store %{{.*}} to %{{.*}} : !simulation.logic<8>
    // CHECK-NEXT: simulation.return
    simulation.func @fields(%ctx: !c {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %a = simulation.context.storage %ctx[0] : !r8
      %b = simulation.context.storage %ctx[1] : !r8
      %d = simulation.context.storage %ctx[2] : !r8
      %x = simulation.ref.load %a : !r8 -> !l8
      %empty = simulation.aggregate.default : !pair
      %pair = simulation.aggregate.insert %x into %empty[0] : (!pair, !l8) -> !pair
      %fa = simulation.aggregate.extract %pair[0] : (!pair) -> !l8
      %fb = simulation.aggregate.extract %pair[1] : (!pair) -> !l8
      %p1 = simulation.logic.binary mul %fa, %fa : !l8
      %p2 = simulation.logic.binary add %p1, %fa : !l8
      %p3 = simulation.logic.binary mul %p2, %p1 : !l8
      simulation.ref.store %p3 to %b : !l8, !r8
      %q1 = simulation.logic.binary add %fb, %p3 : !l8
      %q2 = simulation.logic.binary mul %q1, %p2 : !l8
      simulation.ref.store %q2 to %d : !l8, !r8
      simulation.return
    }
  }
}

// -----

// The lowering tests a condition for X/Z with `c === view(c)` and merges X/Z
// on the path where it holds some. In the copy the condition is known, so
// that test folds to true and the merge path is dead: the X/Z dummy the
// known-false path hands the join never reaches the result, whose store is
// value-only.

!c = !simulation.context
!l1 = !simulation.logic<1>
!l8 = !simulation.logic<8>
!r1 = !simulation.ref<!l1>
!r8 = !simulation.ref<!l8>
module attributes {native.optimization_level = 3 : i32} {
  simulation.design @merges {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !l1 design
    simulation.storage.decl 1 in 0 : !l8 design
    simulation.storage.decl 2 in 0 : !l8 design
    simulation.code_unit.decl 1 in 0 function hierarchy "top.f"
    // CHECK-LABEL: simulation.func @merges
    // CHECK: %[[OK:.*]] = schedule.known.roots %{{.*}}, %{{.*}}, %{{.*}} : !simulation.ref<!simulation.logic<1>>, !simulation.ref<!simulation.logic<8>>, !simulation.ref<!simulation.logic<8>>{{$}}
    // CHECK-NEXT: cf.cond_br %[[OK]] weights([1048575, 1]), ^[[COPY:bb[0-9]+]], ^
    // CHECK: ^[[COPY]]:
    // CHECK: simulation.ref.store %{{.*}} to %{{.*}} {schedule.native.known_store}
    simulation.func @merges(%ctx: !c {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %cond = simulation.context.storage %ctx[0] : !r1
      %a = simulation.context.storage %ctx[1] : !r8
      %d = simulation.context.storage %ctx[2] : !r8
      %c = simulation.ref.load %cond : !r1 -> !l1
      %x = simulation.ref.load %a : !r8 -> !l8
      %bits = simulation.logic.to_bits %c : !l1 -> i1
      %view = simulation.logic.from_bits %bits : i1 -> !l1
      %known = simulation.logic.compare case_eq %c, %view : (!l1, !l1) -> i1
      %true = arith.constant true
      %false = arith.constant false
      %unknown = arith.xori %known, %true : i1
      %dummy = simulation.logic.constant 0 : i8, -1 : i8 : !l8
      %p1 = simulation.logic.binary mul %x, %x : !l8
      cf.cond_br %unknown, ^join(%dummy, %true : !l8, i1), ^plain
    ^plain:
      cf.br ^join(%p1, %false : !l8, i1)
    ^join(%v: !l8, %merge: i1):
      cf.cond_br %merge, ^merged, ^out(%v : !l8)
    ^merged:
      %xc = simulation.logic.constant 0 : i1, -1 : i1 : !l1
      %m = simulation.logic.mux %xc ? %v : %p1 : (!l1, !l8, !l8) -> !l8
      cf.br ^out(%m : !l8)
    ^out(%r: !l8):
      %p2 = simulation.logic.binary add %r, %p1 : !l8
      %p3 = simulation.logic.binary mul %p2, %r : !l8
      simulation.ref.store %p3 to %d : !l8, !r8
      simulation.return
    }
  }
}
