// RUN: obelisk -O0 %s -o %t.o0.native
// RUN: %t.o0.native | FileCheck %s
// RUN: obelisk -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode | FileCheck %s
// RUN: obelisk -O3 %s -o %t.o3.native
// RUN: %t.o3.native | FileCheck %s
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode | FileCheck %s

module native_queue_push_front;
  int queue[$];
  int bounded[$:1];

  initial begin
    queue.push_front(1);
    queue.push_front(2);
    queue.push_front(3);
    $display("queue=%0d,%0d,%0d,%0d",
             queue.size(), queue[0], queue[1], queue[2]);

    bounded.push_front(4);
    bounded.push_front(5);
    bounded.push_front(6);
    $display("bounded=%0d,%0d,%0d",
             bounded.size(), bounded[0], bounded[1]);
  end
endmodule

// CHECK: queue=3,3,2,1
// A full bounded queue ignores a further push.
// CHECK: bounded=2,5,4
