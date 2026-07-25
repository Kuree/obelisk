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

class base;
  int field;

  virtual function int update(input int value, output int doubled,
                              inout int accumulator, ref int mirror);
    doubled = value * 2;
    accumulator = accumulator + value;
    mirror = mirror + 1;
    return value;
  endfunction

  task set_after(input int value, output int result);
    #1;
    result = value;
  endtask
endclass

class child extends base;
  virtual function int update(input int value, output int doubled,
                              inout int accumulator, ref int mirror);
    doubled = value * 3;
    accumulator = accumulator + value + 1;
    mirror = mirror + 2;
    return value + 1;
  endfunction
endclass

class constructed;
  int value;

  function new(input int seed, output int doubled, inout int accumulator,
               ref int mirror);
    value = seed;
    doubled = seed * 2;
    accumulator = accumulator + seed;
    mirror = mirror + 3;
  endfunction
endclass

module top;
  initial begin
    automatic child object = new;
    automatic base handle = object;
    automatic int accumulator = 10;
    automatic int mirror = 20;
    automatic int result;
    automatic int returned =
        handle.update(3, object.field, accumulator, mirror);
    automatic int constructor_out;
    automatic int constructor_accumulator = 7;
    automatic int constructor_mirror = 30;
    automatic constructed constructed_object =
        new(5, constructor_out, constructor_accumulator, constructor_mirror);
    object.set_after(9, result);
    object.set_after(13, object.field);
    $display("%0d %0d %0d %0d %0d", returned, object.field, accumulator,
             mirror, result);
    $display("%0d %0d %0d %0d", constructed_object.value, constructor_out,
             constructor_accumulator, constructor_mirror);
  end
endmodule

// CHECK: 4 13 14 22 9
// CHECK-NEXT: 5 10 12 33
