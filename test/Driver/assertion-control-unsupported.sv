// RUN: obelisk -fno-lto -O0 -DDYNAMIC_CONTROL %s -o %t.dynamic-control
// RUN: not obelisk -fno-lto -O0 -DPROCEDURAL_SCOPE %s -o %t.scope 2>&1 | FileCheck %s --check-prefix=SCOPE
// RUN: not obelisk -fno-lto -O0 -DINVALID_CONTROL %s -o %t.invalid 2>&1 | FileCheck %s --check-prefix=INVALID

module assertion_control_unsupported;
`ifdef DYNAMIC_CONTROL
  int control = 3;
  initial begin
    selected: assert (1'b1);
    $assertcontrol(control, 2, 1, 0, selected);
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

// IEEE 1800-2017 20.12 permits an integer expression for control_type.
// SCOPE: error: assertion-control selector
// SCOPE-SAME: is not an assertion or supported module-instance scope
// INVALID: error: $assertcontrol control type must be in the range 1 through 11
