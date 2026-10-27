// REQUIRES: compiled-sampler
// RUN: %talos --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s

// IEEE 1800-2023 11.4.6, 11.4.13, 18.5.11 and 18.8:
// Integral inside members consume RHS wildcards and OR their four-state
// predicates. Known matches absorb unknown comparisons; inclusive, reversed
// and unknown ranges follow typed relational comparisons. Frozen symbols,
// consuming result casts and active function argument stages share Z3 plans.
// CHECK: function symbol membership PASS

class C;
  rand logic [3:0] value;
  rand logic [3:0] label_value;
  rand bit [3:0] out;
  logic [3:0] low_value;
  logic [3:0] high_value;

  function bit [3:0] membership(logic [3:0] v, logic [3:0] label_v,
                                logic [3:0] lo, logic [3:0] hi);
    return {v inside {label_v}, v inside {[lo : hi]},
            v inside {label_v, [0 : 0]}, v inside {[hi : lo]}};
  endfunction

  constraint relation {
    out == membership(value, label_value, low_value, high_value);
  }
endclass

module top;
  C c;

  task automatic probe(bit [3:0] expected);
    logic [3:0] before_value = c.value;
    logic [3:0] before_label = c.label_value;
    if (!c.randomize() || c.out != expected || c.value !== before_value ||
        c.label_value !== before_label)
      $fatal(1, "four-state membership mismatch");
  endtask

  initial begin
    c = new;
    c.value.rand_mode(0);
    c.label_value.rand_mode(0);
    c.low_value = 1;
    c.high_value = 3;
    c.value = 0;
    c.label_value = 4'bzzzz;
    probe(10);
    c.value = 4'b10xz;
    c.label_value = 4'b1001;
    probe(0);
    c.value = 0;
    c.label_value = 4'b100x;
    probe(2);
    c.value = 2;
    c.label_value = 4'bxx10;
    probe(14);
    c.value = 2;
    c.label_value = 4'bxx00;
    probe(4);
    c.value = 2;
    c.label_value = 5;
    probe(4);
    c.value = 0;
    c.label_value = 0;
    c.low_value = 0;
    c.high_value = 0;
    probe(15);
    c.value = 2;
    c.label_value = 2;
    c.low_value = 4'bxxxx;
    c.high_value = 3;
    probe(10);
    c.value = 8;
    c.label_value = 0;
    probe(0);
    c.value = 0;
    c.label_value = 4'b0x;
    c.low_value = 0;
    c.high_value = 4'bxxxx;
    probe(10);

    c.value.rand_mode(1);
    c.label_value = 4'bxx10;
    c.low_value = 1;
    c.high_value = 3;
    if (!c.randomize() with { value == 2; } || c.out != 14 ||
        c.label_value !== 4'bxx10)
      $fatal(1, "active membership argument mismatch");
    if (!c.randomize(null))
      $fatal(1, "membership checker failed");
    if (c.randomize() with { value == 2; out == 0; } || c.out != 14 ||
        c.value !== 2)
      $fatal(1, "failed membership stage committed");
    $display("function symbol membership PASS");
    $finish;
  end
endmodule
