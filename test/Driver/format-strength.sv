// RUN: obelisk -fno-lto -O0 --vpi=off %s -o %t.o0.native
// RUN: %t.o0.native | FileCheck %s
// RUN: obelisk -fno-lto -O0 --vpi=off --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode | FileCheck %s
// RUN: obelisk -fno-lto -O3 --vpi=off %s -o %t.o3.native
// RUN: %t.o3.native | FileCheck %s
// RUN: obelisk -fno-lto -O3 --vpi=off --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode | FileCheck %s

// IEEE 1800-2017 21.2.1.5: %v renders the scalar net's resolved strength,
// including endpoint-local strength reduction through an ordinary tran.
module format_strength;
  logic left_drive, right_drive, weak_drive;
  wire left, right, weak_net;

  assign (supply1, supply0) left = left_drive;
  assign (pull1, pull0) right = right_drive;
  assign (weak1, weak0) weak_net = weak_drive;
  tran pass(left, right);

  initial begin
    $display("values %v %v %v %v", 1'b0, 1'b1, 1'bx, 1'bz);
    left_drive = 1'bx;
    right_drive = 1'bz;
    weak_drive = 1'bx;
    #1;
    $display("ranges %v %v %v", left, right, weak_net);
    left_drive = 1'b1;
    #1;
    $display("known %v %v", left, right);
  end
endmodule

// CHECK: values St0 St1 StX HiZ
// CHECK-NEXT: ranges SuX StX WeX
// CHECK-NEXT: known Su1 St1
