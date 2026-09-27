// RUN: obelisk-opt --split-input-file --verify-diagnostics %s

module {
  simulation.design @empty_events {
    simulation.scope.decl 0
    simulation.covergroup.decl @cg schema 1
    simulation.code_unit.decl 1 in 0 observer hierarchy "empty_events.evaluate"
    simulation.code_unit.decl 2 in 0 initial hierarchy "empty_events.bad"
    simulation.func private @evaluate(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %handle: !simulation.covergroup_handle<@cg>
            {simulation.capture_kind = 2 : i32}) -> i1
        attributes {entry_kind = 14 : i32, code_unit_id = 1 : i64,
                    simulation.covergroup_block_event_sample_evaluator} {
      %true = arith.constant true
      simulation.return %true : i1
    }
    simulation.func @bad(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %handle: !simulation.covergroup_handle<@cg>
            {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %sampler = simulation.observer.bind @evaluate
          values(%handle : !simulation.covergroup_handle<@cg>) captures 1
          : !simulation.observer<i1>
      // expected-error @below {{requires between one and UINT32_MAX block events}}
      simulation.covergroup.block_event.register %ctx, %handle, %sampler
          {event_kinds = array<i32>, target_ids = array<i64>}
          : (!simulation.context, !simulation.covergroup_handle<@cg>,
             !simulation.observer<i1>) -> ()
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @mismatched_events {
    simulation.scope.decl 0
    simulation.covergroup.decl @cg schema 1
    simulation.code_unit.decl 1 in 0 observer hierarchy "mismatched_events.evaluate"
    simulation.code_unit.decl 2 in 0 initial hierarchy "mismatched_events.bad"
    simulation.func private @evaluate(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %handle: !simulation.covergroup_handle<@cg>
            {simulation.capture_kind = 2 : i32}) -> i1
        attributes {entry_kind = 14 : i32, code_unit_id = 1 : i64,
                    simulation.covergroup_block_event_sample_evaluator} {
      %true = arith.constant true
      simulation.return %true : i1
    }
    simulation.func @bad(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %handle: !simulation.covergroup_handle<@cg>
            {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %sampler = simulation.observer.bind @evaluate
          values(%handle : !simulation.covergroup_handle<@cg>) captures 1
          : !simulation.observer<i1>
      // expected-error @below {{requires one boundary kind per target ID}}
      simulation.covergroup.block_event.register %ctx, %handle, %sampler
          {event_kinds = array<i32: 0>, target_ids = array<i64: 10, 11>}
          : (!simulation.context, !simulation.covergroup_handle<@cg>,
             !simulation.observer<i1>) -> ()
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @wrong_first_capture {
    simulation.scope.decl 0
    simulation.covergroup.decl @cg schema 1
    simulation.covergroup.decl @other schema 2
    simulation.code_unit.decl 1 in 0 observer hierarchy "wrong_first_capture.evaluate"
    simulation.code_unit.decl 2 in 0 initial hierarchy "wrong_first_capture.bad"
    simulation.func private @evaluate(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %handle: !simulation.covergroup_handle<@other>
            {simulation.capture_kind = 2 : i32}) -> i1
        attributes {entry_kind = 14 : i32, code_unit_id = 1 : i64,
                    simulation.covergroup_block_event_sample_evaluator} {
      %true = arith.constant true
      simulation.return %true : i1
    }
    simulation.func @bad(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %handle: !simulation.covergroup_handle<@cg>
            {simulation.capture_kind = 2 : i32},
        %other: !simulation.covergroup_handle<@other>
            {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %sampler = simulation.observer.bind @evaluate
          values(%other : !simulation.covergroup_handle<@other>) captures 1
          : !simulation.observer<i1>
      // expected-error @below {{sampler observer must capture the registered covergroup handle first}}
      simulation.covergroup.block_event.register %ctx, %handle, %sampler
          {event_kinds = array<i32: 0>, target_ids = array<i64: 10>}
          : (!simulation.context, !simulation.covergroup_handle<@cg>,
             !simulation.observer<i1>) -> ()
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @bad_fire_target {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "bad_fire_target.bad"
    simulation.func @bad(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      // expected-error @below {{block-event target ID must be positive}}
      simulation.covergroup.block_event.fire %ctx
          {event_kind = #simulation.block_event_kind<begin>, target_id = 0 : i64}
          : (!simulation.context) -> ()
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @bad_fire_kind {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "bad_fire_kind.bad"
    simulation.func @bad(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      // expected-error @below {{attribute 'event_kind' failed to satisfy constraint}}
      simulation.covergroup.block_event.fire %ctx
          {event_kind = 2 : i32, target_id = 10 : i64}
          : (!simulation.context) -> ()
      simulation.return
    }
  }
}
