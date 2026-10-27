// RUN: talos-opt %s --split-input-file --test-talos-steady-unknown -o /dev/null 2>&1 | FileCheck %s

// A four-state root starts as X (IEEE 1800-2023 6.8). Bits no write stores
// a possibly known value into stay X once the design runs: the high byte
// of `half`, which only its low byte's store reaches; the same byte of
// `copy`, assigned from `half` as a whole; the bits of `literal` that an X
// constant fills; and the high byte of `nba`, updated from `half` by an NBA.
// An X/Z bit of `half` makes all of `sum` X (IEEE 1800-2023 11.4.3). A
// store at a run-time index may make any bit known. A root nothing writes
// keeps its initial X.

!c = !simulation.context
!l8 = !simulation.logic<8>
!l16 = !simulation.logic<16>
!r8 = !simulation.ref<!l8>
!r16 = !simulation.ref<!l16>
module {
  simulation.design @moves {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !l16 design
    simulation.storage.decl 1 in 0 : !l16 design
    simulation.storage.decl 2 in 0 : !l16 design
    simulation.storage.decl 3 in 0 : !l16 design
    simulation.storage.decl 4 in 0 : !l16 design
    simulation.storage.decl 5 in 0 : !l16 design
    simulation.storage.decl 6 in 0 : !l8 design
    simulation.storage.decl 7 in 0 : !l8 design
    simulation.code_unit.decl 1 in 0 function hierarchy "top.f"
    simulation.func @step(%ctx: !c {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %half = simulation.context.storage %ctx[0] : !r16
      %copy = simulation.context.storage %ctx[1] : !r16
      %literal = simulation.context.storage %ctx[2] : !r16
      %sum = simulation.context.storage %ctx[3] : !r16
      %nba = simulation.context.storage %ctx[4] : !r16
      %dynamic = simulation.context.storage %ctx[5] : !r16
      %in = simulation.context.storage %ctx[6] : !r8
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !l8
      simulation.ref.store %one to %in : !l8, !r8
      %x = simulation.ref.load %in : !r8 -> !l8
      %low = simulation.ref.extract %half from 0 : !r16 -> !r8
      simulation.ref.store %x to %low : !l8, !r8
      %h = simulation.ref.load %half : !r16 -> !l16
      simulation.ref.store %h to %copy : !l16, !r16
      %c = simulation.logic.constant 0 : i16, 15 : i16 : !l16
      simulation.ref.store %c to %literal : !l16, !r16
      %s = simulation.logic.binary add %h, %h : !l16
      simulation.ref.store %s to %sum : !l16, !r16
      simulation.nba.enqueue %h to %nba : (!l16, !r16) -> ()
      %slice = simulation.ref.dyn_extract %dynamic from %x : (!r16, !l8) -> !r8
      simulation.ref.store %x to %slice : !l8, !r8
      simulation.return
    }
  }
}

// CHECK-LABEL: steady-unknown @moves
// CHECK-NEXT: root 0: [8,16)
// CHECK-NEXT: root 1: [8,16)
// CHECK-NEXT: root 2: [0,4)
// CHECK-NEXT: root 3: [0,16)
// CHECK-NEXT: root 4: [8,16)
// CHECK-NEXT: root 7: [0,8)
// CHECK-NOT: root

// -----

// Bits follow concatenations, extractions, extensions and selects, and a
// private allocation whose X initializer only its low byte overwrites. A
// store through a function formal reaches the window its only caller passes.

!c = !simulation.context
!l4 = !simulation.logic<4>
!l8 = !simulation.logic<8>
!l16 = !simulation.logic<16>
!r8 = !simulation.ref<!l8>
!r16 = !simulation.ref<!l16>
module {
  simulation.design @paths {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !l16 design
    simulation.storage.decl 1 in 0 : !l16 design
    simulation.storage.decl 2 in 0 : !l16 design
    simulation.storage.decl 3 in 0 : !l8 design
    simulation.storage.decl 4 in 0 : !l16 design
    simulation.code_unit.decl 1 in 0 function hierarchy "top.f"
    simulation.code_unit.decl 2 in 0 function hierarchy "top.set"
    simulation.func @set(%ctx: !c {simulation.capture_kind = 0 : i32},
                         %r: !r16 {simulation.capture_kind = 1 : i32},
                         %v: !l8 {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %low = simulation.ref.extract %r from 0 : !r16 -> !r8
      simulation.ref.store %v to %low : !l8, !r8
      simulation.return
    }
    simulation.func @step(%ctx: !c {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %parts = simulation.context.storage %ctx[0] : !r16
      %slot = simulation.context.storage %ctx[1] : !r16
      %formal = simulation.context.storage %ctx[2] : !r16
      %in = simulation.context.storage %ctx[3] : !r8
      %wide = simulation.context.storage %ctx[4] : !r16
      %k = simulation.logic.constant 1 : i8, 0 : i8 : !l8
      simulation.ref.store %k to %in : !l8, !r8
      %y = simulation.ref.load %in : !r8 -> !l8
      %x = simulation.logic.binary add %y, %y : !l8
      %c = simulation.logic.is_true %x : !l8
      // {x[3:0], 4'bx} zero-extended, or {4'bx, x[3:0]}: either alternative
      // may be stored.
      %nibble = simulation.logic.extract %x from 0 : !l8 -> !l4
      %xs = simulation.logic.constant 0 : i4, 15 : i4 : !l4
      %byte = simulation.logic.concat %nibble, %xs : (!l4, !l4) -> !l8
      %ext = simulation.logic.resize %byte signed = false : !l8 -> !l16
      %swap = simulation.logic.concat %xs, %nibble : (!l4, !l4) -> !l8
      %other = simulation.logic.resize %swap signed = false : !l8 -> !l16
      %pick = arith.select %c, %ext, %other : !l16
      simulation.ref.store %pick to %parts : !l16, !r16
      // A temporary whose high byte keeps its X initializer.
      %undef = simulation.logic.constant 0 : i16, -1 : i16 : !l16
      %tmp = simulation.ref.alloc %undef : !l16 -> !r16
      %tmpLow = simulation.ref.extract %tmp from 0 : !r16 -> !r8
      simulation.ref.store %x to %tmpLow : !l8, !r8
      %t = simulation.ref.load %tmp : !r16 -> !l16
      simulation.ref.store %t to %slot : !l16, !r16
      simulation.call @set(%ctx, %formal, %x) : (!c, !r16, !l8) -> ()
      // A signed extension copies the sign bit's knownness.
      %signed = simulation.logic.resize %byte signed = true : !l8 -> !l16
      simulation.ref.store %signed to %wide : !l16, !r16
      simulation.return
    }
  }
}

// CHECK-LABEL: steady-unknown @paths
// CHECK-NEXT: root 1: [8,16)
// CHECK-NEXT: root 2: [8,16)
// CHECK-NEXT: root 4: [0,4)
// CHECK-NOT: root

// -----

// A write the analysis cannot attribute may store a known value anywhere.

!c = !simulation.context
!l8 = !simulation.logic<8>
!r8 = !simulation.ref<!l8>
module {
  simulation.design @opaque {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !l8 design
    simulation.code_unit.decl 1 in 0 function hierarchy "top.f"
    simulation.func @step(%ctx: !c {simulation.capture_kind = 0 : i32},
                          %r: !r8 {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %v = simulation.logic.constant 1 : i8, 0 : i8 : !l8
      simulation.ref.store %v to %r : !l8, !r8
      simulation.return
    }
  }
}

// CHECK-LABEL: steady-unknown @opaque top
// CHECK-NOT: root

// -----

// Run-once initialization sets a starting value that repeating writes
// replace: it counts only for bits no repeating write overwrites. `reg`'s
// high byte is zeroed by `initial` but refilled with X on every clock;
// `held`'s high byte keeps the zero. An indexed selection of `pipe` whose
// index is no loop's induction may reach any element: every element's low
// nibble may become known, and its high nibble stays X.

!c = !simulation.context
!l4 = !simulation.logic<4>
!l8 = !simulation.logic<8>
!l16 = !simulation.logic<16>
!r8 = !simulation.ref<!l8>
!r16 = !simulation.ref<!l16>
!a = !simulation.unpacked_array<0 : 4 x !l8>
!ra = !simulation.ref<!a>
module {
  simulation.design @phases {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !l16 design
    simulation.storage.decl 1 in 0 : !l16 design
    simulation.storage.decl 2 in 0 : !a design
    simulation.storage.decl 3 in 0 : !l8 design
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.init"
    simulation.code_unit.decl 2 in 0 always_ff hierarchy "top.step"
    simulation.func @init(%ctx: !c {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %reg = simulation.context.storage %ctx[0] : !r16
      %held = simulation.context.storage %ctx[1] : !r16
      %zero = simulation.logic.constant 0 : i16, 0 : i16 : !l16
      simulation.ref.store %zero to %reg : !l16, !r16
      simulation.ref.store %zero to %held : !l16, !r16
      simulation.return
    }
    simulation.func @step(%ctx: !c {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 5 : i32, code_unit_id = 2 : i64} {
      %reg = simulation.context.storage %ctx[0] : !r16
      %held = simulation.context.storage %ctx[1] : !r16
      %pipe = simulation.context.storage %ctx[2] : !ra
      %in = simulation.context.storage %ctx[3] : !r8
      %k = simulation.logic.constant 1 : i8, 0 : i8 : !l8
      simulation.ref.store %k to %in : !l8, !r8
      %y = simulation.ref.load %in : !r8 -> !l8
      %x = simulation.logic.binary add %y, %y : !l8
      %xs = simulation.logic.constant 0 : i8, -1 : i8 : !l8
      %word = simulation.logic.concat %xs, %x : (!l8, !l8) -> !l16
      simulation.ref.store %word to %reg : !l16, !r16
      %heldLow = simulation.ref.extract %held from 0 : !r16 -> !r8
      simulation.ref.store %x to %heldLow : !l8, !r8
      %nibble = simulation.logic.extract %x from 0 : !l8 -> !l4
      %xn = simulation.logic.constant 0 : i4, -1 : i4 : !l4
      %byte = simulation.logic.concat %xn, %nibble : (!l4, !l4) -> !l8
      %i = arith.constant 1 : i32
      %element = simulation.ref.array_element %pipe[%i] : (!ra, i32) -> !r8
      simulation.ref.store %byte to %element : !l8, !r8
      simulation.return
    }
  }
}

// CHECK-LABEL: steady-unknown @phases
// CHECK-NEXT: root 0: [8,16)
// CHECK-NEXT: root 2: [4,8) [12,16) [20,24) [28,32) [36,40)
// CHECK-NOT: root

// -----

// Values carry their bits through operators by the LRM's X rules: XOR needs
// both bits known while a known AND operand may decide a bit (IEEE 1800-2023
// 11.4.8), an X/Z bit makes an arithmetic result or a relation X (11.4.3,
// 11.4.4), equality may be decided by two differing known bits (11.4.5), and
// a constant shift fills vacated bits with known zeros (11.4.10). A memory
// that `initial` zeroes keeps that value where its element writes do not
// land, while a whole-array copy replaces `regs` on every write.

!c = !simulation.context
!l1 = !simulation.logic<1>
!l8 = !simulation.logic<8>
!r1 = !simulation.ref<!l1>
!r8 = !simulation.ref<!l8>
!a = !simulation.unpacked_array<0 : 3 x !l8>
!ra = !simulation.ref<!a>
module {
  simulation.design @operators {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !l8 design
    simulation.storage.decl 1 in 0 : !l8 design
    simulation.storage.decl 2 in 0 : !l8 design
    simulation.storage.decl 3 in 0 : !l8 design
    simulation.storage.decl 4 in 0 : !l8 design
    simulation.storage.decl 5 in 0 : !l1 design
    simulation.storage.decl 6 in 0 : !l1 design
    simulation.storage.decl 7 in 0 : !a design
    simulation.storage.decl 8 in 0 : !a design
    simulation.storage.decl 9 in 0 : !a design
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.init"
    simulation.code_unit.decl 2 in 0 always_ff hierarchy "top.step"
    simulation.func @init(%ctx: !c {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %memory = simulation.context.storage %ctx[8] : !ra
      %regs = simulation.context.storage %ctx[9] : !ra
      %z = simulation.logic.constant 0 : i8, 0 : i8 : !l8
      %all = simulation.aggregate.splat %z : (!l8) -> !a
      simulation.ref.store %all to %memory : !a, !ra
      simulation.ref.store %all to %regs : !a, !ra
      simulation.return
    }
    simulation.func @step(%ctx: !c {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 5 : i32, code_unit_id = 2 : i64} {
      %src = simulation.context.storage %ctx[0] : !r8
      %xored = simulation.context.storage %ctx[1] : !r8
      %anded = simulation.context.storage %ctx[2] : !r8
      %summed = simulation.context.storage %ctx[3] : !r8
      %shifted = simulation.context.storage %ctx[4] : !r8
      %equal = simulation.context.storage %ctx[5] : !r1
      %less = simulation.context.storage %ctx[6] : !r1
      %next = simulation.context.storage %ctx[7] : !ra
      %memory = simulation.context.storage %ctx[8] : !ra
      %regs = simulation.context.storage %ctx[9] : !ra
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !l8
      simulation.ref.store %one to %src : !l8, !r8
      %s = simulation.ref.load %src : !r8 -> !l8
      %m = simulation.logic.constant 0 : i8, -16 : i8 : !l8
      %x = simulation.logic.binary xor %s, %m : !l8
      simulation.ref.store %x to %xored : !l8, !r8
      %y = simulation.logic.binary and %s, %m : !l8
      simulation.ref.store %y to %anded : !l8, !r8
      %z = simulation.logic.binary add %s, %m : !l8
      simulation.ref.store %z to %summed : !l8, !r8
      %four = simulation.logic.constant 4 : i8, 0 : i8 : !l8
      %w = simulation.logic.shift right %m by %four : (!l8, !l8) -> !l8
      simulation.ref.store %w to %shifted : !l8, !r8
      %e = simulation.logic.compare eq %m, %s : (!l8, !l8) -> !l1
      simulation.ref.store %e to %equal : !l1, !r1
      %l = simulation.logic.compare ult %m, %s : (!l8, !l8) -> !l1
      simulation.ref.store %l to %less : !l1, !r1
      %i = arith.constant 1 : i32
      %slot = simulation.ref.array_element %next[%i] : (!ra, i32) -> !r8
      simulation.ref.store %m to %slot : !l8, !r8
      %element = simulation.ref.array_element %memory[%i] : (!ra, i32) -> !r8
      simulation.ref.store %m to %element : !l8, !r8
      %n = simulation.ref.load %next : !ra -> !a
      simulation.ref.store %n to %regs : !a, !ra
      simulation.return
    }
  }
}

// CHECK-LABEL: steady-unknown @operators
// CHECK-NEXT: root 1: [4,8)
// CHECK-NEXT: root 3: [0,8)
// CHECK-NEXT: root 4: [0,4)
// CHECK-NEXT: root 6: [0,1)
// CHECK-NEXT: root 7: [4,8) [12,16) [20,24) [28,32)
// CHECK-NEXT: root 9: [4,8) [12,16) [20,24) [28,32)
// CHECK-NOT: root

// -----

// An indexed write reaches only the elements its index may select: the
// first loop writes element 0 of `packed`, so element 1 keeps its X. A loop
// whose index visits every element of `looped` on each run overwrites it
// like a whole-array write, and so do fixed writes of each element of
// `unrolled`: their initial zeros do not last. A private array starts from
// its X initializer, and `copied` takes the element the step leaves X.

!c = !simulation.context
!l4 = !simulation.logic<4>
!p = !simulation.packed_array<1 : 0 x !l4>
!u = !simulation.unpacked_array<0 : 1 x !l4>
!rp = !simulation.ref<!p>
!ru = !simulation.ref<!u>
!r4 = !simulation.ref<!l4>
module {
  simulation.design @lanes {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !p design
    simulation.storage.decl 1 in 0 : !u design
    simulation.storage.decl 2 in 0 : !u design
    simulation.storage.decl 3 in 0 : !u design
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.init"
    simulation.code_unit.decl 2 in 0 always_ff hierarchy "top.step"
    simulation.func @init(%ctx: !c {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %looped = simulation.context.storage %ctx[1] : !ru
      %unrolled = simulation.context.storage %ctx[2] : !ru
      %z = simulation.logic.constant 0 : i4, 0 : i4 : !l4
      %zeros = simulation.aggregate.splat %z : (!l4) -> !u
      simulation.ref.store %zeros to %looped : !u, !ru
      simulation.ref.store %zeros to %unrolled : !u, !ru
      simulation.return
    }
    simulation.func @step(%ctx: !c {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 5 : i32, code_unit_id = 2 : i64} {
      %packed = simulation.context.storage %ctx[0] : !rp
      %looped = simulation.context.storage %ctx[1] : !ru
      %unrolled = simulation.context.storage %ctx[2] : !ru
      %copied = simulation.context.storage %ctx[3] : !ru
      %k = simulation.logic.constant 5 : i4, 0 : i4 : !l4
      %x = simulation.logic.constant 0 : i4, -1 : i4 : !l4
      %c0 = arith.constant 0 : i32
      %c1 = arith.constant 1 : i32
      %c2 = arith.constant 2 : i32
      cf.br ^first(%c0 : i32)
    ^first(%i: i32):
      %more = arith.cmpi slt, %i, %c1 : i32
      cf.cond_br %more, ^firstBody, ^second(%c0 : i32)
    ^firstBody:
      %lane = simulation.ref.array_element %packed[%i] : (!rp, i32) -> !r4
      simulation.ref.store %k to %lane : !l4, !r4
      %i1 = arith.addi %i, %c1 : i32
      cf.br ^first(%i1 : i32)
    ^second(%j: i32):
      %again = arith.cmpi slt, %j, %c2 : i32
      cf.cond_br %again, ^secondBody, ^rest
    ^secondBody:
      %element = simulation.ref.array_element %looped[%j] : (!ru, i32) -> !r4
      simulation.ref.store %x to %element : !l4, !r4
      %j1 = arith.addi %j, %c1 : i32
      cf.br ^second(%j1 : i32)
    ^rest:
      %e0 = simulation.ref.subelement %unrolled[[0]] : !ru -> !r4
      simulation.ref.store %x to %e0 : !l4, !r4
      %e1 = simulation.ref.subelement %unrolled[[1]] : !ru -> !r4
      simulation.ref.store %x to %e1 : !l4, !r4
      %undefined = simulation.aggregate.default : !u
      %slot = simulation.ref.alloc %undefined : !u -> !ru
      %s0 = simulation.ref.subelement %slot[[0]] : !ru -> !r4
      simulation.ref.store %k to %s0 : !l4, !r4
      %whole = simulation.ref.load %slot : !ru -> !u
      simulation.ref.store %whole to %copied : !u, !ru
      simulation.return
    }
  }
}

// CHECK-LABEL: steady-unknown @lanes
// CHECK-NEXT: root 0: [4,8)
// CHECK-NEXT: root 1: [0,8)
// CHECK-NEXT: root 2: [0,8)
// CHECK-NEXT: root 3: [4,8)
// CHECK-NOT: root

// -----

// Alignment padding between array elements belongs to no value and holds no
// X/Z, whatever a value stored over it carries: only the X bits of
// element 1 stay X, in `next` and in its copy `regs`.

!c = !simulation.context
!l4 = !simulation.logic<4>
!u = !simulation.unpacked_array<0 : 1 x !l4, #simulation.storage_layout<size = 2, alignment = 1, stride = 1, offsets = []>>
!ru = !simulation.ref<!u>
!r4 = !simulation.ref<!l4>
module {
  simulation.design @padding {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !u design
    simulation.storage.decl 1 in 0 : !u design
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.init"
    simulation.code_unit.decl 2 in 0 always_ff hierarchy "top.step"
    simulation.func @init(%ctx: !c {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %regs = simulation.context.storage %ctx[1] : !ru
      %z = simulation.logic.constant 0 : i4, 0 : i4 : !l4
      %zeros = simulation.aggregate.splat %z : (!l4) -> !u
      simulation.ref.store %zeros to %regs : !u, !ru
      simulation.return
    }
    simulation.func @step(%ctx: !c {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 5 : i32, code_unit_id = 2 : i64} {
      %next = simulation.context.storage %ctx[0] : !ru
      %regs = simulation.context.storage %ctx[1] : !ru
      %k = simulation.logic.constant 5 : i4, 0 : i4 : !l4
      %x = simulation.logic.constant 0 : i4, -4 : i4 : !l4
      %e0 = simulation.ref.subelement %next[[0]] : !ru -> !r4
      simulation.ref.store %k to %e0 : !l4, !r4
      %e1 = simulation.ref.subelement %next[[1]] : !ru -> !r4
      simulation.ref.store %x to %e1 : !l4, !r4
      %n = simulation.ref.load %next : !ru -> !u
      simulation.ref.store %n to %regs : !u, !ru
      simulation.return
    }
  }
}

// CHECK-LABEL: steady-unknown @padding
// CHECK-NEXT: root 0: [10,12)
// CHECK-NEXT: root 1: [10,12)
// CHECK-NOT: root

// -----

// A private allocation's initializer, like a root's initial value, lasts
// only where the slot's own writes do not replace it: `full` takes the X
// stored into element 1 over the zero initializer, while `partial` keeps
// the zeros of the element its slot never writes.

!c = !simulation.context
!l4 = !simulation.logic<4>
!u = !simulation.unpacked_array<0 : 1 x !l4>
!ru = !simulation.ref<!u>
!r4 = !simulation.ref<!l4>
module {
  simulation.design @slots {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !u design
    simulation.storage.decl 1 in 0 : !u design
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "top.comb"
    simulation.func @comb(%ctx: !c {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %full = simulation.context.storage %ctx[0] : !ru
      %partial = simulation.context.storage %ctx[1] : !ru
      %z = simulation.logic.constant 0 : i4, 0 : i4 : !l4
      %zeros = simulation.aggregate.splat %z : (!l4) -> !u
      %k = simulation.logic.constant 5 : i4, 0 : i4 : !l4
      %x = simulation.logic.constant 0 : i4, -1 : i4 : !l4
      %a = simulation.ref.alloc %zeros : !u -> !ru
      %a0 = simulation.ref.subelement %a[[0]] : !ru -> !r4
      simulation.ref.store %k to %a0 : !l4, !r4
      %a1 = simulation.ref.subelement %a[[1]] : !ru -> !r4
      simulation.ref.store %x to %a1 : !l4, !r4
      %av = simulation.ref.load %a : !ru -> !u
      simulation.ref.store %av to %full : !u, !ru
      %b = simulation.ref.alloc %zeros : !u -> !ru
      %b0 = simulation.ref.subelement %b[[0]] : !ru -> !r4
      simulation.ref.store %x to %b0 : !l4, !r4
      %bv = simulation.ref.load %b : !ru -> !u
      simulation.ref.store %bv to %partial : !u, !ru
      simulation.return
    }
  }
}

// CHECK-LABEL: steady-unknown @slots
// CHECK-NEXT: root 0: [4,8)
// CHECK-NEXT: root 1: [0,4)
// CHECK-NOT: root
