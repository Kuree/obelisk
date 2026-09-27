// RUN: obelisk-opt %s --split-input-file --verify-diagnostics

// A coverage marker alone must not grant synchronous publication-observer
// privileges. The detached, primed, internal Fork contract is indivisible.
module {
  obelisk_sim.design @missing_detached {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : i1 design
    obelisk_sim.covergroup.decl @cg schema 1
    obelisk_sim.code_unit.decl 1 in 0 observer hierarchy "missing_detached.evaluate"
    obelisk_sim.code_unit.decl 2 in 0 fork hierarchy "missing_detached.owner"
    obelisk_sim.code_unit.decl 3 in 0 observer hierarchy "missing_detached.primary"
    obelisk_sim.func private @evaluate(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %handle: !obelisk_sim.covergroup_handle<@cg>
            {obelisk_sim.capture_kind = 2 : i32}) -> i1
        attributes {entry_kind = 14 : i32, code_unit_id = 1 : i64,
                    obelisk_sim.covergroup_event_sample_evaluator} {
      %true = arith.constant true
      obelisk_sim.return %true : i1
    }
    obelisk_sim.func private @primary(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<i1>
            {obelisk_sim.capture_kind = 2 : i32}) -> i1
        attributes {entry_kind = 14 : i32, code_unit_id = 3 : i64} {
      %value = obelisk_sim.ref.load %clock : !obelisk_sim.ref<i1> -> i1
      obelisk_sim.return %value : i1
    }
    obelisk_sim.func private @owner(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %handle: !obelisk_sim.covergroup_handle<@cg>
            {obelisk_sim.capture_kind = 2 : i32},
        %clock: !obelisk_sim.ref<i1>
            {obelisk_sim.capture_kind = 2 : i32})
        attributes {entry_kind = 13 : i32, code_unit_id = 2 : i64,
                    internal, schedule.covergroup_clocking_sampler,
                    schedule.prime_on_spawn} {
      %sampler = obelisk_sim.observer.bind @evaluate
          values(%handle : !obelisk_sim.covergroup_handle<@cg>) captures 1
          : !obelisk_sim.observer<i1>
      %initial = obelisk_sim.ref.load %clock : !obelisk_sim.ref<i1> -> i1
      %primary = obelisk_sim.observer.bind @primary
          values(%clock, %clock : !obelisk_sim.ref<i1>,
                 !obelisk_sim.ref<i1>) captures 1
          : !obelisk_sim.observer<i1>
      // expected-error @below {{requires a private detached primed covergroup clocking owner}}
      obelisk_sim.covergroup.clock_event.register %ctx, %handle
          events [%primary, %initial, %sampler] conditions 0 edges [1] indices [-1]
          {strobe = false}
          : !obelisk_sim.covergroup_handle<@cg>, !obelisk_sim.observer<i1>,
            i1, !obelisk_sim.observer<i1>
      obelisk_sim.suspend.forever to ^parked
    ^parked:
      obelisk_sim.return
    }
  }
}

// -----

// The sampler is a synchronous call target, not a deferred observer whose
// dependencies can independently wake a scheduler process.
module {
  obelisk_sim.design @observer_dependency {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : i1 design
    obelisk_sim.covergroup.decl @cg schema 1
    obelisk_sim.code_unit.decl 1 in 0 observer hierarchy "observer_dependency.evaluate"
    obelisk_sim.code_unit.decl 2 in 0 fork hierarchy "observer_dependency.owner"
    obelisk_sim.code_unit.decl 3 in 0 observer hierarchy "observer_dependency.primary"
    obelisk_sim.func private @evaluate(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %handle: !obelisk_sim.covergroup_handle<@cg>
            {obelisk_sim.capture_kind = 2 : i32}) -> i1
        attributes {entry_kind = 14 : i32, code_unit_id = 1 : i64,
                    obelisk_sim.covergroup_event_sample_evaluator} {
      %true = arith.constant true
      obelisk_sim.return %true : i1
    }
    obelisk_sim.func private @primary(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<i1>
            {obelisk_sim.capture_kind = 2 : i32}) -> i1
        attributes {entry_kind = 14 : i32, code_unit_id = 3 : i64} {
      %value = obelisk_sim.ref.load %clock : !obelisk_sim.ref<i1> -> i1
      obelisk_sim.return %value : i1
    }
    obelisk_sim.func private @owner(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %handle: !obelisk_sim.covergroup_handle<@cg>
            {obelisk_sim.capture_kind = 2 : i32},
        %clock: !obelisk_sim.ref<i1>
            {obelisk_sim.capture_kind = 2 : i32})
        attributes {entry_kind = 13 : i32, code_unit_id = 2 : i64,
                    internal, schedule.covergroup_clocking_sampler,
                    schedule.detached_controls,
                    schedule.prime_on_spawn} {
      %sampler = obelisk_sim.observer.bind @evaluate
          values(%handle, %clock : !obelisk_sim.covergroup_handle<@cg>,
                 !obelisk_sim.ref<i1>) captures 1
          : !obelisk_sim.observer<i1>
      %initial = obelisk_sim.ref.load %clock : !obelisk_sim.ref<i1> -> i1
      %primary = obelisk_sim.observer.bind @primary
          values(%clock, %clock : !obelisk_sim.ref<i1>,
                 !obelisk_sim.ref<i1>) captures 1
          : !obelisk_sim.observer<i1>
      // expected-error @below {{sampler must be a dependency-free i1 observer.bind token}}
      obelisk_sim.covergroup.clock_event.register %ctx, %handle
          events [%primary, %initial, %sampler] conditions 0 edges [1] indices [-1]
          {strobe = false}
          : !obelisk_sim.covergroup_handle<@cg>, !obelisk_sim.observer<i1>,
            i1, !obelisk_sim.observer<i1>
      obelisk_sim.suspend.forever to ^parked
    ^parked:
      obelisk_sim.return
    }
  }
}

