// RUN: obelisk -O0 %s -o %t.o0.native
// RUN: %t.o0.native | FileCheck %s
// RUN: obelisk -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode | FileCheck %s
// RUN: obelisk -O3 %s -o %t.o3.native
// RUN: %t.o3.native | FileCheck %s
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode | FileCheck %s

module native_queue_delete;
  int queue[$];

  initial begin
    queue.push_back(2);
    queue.push_back(3);
    queue.push_back(4);
    queue.delete(1);
    $display("indexed=%0d,%0d,%0d",
             queue.size(), queue[0], queue[1]);
    queue.delete();
    $display("whole=%0d", queue.size());
  end
endmodule

// CHECK: indexed=2,2,4
// CHECK: whole=0
