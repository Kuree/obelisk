// RUN: not obelisk -O0 -DCONCURRENT %s -o %t.concurrent 2>&1 | FileCheck %s --check-prefix=CONCURRENT
// RUN: not obelisk -O0 -DDYNAMIC_LEVEL %s -o %t.dynamic 2>&1 | FileCheck %s --check-prefix=DYNAMIC
// RUN: not obelisk -O0 -DPROCEDURAL_SCOPE %s -o %t.scope 2>&1 | FileCheck %s --check-prefix=SCOPE
// RUN: not obelisk -O0 -DPASS_CONTROL %s -o %t.pass 2>&1 | FileCheck %s --check-prefix=PASS

module assertion_control_unsupported;
`ifdef CONCURRENT
  logic clock;
  default clocking cb @(posedge clock); endclocking
  restrict property (1'b1);
  initial $assertoff;
`elsif DYNAMIC_LEVEL
  int level = 0;
  initial begin
    selected: assert (1'b1);
    $assertoff(level, selected);
  end
`elsif PROCEDURAL_SCOPE
  task automatic selected_scope;
    assertion: assert (1'b1);
  endtask
  initial $assertoff(0, selected_scope);
`elsif PASS_CONTROL
  initial $assertcontrol(7);
`endif
endmodule

// CONCURRENT: error: assertion control selected concurrent assertion
// DYNAMIC: error: assertion-control levels must be a fixed integer literal
// SCOPE: error: assertion-control selector
// SCOPE-SAME: is not an assertion or supported module-instance scope
// PASS: error: $assertcontrol currently supports only On (3), Off (4), and Kill (5)