// -----

// Every primary carries a construction-time value of exactly its observer
// result type. Physical bitcasts must not silently alter that contract.
module {
  obelisk_sim.design @initial_type_mismatch {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : i8 design
    obelisk_sim.covergroup.decl @cg schema 1
    obelisk_sim.code_unit.decl 1 in 0 observer hierarchy "initial_type_mismatch.evaluate"
    obelisk_sim.code_unit.decl 2 in 0 fork hierarchy "initial_type_mismatch.owner"
    obelisk_sim.code_unit.decl 3 in 0 observer hierarchy "initial_type_mismatch.primary"
    obelisk_sim.func private @evaluate(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %handle: !obelisk_sim.covergroup_handle<@cg>
            {obelisk_sim.capture_kind = 2 : i32}) -> i1
        attributes {entry_kind = 14 : i32, code_unit_id = 1 : i64,
                    obelisk_sim.covergroup_event_sample_evaluator} {
      %true = arith.constant true
      obelisk_sim.return %true : i1
    }
    obelisk_sim.func private @primary(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<i8>
            {obelisk_sim.capture_kind = 2 : i32}) -> i8
        attributes {entry_kind = 14 : i32, code_unit_id = 3 : i64} {
      %value = obelisk_sim.ref.load %clock : !obelisk_sim.ref<i8> -> i8
      obelisk_sim.return %value : i8
    }
    obelisk_sim.func private @owner(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %handle: !obelisk_sim.covergroup_handle<@cg>
            {obelisk_sim.capture_kind = 2 : i32},
        %clock: !obelisk_sim.ref<i8>
            {obelisk_sim.capture_kind = 2 : i32})
        attributes {entry_kind = 13 : i32, code_unit_id = 2 : i64,
                    internal, schedule.covergroup_clocking_sampler,
                    schedule.detached_controls,
                    schedule.prime_on_spawn} {
      %sampler = obelisk_sim.observer.bind @evaluate
          values(%handle : !obelisk_sim.covergroup_handle<@cg>) captures 1
          : !obelisk_sim.observer<i1>
      %initial = arith.constant 0 : i1
      %primary = obelisk_sim.observer.bind @primary
          values(%clock, %clock : !obelisk_sim.ref<i8>,
                 !obelisk_sim.ref<i8>) captures 1
          : !obelisk_sim.observer<i8>
      // expected-error @below {{initial value #0 does not match its primary observer result}}
      obelisk_sim.covergroup.clock_event.register %ctx, %handle
          events [%primary, %initial, %sampler] conditions 0 edges [1] indices [-1]
          {strobe = false}
          : !obelisk_sim.covergroup_handle<@cg>, !obelisk_sim.observer<i8>,
            i1, !obelisk_sim.observer<i1>
      obelisk_sim.suspend.forever to ^parked
    ^parked:
      obelisk_sim.return
    }
  }
}

// -----

