// RUN: not obelisk -fno-lto -O0 -DDYNAMIC_CONTROL %s -o %t.dynamic-control 2>&1 | FileCheck %s --check-prefix=DYNAMIC-CONTROL
// RUN: not obelisk -fno-lto -O0 -DDYNAMIC_MASK %s -o %t.dynamic-mask 2>&1 | FileCheck %s --check-prefix=DYNAMIC-MASK
// RUN: not obelisk -fno-lto -O0 -DPROCEDURAL_SCOPE %s -o %t.scope 2>&1 | FileCheck %s --check-prefix=SCOPE
// RUN: not obelisk -fno-lto -O0 -DINVALID_CONTROL %s -o %t.invalid 2>&1 | FileCheck %s --check-prefix=INVALID

module assertion_control_unsupported;
`ifdef DYNAMIC_CONTROL
  int control = 3;
  initial begin
    selected: assert (1'b1);
    $assertcontrol(control, 2, 1, 0, selected);
  end
`elsif DYNAMIC_MASK
  int mask = 2;
  initial begin
    selected: assert (1'b1);
    $assertcontrol(4, mask, 1, 0, selected);
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

// DYNAMIC-CONTROL: error: assertion-control control type must be a fixed integer literal
// DYNAMIC-MASK: error: assertion-control assertion-type mask must be a fixed integer literal
// SCOPE: error: assertion-control selector
// SCOPE-SAME: is not an assertion or supported module-instance scope
// INVALID: error: $assertcontrol control type must be in the range 1 through 11
