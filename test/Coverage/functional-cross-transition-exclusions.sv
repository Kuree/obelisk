// RUN: %obelisk --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: %t --coverage-output=%t.obcov | FileCheck %s --check-prefix=OUT
// RUN: obelisk-cov report --format=json %t.obcov | FileCheck %s --check-prefix=JSON

// IEEE 1800-2023 19.5.5-19.5.6 removes individual sequences, then 19.6.1.1
// uses the surviving terminal values for binsof. Another alternative, a larger
// repetition count, or a nonconsecutive gap may preserve a terminal removed
// from a shorter sequence. A nonconsecutive gap may itself match the following
// step (19.5.2). One configuration and fourteen samples, including wildcard
// and signed 65-bit terminal sets, negation, with matches $, and tuple-set
// matches $. Disabled iff guards on the exclusions never restore sequences.
module top;
  bit [3:0] value;
  logic signed [64:0] wide;
  bit partner;
  bit enabled;
  covergroup cg(input int maximum);
    fixed_point: coverpoint value {
      bins ordinary = (0 => 1, 2);
      ignore_bins removed = (0 => 1) iff (enabled);
    }
    prefix_point: coverpoint value {
      bins ordinary = (0 => 1, 2), (3 => 1);
      illegal_bins removed = (0 => 1) iff (enabled);
    }
    repetition_point: coverpoint value {
      bins ordinary = (0 => 1, 2 [* 1:maximum]);
      ignore_bins removed = (0 => 1) iff (enabled);
    }
    goto_point: coverpoint value {
      bins ordinary = (0 [-> 1] => 1, 2);
      ignore_bins removed = (0 => 1) iff (enabled);
    }
    bit [1:0] nonconsecutive_point: coverpoint value {
      bins ordinary = (0 [= 1] => 1, 2, 3);
      ignore_bins removed = (0 => 1) iff (enabled);
    }
    wildcard_point: coverpoint value {
      wildcard bins ordinary = (0 => 4'b?1?1);
      wildcard ignore_bins removed = (0 => 4'b?101) iff (enabled);
    }
    wide_point: coverpoint wide {
      bins ordinary = (0 => [-16:15]);
      illegal_bins removed = (0 => [-16:-1]) iff (enabled);
    }
    empty_point: coverpoint value {
      bins ordinary = (0 => 1 => 1, 2);
      ignore_bins removed = (0 => 1) iff (enabled);
    }
    side: coverpoint partner { bins zero = {0}; }
    fixed_cross: cross fixed_point, side {
      option.cross_retain_auto_bins = 0;
      bins removed = binsof(fixed_point.ordinary) intersect {1};
      bins live = binsof(fixed_point.ordinary) intersect {2};
      bins negated = !binsof(fixed_point.ordinary) intersect {1};
      bins all_live = fixed_cross with (fixed_point == 2) matches $;
      bins tuple_live = '{'{4'd2, 1'b0}} matches $;
    }
    prefix_cross: cross prefix_point, side {
      option.cross_retain_auto_bins = 0;
      bins one = binsof(prefix_point.ordinary) intersect {1};
      bins two = binsof(prefix_point.ordinary) intersect {2};
      bins negated = !binsof(prefix_point.ordinary) intersect {1};
      bins all_two = prefix_cross with (prefix_point == 2) matches $;
    }
    repetition_cross: cross repetition_point, side {
      option.cross_retain_auto_bins = 0;
      bins one = binsof(repetition_point.ordinary) intersect {1};
      bins two = binsof(repetition_point.ordinary) intersect {2};
      bins all_two = repetition_cross with (repetition_point == 2) matches $;
      bins tuple_all = '{'{4'd1, 1'b0}, '{4'd2, 1'b0}} matches $;
    }
    goto_cross: cross goto_point, side {
      option.cross_retain_auto_bins = 0;
      bins one = binsof(goto_point.ordinary) intersect {1};
      bins two = binsof(goto_point.ordinary) intersect {2};
      bins all_two = goto_cross with (goto_point == 2) matches $;
    }
    nonconsecutive_cross: cross nonconsecutive_point, side {
      option.cross_retain_auto_bins = 0;
      bins one = binsof(nonconsecutive_point.ordinary) intersect {1};
      bins two = binsof(nonconsecutive_point.ordinary) intersect {2};
      bins negated = !binsof(nonconsecutive_point.ordinary) intersect {1};
      bins all_two = nonconsecutive_cross with (nonconsecutive_point == 2) matches $;
    }
    wildcard_cross: cross wildcard_point, side {
      option.cross_retain_auto_bins = 0;
      bins removed = binsof(wildcard_point.ordinary) intersect {[4:5]};
      bins live = binsof(wildcard_point.ordinary) intersect {7};
      bins all_live = wildcard_cross with ((wildcard_point & 4'b0010) == 2) matches $;
      bins tuple_live = '{'{4'd7, 1'b0}, '{4'd15, 1'b0}} matches $;
    }
    wide_cross: cross wide_point, side {
      option.cross_retain_auto_bins = 0;
      bins removed = binsof(wide_point.ordinary) intersect {[-16:-1]};
      bins live = binsof(wide_point.ordinary) intersect {[0:15]};
      bins lower = binsof(wide_point.ordinary) intersect {0};
      bins upper = binsof(wide_point.ordinary) intersect {15};
    }
    empty_cross: cross empty_point, side {
      option.cross_retain_auto_bins = 0;
      bins removed = binsof(empty_point.ordinary) intersect {1, 2};
    }
  endgroup
  cg cov;
  task take_sample(input bit [3:0] v);
    value = v; wide = v;
    cov.sample();
  endtask
  initial begin
    cov = new(2);
    take_sample(0); take_sample(2);
    take_sample(0); take_sample(3); take_sample(1);
    take_sample(0); take_sample(2); take_sample(1);
    take_sample(0); take_sample(7);
    take_sample(0); take_sample(15);
    take_sample(0);
    value = 1; wide = -1; cov.sample();
    begin
      int c, t;
      real p;
      p = cov.fixed_cross.get_inst_coverage(c, t);
      $display("fixed %0.2f %0d/%0d", p, c, t);
      p = cov.prefix_cross.get_inst_coverage(c, t);
      $display("prefix %0.2f %0d/%0d", p, c, t);
      p = cov.repetition_cross.get_inst_coverage(c, t);
      $display("repetition %0.2f %0d/%0d", p, c, t);
      p = cov.goto_cross.get_inst_coverage(c, t);
      $display("goto %0.2f %0d/%0d", p, c, t);
      p = cov.nonconsecutive_cross.get_inst_coverage(c, t);
      $display("nonconsecutive %0.2f %0d/%0d", p, c, t);
      p = cov.wildcard_cross.get_inst_coverage(c, t);
      $display("wildcard %0.2f %0d/%0d", p, c, t);
      p = cov.wide_cross.get_inst_coverage(c, t);
      $display("wide %0.2f %0d/%0d", p, c, t);
    end
    $finish;
  end
endmodule

// OUT: fixed 100.00 4/4
// OUT-NEXT: prefix 100.00 2/2
// OUT-NEXT: repetition 100.00 3/3
// OUT-NEXT: goto 100.00 2/2
// OUT-NEXT: nonconsecutive 100.00 2/2
// OUT-NEXT: wildcard 100.00 3/3
// OUT-NEXT: wide 100.00 3/3

// Each cross has exactly the bins below, in order. A bin whose selected terminal
// values were all removed is empty (flag 128, not contributing, count 0); a live
// bin has the count of its coverpoint's ordinary bin. No cross has automatic
// bins and covered == total == the number of live bins.
// JSON-DAG: "name":"fixed_cross",{{[^]]*}}"covered":4,"total":4,"automatic_covered":0,"automatic_total":0,{{[^]]*}}],"bins":[{{[{][^}]*}}"name":"removed",{{[^}]*}}"flags":128,{{[^}]*}}"count":0,{{[^}]*}}"contributing":false,{{[^]]*}}]},{{[{][^}]*}}"name":"live",{{[^}]*}}"flags":0,{{[^}]*}}"count":2,{{[^}]*}}"contributing":true,{{[^]]*}}]},{{[{][^}]*}}"name":"negated",{{[^}]*}}"flags":0,{{[^}]*}}"count":2,{{[^}]*}}"contributing":true,{{[^]]*}}]},{{[{][^}]*}}"name":"all_live",{{[^}]*}}"flags":0,{{[^}]*}}"count":2,{{[^}]*}}"contributing":true,{{[^]]*}}]},{{[{][^}]*}}"name":"tuple_live",{{[^}]*}}"flags":0,{{[^}]*}}"count":2,{{[^}]*}}"contributing":true,{{[^]]*}}]}],"automatic_bins":[]
// JSON-DAG: "name":"prefix_cross",{{[^]]*}}"covered":2,"total":2,"automatic_covered":0,"automatic_total":0,{{[^]]*}}],"bins":[{{[{][^}]*}}"name":"one",{{[^}]*}}"flags":0,{{[^}]*}}"count":3,{{[^}]*}}"contributing":true,{{[^]]*}}]},{{[{][^}]*}}"name":"two",{{[^}]*}}"flags":0,{{[^}]*}}"count":3,{{[^}]*}}"contributing":true,{{[^]]*}}]},{{[{][^}]*}}"name":"negated",{{[^}]*}}"flags":128,{{[^}]*}}"count":0,{{[^}]*}}"contributing":false,{{[^]]*}}]},{{[{][^}]*}}"name":"all_two",{{[^}]*}}"flags":128,{{[^}]*}}"count":0,{{[^}]*}}"contributing":false,{{[^]]*}}]}],"automatic_bins":[]
// JSON-DAG: "name":"repetition_cross",{{[^]]*}}"covered":3,"total":3,"automatic_covered":0,"automatic_total":0,{{[^]]*}}],"bins":[{{[{][^}]*}}"name":"one",{{[^}]*}}"flags":0,{{[^}]*}}"count":3,{{[^}]*}}"contributing":true,{{[^]]*}}]},{{[{][^}]*}}"name":"two",{{[^}]*}}"flags":0,{{[^}]*}}"count":3,{{[^}]*}}"contributing":true,{{[^]]*}}]},{{[{][^}]*}}"name":"all_two",{{[^}]*}}"flags":128,{{[^}]*}}"count":0,{{[^}]*}}"contributing":false,{{[^]]*}}]},{{[{][^}]*}}"name":"tuple_all",{{[^}]*}}"flags":0,{{[^}]*}}"count":3,{{[^}]*}}"contributing":true,{{[^]]*}}]}],"automatic_bins":[]
// JSON-DAG: "name":"goto_cross",{{[^]]*}}"covered":2,"total":2,"automatic_covered":0,"automatic_total":0,{{[^]]*}}],"bins":[{{[{][^}]*}}"name":"one",{{[^}]*}}"flags":128,{{[^}]*}}"count":0,{{[^}]*}}"contributing":false,{{[^]]*}}]},{{[{][^}]*}}"name":"two",{{[^}]*}}"flags":0,{{[^}]*}}"count":2,{{[^}]*}}"contributing":true,{{[^]]*}}]},{{[{][^}]*}}"name":"all_two",{{[^}]*}}"flags":0,{{[^}]*}}"count":2,{{[^}]*}}"contributing":true,{{[^]]*}}]}],"automatic_bins":[]
// JSON-DAG: "name":"nonconsecutive_cross",{{[^]]*}}"covered":2,"total":2,"automatic_covered":0,"automatic_total":0,{{[^]]*}}],"bins":[{{[{][^}]*}}"name":"one",{{[^}]*}}"flags":0,{{[^}]*}}"count":7,{{[^}]*}}"contributing":true,{{[^]]*}}]},{{[{][^}]*}}"name":"two",{{[^}]*}}"flags":0,{{[^}]*}}"count":7,{{[^}]*}}"contributing":true,{{[^]]*}}]},{{[{][^}]*}}"name":"negated",{{[^}]*}}"flags":128,{{[^}]*}}"count":0,{{[^}]*}}"contributing":false,{{[^]]*}}]},{{[{][^}]*}}"name":"all_two",{{[^}]*}}"flags":128,{{[^}]*}}"count":0,{{[^}]*}}"contributing":false,{{[^]]*}}]}],"automatic_bins":[]
// JSON-DAG: "name":"wildcard_cross",{{[^]]*}}"covered":3,"total":3,"automatic_covered":0,"automatic_total":0,{{[^]]*}}],"bins":[{{[{][^}]*}}"name":"removed",{{[^}]*}}"flags":128,{{[^}]*}}"count":0,{{[^}]*}}"contributing":false,{{[^]]*}}]},{{[{][^}]*}}"name":"live",{{[^}]*}}"flags":0,{{[^}]*}}"count":2,{{[^}]*}}"contributing":true,{{[^]]*}}]},{{[{][^}]*}}"name":"all_live",{{[^}]*}}"flags":0,{{[^}]*}}"count":2,{{[^}]*}}"contributing":true,{{[^]]*}}]},{{[{][^}]*}}"name":"tuple_live",{{[^}]*}}"flags":0,{{[^}]*}}"count":2,{{[^}]*}}"contributing":true,{{[^]]*}}]}],"automatic_bins":[]
// JSON-DAG: "name":"wide_cross",{{[^]]*}}"covered":3,"total":3,"automatic_covered":0,"automatic_total":0,{{[^]]*}}],"bins":[{{[{][^}]*}}"name":"removed",{{[^}]*}}"flags":128,{{[^}]*}}"count":0,{{[^}]*}}"contributing":false,{{[^]]*}}]},{{[{][^}]*}}"name":"live",{{[^}]*}}"flags":0,{{[^}]*}}"count":5,{{[^}]*}}"contributing":true,{{[^]]*}}]},{{[{][^}]*}}"name":"lower",{{[^}]*}}"flags":0,{{[^}]*}}"count":5,{{[^}]*}}"contributing":true,{{[^]]*}}]},{{[{][^}]*}}"name":"upper",{{[^}]*}}"flags":0,{{[^}]*}}"count":5,{{[^}]*}}"contributing":true,{{[^]]*}}]}],"automatic_bins":[]
// JSON-DAG: "name":"empty_cross",{{[^]]*}}"covered":0,"total":0,"automatic_covered":0,"automatic_total":0,{{[^]]*}}],"bins":[{{[{][^}]*}}"name":"removed",{{[^}]*}}"flags":128,{{[^}]*}}"count":0,{{[^}]*}}"contributing":false,{{[^]]*}}]}],"automatic_bins":[]
// The coverpoint-level ordinary bin is hit by the surviving sequences, so the
// crosses above inherit its count. The removed exclusion bin never counts, and a
// bin emptied by the exclusion (empty_point) is excluded from coverage (19.5.5).
// JSON-DAG: "name":"fixed_point",{{[^]]*}}"covered":1,"total":1,"automatic_covered":0,"automatic_total":0,{{[^]]*}}],"bins":[{{[{][^}]*}}"name":"ordinary",{{[^}]*}}"flags":0,{{[^}]*}}"count":2,{{[^}]*}}"contributing":true,{{[^]]*}}]},{{[{][^}]*}}"name":"removed",{{[^}]*}}"flags":4,{{[^}]*}}"count":0,{{[^}]*}}"contributing":false,{{[^]]*}}]}],"automatic_bins":[]
// JSON-DAG: "name":"prefix_point",{{[^]]*}}"covered":1,"total":1,"automatic_covered":0,"automatic_total":0,{{[^]]*}}],"bins":[{{[{][^}]*}}"name":"ordinary",{{[^}]*}}"flags":0,{{[^}]*}}"count":3,{{[^}]*}}"contributing":true,{{[^]]*}}]},{{[{][^}]*}}"name":"removed",{{[^}]*}}"flags":8,{{[^}]*}}"count":0,{{[^}]*}}"contributing":false,{{[^]]*}}]}],"automatic_bins":[]
// JSON-DAG: "name":"repetition_point",{{[^]]*}}"covered":1,"total":1,"automatic_covered":0,"automatic_total":0,{{[^]]*}}],"bins":[{{[{][^}]*}}"name":"ordinary",{{[^}]*}}"flags":0,{{[^}]*}}"count":3,{{[^}]*}}"contributing":true,{{[^]]*}}]},{{[{][^}]*}}"name":"removed",{{[^}]*}}"flags":4,{{[^}]*}}"count":0,{{[^}]*}}"contributing":false,{{[^]]*}}]}],"automatic_bins":[]
// JSON-DAG: "name":"goto_point",{{[^]]*}}"covered":1,"total":1,"automatic_covered":0,"automatic_total":0,{{[^]]*}}],"bins":[{{[{][^}]*}}"name":"ordinary",{{[^}]*}}"flags":0,{{[^}]*}}"count":2,{{[^}]*}}"contributing":true,{{[^]]*}}]},{{[{][^}]*}}"name":"removed",{{[^}]*}}"flags":4,{{[^}]*}}"count":0,{{[^}]*}}"contributing":false,{{[^]]*}}]}],"automatic_bins":[]
// JSON-DAG: "name":"nonconsecutive_point",{{[^]]*}}"covered":1,"total":1,"automatic_covered":0,"automatic_total":0,{{[^]]*}}],"bins":[{{[{][^}]*}}"name":"ordinary",{{[^}]*}}"flags":0,{{[^}]*}}"count":7,{{[^}]*}}"contributing":true,{{[^]]*}}]},{{[{][^}]*}}"name":"removed",{{[^}]*}}"flags":4,{{[^}]*}}"count":0,{{[^}]*}}"contributing":false,{{[^]]*}}]}],"automatic_bins":[]
// JSON-DAG: "name":"wildcard_point",{{[^]]*}}"covered":1,"total":1,"automatic_covered":0,"automatic_total":0,{{[^]]*}}],"bins":[{{[{][^}]*}}"name":"ordinary",{{[^}]*}}"flags":16,{{[^}]*}}"count":2,{{[^}]*}}"contributing":true,{{[^]]*}}]},{{[{][^}]*}}"name":"removed",{{[^}]*}}"flags":20,{{[^}]*}}"count":0,{{[^}]*}}"contributing":false,{{[^]]*}}]}],"automatic_bins":[]
// JSON-DAG: "name":"wide_point",{{[^]]*}}"covered":1,"total":1,"automatic_covered":0,"automatic_total":0,{{[^]]*}}],"bins":[{{[{][^}]*}}"name":"ordinary",{{[^}]*}}"flags":0,{{[^}]*}}"count":5,{{[^}]*}}"contributing":true,{{[^]]*}}]},{{[{][^}]*}}"name":"removed",{{[^}]*}}"flags":8,{{[^}]*}}"count":0,{{[^}]*}}"contributing":false,{{[^]]*}}]}],"automatic_bins":[]
// JSON-DAG: "name":"empty_point",{{[^]]*}}"covered":0,"total":0,"automatic_covered":0,"automatic_total":0,{{[^]]*}}],"bins":[{{[{][^}]*}}"name":"ordinary",{{[^}]*}}"flags":128,{{[^}]*}}"count":0,{{[^}]*}}"contributing":false,{{[^]]*}}]},{{[{][^}]*}}"name":"removed",{{[^}]*}}"flags":4,{{[^}]*}}"count":0,{{[^}]*}}"contributing":false,{{[^]]*}}]}],"automatic_bins":[]
// JSON: "illegal_bin_diagnostics":[]