// The observer's first capture is the exact instance sampled at publication.
module {
  obelisk_sim.design @wrong_first_capture {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : i1 design
    obelisk_sim.covergroup.decl @cg schema 1
    obelisk_sim.code_unit.decl 1 in 0 observer hierarchy "wrong_first_capture.evaluate"
    obelisk_sim.code_unit.decl 2 in 0 fork hierarchy "wrong_first_capture.owner"
    obelisk_sim.code_unit.decl 3 in 0 observer hierarchy "wrong_first_capture.primary"
    obelisk_sim.func private @evaluate(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<i1>
            {obelisk_sim.capture_kind = 2 : i32}) -> i1
        attributes {entry_kind = 14 : i32, code_unit_id = 1 : i64,
                    obelisk_sim.covergroup_event_sample_evaluator} {
      %true = arith.constant true
      obelisk_sim.return %true : i1
    }
    obelisk_sim.func private @primary(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<i1>
            {obelisk_sim.capture_kind = 2 : i32}) -> i1
        attributes {entry_kind = 14 : i32, code_unit_id = 3 : i64} {
      %value = obelisk_sim.ref.load %clock : !obelisk_sim.ref<i1> -> i1
      obelisk_sim.return %value : i1
    }
    obelisk_sim.func private @owner(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %handle: !obelisk_sim.covergroup_handle<@cg>
            {obelisk_sim.capture_kind = 2 : i32},
        %clock: !obelisk_sim.ref<i1>
            {obelisk_sim.capture_kind = 2 : i32})
        attributes {entry_kind = 13 : i32, code_unit_id = 2 : i64,
                    internal, schedule.covergroup_clocking_sampler,
                    schedule.detached_controls,
                    schedule.prime_on_spawn} {
      %sampler = obelisk_sim.observer.bind @evaluate
          values(%clock : !obelisk_sim.ref<i1>) captures 1
          : !obelisk_sim.observer<i1>
      %initial = obelisk_sim.ref.load %clock : !obelisk_sim.ref<i1> -> i1
      %primary = obelisk_sim.observer.bind @primary
          values(%clock, %clock : !obelisk_sim.ref<i1>,
                 !obelisk_sim.ref<i1>) captures 1
          : !obelisk_sim.observer<i1>
      // expected-error @below {{sampler observer must capture the registered covergroup handle first}}
      obelisk_sim.covergroup.clock_event.register %ctx, %handle
          events [%primary, %initial, %sampler] conditions 0 edges [1] indices [-1]
          {strobe = false}
          : !obelisk_sim.covergroup_handle<@cg>, !obelisk_sim.observer<i1>,
            i1, !obelisk_sim.observer<i1>
      obelisk_sim.suspend.forever to ^parked
    ^parked:
      obelisk_sim.return
    }
  }
}

// -----

// A strobe registration can invoke this observer in the Postponed region.
// The observer must therefore satisfy the same transitive read-only contract
// as an ordinary Postponed code unit.
module {
  obelisk_sim.design @writing_evaluator {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : i1 design
    obelisk_sim.covergroup.decl @cg schema 1
    obelisk_sim.code_unit.decl 1 in 0 observer hierarchy "writing_evaluator.evaluate"
    obelisk_sim.code_unit.decl 2 in 0 fork hierarchy "writing_evaluator.owner"
    obelisk_sim.code_unit.decl 3 in 0 observer hierarchy "writing_evaluator.primary"
    obelisk_sim.func private @evaluate(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %handle: !obelisk_sim.covergroup_handle<@cg>
            {obelisk_sim.capture_kind = 2 : i32},
        %clock: !obelisk_sim.ref<i1>
            {obelisk_sim.capture_kind = 2 : i32}) -> i1
        attributes {entry_kind = 14 : i32, code_unit_id = 1 : i64,
                    obelisk_sim.covergroup_event_sample_evaluator,
                    obelisk_sim.covergroup_strobe_sample_evaluator} {
      %false = arith.constant false
      // expected-error @below {{is not permitted in a read-only postponed code unit}}
      obelisk_sim.ref.store %false to %clock : i1, !obelisk_sim.ref<i1>
      %true = arith.constant true
      obelisk_sim.return %true : i1
    }
    obelisk_sim.func private @primary(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<i1>
            {obelisk_sim.capture_kind = 2 : i32}) -> i1
        attributes {entry_kind = 14 : i32, code_unit_id = 3 : i64} {
      %value = obelisk_sim.ref.load %clock : !obelisk_sim.ref<i1> -> i1
      obelisk_sim.return %value : i1
    }
    obelisk_sim.func private @owner(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %handle: !obelisk_sim.covergroup_handle<@cg>
            {obelisk_sim.capture_kind = 2 : i32},
        %clock: !obelisk_sim.ref<i1>
            {obelisk_sim.capture_kind = 2 : i32})
        attributes {entry_kind = 13 : i32, code_unit_id = 2 : i64,
                    internal, schedule.covergroup_clocking_sampler,
                    schedule.detached_controls,
                    schedule.prime_on_spawn} {
      %sampler = obelisk_sim.observer.bind @evaluate
          values(%handle, %clock : !obelisk_sim.covergroup_handle<@cg>,
                 !obelisk_sim.ref<i1>) captures 2
          : !obelisk_sim.observer<i1>
      %initial = obelisk_sim.ref.load %clock : !obelisk_sim.ref<i1> -> i1
      %primary = obelisk_sim.observer.bind @primary
          values(%clock, %clock : !obelisk_sim.ref<i1>,
                 !obelisk_sim.ref<i1>) captures 1
          : !obelisk_sim.observer<i1>
      obelisk_sim.covergroup.clock_event.register %ctx, %handle
          events [%primary, %initial, %sampler] conditions 0 edges [1] indices [-1]
          {strobe = true}
          : !obelisk_sim.covergroup_handle<@cg>, !obelisk_sim.observer<i1>,
            i1, !obelisk_sim.observer<i1>
      obelisk_sim.suspend.forever to ^parked
    ^parked:
      obelisk_sim.return
    }
  }
}

