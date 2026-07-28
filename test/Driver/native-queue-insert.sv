// RUN: obelisk -O0 %s -o %t.o0.native
// RUN: %t.o0.native | FileCheck %s
// RUN: obelisk -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode | FileCheck %s
// RUN: obelisk -O3 %s -o %t.o3.native
// RUN: %t.o3.native | FileCheck %s
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode | FileCheck %s

module native_queue_insert;
  int queue[$];
  int bounded[$:1];

  initial begin
    queue.insert(0, 1);
    queue.insert(0, 2);
    queue.insert(1, 3);
    $display("queue=%0d,%0d,%0d,%0d",
             queue.size(), queue[0], queue[1], queue[2]);

    bounded.insert(0, 4);
    bounded.insert(1, 5);
    bounded.insert(1, 6);
    $display("bounded=%0d,%0d,%0d",
             bounded.size(), bounded[0], bounded[1]);
  end
endmodule

// CHECK: queue=3,2,3,1
// The full bounded queue ignores a further insertion.
// CHECK: bounded=2,4,5
