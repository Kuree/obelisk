// RUN: obelisk-opt --split-input-file --verify-diagnostics %s

module {
  obelisk_sim.design @empty_events {
    obelisk_sim.scope.decl 0
    obelisk_sim.covergroup.decl @cg schema 1
    obelisk_sim.code_unit.decl 1 in 0 observer hierarchy "empty_events.evaluate"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "empty_events.bad"
    obelisk_sim.func private @evaluate(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %handle: !obelisk_sim.covergroup_handle<@cg>
            {obelisk_sim.capture_kind = 2 : i32}) -> i1
        attributes {entry_kind = 14 : i32, code_unit_id = 1 : i64,
                    obelisk_sim.covergroup_block_event_sample_evaluator} {
      %true = arith.constant true
      obelisk_sim.return %true : i1
    }
    obelisk_sim.func @bad(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %handle: !obelisk_sim.covergroup_handle<@cg>
            {obelisk_sim.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %sampler = obelisk_sim.observer.bind @evaluate
          values(%handle : !obelisk_sim.covergroup_handle<@cg>) captures 1
          : !obelisk_sim.observer<i1>
      // expected-error @below {{requires between one and UINT32_MAX block events}}
      obelisk_sim.covergroup.block_event.register %ctx, %handle, %sampler
          {event_kinds = array<i32>, target_ids = array<i64>}
          : (!obelisk_sim.context, !obelisk_sim.covergroup_handle<@cg>,
             !obelisk_sim.observer<i1>) -> ()
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @mismatched_events {
    obelisk_sim.scope.decl 0
    obelisk_sim.covergroup.decl @cg schema 1
    obelisk_sim.code_unit.decl 1 in 0 observer hierarchy "mismatched_events.evaluate"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "mismatched_events.bad"
    obelisk_sim.func private @evaluate(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %handle: !obelisk_sim.covergroup_handle<@cg>
            {obelisk_sim.capture_kind = 2 : i32}) -> i1
        attributes {entry_kind = 14 : i32, code_unit_id = 1 : i64,
                    obelisk_sim.covergroup_block_event_sample_evaluator} {
      %true = arith.constant true
      obelisk_sim.return %true : i1
    }
    obelisk_sim.func @bad(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %handle: !obelisk_sim.covergroup_handle<@cg>
            {obelisk_sim.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %sampler = obelisk_sim.observer.bind @evaluate
          values(%handle : !obelisk_sim.covergroup_handle<@cg>) captures 1
          : !obelisk_sim.observer<i1>
      // expected-error @below {{requires one boundary kind per target ID}}
      obelisk_sim.covergroup.block_event.register %ctx, %handle, %sampler
          {event_kinds = array<i32: 0>, target_ids = array<i64: 10, 11>}
          : (!obelisk_sim.context, !obelisk_sim.covergroup_handle<@cg>,
             !obelisk_sim.observer<i1>) -> ()
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @wrong_first_capture {
    obelisk_sim.scope.decl 0
    obelisk_sim.covergroup.decl @cg schema 1
    obelisk_sim.covergroup.decl @other schema 2
    obelisk_sim.code_unit.decl 1 in 0 observer hierarchy "wrong_first_capture.evaluate"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "wrong_first_capture.bad"
    obelisk_sim.func private @evaluate(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %handle: !obelisk_sim.covergroup_handle<@other>
            {obelisk_sim.capture_kind = 2 : i32}) -> i1
        attributes {entry_kind = 14 : i32, code_unit_id = 1 : i64,
                    obelisk_sim.covergroup_block_event_sample_evaluator} {
      %true = arith.constant true
      obelisk_sim.return %true : i1
    }
    obelisk_sim.func @bad(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %handle: !obelisk_sim.covergroup_handle<@cg>
            {obelisk_sim.capture_kind = 2 : i32},
        %other: !obelisk_sim.covergroup_handle<@other>
            {obelisk_sim.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %sampler = obelisk_sim.observer.bind @evaluate
          values(%other : !obelisk_sim.covergroup_handle<@other>) captures 1
          : !obelisk_sim.observer<i1>
      // expected-error @below {{sampler observer must capture the registered covergroup handle first}}
      obelisk_sim.covergroup.block_event.register %ctx, %handle, %sampler
          {event_kinds = array<i32: 0>, target_ids = array<i64: 10>}
          : (!obelisk_sim.context, !obelisk_sim.covergroup_handle<@cg>,
             !obelisk_sim.observer<i1>) -> ()
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_fire_target {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "bad_fire_target.bad"
    obelisk_sim.func @bad(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      // expected-error @below {{block-event target ID must be positive}}
      obelisk_sim.covergroup.block_event.fire %ctx
          {event_kind = 0 : i32, target_id = 0 : i64}
          : (!obelisk_sim.context) -> ()
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_fire_kind {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "bad_fire_kind.bad"
    obelisk_sim.func @bad(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      // expected-error @below {{block-event kind must be begin (0) or end (1)}}
      obelisk_sim.covergroup.block_event.fire %ctx
          {event_kind = 2 : i32, target_id = 10 : i64}
          : (!obelisk_sim.context) -> ()
      obelisk_sim.return
    }
  }
}
