// RUN: obelisk --vpi=read -O0 --top=top %s -o %t.read-o0
// RUN: %t.read-o0 | FileCheck %s
// RUN: obelisk --vpi=read -O3 --top=top %s -o %t.read-o3
// RUN: %t.read-o3 | FileCheck %s
// RUN: obelisk --vpi=full -O3 --top=top %s -o %t.full
// RUN: %t.full | FileCheck %s

// A generated scope is a lexical VPI record, not an execution scope. Its
// child instances must still validate and remain visible with dormant VPI.
// CHECK: generated children 1 1 1
module child;
  logic value = 1;
endmodule
module top;
  for (genvar i = 0; i < 2; ++i) begin : generated
    child u();
  end
  if (1) begin : conditional
    child u();
  end
  initial begin
    #1;
    $display("generated children %b %b %b", generated[0].u.value,
             generated[1].u.value, conditional.u.value);
    $finish;
  end
endmodule
