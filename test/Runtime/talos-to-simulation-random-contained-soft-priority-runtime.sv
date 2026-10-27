// RUN: %talos --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s
// REQUIRES: compiled-sampler

// IEEE 1800-2023 18.5.13.1: constraints in contained objects have lower soft
// priority than all constraints in their container, and constraints in objects
// whose handles are declared later have higher priority.
// CHECK: container over contained 2
// CHECK: later handle over earlier 4
// CHECK: middle over inner 6

class Leaf;
  rand bit [3:0] v;
  constraint leaf { soft v == 1; }
endclass

class Middle;
  rand Leaf leaf = new;
  constraint middle { soft leaf.v == 6; }
endclass

class Outer;
  rand Leaf h = new;
  constraint outer { soft h.v == 2; }
endclass

class First;
  rand bit [3:0] v;
  constraint prefer { soft v == 3; }
endclass

class Second;
  rand bit [3:0] v;
  constraint prefer { soft v == 4; }
endclass

class Ordered;
  rand First a = new;
  rand Second b = new;
  constraint link { a.v == b.v; }
endclass

class Deep;
  rand Middle m = new;
endclass

module top;
  initial begin
    Outer o = new;
    Ordered p = new;
    Deep d = new;
    void'(o.randomize());
    $display("container over contained %0d", o.h.v);
    void'(p.randomize());
    $display("later handle over earlier %0d", p.a.v);
    void'(d.randomize());
    $display("middle over inner %0d", d.m.leaf.v);
  end
endmodule
