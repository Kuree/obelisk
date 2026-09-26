// RUN: obelisk --std=1800-2023 -O0 %s -o %t.native
// RUN: %t.native > %t.native.out
// RUN: obelisk --std=1800-2023 -O0 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode > %t.bytecode.out
// RUN: diff -u %t.native.out %t.bytecode.out
// RUN: obelisk --std=1800-2023 -O3 %s -o %t.o3.native
// RUN: %t.o3.native > %t.o3.native.out
// RUN: obelisk --std=1800-2023 -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode > %t.o3.bytecode.out
// RUN: diff -u %t.native.out %t.o3.native.out
// RUN: diff -u %t.native.out %t.o3.bytecode.out
// RUN: FileCheck %s < %t.native.out

module top;
  class worker;
    int value;

    function new(int seed);
      value = seed;
    endfunction

    task set_value(int next);
      #1;
      value = next;
    endtask

    virtual task bump(int amount);
      #1;
      value = value + amount;
    endtask
  endclass

  class special_worker extends worker;
    function new(int seed);
      super.new(seed);
    endfunction

    virtual task bump(int amount);
      #1;
      value = value + 2 * amount;
    endtask
  endclass

  initial begin : call_class_task
    automatic worker item;
    automatic special_worker actual;
    actual = new(3);
    item = actual;
    actual.set_value(3);
    item.bump(2);
    $display("%0d", item.value);
  end
endmodule

// CHECK: 7
