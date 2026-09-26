// RUN: obelisk -O0 --vpi=off %s -o %t.native
// RUN: %t.native > %t.native.out
// RUN: obelisk -O0 --vpi=off --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode > %t.bytecode.out
// RUN: diff -u %t.bytecode.out %t.native.out
// RUN: FileCheck %s < %t.native.out

module native_vector_inertial_delay;
  logic [3:0] source = 0;
  wire [3:0] #5 net_delayed;
  wire [3:0] #5 assignment_delayed = source;
  assign net_delayed = source;

  initial begin
    #2 source = 1;
    #4 source = 2;
    #4 source = 3;
    #1 begin
      assert (net_delayed === 0);
      assert (assignment_delayed === 0);
    end
    #5 begin
      assert (net_delayed === 3);
      assert (assignment_delayed === 3);
    end
    $display("vector inertial delay passed");
  end

  // CHECK: vector inertial delay passed
endmodule
