// RUN: obelisk -emit-slang --std=1800-2023 %s | FileCheck %s --check-prefix=SLANG
// RUN: obelisk -emit-obelisk --std=1800-2017 %s | FileCheck %s --check-prefix=OBELISK

module procedural_timing_import;
  event first_event;
  event second_event;
  logic lhs;
  logic rhs;

  initial begin
    fork
      lhs = rhs;
      rhs = lhs;
    join_none

    lhs = #1 rhs;
    lhs <= @(first_event) rhs;

    wait_order (first_event, second_event)
      lhs = 1'b1;
    else
      lhs = 1'b0;

    -> first_event;
    ->> #1 second_event;
  end
endmodule

// SLANG: slang.statement.block attributes {{.*}}block_kind = 3 : i32
// SLANG: slang.expression.assignment attributes {{.*}}assignment_kind = 0 : i32{{.*}}has_timing_control = true
// SLANG: slang.expression.assignment attributes {{.*}}assignment_kind = 1 : i32{{.*}}has_timing_control = true
// SLANG: slang.statement.wait_order attributes {{.*}}event_count = 2 : i64{{.*}}has_failure_action = true{{.*}}has_success_action = true
// SLANG: slang.statement.event_trigger attributes {node_id
// SLANG: slang.statement.event_trigger attributes {{.*}}has_timing_control = true{{.*}}is_nonblocking = true

// OBELISK: obelisk.sv.statement.block attributes {{.*}}block_kind = 3 : i32
// OBELISK: obelisk.sv.expression.assignment attributes {{.*}}assignment_kind = 0 : i32{{.*}}has_timing_control = true
// OBELISK: obelisk.sv.expression.assignment attributes {{.*}}assignment_kind = 1 : i32{{.*}}has_timing_control = true
// OBELISK: obelisk.sv.statement.wait_order attributes {{.*}}event_count = 2 : i64{{.*}}has_failure_action = true{{.*}}has_success_action = true
// OBELISK: obelisk.sv.statement.event_trigger attributes {node_id
// OBELISK: obelisk.sv.statement.event_trigger attributes {{.*}}has_timing_control = true{{.*}}is_nonblocking = true
