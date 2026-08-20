// RUN: not obelisk -fno-lto -O0 -DDYNAMIC_LEVEL %s -o %t.dynamic 2>&1 | FileCheck %s --check-prefix=DYNAMIC
// RUN: not obelisk -fno-lto -O0 -DACTION_DYNAMIC %s -o %t.action-dynamic 2>&1 | FileCheck %s --check-prefix=ACTION-DYNAMIC
// RUN: not obelisk -fno-lto -O0 -DPROCEDURAL_SCOPE %s -o %t.scope 2>&1 | FileCheck %s --check-prefix=SCOPE
// RUN: not obelisk -fno-lto -O0 -DINVALID_CONTROL %s -o %t.invalid 2>&1 | FileCheck %s --check-prefix=INVALID

module assertion_control_unsupported;
`ifdef DYNAMIC_LEVEL
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

// DYNAMIC: error: assertion-control levels must be a fixed integer literal
// ACTION-DYNAMIC: error: assertion-control levels must be a fixed integer literal
// SCOPE: error: assertion-control selector
// SCOPE-SAME: is not an assertion or supported module-instance scope
// INVALID: error: $assertcontrol control type must be in the range 1 through 11
