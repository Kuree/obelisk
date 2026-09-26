// RUN: obelisk %s -o %t.native
// RUN: %t.native > %t.native.out
// RUN: obelisk --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode > %t.bytecode.out
// RUN: diff -u %t.native.out %t.bytecode.out
// RUN: FileCheck %s < %t.native.out

module native_named_event;
  event ready;
  event other;

  initial begin
    $display("equal=%0d distinct=%0d", ready == ready, ready != other);
    @ready;
    $display("blocking=%0d", ready.triggered);
    @ready;
    $display("nonblocking=%0d", ready.triggered);
    #1;
    $display("cleared=%0d", ready.triggered);
  end

  initial begin
    #1 -> ready;
    ->> #2 ready;
  end

  initial begin
    #2;
    $display("before-delayed=%0d", ready.triggered);
  end
endmodule

// CHECK: equal=1 distinct=1
// CHECK-NEXT: blocking=1
// CHECK-NEXT: before-delayed=0
// CHECK-NEXT: nonblocking=1
// CHECK-NEXT: cleared=0
