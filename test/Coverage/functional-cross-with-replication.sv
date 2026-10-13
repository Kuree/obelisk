// RUN: %obelisk --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: %t --coverage-output=%t.obcov | FileCheck %s --check-prefix=OUT
// RUN: obelisk-cov report --format=json %t.obcov | FileCheck %s --check-prefix=BINS
// RUN: obelisk-cov report --format=text %t.obcov | FileCheck %s --check-prefix=REPORT

// IEEE 1800-2023 11.4.12.1, 11.8.1 and 19.6.1.2. Replications are
// unsigned, preserve four-state planes, and evaluate their payload once.
// Zero-size replications contribute no bits to a containing concatenation.
module top;
  bit [64:0] value;
  covergroup cg;
    option.cross_retain_auto_bins = 0;
    logic [64:0] ca: coverpoint value { bins all = {[0:65'h1_ffff_ffff_ffff_ffff]}; }
    logic cb: coverpoint 1'b0 { bins zero = {0}; }
    repeated: cross ca, cb {
      bins selected = repeated with ({2{ca}} === 130'b0) matches 1;
      bins too_many = repeated with ({2{ca}} === 130'b0) matches 2;
    }
    zero_size: cross ca, cb {
      bins selected = zero_size with ({{0{ca}}, ca} === 65'b0) matches 1;
    }
    nested: cross ca, cb {
      bins selected = nested with ({2{ca[0], ca[1 +: 2]}} === 6'b0) matches 65'h4000_0000_0000_0000;
    }
    unsigned_result: cross ca, cb {
      bins selected = unsigned_result with ({2{signed'(ca)}} >= 130'sb0) matches $;
    }
  endgroup
  covergroup masks(logic [3:0] mask);
    option.cross_retain_auto_bins = 0;
    logic [64:0] ca: coverpoint value { bins all = {[0:65'h1_ffff_ffff_ffff_ffff]}; }
    logic cb: coverpoint 1'b0 { bins zero = {0}; }
    repeated: cross ca, cb {
      bins selected = repeated with ({2{mask, ca[0]}} === 10'bz1x00z1x00) matches 65'h1_0000_0000_0000_0000;
    }
  endgroup
  cg cov;
  masks planes;
  initial begin
    cov = new;
    planes = new(4'bz1x0);
    cov.sample();
    planes.sample();
    $display("replication %0.0f zero %0.0f nested %0.0f unsigned %0.0f planes %0.0f",
             cov.repeated.get_inst_coverage(), cov.zero_size.get_inst_coverage(),
             cov.nested.get_inst_coverage(), cov.unsigned_result.get_inst_coverage(),
             planes.repeated.get_inst_coverage());
    $finish;
  end
endmodule

// Every with-bin covers exactly its matches count of tuples (100%), so the
// 2^65-tuple counts follow from replication semantics: {2{ca}} is zero only
// for ca == 0, nested selects 2^62 values, the unsigned result makes the
// signed comparison always true, and masks[z1x0] matches only ca[0] == 0.
// OUT: replication 100 zero 100 nested 100 unsigned 100 planes 100

// Cross bins are evaluated by predicates, not materialized tuples (19.6.1.2).
// BINS: "resolved_functional_tuples":[]
// A matches count no tuple set can reach leaves the bin empty and excluded.
// BINS-DAG: "name":"too_many","hierarchy":"top.cg.repeated.too_many",{{[^}]*}}"flags":128,{{[^}]*}}"count":0,{{[^}]*}}"excluded":true,{{[^}]*}}"covered":false
// BINS-DAG: "hierarchy":"top.cg.repeated.selected",{{[^}]*}}"count":1,{{[^}]*}}"covered":true
// BINS-DAG: "hierarchy":"top.cg.zero_size.selected",{{[^}]*}}"count":1,{{[^}]*}}"covered":true
// BINS-DAG: "hierarchy":"top.cg.nested.selected",{{[^}]*}}"count":1,{{[^}]*}}"covered":true
// BINS-DAG: "hierarchy":"top.cg.unsigned_result.selected",{{[^}]*}}"count":1,{{[^}]*}}"covered":true
// BINS-DAG: "hierarchy":"top.masks.repeated.selected",{{[^}]*}}"count":1,{{[^}]*}}"covered":true

// REPORT: functional: 100.00% (9/9)
// REPORT-DAG: bin too_many: 0 [excluded] {empty}
// REPORT-DAG: cross unsigned_result: 1/1 (100.00%)
// REPORT-DAG: cross zero_size: 1/1 (100.00%)
// REPORT-DAG: cross nested: 1/1 (100.00%)
