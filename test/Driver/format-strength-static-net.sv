// RUN: obelisk -O0 --vpi=off %s -o %t.o0.native
// RUN: %t.o0.native | FileCheck %s
// RUN: obelisk -O0 --vpi=off --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode | FileCheck %s
// RUN: obelisk -O3 --vpi=off %s -o %t.o3.native
// RUN: %t.o3.native | FileCheck %s
// RUN: obelisk -O3 --vpi=off --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode | FileCheck %s

// A native generic schedule still retains the immutable driver/topology image
// when direct-net %v appears only in formatted output.
module format_strength_static_net;
  wire test_net;
  assign (pull1, strong0) test_net = 1'b1;
  initial begin
    $display("direct=%v", test_net);
    $display("string=%s", $sformatf("%v", test_net));
  end
endmodule

// CHECK: direct=Pu1
// CHECK: string=Pu1
