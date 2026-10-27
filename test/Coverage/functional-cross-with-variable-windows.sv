// RUN: %talos --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: %t --coverage-output=%t.obcov | FileCheck %s
// RUN: talos-cov report --format=json %t.obcov | FileCheck %s --check-prefix=JSON

// IEEE 1800-2023 11.5.1 and 19.6.1.2. Each cross has one broad tuple bin;
// its associated values cover 65 source bits and signed or four-state indices.
// Overlapping indexed windows share selector states and retain X padding.
module top;
  bit [64:0] value;
  bit [0:64] ascending;
  bit [3:0][1:0] packed_value;
  bit signed [7:0] position;
  logic [1:0] unknown_position;
  covergroup cg;
    option.cross_retain_auto_bins = 0;
    logic [64:0] ca: coverpoint value { bins all = {[0:65'h1_ffff_ffff_ffff_ffff]}; }
    logic [0:64] cu: coverpoint ascending { bins all = {[0:65'h1_ffff_ffff_ffff_ffff]}; }
    logic [3:0][1:0] cp: coverpoint packed_value { bins all = {[0:255]}; }
    bit signed [7:0] ci: coverpoint position { bins all = {[-1:65]}; }
    low_window: cross ca, ci {
      bins selected = low_window with (ca[ci +: 2] === 2'b00)
          matches 128'h20_0000_0000_0000_0000;
    }
    high_window: cross ca, ci {
      bins selected = high_window with (ca[ci -: 2] === 2'b00)
          matches 128'h20_0000_0000_0000_0000;
    }
    ascending_up: cross cu, ci {
      bins selected = ascending_up with (cu[ci +: 2] === 2'b00)
          matches 128'h20_0000_0000_0000_0000;
    }
    ascending_down: cross cu, ci {
      bins selected = ascending_down with (cu[ci -: 2] === 2'b00)
          matches 128'h20_0000_0000_0000_0000;
    }
    negative_padding: cross ca, ci {
      bins selected = negative_padding with (ci == -1 && ca[ci +: 2] === 2'b0x)
          matches 128'h1_0000_0000_0000_0000;
    }
    positive_padding: cross ca, ci {
      bins selected = positive_padding with (ci == 64 && ca[ci +: 2] === 2'bx0)
          matches 128'h1_0000_0000_0000_0000;
    }
    outside: cross ca, ci {
      bins selected = outside with (ci == 65 && ca[ci +: 2] === 2'bxx)
          matches 128'h2_0000_0000_0000_0000;
    }
    packed_elements: cross cp, ci {
      bins selected = packed_elements with (cp[ci +: 2] === 4'b0000) matches 48;
    }
  endgroup
  covergroup unknown_group;
    option.cross_retain_auto_bins = 0;
    logic [64:0] ca: coverpoint value { bins all = {[0:65'h1_ffff_ffff_ffff_ffff]}; }
    logic [1:0] ci: coverpoint unknown_position {
      bins nonzero = {[1:3]};
      bins known = {0};
    }
    product: cross ca, ci {
      // Every nonzero ci makes its inner window partially or entirely X.
      // No X/Z bin entry is needed: 19.5.7 removes such entries on resolution.
      bins selected = product with (ca[ci[ci +: 2] +: 2] === 2'bxx) matches $;
    }
  endgroup
  cg cov;
  unknown_group unknown_cov;
  initial begin
    cov = new;
    unknown_cov = new;
    unknown_position = 1;
    cov.sample();
    unknown_cov.sample();
    $display("low %0.0f high %0.0f", cov.low_window.get_inst_coverage(),
             cov.high_window.get_inst_coverage());
    $display("ascending %0.0f %0.0f", cov.ascending_up.get_inst_coverage(),
             cov.ascending_down.get_inst_coverage());
    $display("padding %0.0f %0.0f outside %0.0f",
             cov.negative_padding.get_inst_coverage(),
             cov.positive_padding.get_inst_coverage(), cov.outside.get_inst_coverage());
    $display("packed %0.0f unknown %0.0f", cov.packed_elements.get_inst_coverage(),
             unknown_cov.product.get_inst_coverage());
    $finish;
  end
endmodule
// CHECK: low 100 high 100
// CHECK-NEXT: ascending 100 100
// CHECK-NEXT: padding 100 100 outside 100
// CHECK-NEXT: packed 100 unknown 100
// $display values: an indexed part select with a variable base takes its
// window from the cross value tuple; bits outside the declared range read as X
// (11.5.1). "matches N" needs N satisfying value tuples, "matches $" all of
// them (19.6.1.2): e.g. ca[ci +: 2] === 2'b00 holds for ci in 0..63 with 2^63
// values of ca each, i.e. 2^69 tuples, and ci[ci +: 2] is X for every nonzero
// 2-bit ci, so the X-indexed window is all X.

// The report keeps the compiled windows: a kind-23 part select whose base
// operand is a typed kind-2 target of signed width 8, and a packed-element
// select with element_width=2 and width 4. No value tuple is materialized.
// JSON-DAG: "kind":23,"bit_width":2,"signedness":1,"ordinal":2,"flags":6,"element_width":1,"index_base":64
// JSON-DAG: "kind":23,"bit_width":4,"signedness":1,"ordinal":2,"flags":0,"element_width":2,"index_base":0
// JSON-DAG: "kind":2,"bit_width":8,"signedness":2
// JSON: "resolved_functional_tuples":[]
