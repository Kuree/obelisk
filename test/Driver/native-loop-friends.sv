// RUN: obelisk -fno-lto -O0 %s -o %t.o0.native
// RUN: %t.o0.native > %t.o0.native.out
// RUN: obelisk -fno-lto -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode > %t.o0.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o0.bytecode.out
// RUN: obelisk -fno-lto -O3 %s -o %t.o3.native
// RUN: %t.o3.native > %t.o3.native.out
// RUN: obelisk -fno-lto -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode > %t.o3.bytecode.out
// RUN: diff -u %t.o3.native.out %t.o3.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o3.native.out
// RUN: FileCheck %s < %t.o3.native.out

module native_loop_friends;
  timeunit 1ns;
  timeprecision 1ns;

  logic [3:1] bits;
  logic [2:1][1:0] matrix;
  logic [2:1][5:5] singleton;
  logic [1:3] ascending;
  logic [-2:0] negative;
  integer unpacked_values [2:4];
  integer i;
  integer sum;
  integer once;
  integer order;
  integer matrix_order;
  integer singleton_order;
  integer ascending_order;
  integer negative_order;
  integer skipped_order;
  integer nested_order;
  integer fork_order;
  integer unpacked_order;
  integer unused_count;

  initial begin
    i = 0;
    sum = 0;
    do begin
      #1;
      i++;
      if (i == 2)
        continue;
      sum = sum + i;
    end while (i < 3);

    once = 0;
    do
      once = once + 1;
    while (0);

    forever begin
      #1;
      i--;
      if (i == 1)
        continue;
      sum = sum + 10;
      if (i == 0)
        break;
    end

    bits = 3'b101;
    order = 0;
    foreach (bits[index]) begin
      order = order * 10 + index;
      #1;
      if (index == 2)
        continue;
      sum = sum + bits[index];
    end

    matrix_order = 0;
    foreach (matrix[row, column])
      matrix_order = matrix_order * 100 + row * 10 + column;

    skipped_order = 0;
    foreach (matrix[, column])
      skipped_order = skipped_order * 10 + column;

    singleton_order = 0;
    foreach (singleton[row, column])
      singleton_order = singleton_order * 100 + row * 10 + column;

    ascending_order = 0;
    foreach (ascending[index])
      ascending_order = ascending_order * 10 + index;

    negative_order = 0;
    foreach (negative[index])
      negative_order = negative_order * 10 + index + 3;

    nested_order = 0;
    foreach (bits[index]) begin
      nested_order = nested_order * 10 + index;
      foreach (ascending[index]) begin
        nested_order = nested_order * 10 + index;
        break;
      end
      nested_order = nested_order * 10 + index;
    end

    fork_order = 0;
    foreach (bits[index]) begin
      fork
        begin
          #1;
          fork_order = fork_order * 10 + index;
        end
      join
    end

    unpacked_values[2] = 4;
    unpacked_values[3] = 5;
    unpacked_values[4] = 6;
    unpacked_order = 0;
    foreach (unpacked_values[index]) begin
      unpacked_order = unpacked_order * 10 + index;
      sum = sum + unpacked_values[index];
    end

    unused_count = 0;
    foreach (bits[unused_index])
      unused_count = unused_count + 1;

    $display(
        "loops time=%0t i=%0d sum=%0d once=%0d order=%0d matrix=%0d skipped=%0d singleton=%0d asc=%0d negative=%0d nested=%0d fork=%0d unpacked=%0d unused=%0d",
        $time, i, sum, once, order, matrix_order, skipped_order,
        singleton_order, ascending_order, negative_order, nested_order,
        fork_order, unpacked_order, unused_count);
  end
endmodule

// CHECK: loops time=12 i=0 sum=41 once=1 order=321 matrix=21201110 skipped=10 singleton=2515 asc=123 negative=123 nested=313212111 fork=321 unpacked=234 unused=3
