// RUN: %obelisk --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: %t --coverage-output=%t.obcov | FileCheck %s --check-prefix=OUT
// RUN: obelisk-cov report --format=json %t.obcov | FileCheck %s --check-prefix=JSON
// RUN: obelisk-cov inspect %t.obcov | FileCheck %s --check-prefix=INSPECT

// Construction indexes must keep each selector's dense predicate batch,
// target sample descriptor, and CrossValType layout with its own owner.
// The two types share local point names but have different field widths.
// IEEE 1800-2023 19.6.1.2 applies Count and All per bin tuple; 19.6.1.3 uses
// typed tuples. Two configurations and four samples each cover both contracts.
module top;
  bit [1:0] value;
  bit partner;
  covergroup cg_a(input int need);
    point: coverpoint value {
      bins low = {0, 1};
      bins high = {2, 3};
    }
    side: coverpoint partner { bins zero = {0}; }
    count_a: cross point, side {
      option.cross_retain_auto_bins = 0;
      bins selected = count_a with (point == side) matches need;
    }
    all_a: cross point, side {
      option.cross_retain_auto_bins = 0;
      bins selected = all_a with (point >= 2) matches $;
    }
    tuple_a: cross point, side {
      option.cross_retain_auto_bins = 0;
      bins selected = '{'{2'd1, 1'b0}};
    }
  endgroup
  covergroup cg_b;
    bit [3:0] point: coverpoint value {
      bins odd = {1, 3};
      bins even = {0, 2};
    }
    side: coverpoint partner { bins zero = {0}; }
    count_b: cross point, side {
      option.cross_retain_auto_bins = 0;
      bins selected = count_b with (point == side) matches 1;
    }
    all_b: cross point, side {
      option.cross_retain_auto_bins = 0;
      bins selected = all_b with ((point & 1) == 1) matches $;
    }
    tuple_b: cross point, side {
      option.cross_retain_auto_bins = 0;
      bins selected = '{'{4'd3, 1'b0}};
    }
  endgroup
  cg_a first;
  cg_b second;
  initial begin
    first = new(1);
    second = new;
    for (int v = 0; v != 4; ++v) begin
      value = v;
      first.sample();
      second.sample();
    end
    $display("A %0.2f %0.2f %0.2f", first.count_a.get_inst_coverage(),
             first.all_a.get_inst_coverage(), first.tuple_a.get_inst_coverage());
    $display("B %0.2f %0.2f %0.2f", second.count_b.get_inst_coverage(),
             second.all_b.get_inst_coverage(), second.tuple_b.get_inst_coverage());
    $finish;
  end
endmodule


// OUT: A 100.00 100.00 100.00
// OUT-NEXT: B 100.00 100.00 100.00

// Every cross has covered == total == 1, no automatic bins, and exactly one
// user bin hit by two samples and contributing to coverage; no illegal-bin
// diagnostics are produced.
// JSON-DAG: "name":"count_a",{{[^]]*}}"covered":1,"total":1,"automatic_covered":0,"automatic_total":0,{{[^]]*}}],"bins":[{{[{][^}]*}}"name":"selected",{{[^}]*}}"count":2,{{[^}]*}}"contributing":true,{{[^]]*}}]}],"automatic_bins":[]
// JSON-DAG: "name":"all_a",{{[^]]*}}"covered":1,"total":1,"automatic_covered":0,"automatic_total":0,{{[^]]*}}],"bins":[{{[{][^}]*}}"name":"selected",{{[^}]*}}"count":2,{{[^}]*}}"contributing":true,{{[^]]*}}]}],"automatic_bins":[]
// JSON-DAG: "name":"tuple_a",{{[^]]*}}"covered":1,"total":1,"automatic_covered":0,"automatic_total":0,{{[^]]*}}],"bins":[{{[{][^}]*}}"name":"selected",{{[^}]*}}"count":2,{{[^}]*}}"contributing":true,{{[^]]*}}]}],"automatic_bins":[]
// JSON-DAG: "name":"count_b",{{[^]]*}}"covered":1,"total":1,"automatic_covered":0,"automatic_total":0,{{[^]]*}}],"bins":[{{[{][^}]*}}"name":"selected",{{[^}]*}}"count":2,{{[^}]*}}"contributing":true,{{[^]]*}}]}],"automatic_bins":[]
// JSON-DAG: "name":"all_b",{{[^]]*}}"covered":1,"total":1,"automatic_covered":0,"automatic_total":0,{{[^]]*}}],"bins":[{{[{][^}]*}}"name":"selected",{{[^}]*}}"count":2,{{[^}]*}}"contributing":true,{{[^]]*}}]}],"automatic_bins":[]
// JSON-DAG: "name":"tuple_b",{{[^]]*}}"covered":1,"total":1,"automatic_covered":0,"automatic_total":0,{{[^]]*}}],"bins":[{{[{][^}]*}}"name":"selected",{{[^}]*}}"count":2,{{[^}]*}}"contributing":true,{{[^]]*}}]}],"automatic_bins":[]
// JSON: "illegal_bin_diagnostics":[]

// INSPECT: functional-configurations: 2
// INSPECT: resolved-cross-plans: 6
