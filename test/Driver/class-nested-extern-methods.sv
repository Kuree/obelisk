// XFAIL: *
// Slang v11 does not resolve multiply-qualified out-of-block class methods.
// RUN: obelisk --std=1800-2017 -O0 %s -o %t.o0.native
// RUN: %t.o0.native > %t.o0.native.out
// RUN: obelisk --std=1800-2017 -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode > %t.o0.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o0.bytecode.out
// RUN: obelisk --std=1800-2017 -O3 %s -o %t.o3.native
// RUN: %t.o3.native > %t.o3.native.out
// RUN: obelisk --std=1800-2017 -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode > %t.o3.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o3.native.out
// RUN: diff -u %t.o0.native.out %t.o3.bytecode.out
// RUN: FileCheck %s < %t.o0.native.out

class left;
  class nested;
    int bias;
    function new(int bias);
      this.bias = bias;
    endfunction
    extern function int add(int value);
    extern static function int tag();
  endclass
endclass

class right;
  class nested;
    extern function int add(int value);
    extern static function int tag();
  endclass
endclass

function int left::nested::add(int value);
  return bias + value;
endfunction

function int left::nested::tag();
  return 11;
endfunction

function int right::nested::add(int value);
  return value + 20;
endfunction

function int right::nested::tag();
  return 22;
endfunction

module top;
  initial begin
    automatic left::nested l = new(3);
    automatic right::nested r = new;
    $display("%0d %0d %0d %0d", l.add(4), left::nested::tag(), r.add(5),
             right::nested::tag());
  end
endmodule

// CHECK: 7 11 25 22
