// RUN: not obelisk -fno-lto -O0 -DCONCURRENT %s -o %t.concurrent 2>&1 | FileCheck %s --check-prefix=CONCURRENT
// RUN: not obelisk -fno-lto -O0 -DACTION_CONCURRENT %s -o %t.action-concurrent 2>&1 | FileCheck %s --check-prefix=ACTION-CONCURRENT
// RUN: not obelisk -fno-lto -O0 -DDYNAMIC_LEVEL %s -o %t.dynamic 2>&1 | FileCheck %s --check-prefix=DYNAMIC
// RUN: not obelisk -fno-lto -O0 -DACTION_DYNAMIC %s -o %t.action-dynamic 2>&1 | FileCheck %s --check-prefix=ACTION-DYNAMIC
// RUN: not obelisk -fno-lto -O0 -DPROCEDURAL_SCOPE %s -o %t.scope 2>&1 | FileCheck %s --check-prefix=SCOPE
// RUN: not obelisk -fno-lto -O0 -DINVALID_CONTROL %s -o %t.invalid 2>&1 | FileCheck %s --check-prefix=INVALID

module assertion_control_unsupported;
`ifdef CONCURRENT
  logic clock;
  default clocking cb @(posedge clock); endclocking
  restrict property (1'b1);
  initial $assertoff;
`elsif ACTION_CONCURRENT
  logic clock;
  default clocking cb @(posedge clock); endclocking
  assert property (1'b1);
  initial $assertpassoff;
`elsif DYNAMIC_LEVEL
  int level = 0;
  initial begin
    selected: assert (1'b1);
    $assertoff(level, selected);
  end
`elsif ACTION_DYNAMIC
  int level = 0;
  initial begin
    selected: assert (1'b1);
    $assertpassoff(level, selected);
  end
`elsif PROCEDURAL_SCOPE
  task automatic selected_scope;
    assertion: assert (1'b1);
  endtask
  initial $assertoff(0, selected_scope);
`elsif INVALID_CONTROL
  initial $assertcontrol(12);
`endif
endmodule

// CONCURRENT: error: assertion control selected concurrent assertion
// ACTION-CONCURRENT: error: assertion control selected concurrent assertion
// DYNAMIC: error: assertion-control levels must be a fixed integer literal
// ACTION-DYNAMIC: error: assertion-control levels must be a fixed integer literal
// SCOPE: error: assertion-control selector
// SCOPE-SAME: is not an assertion or supported module-instance scope
// INVALID: error: $assertcontrol control type must be in the range 1 through 11