// -----

// A non-strobe clock-event sample is ordinary procedural evaluation at the
// event instant. It must not inherit the Postponed region's read-only rule.
module {
  obelisk_sim.design @nonstrobe_writing_evaluator {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : i1 design
    obelisk_sim.covergroup.decl @cg schema 1
    obelisk_sim.code_unit.decl 1 in 0 observer hierarchy "nonstrobe_writing_evaluator.evaluate"
    obelisk_sim.code_unit.decl 2 in 0 fork hierarchy "nonstrobe_writing_evaluator.owner"
    obelisk_sim.code_unit.decl 3 in 0 observer hierarchy "nonstrobe_writing_evaluator.primary"
    obelisk_sim.func private @evaluate(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %handle: !obelisk_sim.covergroup_handle<@cg>
            {obelisk_sim.capture_kind = 2 : i32},
        %clock: !obelisk_sim.ref<i1>
            {obelisk_sim.capture_kind = 2 : i32}) -> i1
        attributes {entry_kind = 14 : i32, code_unit_id = 1 : i64,
                    obelisk_sim.covergroup_event_sample_evaluator} {
      %false = arith.constant false
      obelisk_sim.ref.store %false to %clock : i1, !obelisk_sim.ref<i1>
      %true = arith.constant true
      obelisk_sim.return %true : i1
    }
    obelisk_sim.func private @primary(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<i1>
            {obelisk_sim.capture_kind = 2 : i32}) -> i1
        attributes {entry_kind = 14 : i32, code_unit_id = 3 : i64} {
      %value = obelisk_sim.ref.load %clock : !obelisk_sim.ref<i1> -> i1
      obelisk_sim.return %value : i1
    }
    obelisk_sim.func private @owner(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %handle: !obelisk_sim.covergroup_handle<@cg>
            {obelisk_sim.capture_kind = 2 : i32},
        %clock: !obelisk_sim.ref<i1>
            {obelisk_sim.capture_kind = 2 : i32})
        attributes {entry_kind = 13 : i32, code_unit_id = 2 : i64,
                    internal, schedule.covergroup_clocking_sampler,
                    schedule.detached_controls,
                    schedule.prime_on_spawn} {
      %sampler = obelisk_sim.observer.bind @evaluate
          values(%handle, %clock : !obelisk_sim.covergroup_handle<@cg>,
                 !obelisk_sim.ref<i1>) captures 2
          : !obelisk_sim.observer<i1>
      %initial = obelisk_sim.ref.load %clock : !obelisk_sim.ref<i1> -> i1
      %primary = obelisk_sim.observer.bind @primary
          values(%clock, %clock : !obelisk_sim.ref<i1>,
                 !obelisk_sim.ref<i1>) captures 1
          : !obelisk_sim.observer<i1>
      obelisk_sim.covergroup.clock_event.register %ctx, %handle
          events [%primary, %initial, %sampler] conditions 0 edges [1] indices [-1]
          {strobe = false}
          : !obelisk_sim.covergroup_handle<@cg>, !obelisk_sim.observer<i1>,
            i1, !obelisk_sim.observer<i1>
      obelisk_sim.suspend.forever to ^parked
    ^parked:
      obelisk_sim.return
    }
  }
}
