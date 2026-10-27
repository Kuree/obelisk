// RUN: %talos --std=1800-2023 -emit-talos %s | talos-opt '--lower-talos-to-sim=opt-level=0' \
// RUN:   '--test-talos-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' \
// RUN:   --test-talos-print-coverage-schema -o /dev/null 2>&1 | FileCheck %s --check-prefix=SCHEMA
// RUN: %talos --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: %t --coverage-output=%t.obcov > %t.out 2> %t.err
// RUN: FileCheck %s --check-prefix=OUT < %t.out
// RUN: FileCheck %s --check-prefix=WARN < %t.err
// RUN: talos-cov report --format=json %t.obcov | FileCheck %s --check-prefix=JSON

// IEEE 1800-2023 19.5.5-19.5.6 removes state exclusions before 19.6.1 selects
// associated values. Disabled iff guards do not restore those values. Negation,
// tuple sets and matches $ use the same remaining sets. State exclusions have no
// effect on transition bins, including their terminal values. 19.5.7 discards
// X/Z singleton bin elements with a warning. An X in a wildcard position must
// not match the wildcard's 0/1 expansion.
module top;
  bit [1:0] value;
  bit partner;
  bit enabled;
  real wide;
  shortreal narrow;
  logic [3:0] wildcard_value;
  logic [1:0] unknown_value;
  bit [1:0] transition_value;
  logic signed [64:0] wide_integral;
  covergroup cg(input int selected_value);
    ignored_point: coverpoint value {
      bins ordinary = {1, 2};
      ignore_bins removed = {1} iff (enabled);
    }
    illegal_point: coverpoint value {
      bins ordinary = {1, 2};
      illegal_bins removed = {1} iff (enabled);
    }
    partner_point: coverpoint partner { bins zero = {0}; }
    real_point: coverpoint wide {
      bins ordinary = {[0.0:4.0]};
      ignore_bins removed = {[0.0:1.0], [3.0:4.0]} iff (enabled);
    }
    short_point: coverpoint narrow {
      bins ordinary = {[0.0:4.0]};
      illegal_bins removed = {[0.0:1.0], [3.0:4.0]} iff (enabled);
    }
    wildcard_point: coverpoint wildcard_value {
      wildcard bins ordinary = {4'b?1??};
      wildcard ignore_bins low = {4'b?100} iff (enabled);
      wildcard ignore_bins high = {4'b?101} iff (enabled);
    }
    wildcard_empty_point: coverpoint wildcard_value {
      wildcard bins ordinary = {4'b?1??};
      wildcard ignore_bins low = {4'b?10?} iff (enabled);
      wildcard ignore_bins high = {4'b?11?} iff (enabled);
    }
    unknown_point: coverpoint unknown_value {
      bins ordinary = {2'bx0, 2'bx1};
      ignore_bins removed = {2'bx0} iff (enabled);
      wildcard ignore_bins known = {2'b??} iff (enabled);
    }
    transition_point: coverpoint transition_value {
      bins ordinary = (1 => 2);
      ignore_bins removed = {2} iff (enabled);
    }
    wide_point: coverpoint wide_integral {
      bins ordinary = {[-16:15]};
      ignore_bins removed[] = {[-16:-1]} iff (enabled);
    }
    ignored_cross: cross ignored_point, partner_point {
      option.cross_retain_auto_bins = 0;
      bins removed = binsof(ignored_point.ordinary) intersect {1};
      bins outside = binsof(ignored_point.ordinary) intersect {1, 3};
      bins live = binsof(ignored_point.ordinary) intersect {2};
      bins arithmetic = binsof(ignored_point.ordinary) intersect {selected_value + 1};
      bins negated = !binsof(ignored_point.ordinary) intersect {1};
      bins all_live = ignored_cross with (ignored_point == 2) matches $;
      bins set_live = '{'{2'b10, 1'b0}} matches $;
    }
    illegal_cross: cross illegal_point, partner_point {
      option.cross_retain_auto_bins = 0;
      bins removed = binsof(illegal_point.ordinary) intersect {1};
      bins live = binsof(illegal_point.ordinary) intersect {2};
    }
    real_cross: cross real_point, partner_point {
      option.cross_retain_auto_bins = 0;
      bins removed = binsof(real_point.ordinary) intersect {[0.0:1.0]};
      bins live = binsof(real_point.ordinary) intersect {[1.5:2.5]};
      bins tiny = binsof(real_point.ordinary) intersect {[1.0:1.00000003]};
      bins scalar = binsof(real_point.ordinary) intersect {1.0};
      bins lower = binsof(real_point.ordinary) intersect {[$:1.0]};
      bins upper = binsof(real_point.ordinary) intersect {[1.5:$]};
      bins integer_bounds = binsof(real_point.ordinary) intersect {[1:2]};
    }
    short_cross: cross short_point, partner_point {
      option.cross_retain_auto_bins = 0;
      bins removed = binsof(short_point.ordinary) intersect {[0.0:1.0]};
      bins live = binsof(short_point.ordinary) intersect {[1.5:2.5]};
      bins tiny = binsof(short_point.ordinary) intersect {[1.0:1.00000003]};
      bins scalar = binsof(short_point.ordinary) intersect {1.0};
      bins lower = binsof(short_point.ordinary) intersect {[$:1.0]};
      bins upper = binsof(short_point.ordinary) intersect {[1.5:$]};
      bins integer_bounds = binsof(short_point.ordinary) intersect {[1:2]};
    }
    wildcard_cross: cross wildcard_point, partner_point {
      option.cross_retain_auto_bins = 0;
      bins removed = binsof(wildcard_point.ordinary) intersect {[4:5]};
      bins live = binsof(wildcard_point.ordinary) intersect {[6:7]};
    }
    wildcard_empty_cross: cross wildcard_empty_point, partner_point {
      option.cross_retain_auto_bins = 0;
      bins removed = binsof(wildcard_empty_point.ordinary) intersect {7};
    }
    unknown_cross: cross unknown_point, partner_point {
      option.cross_retain_auto_bins = 0;
      bins removed = binsof(unknown_point.ordinary) intersect {2'bx0};
      bins live = binsof(unknown_point.ordinary) intersect {2'bx1};
      bins known = binsof(unknown_point.ordinary) intersect {2'b01};
    }
    transition_cross: cross transition_point, partner_point {
      option.cross_retain_auto_bins = 0;
      bins live = binsof(transition_point.ordinary) intersect {2};
    }
    wide_cross: cross wide_point, partner_point {
      option.cross_retain_auto_bins = 0;
      bins removed = binsof(wide_point.ordinary) intersect {[-16:-1]};
      bins live = binsof(wide_point.ordinary) intersect {[0:15]};
    }
  endgroup
  cg cov;
  initial begin
    cov = new(1);
    wide_integral = -1;
    transition_value = 1;
    wildcard_value = 4'bx101;
    unknown_value = 2'bx0;
    cov.sample();
    value = 2;
    wide = 2.0;
    narrow = 2.0;
    wildcard_value = 7;
    unknown_value = 2'bx1;
    transition_value = 2;
    wide_integral = 2;
    cov.sample();
    begin
      int c, t;
      real p;
      p = cov.ignored_cross.get_inst_coverage(c, t);
      $display("ignored %0.2f %0d/%0d", p, c, t);
      p = cov.illegal_cross.get_inst_coverage(c, t);
      $display("illegal %0.2f %0d/%0d", p, c, t);
      p = cov.real_cross.get_inst_coverage(c, t);
      $display("real %0.2f %0d/%0d", p, c, t);
      p = cov.short_cross.get_inst_coverage(c, t);
      $display("short %0.2f %0d/%0d", p, c, t);
      p = cov.wildcard_cross.get_inst_coverage(c, t);
      $display("wildcard %0.2f %0d/%0d", p, c, t);
      p = cov.transition_cross.get_inst_coverage(c, t);
      $display("transition %0.2f %0d/%0d", p, c, t);
      p = cov.wide_cross.get_inst_coverage(c, t);
      $display("wide %0.2f %0d/%0d", p, c, t);
    end
    $finish;
  end
endmodule

// Real-typed binsof intersect targets lower to real value sets (kind 2, width
// 64) whose atoms are real intervals (kind 3): unbounded lower [$:1.0] (flags
// 83), unbounded upper [1.5:$] (flags 99) and a scalar 1.0 (flags 3), for both
// real and shortreal coverpoints.
// SCHEMA-DAG: functional_value_atom set={{[0-9]+}} ordinal=0 kind=3 flags=83 lower_expression=0
// SCHEMA-DAG: functional_value_atom set={{[0-9]+}} ordinal=0 kind=3 flags=83 lower_expression=0
// SCHEMA-DAG: functional_value_atom set={{[0-9]+}} ordinal=0 kind=3 flags=99 lower_expression={{[1-9][0-9]*}} upper_expression=0
// SCHEMA-DAG: functional_value_atom set={{[0-9]+}} ordinal=0 kind=3 flags=99 lower_expression={{[1-9][0-9]*}} upper_expression=0
// SCHEMA-DAG: functional_value_atom set={{[0-9]+}} ordinal=0 kind=3 flags=3 lower_expression={{[1-9][0-9]*}} upper_expression=0
// SCHEMA-DAG: functional_value_atom set={{[0-9]+}} ordinal=0 kind=3 flags=3 lower_expression={{[1-9][0-9]*}} upper_expression=0

// OUT: ignored 100.00 5/5
// OUT-NEXT: illegal 100.00 1/1
// OUT-NEXT: real 100.00 4/4
// OUT-NEXT: short 100.00 3/3
// OUT-NEXT: wildcard 100.00 1/1
// OUT-NEXT: transition 100.00 1/1
// OUT-NEXT: wide 100.00 1/1

// WARN-COUNT-3: warning: functional bin singleton contains X or Z and is ignored
// WARN-COUNT-2: warning: cross bin intersect value contains X or Z and is ignored

// Every cross has covered == total == its number of live bins, no automatic bins,
// and exactly the bins below: a bin whose selected values were all removed is
// empty (flag 128, count 0, not contributing); a live bin is hit once. The real
// and shortreal rows pin interval intersection on a surviving open interval:
// [1.0:1.00000003] stays live for real but rounds onto the removed 1.0 for
// shortreal; X/Z bin elements (unknown_cross) leave nothing to select.
// JSON-DAG: "name":"ignored_cross",{{[^]]*}}"covered":5,"total":5,"automatic_covered":{{[0-9]+}},"automatic_total":0,{{[^]]*}}],"bins":[{{[{][^}]*}}"name":"removed",{{[^}]*}}"flags":128,{{[^}]*}}"count":0,{{[^}]*}}"contributing":false,{{[^]]*}}]},{{[{][^}]*}}"name":"outside",{{[^}]*}}"flags":128,{{[^}]*}}"count":0,{{[^}]*}}"contributing":false,{{[^]]*}}]},{{[{][^}]*}}"name":"live",{{[^}]*}}"flags":0,{{[^}]*}}"count":1,{{[^}]*}}"contributing":true,{{[^]]*}}]},{{[{][^}]*}}"name":"arithmetic",{{[^}]*}}"flags":0,{{[^}]*}}"count":1,{{[^}]*}}"contributing":true,{{[^]]*}}]},{{[{][^}]*}}"name":"negated",{{[^}]*}}"flags":0,{{[^}]*}}"count":1,{{[^}]*}}"contributing":true,{{[^]]*}}]},{{[{][^}]*}}"name":"all_live",{{[^}]*}}"flags":0,{{[^}]*}}"count":1,{{[^}]*}}"contributing":true,{{[^]]*}}]},{{[{][^}]*}}"name":"set_live",{{[^}]*}}"flags":0,{{[^}]*}}"count":1,{{[^}]*}}"contributing":true,{{[^]]*}}]}],"automatic_bins":[]
// JSON-DAG: "name":"illegal_cross",{{[^]]*}}"covered":1,"total":1,"automatic_covered":{{[0-9]+}},"automatic_total":0,{{[^]]*}}],"bins":[{{[{][^}]*}}"name":"removed",{{[^}]*}}"flags":128,{{[^}]*}}"count":0,{{[^}]*}}"contributing":false,{{[^]]*}}]},{{[{][^}]*}}"name":"live",{{[^}]*}}"flags":0,{{[^}]*}}"count":1,{{[^}]*}}"contributing":true,{{[^]]*}}]}],"automatic_bins":[]
// JSON-DAG: "name":"real_cross",{{[^]]*}}"covered":4,"total":4,"automatic_covered":{{[0-9]+}},"automatic_total":0,{{[^]]*}}],"bins":[{{[{][^}]*}}"name":"removed",{{[^}]*}}"flags":128,{{[^}]*}}"count":0,{{[^}]*}}"contributing":false,{{[^]]*}}]},{{[{][^}]*}}"name":"live",{{[^}]*}}"flags":0,{{[^}]*}}"count":1,{{[^}]*}}"contributing":true,{{[^]]*}}]},{{[{][^}]*}}"name":"tiny",{{[^}]*}}"flags":0,{{[^}]*}}"count":1,{{[^}]*}}"contributing":true,{{[^]]*}}]},{{[{][^}]*}}"name":"scalar",{{[^}]*}}"flags":128,{{[^}]*}}"count":0,{{[^}]*}}"contributing":false,{{[^]]*}}]},{{[{][^}]*}}"name":"lower",{{[^}]*}}"flags":128,{{[^}]*}}"count":0,{{[^}]*}}"contributing":false,{{[^]]*}}]},{{[{][^}]*}}"name":"upper",{{[^}]*}}"flags":0,{{[^}]*}}"count":1,{{[^}]*}}"contributing":true,{{[^]]*}}]},{{[{][^}]*}}"name":"integer_bounds",{{[^}]*}}"flags":0,{{[^}]*}}"count":1,{{[^}]*}}"contributing":true,{{[^]]*}}]}],"automatic_bins":[]
// JSON-DAG: "name":"short_cross",{{[^]]*}}"covered":3,"total":3,"automatic_covered":{{[0-9]+}},"automatic_total":0,{{[^]]*}}],"bins":[{{[{][^}]*}}"name":"removed",{{[^}]*}}"flags":128,{{[^}]*}}"count":0,{{[^}]*}}"contributing":false,{{[^]]*}}]},{{[{][^}]*}}"name":"live",{{[^}]*}}"flags":0,{{[^}]*}}"count":1,{{[^}]*}}"contributing":true,{{[^]]*}}]},{{[{][^}]*}}"name":"tiny",{{[^}]*}}"flags":128,{{[^}]*}}"count":0,{{[^}]*}}"contributing":false,{{[^]]*}}]},{{[{][^}]*}}"name":"scalar",{{[^}]*}}"flags":128,{{[^}]*}}"count":0,{{[^}]*}}"contributing":false,{{[^]]*}}]},{{[{][^}]*}}"name":"lower",{{[^}]*}}"flags":128,{{[^}]*}}"count":0,{{[^}]*}}"contributing":false,{{[^]]*}}]},{{[{][^}]*}}"name":"upper",{{[^}]*}}"flags":0,{{[^}]*}}"count":1,{{[^}]*}}"contributing":true,{{[^]]*}}]},{{[{][^}]*}}"name":"integer_bounds",{{[^}]*}}"flags":0,{{[^}]*}}"count":1,{{[^}]*}}"contributing":true,{{[^]]*}}]}],"automatic_bins":[]
// JSON-DAG: "name":"wildcard_cross",{{[^]]*}}"covered":1,"total":1,"automatic_covered":{{[0-9]+}},"automatic_total":0,{{[^]]*}}],"bins":[{{[{][^}]*}}"name":"removed",{{[^}]*}}"flags":128,{{[^}]*}}"count":0,{{[^}]*}}"contributing":false,{{[^]]*}}]},{{[{][^}]*}}"name":"live",{{[^}]*}}"flags":0,{{[^}]*}}"count":1,{{[^}]*}}"contributing":true,{{[^]]*}}]}],"automatic_bins":[]
// JSON-DAG: "name":"wildcard_empty_cross",{{[^]]*}}"covered":0,"total":0,"automatic_covered":{{[0-9]+}},"automatic_total":0,{{[^]]*}}],"bins":[{{[{][^}]*}}"name":"removed",{{[^}]*}}"flags":128,{{[^}]*}}"count":0,{{[^}]*}}"contributing":false,{{[^]]*}}]}],"automatic_bins":[]
// JSON-DAG: "name":"unknown_cross",{{[^]]*}}"covered":0,"total":0,"automatic_covered":{{[0-9]+}},"automatic_total":0,{{[^]]*}}],"bins":[{{[{][^}]*}}"name":"removed",{{[^}]*}}"flags":128,{{[^}]*}}"count":0,{{[^}]*}}"contributing":false,{{[^]]*}}]},{{[{][^}]*}}"name":"live",{{[^}]*}}"flags":128,{{[^}]*}}"count":0,{{[^}]*}}"contributing":false,{{[^]]*}}]},{{[{][^}]*}}"name":"known",{{[^}]*}}"flags":128,{{[^}]*}}"count":0,{{[^}]*}}"contributing":false,{{[^]]*}}]}],"automatic_bins":[]
// JSON-DAG: "name":"transition_cross",{{[^]]*}}"covered":1,"total":1,"automatic_covered":{{[0-9]+}},"automatic_total":0,{{[^]]*}}],"bins":[{{[{][^}]*}}"name":"live",{{[^}]*}}"flags":0,{{[^}]*}}"count":1,{{[^}]*}}"contributing":true,{{[^]]*}}]}],"automatic_bins":[]
// JSON-DAG: "name":"wide_cross",{{[^]]*}}"covered":1,"total":1,"automatic_covered":{{[0-9]+}},"automatic_total":0,{{[^]]*}}],"bins":[{{[{][^}]*}}"name":"removed",{{[^}]*}}"flags":128,{{[^}]*}}"count":0,{{[^}]*}}"contributing":false,{{[^]]*}}]},{{[{][^}]*}}"name":"live",{{[^}]*}}"flags":0,{{[^}]*}}"count":1,{{[^}]*}}"contributing":true,{{[^]]*}}]}],"automatic_bins":[]
// Coverpoint ordinary bins: ignored/illegal values are removed from the bin;
// a wildcard ordinary bin with every value removed is empty (flags 144), and the
// X/Z-only ordinary bin is empty (128). Sampled X values never count.
// JSON-DAG: "name":"ignored_point",{{[^]]*}}"covered":1,"total":1,"automatic_covered":{{[0-9]+}},"automatic_total":0,{{[^]]*}}],"bins":[{{[{][^}]*}}"name":"ordinary",{{[^}]*}}"flags":0,{{[^}]*}}"count":1,{{[^}]*}}"contributing":true,{{[^]]*}}
// JSON-DAG: "name":"illegal_point",{{[^]]*}}"covered":1,"total":1,"automatic_covered":{{[0-9]+}},"automatic_total":0,{{[^]]*}}],"bins":[{{[{][^}]*}}"name":"ordinary",{{[^}]*}}"flags":0,{{[^}]*}}"count":1,{{[^}]*}}"contributing":true,{{[^]]*}}
// JSON-DAG: "name":"real_point",{{[^]]*}}"covered":1,"total":1,"automatic_covered":{{[0-9]+}},"automatic_total":0,{{[^]]*}}],"bins":[{{[{][^}]*}}"name":"ordinary",{{[^}]*}}"flags":0,{{[^}]*}}"count":1,{{[^}]*}}"contributing":true,{{[^]]*}}
// JSON-DAG: "name":"short_point",{{[^]]*}}"covered":1,"total":1,"automatic_covered":{{[0-9]+}},"automatic_total":0,{{[^]]*}}],"bins":[{{[{][^}]*}}"name":"ordinary",{{[^}]*}}"flags":0,{{[^}]*}}"count":1,{{[^}]*}}"contributing":true,{{[^]]*}}
// JSON-DAG: "name":"wildcard_point",{{[^]]*}}"covered":1,"total":1,"automatic_covered":{{[0-9]+}},"automatic_total":0,{{[^]]*}}],"bins":[{{[{][^}]*}}"name":"ordinary",{{[^}]*}}"flags":16,{{[^}]*}}"count":1,{{[^}]*}}"contributing":true,{{[^]]*}}
// JSON-DAG: "name":"transition_point",{{[^]]*}}"covered":1,"total":1,"automatic_covered":{{[0-9]+}},"automatic_total":0,{{[^]]*}}],"bins":[{{[{][^}]*}}"name":"ordinary",{{[^}]*}}"flags":0,{{[^}]*}}"count":1,{{[^}]*}}"contributing":true,{{[^]]*}}
// JSON-DAG: "name":"wide_point",{{[^]]*}}"covered":1,"total":1,"automatic_covered":{{[0-9]+}},"automatic_total":0,{{[^]]*}}],"bins":[{{[{][^}]*}}"name":"ordinary",{{[^}]*}}"flags":0,{{[^}]*}}"count":1,{{[^}]*}}"contributing":true,{{[^]]*}}
// JSON-DAG: "name":"wildcard_empty_point",{{[^]]*}}"covered":0,"total":0,"automatic_covered":{{[0-9]+}},"automatic_total":0,{{[^]]*}}],"bins":[{{[{][^}]*}}"name":"ordinary",{{[^}]*}}"flags":144,{{[^}]*}}"count":0,{{[^}]*}}"contributing":false,{{[^]]*}}
// JSON-DAG: "name":"unknown_point",{{[^]]*}}"covered":0,"total":0,"automatic_covered":{{[0-9]+}},"automatic_total":0,{{[^]]*}}],"bins":[{{[{][^}]*}}"name":"ordinary",{{[^}]*}}"flags":128,{{[^}]*}}"count":0,{{[^}]*}}"contributing":false,{{[^]]*}}
// JSON: "illegal_bin_diagnostics":[]
