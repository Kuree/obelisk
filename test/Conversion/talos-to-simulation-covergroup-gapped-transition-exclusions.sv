// RUN: %talos --std=1800-2023 --coverage=functional -emit-talos %s | talos-opt '--lower-talos-to-sim=opt-level=0' '--test-talos-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' -o %t.mlir
// RUN: talos-opt %t.mlir --test-talos-print-coverage-schema -o /dev/null 2>&1 | FileCheck %s --check-prefix=SCHEMA --implicit-check-not='owner=[[TWO_ITEM]] owner_kind=2 role=2' --implicit-check-not='owner=[[NARROW_ITEM]] owner_kind=2 role=2'
// RUN: talos-opt %t.mlir | FileCheck %s --check-prefix=SIM
// RUN: %talos --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: %t --coverage-output=%t.obcov
// RUN: talos-cov report --format=json %t.obcov | FileCheck %s --check-prefix=JSON

// IEEE 1800-2023 19.5.2 permits unbounded gaps in goto and nonconsecutive
// repetitions; 19.5.5-19.5.6 require bounded excluded words. The schema keeps
// the repetition as a bounded step with the gap flag and marks the sample
// descriptor of a coverpoint whose domain can produce a gap. A two-state
// coverpoint whose declared values span its whole domain has none.
// SCHEMA-DAG: functional_item id=[[TWO_ITEM:[0-9]+]] {{.*}} name=two_state_gaps
// SCHEMA-DAG: functional_item id=[[NARROW_ITEM:[0-9]+]] {{.*}} name=empty_nonconsecutive
// SCHEMA-DAG: functional_item id=[[FOUR_ITEM:[0-9]+]] {{.*}} name=four_state_gaps
// SCHEMA-DAG: functional_item id=[[GOTO_ITEM:[0-9]+]] {{.*}} name=goto_point
// SCHEMA-DAG: functional_bin id=[[GOTO:[0-9]+]] {{.*}} hierarchy=top.cg.goto_point.ordinary
// SCHEMA-DAG: functional_bin id=[[NONCONSECUTIVE:[0-9]+]] {{.*}} hierarchy=top.cg.nonconsecutive_point.ordinary
// SCHEMA-DAG: functional_bin id=[[RANGED_GOTO:[0-9]+]] {{.*}} hierarchy=top.cg.ranged_goto.ordinary
// SCHEMA-DAG: functional_bin id=[[RANGED_NONCONSECUTIVE:[0-9]+]] {{.*}} hierarchy=top.cg.ranged_nonconsecutive.ordinary
// SCHEMA-DAG: functional_bin id=[[FOUR:[0-9]+]] {{.*}} hierarchy=top.cg.four_state_gaps.ordinary
// SCHEMA-DAG: functional_bin id=[[TWO:[0-9]+]] {{.*}} hierarchy=top.cg.two_state_gaps.ordinary
// Steps follow the alternatives. Ending this barrier on a step line instead
// would hide whichever step sorts first, and steps sort by path-derived IDs.
// SCHEMA: transition_alternative
// SCHEMA-DAG: transition_step bin=[[GOTO]] {{.*}} upper_expression=0 {{.*}} ordinal=0 repetition=3 flags=1
// SCHEMA-DAG: transition_step bin=[[NONCONSECUTIVE]] {{.*}} upper_expression=0 {{.*}} ordinal=0 repetition=4 flags=1
// SCHEMA-DAG: transition_step bin=[[RANGED_GOTO]] {{.*}} upper_expression={{[1-9][0-9]*}} {{.*}} ordinal=0 repetition=3 flags=1
// SCHEMA-DAG: transition_step bin=[[RANGED_NONCONSECUTIVE]] {{.*}} upper_expression={{[1-9][0-9]*}} {{.*}} ordinal=0 repetition=4 flags=1
// SCHEMA-DAG: transition_step bin=[[FOUR]] {{.*}} ordinal=0 repetition=3 flags=1
// SCHEMA-DAG: transition_step bin=[[TWO]] {{.*}} ordinal=0 repetition=3 flags=1
// SCHEMA-DAG: functional_expression {{.*}} owner=[[FOUR_ITEM]] owner_kind=2 role=2 {{.*}} flags=8
// SCHEMA-DAG: functional_expression {{.*}} owner=[[GOTO_ITEM]] owner_kind=2 role=2 {{.*}} flags=8
// SIM-DAG: simulation.covergroup.create
// SIM-DAG: simulation.covergroup.sample

