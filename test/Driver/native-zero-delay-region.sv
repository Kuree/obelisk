// RUN: obelisk %s -o %t.native
// RUN: %t.native > %t.native.out
// RUN: obelisk --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode > %t.bytecode.out
// RUN: diff -u %t.native.out %t.bytecode.out
// RUN: FileCheck %s < %t.native.out

module native_zero_delay_region;
  logic trigger;

  initial begin
    trigger = 0;
    #0;
    $display("inactive");
  end

  initial begin
    trigger = 1;
    $display("active");
  end
endmodule

// CHECK: active
// CHECK-NEXT: inactive
