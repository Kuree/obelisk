// RUN: talos-opt %s --split-input-file --pass-pipeline='builtin.module(schedule-version-known-regions)' | FileCheck %s

// Structural regressions for known-state versioning. The output must verify:
// every region keeps a single entry, values leaving a region merge only where
// both copies exit, and no reference is merged through a block argument.

!logic8 = !simulation.logic<8>
!memory = !simulation.unpacked_array<0 : 7 x !simulation.logic<8>>

module attributes {native.optimization_level = 3 : i32} {
  simulation.design @version_known_regions {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 function hierarchy "top.handle_live_out"
    simulation.code_unit.decl 2 in 0 function hierarchy "top.merge_addressed_read"
    simulation.code_unit.decl 3 in 0 function hierarchy "top.double_edge_exit"
    simulation.code_unit.decl 4 in 0 function hierarchy "top.shared_merge"
    simulation.code_unit.decl 5 in 0 initial hierarchy "top.process"
    simulation.code_unit.decl 6 in 0 function hierarchy "top.with_variant"
    simulation.code_unit.decl 7 in 0 function hierarchy "top.path_predicate"
    simulation.code_unit.decl 8 in 0 function hierarchy "top.writes_unknown"
    simulation.code_unit.decl 9 in 0 function hierarchy "top.reads_unknown"
    simulation.code_unit.decl 10 in 0 function hierarchy "top.merged_known"
    simulation.storage.decl 0 in 0 : !memory design hierarchy "top.mem"
    simulation.storage.decl 1 in 0 : !logic8 design hierarchy "top.a"
    simulation.storage.decl 2 in 0 : !logic8 design hierarchy "top.b"
    simulation.storage.decl 3 in 0 : !logic8 design hierarchy "top.q"
    simulation.storage.decl 4 in 0 : !logic8 design hierarchy "top.dont_care"
    simulation.storage.decl 5 in 0 : !logic8 design hierarchy "top.sink"

    // The NBA destination is a reference computed inside the run. Merging it
    // through the exit would hide its static root from NBA planning; it is
    // recomputed after the run instead, and only the logic value merges.
    // CHECK-LABEL: simulation.func @handle_live_out
    // CHECK-NOT: : !simulation.ref<!simulation.logic<8>>):
    // CHECK: {{^ *}}^[[EXIT:bb[0-9]+]](%[[VALUE:.*]]: !simulation.logic<8>):
    // CHECK-NEXT: %[[DEST:.*]] = simulation.context.storage %arg0[3]
    // CHECK-NEXT: simulation.nba.enqueue %[[VALUE]] to %[[DEST]]
    simulation.func @handle_live_out(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %a = simulation.context.storage %ctx[1] : !simulation.ref<!logic8>
      %x = simulation.ref.load %a : !simulation.ref<!logic8> -> !logic8
      %y = simulation.logic.binary add %x, %x : !logic8
      %z = simulation.logic.binary and %y, %x : !logic8
      %q = simulation.context.storage %ctx[3] : !simulation.ref<!logic8>
      simulation.nba.enqueue %z to %q : (!logic8, !simulation.ref<!logic8>) -> ()
      simulation.return
    }

    // A run entering the merge owns its argument, so a read addressed by the
    // argument cannot be checked where that run begins. The read starts its
    // own run, whose check recomputes the address from the argument. Original
    // blocks keep their positions.
    // CHECK-LABEL: simulation.func @merge_addressed_read
    // CHECK: {{^ *}}^bb1:
    // CHECK-NEXT: simulation.logic.binary add
    // CHECK: {{^ *}}^bb2:
    // CHECK-NEXT: simulation.logic.binary or
    // CHECK: {{^ *}}^bb3(%[[M:.*]]: !simulation.logic<8>):
    // CHECK: %[[LOW:.*]] = simulation.logic.extract %[[M]] from 0
    // CHECK: %[[BITS:.*]] = simulation.logic.to_bits %[[LOW]]
    // CHECK: %[[INDEX:.*]] = arith.extui %[[BITS]]
    // CHECK: %[[SLOT:.*]] = simulation.ref.array_element %{{.*}}[%[[INDEX]]]
    // CHECK: %[[PROBE:.*]] = simulation.ref.load %[[SLOT]]
    // CHECK: schedule.known.value %[[PROBE]]
    // CHECK: cf.cond_br %{{.*}} weights([1048575, 1])
    simulation.func @merge_addressed_read(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %a = simulation.context.storage %ctx[1] : !simulation.ref<!logic8>
      %v = simulation.ref.load %a : !simulation.ref<!logic8> -> !logic8
      %bit = simulation.logic.extract %v from 0 : !logic8 -> !simulation.logic<1>
      %taken = simulation.logic.is_true %bit : !simulation.logic<1>
      cf.cond_br %taken, ^left, ^right
    ^left:
      %p = simulation.logic.binary add %v, %v : !logic8
      cf.br ^merge(%p : !logic8)
    ^right:
      %r = simulation.logic.binary or %v, %v : !logic8
      cf.br ^merge(%r : !logic8)
    ^merge(%m: !logic8):
      %low = simulation.logic.extract %m from 0 : !logic8 -> !simulation.logic<3>
      %bits = simulation.logic.to_bits %low : !simulation.logic<3> -> i3
      %index = arith.extui %bits : i3 to i65
      %mem = simulation.context.storage %ctx[0] : !simulation.ref<!memory>
      %slot = simulation.ref.array_element %mem[%index] : (!simulation.ref<!memory>, i65) -> !simulation.ref<!logic8>
      %e = simulation.ref.load %slot : !simulation.ref<!logic8> -> !logic8
      %f = simulation.logic.binary add %e, %e : !logic8
      %g = simulation.logic.binary and %f, %e : !logic8
      %sink = simulation.context.storage %ctx[5] : !simulation.ref<!logic8>
      simulation.ref.store %g to %sink : !logic8, !simulation.ref<!logic8>
      simulation.return
    }

    // Both edges of one branch enter the run's exit. A value used after the
    // run merges with one operand per edge, and the next run checks that
    // merge rather than the original it replaced.
    // CHECK-LABEL: simulation.func @double_edge_exit
    // CHECK: {{^ *}}^[[JOIN:bb[0-9]+]](%[[ARG:.*]]: !simulation.logic<8>, %[[MERGED:.*]]: !simulation.logic<8>):
    // CHECK-NEXT: simulation.time.now
    // CHECK-NEXT: schedule.known.value %[[ARG]]
    // CHECK-NEXT: schedule.known.value %[[MERGED]]
    // CHECK-COUNT-2: cf.cond_br %{{.*}}, ^[[JOIN]](%[[S:[0-9]+]], %[[M:[0-9]+]] : !simulation.logic<8>, !simulation.logic<8>), ^[[JOIN]](%[[M]], %[[M]] : !simulation.logic<8>, !simulation.logic<8>)
    simulation.func @double_edge_exit(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> !logic8
        attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %a = simulation.context.storage %ctx[1] : !simulation.ref<!logic8>
      %v = simulation.ref.load %a : !simulation.ref<!logic8> -> !logic8
      %s = simulation.logic.binary add %v, %v : !logic8
      %m = simulation.logic.binary and %s, %v : !logic8
      %bit = simulation.logic.extract %m from 0 : !logic8 -> !simulation.logic<1>
      %taken = simulation.logic.is_true %bit : !simulation.logic<1>
      cf.cond_br %taken, ^join(%s : !logic8), ^join(%m : !logic8)
    ^join(%j: !logic8):
      %now = simulation.time.now %ctx
      %u = simulation.logic.binary or %j, %m : !logic8
      %w = simulation.logic.binary add %u, %m : !logic8
      simulation.return %w : !logic8
    }

    // Two arms merge at ^merge, which neither dominates; a third arm
    // bypasses it. Each arm's run ends where the merge begins, and the run
    // starting there is versioned once, after the arms, by the chain of the
    // merge's dominator.
    // CHECK-LABEL: simulation.func @shared_merge
    // CHECK-COUNT-2: simulation.logic.binary xor
    // CHECK-NOT: simulation.logic.binary xor
    simulation.func @shared_merge(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %a = simulation.context.storage %ctx[1] : !simulation.ref<!logic8>
      %b = simulation.context.storage %ctx[2] : !simulation.ref<!logic8>
      %v = simulation.ref.load %a : !simulation.ref<!logic8> -> !logic8
      %bit0 = simulation.logic.extract %v from 0 : !logic8 -> !simulation.logic<1>
      %first = simulation.logic.is_true %bit0 : !simulation.logic<1>
      cf.cond_br %first, ^one, ^choose
    ^choose:
      %bit1 = simulation.logic.extract %v from 1 : !logic8 -> !simulation.logic<1>
      %second = simulation.logic.is_true %bit1 : !simulation.logic<1>
      cf.cond_br %second, ^two, ^bypass
    ^one:
      %x1 = simulation.logic.binary add %v, %v : !logic8
      cf.br ^merge(%x1 : !logic8)
    ^two:
      %x2 = simulation.logic.binary or %v, %v : !logic8
      cf.br ^merge(%x2 : !logic8)
    ^bypass:
      %now = simulation.time.now %ctx
      cf.br ^done
    ^merge(%y: !logic8):
      %w = simulation.ref.load %b : !simulation.ref<!logic8> -> !logic8
      %z = simulation.logic.binary xor %y, %w : !logic8
      %t = simulation.logic.binary and %z, %w : !logic8
      simulation.ref.store %t to %b : !logic8, !simulation.ref<!logic8>
      cf.br ^done
    ^done:
      simulation.return
    }

    // A value the facts prove known merges with its copy into a value known
    // on both paths, so the next run checks only the four-state merge.
    // CHECK-LABEL: simulation.func @merged_known
    // CHECK: {{^ *}}^bb2(%{{.*}}: !simulation.logic<8>, %[[S:.*]]: !simulation.logic<8>):
    // CHECK-NEXT: simulation.time.now
    // CHECK-NEXT: %[[KNOWN:.*]] = schedule.known.value %[[S]]
    // CHECK-NEXT: cf.cond_br %[[KNOWN]]
    simulation.func @merged_known(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> !logic8
        attributes {entry_kind = 8 : i32, code_unit_id = 10 : i64} {
      %a = simulation.context.storage %ctx[1] : !simulation.ref<!logic8>
      %x = simulation.ref.load %a : !simulation.ref<!logic8> -> !logic8
      %c = arith.constant 5 : i8
      %k = simulation.logic.from_bits %c : i8 -> !logic8
      %y = simulation.logic.binary add %x, %k : !logic8
      %s = simulation.logic.binary and %y, %x : !logic8
      %now = simulation.time.now %ctx
      %u = simulation.logic.binary or %s, %k : !logic8
      %w = simulation.logic.binary add %u, %k : !logic8
      simulation.return %w : !logic8
    }

    // A process body runs its code once; only function bodies are versioned.
    // CHECK-LABEL: simulation.func @process
    // CHECK-NOT: schedule.known.value
    // CHECK: simulation.return
    simulation.func @process(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 5 : i64} {
      %a = simulation.context.storage %ctx[1] : !simulation.ref<!logic8>
      %x = simulation.ref.load %a : !simulation.ref<!logic8> -> !logic8
      %y = simulation.logic.binary add %x, %x : !logic8
      %z = simulation.logic.binary and %y, %x : !logic8
      simulation.ref.store %z to %a : !logic8, !simulation.ref<!logic8>
      simulation.return
    }

    // A source whose two-state variant carries the steady state, and a path
    // predicate, are left unchanged.
    // CHECK-LABEL: simulation.func @with_variant
    // CHECK-NOT: schedule.known.value
    // CHECK: simulation.return
    simulation.func @with_variant(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 6 : i64,
                    schedule.eval.two_state_variant = @handle_live_out} {
      %a = simulation.context.storage %ctx[1] : !simulation.ref<!logic8>
      %x = simulation.ref.load %a : !simulation.ref<!logic8> -> !logic8
      %y = simulation.logic.binary add %x, %x : !logic8
      %z = simulation.logic.binary and %y, %x : !logic8
      simulation.ref.store %z to %a : !logic8, !simulation.ref<!logic8>
      simulation.return
    }
    // CHECK-LABEL: simulation.func @path_predicate
    // CHECK-NOT: schedule.known.value
    // CHECK: simulation.return
    simulation.func @path_predicate(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 7 : i64,
                    schedule.eval.path_known_predicate} {
      %a = simulation.context.storage %ctx[1] : !simulation.ref<!logic8>
      %x = simulation.ref.load %a : !simulation.ref<!logic8> -> !logic8
      %y = simulation.logic.binary add %x, %x : !logic8
      %z = simulation.logic.binary and %y, %x : !logic8
      simulation.ref.store %z to %a : !logic8, !simulation.ref<!logic8>
      simulation.return
    }

    // A variable the design assigns X/Z is expected to hold it at run time,
    // so code reading it would pay for a failing check and stays four-state.
    // CHECK-LABEL: simulation.func @writes_unknown
    // CHECK-LABEL: simulation.func @reads_unknown
    // CHECK-NOT: schedule.known.value
    // CHECK: simulation.return
    simulation.func @writes_unknown(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 8 : i64} {
      %d = simulation.context.storage %ctx[4] : !simulation.ref<!logic8>
      %x = simulation.logic.constant 0 : i8, -1 : i8 : !logic8
      simulation.ref.store %x to %d : !logic8, !simulation.ref<!logic8>
      simulation.return
    }
    simulation.func @reads_unknown(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 9 : i64} {
      %d = simulation.context.storage %ctx[4] : !simulation.ref<!logic8>
      %sink = simulation.context.storage %ctx[5] : !simulation.ref<!logic8>
      %x = simulation.ref.load %d : !simulation.ref<!logic8> -> !logic8
      %y = simulation.logic.binary add %x, %x : !logic8
      %z = simulation.logic.binary and %y, %x : !logic8
      simulation.ref.store %z to %sink : !logic8, !simulation.ref<!logic8>
      simulation.return
    }
  }
}

// -----

// Packed lowering skips the optional whole-design solve above the function
// limit unless a guarded variant needs it, and without that solve a copy is
// no better than the original, so such a design is left unchanged.
module attributes {native.optimization_level = 3 : i32,
                   native.max_state_domain_functions = 0 : i64} {
  simulation.design @over_limit {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 function hierarchy "top.over_limit"
    simulation.storage.decl 0 in 0 : !simulation.logic<8> design hierarchy "top.a"
    // CHECK-LABEL: simulation.func @over_limit
    // CHECK-NOT: schedule.known.value
    // CHECK: simulation.return
    simulation.func @over_limit(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %a = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<8>>
      %x = simulation.ref.load %a : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %y = simulation.logic.binary add %x, %x : !simulation.logic<8>
      %z = simulation.logic.binary and %y, %x : !simulation.logic<8>
      simulation.ref.store %z to %a : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.return
    }
  }
}