module top;
  bit [1:0] value;
  bit narrow;
  logic [1:0] four;
  bit [1:0] two;
  bit enabled;
  bit partner;
  covergroup cg(input int lower, upper);
    goto_point: coverpoint value {
      bins ordinary = (0 [-> 2] => 1);
      ignore_bins removed = (0 => 0) iff (enabled);
    }
    nonconsecutive_point: coverpoint value {
      bins ordinary = (0 [= 2] => 1);
      illegal_bins removed = (0 => 0) iff (enabled);
    }
    ranged_goto: coverpoint value {
      bins ordinary = (0 [-> lower:upper] => 1);
      ignore_bins removed = (0 => 0) iff (enabled);
    }
    ranged_nonconsecutive: coverpoint value {
      bins ordinary = (0 [= lower:upper] => 1);
      ignore_bins removed = (0 => 0) iff (enabled);
    }
    empty_goto: coverpoint value {
      bins ordinary = (0 [-> 1] => 1);
      ignore_bins removed = (0 => 1) iff (enabled);
    }
    live_nonconsecutive: coverpoint value {
      bins ordinary = (0 [= 1] => 1);
      ignore_bins removed = (0 => 1) iff (enabled);
    }
    bit empty_nonconsecutive: coverpoint narrow {
      bins ordinary = (0 [= 1] => 1);
      ignore_bins removed = (0 => 1) iff (enabled);
    }
    four_state_gaps: coverpoint four {
      bins ordinary = ([0:3] [-> 2]);
      ignore_bins removed = ([0:3] => [0:3]) iff (enabled);
    }
    bit [1:0] two_state_gaps: coverpoint two {
      bins ordinary = ([0:3] [-> 2]);
      ignore_bins removed = ([0:3] => [0:3]) iff (enabled);
    }
    side: coverpoint partner { bins zero = {0}; }
    live_product: cross goto_point, side;
    empty_product: cross empty_goto, side;
  endgroup
  cg cov;
  task take_sample(input bit [1:0] v, input logic [1:0] f);
    value = v; narrow = v[0]; two = v; four = f;
    cov.sample();
  endtask
  initial begin
    cov = new(1, 2);
    repeat (16) take_sample(3, 'x);
    take_sample(0, 0);
    take_sample(2, 'x);
    take_sample(0, 1);
    take_sample(1, 'x);
    take_sample(0, 0);
    take_sample(2, 'x);
    take_sample(0, 1);
    take_sample(2, 'x);
    take_sample(1, 0);
    // The exact two-occurrence match contains the ignored pair. The ranged
    // bins still accept a fresh one-occurrence suffix beginning on its last 0.
    take_sample(0, 0);
    take_sample(0, 0);
    take_sample(1, 0);
    $finish;
  end
endmodule

// Exclusion membership ignores iff (19.5.5-19.5.6): every ordinary word that
// cannot avoid an excluded pair is removed. Bins with no surviving word are
// flagged empty (128) and leave the denominator; the others count exactly the
// gap-separated matches that avoid the pair, and no illegal bin fires.
// JSON-DAG: "name":"goto_point","comment":""{{[^{]*}}"covered":1,"total":1,{{[^{]*}}"bins":[{{[{]}}"id":{{[0-9]+}},"template_bin":{{[0-9]+}},"name":"ordinary",{{[^}]*}}"flags":0,{{[^}]*}}"count":1,{{[^}]*}}"excluded":false,"contributing":true
// JSON-DAG: "name":"nonconsecutive_point","comment":""{{[^{]*}}"covered":1,"total":1,{{[^{]*}}"bins":[{{[{]}}"id":{{[0-9]+}},"template_bin":{{[0-9]+}},"name":"ordinary",{{[^}]*}}"flags":0,{{[^}]*}}"count":2,{{[^}]*}}"excluded":false,"contributing":true
// JSON-DAG: "name":"ranged_goto","comment":""{{[^{]*}}"covered":1,"total":1,{{[^{]*}}"bins":[{{[{]}}"id":{{[0-9]+}},"template_bin":{{[0-9]+}},"name":"ordinary",{{[^}]*}}"flags":0,{{[^}]*}}"count":2,{{[^}]*}}"excluded":false,"contributing":true
// JSON-DAG: "name":"ranged_nonconsecutive","comment":""{{[^{]*}}"covered":1,"total":1,{{[^{]*}}"bins":[{{[{]}}"id":{{[0-9]+}},"template_bin":{{[0-9]+}},"name":"ordinary",{{[^}]*}}"flags":0,{{[^}]*}}"count":3,{{[^}]*}}"excluded":false,"contributing":true
// JSON-DAG: "name":"live_nonconsecutive","comment":""{{[^{]*}}"covered":1,"total":1,{{[^{]*}}"bins":[{{[{]}}"id":{{[0-9]+}},"template_bin":{{[0-9]+}},"name":"ordinary",{{[^}]*}}"flags":0,{{[^}]*}}"count":1,{{[^}]*}}"excluded":false,"contributing":true
// JSON-DAG: "name":"four_state_gaps","comment":""{{[^{]*}}"covered":1,"total":1,{{[^{]*}}"bins":[{{[{]}}"id":{{[0-9]+}},"template_bin":{{[0-9]+}},"name":"ordinary",{{[^}]*}}"flags":0,{{[^}]*}}"count":4,{{[^}]*}}"excluded":false,"contributing":true
// JSON-DAG: "name":"empty_goto","comment":""{{[^{]*}}"covered":0,"total":0,{{[^{]*}}"bins":[{{[{]}}"id":{{[0-9]+}},"template_bin":{{[0-9]+}},"name":"ordinary",{{[^}]*}}"flags":128,{{[^}]*}}"count":0,{{[^}]*}}"excluded":true,"contributing":false
// JSON-DAG: "name":"empty_nonconsecutive","comment":""{{[^{]*}}"covered":0,"total":0,{{[^{]*}}"bins":[{{[{]}}"id":{{[0-9]+}},"template_bin":{{[0-9]+}},"name":"ordinary",{{[^}]*}}"flags":128,{{[^}]*}}"count":0,{{[^}]*}}"excluded":true,"contributing":false
// JSON-DAG: "name":"two_state_gaps","comment":""{{[^{]*}}"covered":0,"total":0,{{[^{]*}}"bins":[{{[{]}}"id":{{[0-9]+}},"template_bin":{{[0-9]+}},"name":"ordinary",{{[^}]*}}"flags":128,{{[^}]*}}"count":0,{{[^}]*}}"excluded":true,"contributing":false
// JSON-DAG: "name":"live_product","comment":""{{[^{]*}}"automatic_covered":1,"automatic_total":1,
// JSON-DAG: "name":"empty_product","comment":""{{[^{]*}}"automatic_covered":0,"automatic_total":0,
// JSON: "illegal_bin_diagnostics":[]
