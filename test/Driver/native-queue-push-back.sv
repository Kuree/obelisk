// RUN: obelisk -O0 %s -o %t.o0.native
// RUN: %t.o0.native | FileCheck %s
// RUN: obelisk -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode | FileCheck %s
// RUN: obelisk -O3 %s -o %t.o3.native
// RUN: %t.o3.native | FileCheck %s
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode | FileCheck %s

module native_queue_push_back;
  int unbounded[$];
  int bounded[$:1];

  initial begin
    unbounded.push_back(4);
    unbounded.push_back(3);
    $display("unbounded=%0d,%0d,%0d",
             unbounded.size(), unbounded[0], unbounded[1]);

    bounded.push_back(1);
    bounded.push_back(2);
    bounded.push_back(3);
    $display("bounded=%0d,%0d,%0d",
             bounded.size(), bounded[0], bounded[1]);
  end
endmodule

// CHECK: unbounded=2,4,3
// The declared maximum index is one, so the third append is ignored.
// CHECK: bounded=2,1,2
