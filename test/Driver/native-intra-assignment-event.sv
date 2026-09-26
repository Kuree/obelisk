// RUN: obelisk %s -o %t.native
// RUN: %t.native > %t.native.out
// RUN: obelisk --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode > %t.bytecode.out
// RUN: diff -u %t.native.out %t.bytecode.out
// RUN: FileCheck %s < %t.native.out

module native_intra_assignment_event;
  logic clk;
  logic lhs;
  logic rhs;

  initial begin
    clk = 0;
    rhs = 1;
    #1 clk = 1;
    #1 begin
      clk = 0;
      rhs = 0;
    end
    #1 clk = 1;
    #1 clk = 0;
    #1 clk = 1;
  end

  initial begin
    lhs = @(posedge clk) rhs;
    $display("event=%0d", lhs);
    lhs = repeat (2) @(posedge clk) rhs;
    $display("repeat=%0d", lhs);
  end
endmodule

// Both assignments capture the value 1 at encounter time. `rhs` changes to
// zero before the repeated control commits.
// CHECK: event=1
// CHECK-NEXT: repeat=1
