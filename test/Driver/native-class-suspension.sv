// RUN: obelisk --std=1800-2023 -O0 %s -o %t.native
// RUN: %t.native > %t.native.out
// RUN: obelisk --std=1800-2023 -O0 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode > %t.bytecode.out
// RUN: diff -u %t.native.out %t.bytecode.out
// RUN: FileCheck %s < %t.native.out

module top;
  class item;
    int value;
  endclass

  class pressure;
    bit [262143:0] payload;
  endclass

  initial begin : exercise_frame_root
    automatic item object;
    object = new;
    object.value = #1 7;
    $display("%0d", object.value);
  end

  initial begin : force_collection
    automatic pressure garbage;
    repeat (1100)
      garbage = new;
  end
endmodule

// CHECK: 7
