// XFAIL: *
// Slang v11 does not resolve multiply-qualified out-of-block class methods.
// RUN: talos -O3 --std=1800-2017 %s -o %t.native
// RUN: %t.native > %t.native.out
// RUN: FileCheck %s < %t.native.out
// RUN: talos -O3 --std=1800-2017 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode | diff -u %t.native.out -

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
