// RUN: obelisk-opt %s --split-input-file --verify-diagnostics

// A coverage marker alone must not grant synchronous publication-observer
// privileges. The detached, primed, internal Fork contract is indivisible.
module {
  simulation.design @missing_detached {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.covergroup.decl @cg schema 1
    simulation.code_unit.decl 1 in 0 observer hierarchy "missing_detached.evaluate"
    simulation.code_unit.decl 2 in 0 fork hierarchy "missing_detached.owner"
    simulation.code_unit.decl 3 in 0 observer hierarchy "missing_detached.primary"
    simulation.func private @evaluate(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %handle: !simulation.covergroup_handle<@cg>
            {simulation.capture_kind = 2 : i32}) -> i1
        attributes {entry_kind = 14 : i32, code_unit_id = 1 : i64,
                    simulation.covergroup_event_sample_evaluator} {
      %true = arith.constant true
      simulation.return %true : i1
    }
    simulation.func private @primary(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<i1>
            {simulation.capture_kind = 2 : i32}) -> i1
        attributes {entry_kind = 14 : i32, code_unit_id = 3 : i64} {
      %value = simulation.ref.load %clock : !simulation.ref<i1> -> i1
      simulation.return %value : i1
    }
    simulation.func private @owner(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %handle: !simulation.covergroup_handle<@cg>
            {simulation.capture_kind = 2 : i32},
        %clock: !simulation.ref<i1>
            {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 13 : i32, code_unit_id = 2 : i64,
                    internal, schedule.covergroup_clocking_sampler,
                    schedule.prime_on_spawn} {
      %sampler = simulation.observer.bind @evaluate
          values(%handle : !simulation.covergroup_handle<@cg>) captures 1
          : !simulation.observer<i1>
      %initial = simulation.ref.load %clock : !simulation.ref<i1> -> i1
      %primary = simulation.observer.bind @primary
          values(%clock, %clock : !simulation.ref<i1>,
                 !simulation.ref<i1>) captures 1
          : !simulation.observer<i1>
      // expected-error @below {{requires a private detached primed covergroup clocking owner}}
      simulation.covergroup.clock_event.register %ctx, %handle
          events [%primary, %initial, %sampler] conditions 0 edges [1] indices [-1]
          {strobe = false}
          : !simulation.covergroup_handle<@cg>, !simulation.observer<i1>,
            i1, !simulation.observer<i1>
      simulation.suspend.forever to ^parked
    ^parked:
      simulation.return
    }
  }
}

// -----

// The sampler is a synchronous call target, not a deferred observer whose
// dependencies can independently wake a scheduler process.
module {
  simulation.design @observer_dependency {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.covergroup.decl @cg schema 1
    simulation.code_unit.decl 1 in 0 observer hierarchy "observer_dependency.evaluate"
    simulation.code_unit.decl 2 in 0 fork hierarchy "observer_dependency.owner"
    simulation.code_unit.decl 3 in 0 observer hierarchy "observer_dependency.primary"
    simulation.func private @evaluate(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %handle: !simulation.covergroup_handle<@cg>
            {simulation.capture_kind = 2 : i32}) -> i1
        attributes {entry_kind = 14 : i32, code_unit_id = 1 : i64,
                    simulation.covergroup_event_sample_evaluator} {
      %true = arith.constant true
      simulation.return %true : i1
    }
    simulation.func private @primary(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<i1>
            {simulation.capture_kind = 2 : i32}) -> i1
        attributes {entry_kind = 14 : i32, code_unit_id = 3 : i64} {
      %value = simulation.ref.load %clock : !simulation.ref<i1> -> i1
      simulation.return %value : i1
    }
    simulation.func private @owner(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %handle: !simulation.covergroup_handle<@cg>
            {simulation.capture_kind = 2 : i32},
        %clock: !simulation.ref<i1>
            {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 13 : i32, code_unit_id = 2 : i64,
                    internal, schedule.covergroup_clocking_sampler,
                    schedule.detached_controls,
                    schedule.prime_on_spawn} {
      %sampler = simulation.observer.bind @evaluate
          values(%handle, %clock : !simulation.covergroup_handle<@cg>,
                 !simulation.ref<i1>) captures 1
          : !simulation.observer<i1>
      %initial = simulation.ref.load %clock : !simulation.ref<i1> -> i1
      %primary = simulation.observer.bind @primary
          values(%clock, %clock : !simulation.ref<i1>,
                 !simulation.ref<i1>) captures 1
          : !simulation.observer<i1>
      // expected-error @below {{sampler must be a dependency-free i1 observer.bind token}}
      simulation.covergroup.clock_event.register %ctx, %handle
          events [%primary, %initial, %sampler] conditions 0 edges [1] indices [-1]
          {strobe = false}
          : !simulation.covergroup_handle<@cg>, !simulation.observer<i1>,
            i1, !simulation.observer<i1>
      simulation.suspend.forever to ^parked
    ^parked:
      simulation.return
    }
  }
}

// -----

// Every primary carries a construction-time value of exactly its observer
// result type. Physical bitcasts must not silently alter that contract.
module {
  simulation.design @initial_type_mismatch {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i8 design
    simulation.covergroup.decl @cg schema 1
    simulation.code_unit.decl 1 in 0 observer hierarchy "initial_type_mismatch.evaluate"
    simulation.code_unit.decl 2 in 0 fork hierarchy "initial_type_mismatch.owner"
    simulation.code_unit.decl 3 in 0 observer hierarchy "initial_type_mismatch.primary"
    simulation.func private @evaluate(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %handle: !simulation.covergroup_handle<@cg>
            {simulation.capture_kind = 2 : i32}) -> i1
        attributes {entry_kind = 14 : i32, code_unit_id = 1 : i64,
                    simulation.covergroup_event_sample_evaluator} {
      %true = arith.constant true
      simulation.return %true : i1
    }
    simulation.func private @primary(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<i8>
            {simulation.capture_kind = 2 : i32}) -> i8
        attributes {entry_kind = 14 : i32, code_unit_id = 3 : i64} {
      %value = simulation.ref.load %clock : !simulation.ref<i8> -> i8
      simulation.return %value : i8
    }
    simulation.func private @owner(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %handle: !simulation.covergroup_handle<@cg>
            {simulation.capture_kind = 2 : i32},
        %clock: !simulation.ref<i8>
            {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 13 : i32, code_unit_id = 2 : i64,
                    internal, schedule.covergroup_clocking_sampler,
                    schedule.detached_controls,
                    schedule.prime_on_spawn} {
      %sampler = simulation.observer.bind @evaluate
          values(%handle : !simulation.covergroup_handle<@cg>) captures 1
          : !simulation.observer<i1>
      %initial = arith.constant 0 : i1
      %primary = simulation.observer.bind @primary
          values(%clock, %clock : !simulation.ref<i8>,
                 !simulation.ref<i8>) captures 1
          : !simulation.observer<i8>
      // expected-error @below {{initial value #0 does not match its primary observer result}}
      simulation.covergroup.clock_event.register %ctx, %handle
          events [%primary, %initial, %sampler] conditions 0 edges [1] indices [-1]
          {strobe = false}
          : !simulation.covergroup_handle<@cg>, !simulation.observer<i8>,
            i1, !simulation.observer<i1>
      simulation.suspend.forever to ^parked
    ^parked:
      simulation.return
    }
  }
}

// -----

// The observer's first capture is the exact instance sampled at publication.
module {
  simulation.design @wrong_first_capture {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.covergroup.decl @cg schema 1
    simulation.code_unit.decl 1 in 0 observer hierarchy "wrong_first_capture.evaluate"
    simulation.code_unit.decl 2 in 0 fork hierarchy "wrong_first_capture.owner"
    simulation.code_unit.decl 3 in 0 observer hierarchy "wrong_first_capture.primary"
    simulation.func private @evaluate(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<i1>
            {simulation.capture_kind = 2 : i32}) -> i1
        attributes {entry_kind = 14 : i32, code_unit_id = 1 : i64,
                    simulation.covergroup_event_sample_evaluator} {
      %true = arith.constant true
      simulation.return %true : i1
    }
    simulation.func private @primary(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<i1>
            {simulation.capture_kind = 2 : i32}) -> i1
        attributes {entry_kind = 14 : i32, code_unit_id = 3 : i64} {
      %value = simulation.ref.load %clock : !simulation.ref<i1> -> i1
      simulation.return %value : i1
    }
    simulation.func private @owner(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %handle: !simulation.covergroup_handle<@cg>
            {simulation.capture_kind = 2 : i32},
        %clock: !simulation.ref<i1>
            {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 13 : i32, code_unit_id = 2 : i64,
                    internal, schedule.covergroup_clocking_sampler,
                    schedule.detached_controls,
                    schedule.prime_on_spawn} {
      %sampler = simulation.observer.bind @evaluate
          values(%clock : !simulation.ref<i1>) captures 1
          : !simulation.observer<i1>
      %initial = simulation.ref.load %clock : !simulation.ref<i1> -> i1
      %primary = simulation.observer.bind @primary
          values(%clock, %clock : !simulation.ref<i1>,
                 !simulation.ref<i1>) captures 1
          : !simulation.observer<i1>
      // expected-error @below {{sampler observer must capture the registered covergroup handle first}}
      simulation.covergroup.clock_event.register %ctx, %handle
          events [%primary, %initial, %sampler] conditions 0 edges [1] indices [-1]
          {strobe = false}
          : !simulation.covergroup_handle<@cg>, !simulation.observer<i1>,
            i1, !simulation.observer<i1>
      simulation.suspend.forever to ^parked
    ^parked:
      simulation.return
    }
  }
}

// -----

// A strobe registration can invoke this observer in the Postponed region.
// The observer must therefore satisfy the same transitive read-only contract
// as an ordinary Postponed code unit.
module {
  simulation.design @writing_evaluator {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.covergroup.decl @cg schema 1
    simulation.code_unit.decl 1 in 0 observer hierarchy "writing_evaluator.evaluate"
    simulation.code_unit.decl 2 in 0 fork hierarchy "writing_evaluator.owner"
    simulation.code_unit.decl 3 in 0 observer hierarchy "writing_evaluator.primary"
    simulation.func private @evaluate(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %handle: !simulation.covergroup_handle<@cg>
            {simulation.capture_kind = 2 : i32},
        %clock: !simulation.ref<i1>
            {simulation.capture_kind = 2 : i32}) -> i1
        attributes {entry_kind = 14 : i32, code_unit_id = 1 : i64,
                    simulation.covergroup_event_sample_evaluator,
                    simulation.covergroup_strobe_sample_evaluator} {
      %false = arith.constant false
      // expected-error @below {{is not permitted in a read-only postponed code unit}}
      simulation.ref.store %false to %clock : i1, !simulation.ref<i1>
      %true = arith.constant true
      simulation.return %true : i1
    }
    simulation.func private @primary(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<i1>
            {simulation.capture_kind = 2 : i32}) -> i1
        attributes {entry_kind = 14 : i32, code_unit_id = 3 : i64} {
      %value = simulation.ref.load %clock : !simulation.ref<i1> -> i1
      simulation.return %value : i1
    }
    simulation.func private @owner(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %handle: !simulation.covergroup_handle<@cg>
            {simulation.capture_kind = 2 : i32},
        %clock: !simulation.ref<i1>
            {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 13 : i32, code_unit_id = 2 : i64,
                    internal, schedule.covergroup_clocking_sampler,
                    schedule.detached_controls,
                    schedule.prime_on_spawn} {
      %sampler = simulation.observer.bind @evaluate
          values(%handle, %clock : !simulation.covergroup_handle<@cg>,
                 !simulation.ref<i1>) captures 2
          : !simulation.observer<i1>
      %initial = simulation.ref.load %clock : !simulation.ref<i1> -> i1
      %primary = simulation.observer.bind @primary
          values(%clock, %clock : !simulation.ref<i1>,
                 !simulation.ref<i1>) captures 1
          : !simulation.observer<i1>
      simulation.covergroup.clock_event.register %ctx, %handle
          events [%primary, %initial, %sampler] conditions 0 edges [1] indices [-1]
          {strobe = true}
          : !simulation.covergroup_handle<@cg>, !simulation.observer<i1>,
            i1, !simulation.observer<i1>
      simulation.suspend.forever to ^parked
    ^parked:
      simulation.return
    }
  }
}

// -----

// A non-strobe clock-event sample is ordinary procedural evaluation at the
// event instant. It must not inherit the Postponed region's read-only rule.
module {
  simulation.design @nonstrobe_writing_evaluator {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.covergroup.decl @cg schema 1
    simulation.code_unit.decl 1 in 0 observer hierarchy "nonstrobe_writing_evaluator.evaluate"
    simulation.code_unit.decl 2 in 0 fork hierarchy "nonstrobe_writing_evaluator.owner"
    simulation.code_unit.decl 3 in 0 observer hierarchy "nonstrobe_writing_evaluator.primary"
    simulation.func private @evaluate(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %handle: !simulation.covergroup_handle<@cg>
            {simulation.capture_kind = 2 : i32},
        %clock: !simulation.ref<i1>
            {simulation.capture_kind = 2 : i32}) -> i1
        attributes {entry_kind = 14 : i32, code_unit_id = 1 : i64,
                    simulation.covergroup_event_sample_evaluator} {
      %false = arith.constant false
      simulation.ref.store %false to %clock : i1, !simulation.ref<i1>
      %true = arith.constant true
      simulation.return %true : i1
    }
    simulation.func private @primary(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<i1>
            {simulation.capture_kind = 2 : i32}) -> i1
        attributes {entry_kind = 14 : i32, code_unit_id = 3 : i64} {
      %value = simulation.ref.load %clock : !simulation.ref<i1> -> i1
      simulation.return %value : i1
    }
    simulation.func private @owner(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %handle: !simulation.covergroup_handle<@cg>
            {simulation.capture_kind = 2 : i32},
        %clock: !simulation.ref<i1>
            {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 13 : i32, code_unit_id = 2 : i64,
                    internal, schedule.covergroup_clocking_sampler,
                    schedule.detached_controls,
                    schedule.prime_on_spawn} {
      %sampler = simulation.observer.bind @evaluate
          values(%handle, %clock : !simulation.covergroup_handle<@cg>,
                 !simulation.ref<i1>) captures 2
          : !simulation.observer<i1>
      %initial = simulation.ref.load %clock : !simulation.ref<i1> -> i1
      %primary = simulation.observer.bind @primary
          values(%clock, %clock : !simulation.ref<i1>,
                 !simulation.ref<i1>) captures 1
          : !simulation.observer<i1>
      simulation.covergroup.clock_event.register %ctx, %handle
          events [%primary, %initial, %sampler] conditions 0 edges [1] indices [-1]
          {strobe = false}
          : !simulation.covergroup_handle<@cg>, !simulation.observer<i1>,
            i1, !simulation.observer<i1>
      simulation.suspend.forever to ^parked
    ^parked:
      simulation.return
    }
  }
}
