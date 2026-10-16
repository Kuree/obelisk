// RUN: obelisk-opt %s --obelisk-sim-compile-random-plans | obelisk-opt --test-obelisk-print-random-programs -o /dev/null 2>&1 | FileCheck %s
// REQUIRES: compiled-sampler

// The sampler plan compiler prepares one decision diagram per set of random
// fields that an atom's top-level conjuncts read, so the target still sees
// which conjuncts read disjoint inputs and samples them apart (IEEE 1800-2023
// 18.5.9 permits any solving order with the same outcome). One diagram over
// interleaved independent fields would multiply their widths.

// a0 (bits 0-7) < 100 && a1 (bits 8-15) < 100: one diagram per element.
// CHECK-LABEL: simulation.random.solve: version 1 width 16
// CHECK: plan root 7 conjuncts 2
// CHECK-NOT: plan free

// A size s (bits 0-3) guards d0 (bits 4-7) and d1 (bits 8-11), as in the
// shadow of a dynamic array: (0 < s) -> d0 < 5 && (1 < s) -> d1 < 5. The
// guarded constraints share only the size, so each keeps its own diagram, and
// each records free fields: eliminating its element (or the size) alone
// leaves a true projection, so projecting onto a size solved first
// (18.5.7.1) needs no work at run time.
// CHECK-LABEL: simulation.random.solve: version 1 width 12
// CHECK: plan root 15 conjuncts 2
// CHECK-NEXT: plan free root 15 conjunct 0 offset 0 width 4
// CHECK-NEXT: plan free root 15 conjunct 0 offset 4 width 4
// CHECK-NEXT: plan free root 15 conjunct 1 offset 0 width 4
// CHECK-NEXT: plan free root 15 conjunct 1 offset 8 width 4

module {
  func.func @independent(%ctx: !simulation.context) {
    %zero = arith.constant 0 : i64
    %one = arith.constant 1 : i64
    %mask = arith.constant 65535 : i64
    %r:3 = simulation.random.solve %ctx from %zero mutable %mask constraints %zero limit %one state %zero increment %one captures() "\4F\44\52\31\01\00\18\00\10\00\00\00\08\00\00\00\00\00\00\00\20\00\00\00\01\08\00\00\00\00\00\00\00\00\00\00\00\00\00\00\22\08\00\00\00\00\00\00\64\00\00\00\00\00\00\00\1A\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\01\08\00\00\08\00\00\00\00\00\00\00\00\00\00\00\22\08\00\00\00\00\00\00\64\00\00\00\00\00\00\00\1A\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\1B\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\20\01\00\00\FF\FF\FF\FF\00\00\00\00\00\00\00\00" : !simulation.context
    return
  }
  func.func @guarded(%ctx: !simulation.context) {
    %zero = arith.constant 0 : i64
    %one = arith.constant 1 : i64
    %mask = arith.constant 4095 : i64
    %r:3 = simulation.random.solve %ctx from %zero mutable %mask constraints %zero limit %one state %zero increment %one captures() "\4F\44\52\31\01\00\18\00\0C\00\00\00\10\00\00\00\00\00\00\00\20\00\00\00\22\04\00\00\00\00\00\00\00\00\00\00\00\00\00\00\01\04\00\00\00\00\00\00\00\00\00\00\00\00\00\00\1A\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\01\04\00\00\04\00\00\00\00\00\00\00\00\00\00\00\22\04\00\00\00\00\00\00\05\00\00\00\00\00\00\00\1A\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\1D\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\22\04\00\00\00\00\00\00\01\00\00\00\00\00\00\00\01\04\00\00\00\00\00\00\00\00\00\00\00\00\00\00\1A\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\01\04\00\00\08\00\00\00\00\00\00\00\00\00\00\00\22\04\00\00\00\00\00\00\05\00\00\00\00\00\00\00\1A\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\1D\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\1B\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\20\01\00\00\FF\FF\FF\FF\00\00\00\00\00\00\00\00" : !simulation.context
    return
  }
}
