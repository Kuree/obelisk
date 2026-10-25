// RUN: obelisk-opt %s -o %t.original
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.func(obelisk-sim-unroll-bounded-loops))' -o %t.after && diff -u %t.original %t.after

// Every rejected candidate must preserve the exact original CFG, including
// initializer edges. No speculative preheader may remain after a failed
// initializer, step, finite-control, fixed-width or SSA proof.
!counter = !simulation.logic<8>
module {
  // An initial activation and a resume need not use the same initializer.
  simulation.func @different_reset(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<!counter> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 3 : i32} {
    %zero = simulation.logic.constant 0 : i8, 0 : i8 : !counter
    cf.br ^head(%zero : !counter)
  ^head(%i: !counter):
    %limit = simulation.logic.constant 2 : i8, 0 : i8 : !counter
    %comparison = simulation.logic.compare sle %i, %limit : (!counter, !counter) -> !simulation.logic<1>
    %test = simulation.logic.is_true %comparison : !simulation.logic<1>
    cf.cond_br %test, ^body, ^wait
  ^body:
    simulation.ref.store %i to %out : !counter, !simulation.ref<!counter>
    %step = simulation.logic.constant 1 : i8, 0 : i8 : !counter
    %next = simulation.logic.binary add %i, %step : !counter
    cf.br ^head(%next : !counter)
  ^wait:
    %delay = simulation.time.constant 1
    simulation.suspend.delay %delay to ^resume
  ^resume:
    %restart = simulation.logic.constant 1 : i8, 0 : i8 : !counter
    cf.br ^head(%restart : !counter)
  }

  // An unknown reset is not an integer initializer (LRM 12.7.1).
  simulation.func @unknown_reset(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<!counter> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 3 : i32} {
    %zero = simulation.logic.constant 0 : i8, 0 : i8 : !counter
    cf.br ^head(%zero : !counter)
  ^head(%i: !counter):
    %limit = simulation.logic.constant 2 : i8, 0 : i8 : !counter
    %comparison = simulation.logic.compare sle %i, %limit : (!counter, !counter) -> !simulation.logic<1>
    %test = simulation.logic.is_true %comparison : !simulation.logic<1>
    cf.cond_br %test, ^body, ^wait
  ^body:
    simulation.ref.store %i to %out : !counter, !simulation.ref<!counter>
    %step = simulation.logic.constant 1 : i8, 0 : i8 : !counter
    %next = simulation.logic.binary add %i, %step : !counter
    cf.br ^head(%next : !counter)
  ^wait:
    %delay = simulation.time.constant 1
    simulation.suspend.delay %delay to ^resume
  ^resume:
    %restart = simulation.logic.constant 0 : i8, 1 : i8 : !counter
    cf.br ^head(%restart : !counter)
  }

  // A dynamic reset must remain a load at each resume, not a captured value.
  simulation.func @loaded_reset(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<!counter> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 3 : i32} {
    %zero = simulation.logic.constant 0 : i8, 0 : i8 : !counter
    cf.br ^head(%zero : !counter)
  ^head(%i: !counter):
    %limit = simulation.logic.constant 2 : i8, 0 : i8 : !counter
    %comparison = simulation.logic.compare sle %i, %limit : (!counter, !counter) -> !simulation.logic<1>
    %test = simulation.logic.is_true %comparison : !simulation.logic<1>
    cf.cond_br %test, ^body, ^wait
  ^body:
    simulation.ref.store %i to %out : !counter, !simulation.ref<!counter>
    %step = simulation.logic.constant 1 : i8, 0 : i8 : !counter
    %next = simulation.logic.binary add %i, %step : !counter
    cf.br ^head(%next : !counter)
  ^wait:
    %delay = simulation.time.constant 1
    simulation.suspend.delay %delay to ^resume
  ^resume:
    %restart = simulation.ref.load %out : !simulation.ref<!counter> -> !counter
    cf.br ^head(%restart : !counter)
  }

  // Common known initializers do not justify a step containing Z.
  simulation.func @unknown_step(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<!counter> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 3 : i32} {
    %zero = simulation.logic.constant 0 : i8, 0 : i8 : !counter
    cf.br ^head(%zero : !counter)
  ^head(%i: !counter):
    %limit = simulation.logic.constant 2 : i8, 0 : i8 : !counter
    %comparison = simulation.logic.compare sle %i, %limit : (!counter, !counter) -> !simulation.logic<1>
    %test = simulation.logic.is_true %comparison : !simulation.logic<1>
    cf.cond_br %test, ^body, ^wait
  ^body:
    simulation.ref.store %i to %out : !counter, !simulation.ref<!counter>
    %step = simulation.logic.constant 1 : i8, 1 : i8 : !counter
    %next = simulation.logic.binary add %i, %step : !counter
    cf.br ^head(%next : !counter)
  ^wait:
    %delay = simulation.time.constant 1
    simulation.suspend.delay %delay to ^resume
  ^resume:
    %restart = simulation.logic.constant 0 : i8, 0 : i8 : !counter
    cf.br ^head(%restart : !counter)
  }

  // The final stride wraps before the next test (LRM 11.6.1/12.7.1).
  simulation.func @signed_overflow(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<!counter> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 3 : i32} {
    %start = simulation.logic.constant 126 : i8, 0 : i8 : !counter
    cf.br ^head(%start : !counter)
  ^head(%i: !counter):
    %limit = simulation.logic.constant 127 : i8, 0 : i8 : !counter
    %comparison = simulation.logic.compare slt %i, %limit : (!counter, !counter) -> !simulation.logic<1>
    %test = simulation.logic.is_true %comparison : !simulation.logic<1>
    cf.cond_br %test, ^body, ^wait
  ^body:
    simulation.ref.store %i to %out : !counter, !simulation.ref<!counter>
    %step = simulation.logic.constant 2 : i8, 0 : i8 : !counter
    %next = simulation.logic.binary add %i, %step : !counter
    cf.br ^head(%next : !counter)
  ^wait:
    %delay = simulation.time.constant 1
    simulation.suspend.delay %delay to ^resume
  ^resume:
    %restart = simulation.logic.constant 126 : i8, 0 : i8 : !counter
    cf.br ^head(%restart : !counter)
  }

  // A suspension inside the procedural body cannot become a finite native
  // recurrence just because a separate suspension follows the loop.
  simulation.func @body_suspension(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<!counter> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 3 : i32} {
    %zero = simulation.logic.constant 0 : i8, 0 : i8 : !counter
    cf.br ^head(%zero : !counter)
  ^head(%i: !counter):
    %limit = simulation.logic.constant 2 : i8, 0 : i8 : !counter
    %comparison = simulation.logic.compare sle %i, %limit : (!counter, !counter) -> !simulation.logic<1>
    %test = simulation.logic.is_true %comparison : !simulation.logic<1>
    cf.cond_br %test, ^body, ^wait
  ^body:
    %body_delay = simulation.time.constant 1
    simulation.suspend.delay %body_delay to ^write
  ^write:
    simulation.ref.store %i to %out : !counter, !simulation.ref<!counter>
    %step = simulation.logic.constant 1 : i8, 0 : i8 : !counter
    %next = simulation.logic.binary add %i, %step : !counter
    cf.br ^head(%next : !counter)
  ^wait:
    %delay = simulation.time.constant 1
    simulation.suspend.delay %delay to ^resume
  ^resume:
    %restart = simulation.logic.constant 0 : i8, 0 : i8 : !counter
    cf.br ^head(%restart : !counter)
  }

  // The header argument has an unrepaired direct use outside the loop.
  simulation.func @escaping_induction(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<!counter> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 3 : i32} {
    %zero = simulation.logic.constant 0 : i8, 0 : i8 : !counter
    cf.br ^head(%zero : !counter)
  ^head(%i: !counter):
    %limit = simulation.logic.constant 2 : i8, 0 : i8 : !counter
    %comparison = simulation.logic.compare sle %i, %limit : (!counter, !counter) -> !simulation.logic<1>
    %test = simulation.logic.is_true %comparison : !simulation.logic<1>
    cf.cond_br %test, ^body, ^wait
  ^body:
    simulation.ref.store %i to %out : !counter, !simulation.ref<!counter>
    %step = simulation.logic.constant 1 : i8, 0 : i8 : !counter
    %next = simulation.logic.binary add %i, %step : !counter
    cf.br ^head(%next : !counter)
  ^wait:
    simulation.ref.store %i to %out : !counter, !simulation.ref<!counter>
    %delay = simulation.time.constant 1
    simulation.suspend.delay %delay to ^resume
  ^resume:
    %restart = simulation.logic.constant 0 : i8, 0 : i8 : !counter
    cf.br ^head(%restart : !counter)
  }
}
