// RUN: %obelisk --std=1800-2023 --coverage=functional -emit-obelisk %s | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' '--test-obelisk-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' -o %t.mlir
// RUN: obelisk-opt %t.mlir --test-obelisk-print-coverage-schema -o /dev/null 2>&1 | FileCheck %s --check-prefix=SCHEMA
// RUN: obelisk-opt %t.mlir | FileCheck %s --check-prefix=SIM
// RUN: %obelisk --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: not %t --coverage-output=%t.obcov | FileCheck %s --check-prefix=OUT
// RUN: obelisk-cov report --format=text %t.obcov | FileCheck %s --check-prefix=REPORT

// IEEE 1800-2023 19.5 guards each bin's count with iff, and 19.5.5-19.5.6
// allow ignored and illegal fixed-length sequences in the same coverpoint.
// Transition bins of every kind keep the guard as a sample expression
// (role 5) owned by the bin instead of evaluating it while building the schema.
// SCHEMA-DAG: functional_bin id=[[PATH:[1-9][0-9]*]] {{.*}} name=path kind=2 flags=0
// SCHEMA-DAG: functional_bin id=[[IGNORED:[1-9][0-9]*]] {{.*}} name=removed kind=2 flags=4
// SCHEMA-DAG: functional_bin id=[[ILLEGAL:[1-9][0-9]*]] {{.*}} name=bad kind=2 flags=8
// SCHEMA-DAG: functional_bin id=[[GATED:[1-9][0-9]*]] {{.*}} name=gated kind=2 flags=0
// SCHEMA-DAG: functional_bin id=[[CUT:[1-9][0-9]*]] {{.*}} name=cut kind=2 flags=4
// SCHEMA-DAG: functional_bin id=[[TRAP:[1-9][0-9]*]] {{.*}} name=trap kind=2 flags=8
// SCHEMA-DAG: functional_expression id=[[PATH_IFF:[1-9][0-9]*]] owner=[[PATH]] owner_kind=3 role=5 result_kind=1
// SCHEMA-DAG: functional_expression id=[[IGNORED_IFF:[1-9][0-9]*]] owner=[[IGNORED]] owner_kind=3 role=5 result_kind=1
// SCHEMA-DAG: functional_expression id=[[ILLEGAL_IFF:[1-9][0-9]*]] owner=[[ILLEGAL]] owner_kind=3 role=5 result_kind=1
// SCHEMA-DAG: functional_expression id=[[GATED_IFF:[1-9][0-9]*]] owner=[[GATED]] owner_kind=3 role=5 result_kind=1
// SCHEMA-DAG: functional_expression id=[[CUT_IFF:[1-9][0-9]*]] owner=[[CUT]] owner_kind=3 role=5 result_kind=1
// SCHEMA-DAG: functional_expression id=[[TRAP_IFF:[1-9][0-9]*]] owner=[[TRAP]] owner_kind=3 role=5 result_kind=1
// SCHEMA-DAG: functional_bin_plan bin=[[PATH]] value_set=0 iff_expression=[[PATH_IFF]]
// SCHEMA-DAG: functional_bin_plan bin=[[IGNORED]] value_set=0 iff_expression=[[IGNORED_IFF]]
// SCHEMA-DAG: functional_bin_plan bin=[[ILLEGAL]] value_set=0 iff_expression=[[ILLEGAL_IFF]]
// SCHEMA-DAG: functional_bin_plan bin=[[GATED]] value_set=0 iff_expression=[[GATED_IFF]]
// SCHEMA-DAG: functional_bin_plan bin=[[CUT]] value_set=0 iff_expression=[[CUT_IFF]]
// SCHEMA-DAG: functional_bin_plan bin=[[TRAP]] value_set=0 iff_expression=[[TRAP_IFF]]
// SIM: simulation.covergroup.sample {{.*}}values[{{.*}}] ids [{{.*}}!simulation.logic<3>

module top;
  bit [2:0] value;
  bit enable_path = 1, enable_ignore, enable_illegal, gate;
  covergroup cg;
    // Every ordinary word contains the ignored (1 => 2) or illegal (2 => 3).
    cp: coverpoint value {
      bins path = (1 => 2) iff (enable_path);
      bins longer = (1 => 2 => 3) iff (enable_path);
      ignore_bins removed = (1 => 2) iff (enable_ignore);
      illegal_bins bad = (2 => 3) iff (enable_illegal);
      bins fallback_cp = default sequence;
    }
    // The exclusions share no word with the ordinary bins, which survive.
    live: coverpoint value {
      bins gated = (4 => 5) iff (gate);
      bins gated_long = (4 => 5 => 6) iff (gate);
      ignore_bins cut = (6 => 7) iff (enable_ignore);
      illegal_bins trap = (7 => 0) iff (enable_illegal);
      bins fallback_live = default sequence;
    }
  endgroup
  cg cov;
  task automatic take(input bit [2:0] v);
    value = v;
    cov.sample();
  endtask
  initial begin
    cov = new;
    gate = 1;
    take(4); take(5); take(6);
    gate = 0;
    take(4); take(5); take(6);
    gate = 1;
    take(6); take(7); take(0);
    take(1); take(2); take(3);
    $display("guards off cp=%0.2f live=%0.2f", cov.cp.get_inst_coverage(),
             cov.live.get_inst_coverage());
    enable_illegal = 1;
    take(7); take(0);
    take(2); take(3);
    $finish;
  end
endmodule

// The false iff only stops gated from counting its second 4=>5=>6 pass, and
// the false illegal guards report nothing for the first 7=>0 and 2=>3. The
// ignored and illegal sequences stay excluded from the ordinary words and
// from the default sequence whatever their guards say (19.5.5-19.5.6).
// OUT: guards off cp=0.00 live=100.00
// REPORT: functional: 100.00% (2/2)
// REPORT-DAG: coverpoint cp: 0/0 (0.00%)
// REPORT-DAG: bin path: 0 [excluded] {empty}
// REPORT-DAG: bin longer: 0 [excluded] {empty}
// REPORT-DAG: bin removed: 0 [excluded] {ignore}
// REPORT-DAG: bin bad: 0 [excluded] {illegal}
// REPORT-DAG: bin fallback_cp: 12 [excluded] {default sequence}
// REPORT-DAG: coverpoint live: 2/2 (100.00%)
// REPORT-DAG: bin gated: 1 [covered]
// REPORT-DAG: bin gated_long: 1 [covered]
// REPORT-DAG: bin cut: 0 [excluded] {ignore}
// REPORT-DAG: bin trap: 0 [excluded] {illegal}
// REPORT-DAG: bin fallback_live: 10 [excluded] {default sequence}
// REPORT: illegal-bin diagnostics:
// REPORT-DAG: top.cg.cp.bad instance 1 test "default": 1 at time 0 - "illegal bin sampled"
// REPORT-DAG: top.cg.live.trap instance 1 test "default": 1 at time 0 - "illegal bin sampled"
