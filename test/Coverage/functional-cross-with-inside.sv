// RUN: %talos --std=1800-2023 -O0 --coverage=functional -o %t %s 2>&1 | FileCheck %s --check-prefix=WARN
// RUN: %t --coverage-output=%t.obcov | FileCheck %s --check-prefix=OUT
// RUN: talos-cov report --format=json %t.obcov | FileCheck %s --check-prefix=BINS
// RUN: talos-cov report --format=text %t.obcov | FileCheck %s --check-prefix=REPORT

// IEEE 1800-2023 11.4.6, 11.4.13, 19.6.1.2. Singular members use
// asymmetric wildcard equality; ranges are inclusive. A known member match
// dominates unknown comparisons. Absolute tolerance casts to the center's
// type, wraps in that type, and then swaps the resulting bounds if needed.
module top;
  bit [64:0] value;
  bit [1:0] center;
  bit signed [64:0] signed_value;
  covergroup cg;
    option.cross_retain_auto_bins = 0;
    logic [64:0] ca: coverpoint value { bins all = {[0:65'h1_ffff_ffff_ffff_ffff]}; }
    logic [1:0] cb: coverpoint center { bins zero = {0}; }
    listed: cross ca, cb {
      bins selected = listed with (ca inside {[65'b0:65'b11], 65'b111}) matches 5;
      bins too_many = listed with (ca inside {[65'b0:65'b11], 65'b111}) matches 6;
    }
    variable_bounds: cross ca, cb {
      bins selected = variable_bounds with (ca inside {[65'(cb):65'(cb) + 65'd3], 65'd7}) matches 5;
    }
    nested: cross ca, cb {
      bins selected = nested with ((ca inside {65'b1}) inside {1'b1}) matches 1;
    }
    wildcard_equal: cross ca, cb {
      bins selected = wildcard_equal with (ca ==? 65'b0xz) matches 4;
    }
    wildcard_not_equal: cross ca, cb {
      bins selected = wildcard_not_equal with (ca !=? 65'b0xz) matches 65'h1_ffff_ffff_ffff_fffc;
    }
    tolerance: cross ca, cb {
      bins selected = tolerance with (ca[1:0] inside {[cb +/- 2'b01]}) matches 65'h1_8000_0000_0000_0000;
    }
  endgroup
  covergroup signed_ranges;
    option.cross_retain_auto_bins = 0;
    logic signed [64:0] cs: coverpoint signed_value {
      bins all = {[65'sh1_0000_0000_0000_0000:65'sh0_ffff_ffff_ffff_ffff]};
    }
    logic cb: coverpoint 1'b0 { bins zero = {0}; }
    negative: cross cs, cb {
      bins selected = negative with (cs inside {[$:-65'sb1]}) matches 65'h1_0000_0000_0000_0000;
    }
    nonnegative: cross cs, cb {
      bins selected = nonnegative with (cs inside {[65'sb0:$]}) matches 65'h1_0000_0000_0000_0000;
    }
    unbounded: cross cs, cb {
      bins selected = unbounded with (cs inside {[$:65'sh0_ffff_ffff_ffff_ffff]}) matches $;
    }
    backwards: cross cs, cb {
      bins reversed = backwards with (cs inside {[65'sb1:-65'sb1]});
    }
  endgroup
  covergroup planes(logic [1:0] lhs, logic [1:0] rhs);
    option.cross_retain_auto_bins = 0;
    logic [64:0] ca: coverpoint value { bins all = {[0:65'h1_ffff_ffff_ffff_ffff]}; }
    logic cb: coverpoint 1'b0 { bins zero = {0}; }
    unknown: cross ca, cb {
      bins selected = unknown with (({ca[0], lhs} ==? {1'b0, rhs}) === 1'bx) matches 65'h1_0000_0000_0000_0000;
    }
    known_match: cross ca, cb {
      bins selected = known_match with (({ca[0], lhs} inside {3'b000, 3'bxxx}) === 1'b1) matches $;
    }
    unknown_range: cross ca, cb {
      bins selected = unknown_range with (({ca[0], lhs} inside {[3'b000:3'b111]}) === 1'bx) matches $;
    }
  endgroup
  cg cov;
  signed_ranges ranges;
  planes four_state;
  initial begin
    cov = new;
    ranges = new;
    four_state = new(2'bx0, 2'b00);
    cov.sample();
    ranges.sample();
    four_state.sample();
    value = 1;
    cov.sample();
    value = 4;
    cov.sample();
    signed_value = -65'sb1;
    ranges.sample();
    $display("inside %0.0f bounds %0.0f nested %0.0f wildcard %0.0f %0.0f tolerance %0.0f",
             cov.listed.get_inst_coverage(), cov.variable_bounds.get_inst_coverage(), cov.nested.get_inst_coverage(),
             cov.wildcard_equal.get_inst_coverage(), cov.wildcard_not_equal.get_inst_coverage(), cov.tolerance.get_inst_coverage());
    $display("ranges %0.0f %0.0f %0.0f planes %0.0f %0.0f %0.0f",
             ranges.negative.get_inst_coverage(), ranges.nonnegative.get_inst_coverage(), ranges.unbounded.get_inst_coverage(),
             four_state.unknown.get_inst_coverage(), four_state.known_match.get_inst_coverage(), four_state.unknown_range.get_inst_coverage());
    $finish;
  end
endmodule

// WARN: warning: reversed range will behave as if it were empty

// Each with-bin is covered only when the number of tuples satisfying the
// predicate equals matches (19.6.1.4); values follow 11.4.13: ranges are
// inclusive, ca ==? 65'b0xz leaves 2 bits free (4 values), [0 +/- 1] over
// 2 bits wraps to [3:1] and is swapped to [1:3] (3 of 4 values), and
// [$:-1], [0:$] each take 2^64 signed values.
// OUT: inside 100 bounds 100 nested 100 wildcard 100 100 tolerance 100
// OUT-NEXT: ranges 100 100 100 planes 100 100 100

// Cross bins are evaluated by predicates, not materialized tuples.
// BINS: "resolved_functional_tuples":[]
// Impossible matches and reversed ranges (empty, 11.4.13) leave empty bins.
// BINS-DAG: "name":"too_many","hierarchy":"top.cg.listed.too_many",{{[^}]*}}"flags":128,{{[^}]*}}"count":0,{{[^}]*}}"excluded":true,{{[^}]*}}"covered":false
// BINS-DAG: "name":"reversed","hierarchy":"top.signed_ranges.backwards.reversed",{{[^}]*}}"flags":128,{{[^}]*}}"count":0,{{[^}]*}}"excluded":true,{{[^}]*}}"covered":false
// Three samples in cg, two in signed_ranges, one in planes. A known inside
// member dominates unknown comparisons; unknown range comparisons yield x.
// BINS-DAG: "hierarchy":"top.cg.listed.selected",{{[^}]*}}"count":3,{{[^}]*}}"covered":true
// BINS-DAG: "hierarchy":"top.cg.variable_bounds.selected",{{[^}]*}}"count":3,{{[^}]*}}"covered":true
// BINS-DAG: "hierarchy":"top.cg.nested.selected",{{[^}]*}}"count":3,{{[^}]*}}"covered":true
// BINS-DAG: "hierarchy":"top.cg.wildcard_equal.selected",{{[^}]*}}"count":3,{{[^}]*}}"covered":true
// BINS-DAG: "hierarchy":"top.cg.wildcard_not_equal.selected",{{[^}]*}}"count":3,{{[^}]*}}"covered":true
// BINS-DAG: "hierarchy":"top.cg.tolerance.selected",{{[^}]*}}"count":3,{{[^}]*}}"covered":true
// BINS-DAG: "hierarchy":"top.signed_ranges.negative.selected",{{[^}]*}}"count":2,{{[^}]*}}"covered":true
// BINS-DAG: "hierarchy":"top.signed_ranges.nonnegative.selected",{{[^}]*}}"count":2,{{[^}]*}}"covered":true
// BINS-DAG: "hierarchy":"top.signed_ranges.unbounded.selected",{{[^}]*}}"count":2,{{[^}]*}}"covered":true
// BINS-DAG: "hierarchy":"top.planes.unknown.selected",{{[^}]*}}"count":1,{{[^}]*}}"covered":true
// BINS-DAG: "hierarchy":"top.planes.known_match.selected",{{[^}]*}}"count":1,{{[^}]*}}"covered":true
// BINS-DAG: "hierarchy":"top.planes.unknown_range.selected",{{[^}]*}}"count":1,{{[^}]*}}"covered":true

// REPORT: functional: 100.00% (18/18)
// REPORT-DAG: cross backwards: 0/0 (0.00%)
// REPORT-DAG: cross listed: 1/1 (100.00%)
