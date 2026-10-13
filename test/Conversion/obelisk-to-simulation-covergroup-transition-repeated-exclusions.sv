// RUN: %obelisk --std=1800-2023 --coverage=functional -emit-obelisk %s | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' '--test-obelisk-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' -o %t.mlir
// RUN: obelisk-opt %t.mlir --test-obelisk-print-coverage-schema -o /dev/null 2>&1 | FileCheck %s --check-prefix=SCHEMA \
// RUN:   --implicit-check-not='transition_step bin=[[IG1_ORD]] {{.*}}ordinal=2' \
// RUN:   --implicit-check-not='transition_step bin=[[IG2_REM]] {{.*}}ordinal=2' \
// RUN:   --implicit-check-not='transition_step bin=[[IG3_ORD]] {{.*}}ordinal=2' \
// RUN:   --implicit-check-not='transition_step bin=[[IG4_REM]] {{.*}}ordinal=2' \
// RUN:   --implicit-check-not='transition_step bin=[[IL1_ORD]] {{.*}}ordinal=2' \
// RUN:   --implicit-check-not='transition_step bin=[[IL2_REM]] {{.*}}ordinal=2' \
// RUN:   --implicit-check-not='transition_step bin=[[IL3_ORD]] {{.*}}ordinal=2' \
// RUN:   --implicit-check-not='transition_step bin=[[IL4_REM]] {{.*}}ordinal=2'
// RUN: obelisk-opt %t.mlir | FileCheck %s --check-prefix=SIM
// RUN: %obelisk --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: not %t --coverage-output=%t.obcov
// RUN: obelisk-cov report --format=text %t.obcov | FileCheck %s --check-prefix=REPORT
// RUN: obelisk-cov report --format=json %t.obcov | FileCheck %s --check-prefix=JSON

// IEEE 1800-2023 19.5.2 and 19.5.5-19.5.6 allow bounded consecutive
// repetitions in ordinary and exclusion sequences. The schema keeps a
// repetition as one step carrying its count (repetition=2, flag 1) and, for a
// range, the lower and upper bound expressions, instead of one stored step per
// repeated sample. Ignore and illegal kinds share the encoding (flags 4 and 8).
// SCHEMA-DAG: functional_bin id=[[IG1_ORD:[1-9][0-9]*]] {{.*}} name=ordinary kind=2 flags=0 {{.*}}hierarchy=top.cg.ig1.ordinary
// SCHEMA-DAG: functional_bin id=[[IG1_REM:[1-9][0-9]*]] {{.*}} name=removed kind=2 flags=4 {{.*}}hierarchy=top.cg.ig1.removed
// SCHEMA-DAG: functional_bin id=[[IG2_ORD:[1-9][0-9]*]] {{.*}} name=ordinary kind=2 flags=0 {{.*}}hierarchy=top.cg.ig2.ordinary
// SCHEMA-DAG: functional_bin id=[[IG2_REM:[1-9][0-9]*]] {{.*}} name=removed kind=2 flags=4 {{.*}}hierarchy=top.cg.ig2.removed
// SCHEMA-DAG: functional_bin id=[[IG3_ORD:[1-9][0-9]*]] {{.*}} name=ordinary kind=2 flags=0 {{.*}}hierarchy=top.cg.ig3.ordinary
// SCHEMA-DAG: functional_bin id=[[IG3_REM:[1-9][0-9]*]] {{.*}} name=removed kind=2 flags=4 {{.*}}hierarchy=top.cg.ig3.removed
// SCHEMA-DAG: functional_bin id=[[IG4_ORD:[1-9][0-9]*]] {{.*}} name=ordinary kind=2 flags=0 {{.*}}hierarchy=top.cg.ig4.ordinary
// SCHEMA-DAG: functional_bin id=[[IG4_REM:[1-9][0-9]*]] {{.*}} name=removed kind=2 flags=4 {{.*}}hierarchy=top.cg.ig4.removed
// SCHEMA-DAG: functional_bin id=[[IL1_ORD:[1-9][0-9]*]] {{.*}} name=ordinary kind=2 flags=0 {{.*}}hierarchy=top.cg.il1.ordinary
// SCHEMA-DAG: functional_bin id=[[IL1_REM:[1-9][0-9]*]] {{.*}} name=removed kind=2 flags=8 {{.*}}hierarchy=top.cg.il1.removed
// SCHEMA-DAG: functional_bin id=[[IL2_ORD:[1-9][0-9]*]] {{.*}} name=ordinary kind=2 flags=0 {{.*}}hierarchy=top.cg.il2.ordinary
// SCHEMA-DAG: functional_bin id=[[IL2_REM:[1-9][0-9]*]] {{.*}} name=removed kind=2 flags=8 {{.*}}hierarchy=top.cg.il2.removed
// SCHEMA-DAG: functional_bin id=[[IL3_ORD:[1-9][0-9]*]] {{.*}} name=ordinary kind=2 flags=0 {{.*}}hierarchy=top.cg.il3.ordinary
// SCHEMA-DAG: functional_bin id=[[IL3_REM:[1-9][0-9]*]] {{.*}} name=removed kind=2 flags=8 {{.*}}hierarchy=top.cg.il3.removed
// SCHEMA-DAG: functional_bin id=[[IL4_ORD:[1-9][0-9]*]] {{.*}} name=ordinary kind=2 flags=0 {{.*}}hierarchy=top.cg.il4.ordinary
// SCHEMA-DAG: functional_bin id=[[IL4_REM:[1-9][0-9]*]] {{.*}} name=removed kind=2 flags=8 {{.*}}hierarchy=top.cg.il4.removed
// SCHEMA-DAG: transition_step bin=[[IG1_ORD]] {{.*}} lower_expression={{[1-9][0-9]*}} upper_expression=0 {{.*}}ordinal=0 repetition=2 flags=1
// SCHEMA-DAG: transition_step bin=[[IG2_REM]] {{.*}} lower_expression={{[1-9][0-9]*}} upper_expression=0 {{.*}}ordinal=0 repetition=2 flags=1
// SCHEMA-DAG: transition_step bin=[[IG3_ORD]] {{.*}} lower_expression={{[1-9][0-9]*}} upper_expression={{[1-9][0-9]*}} {{.*}}ordinal=0 repetition=2 flags=1
// SCHEMA-DAG: transition_step bin=[[IG4_REM]] {{.*}} lower_expression={{[1-9][0-9]*}} upper_expression={{[1-9][0-9]*}} {{.*}}ordinal=0 repetition=2 flags=1
// SCHEMA-DAG: transition_step bin=[[IL1_ORD]] {{.*}} lower_expression={{[1-9][0-9]*}} upper_expression=0 {{.*}}ordinal=0 repetition=2 flags=1
// SCHEMA-DAG: transition_step bin=[[IL2_REM]] {{.*}} lower_expression={{[1-9][0-9]*}} upper_expression=0 {{.*}}ordinal=0 repetition=2 flags=1
// SCHEMA-DAG: transition_step bin=[[IL3_ORD]] {{.*}} lower_expression={{[1-9][0-9]*}} upper_expression={{[1-9][0-9]*}} {{.*}}ordinal=0 repetition=2 flags=1
// SCHEMA-DAG: transition_step bin=[[IL4_REM]] {{.*}} lower_expression={{[1-9][0-9]*}} upper_expression={{[1-9][0-9]*}} {{.*}}ordinal=0 repetition=2 flags=1
// SIM: simulation.covergroup.sample {{.*}}!simulation.logic<2>

module top;
  bit [1:0] value;
  covergroup cg;
    ig1: coverpoint value { bins ordinary = (0 [* 2] => 1); ignore_bins removed = (0 => 1); }
    ig2: coverpoint value { bins ordinary = (0 => 0 => 1); ignore_bins removed = (0 [* 2] => 1); }
    ig3: coverpoint value { bins ordinary = (0 [* 1:3] => 1); ignore_bins removed = (0 => 0 => 1); }
    ig4: coverpoint value { bins ordinary = (0 => 0 => 0 => 1); ignore_bins removed = (0 [* 2:3] => 1); }
    il1: coverpoint value { bins ordinary = (0 [* 2] => 1); illegal_bins removed = (0 => 1); }
    il2: coverpoint value { bins ordinary = (0 => 0 => 1); illegal_bins removed = (0 [* 2] => 1); }
    il3: coverpoint value { bins ordinary = (0 [* 1:3] => 1); illegal_bins removed = (0 => 0 => 1); }
    il4: coverpoint value { bins ordinary = (0 => 0 => 0 => 1); illegal_bins removed = (0 [* 2:3] => 1); }
  endgroup
  cg cov;
  task automatic take(input bit [1:0] v);
    value = v;
    cov.sample();
  endtask
  initial begin
    cov = new;
    take(0); take(1); take(2);
    take(0); take(0); take(1); take(2);
    take(0); take(0); take(0); take(1); take(2);
    take(0); take(0); take(0); take(0); take(1); take(2);
    $finish;
  end
endmodule

// A removal keeps only the ordinary sequences that can be matched without also
// matching an excluded one (19.5.5): (0[*2]=>1), (0=>0=>1) and (0=>0=>0=>1)
// each contain or equal an excluded word and leave empty bins, while
// (0[*1:3]=>1) keeps (0=>1), which the four 0=>1 endings count. Excluded
// words never count; illegal ones report once per completing sample, so the
// four-, three-, three- and three-completion streams give 4, 3, 3 and 3.
// REPORT-DAG: coverpoint ig1: 0/0 (0.00%)
// REPORT-DAG: coverpoint ig2: 0/0 (0.00%)
// REPORT-DAG: coverpoint ig3: 1/1 (100.00%)
// REPORT-DAG: coverpoint ig4: 0/0 (0.00%)
// REPORT-DAG: coverpoint il1: 0/0 (0.00%)
// REPORT-DAG: coverpoint il2: 0/0 (0.00%)
// REPORT-DAG: coverpoint il3: 1/1 (100.00%)
// REPORT-DAG: coverpoint il4: 0/0 (0.00%)
// REPORT: illegal-bin diagnostics:
// REPORT-DAG: top.cg.il1.removed instance 1 test "default": 4 at time 0 - "illegal bin sampled"
// REPORT-DAG: top.cg.il2.removed instance 1 test "default": 3 at time 0 - "illegal bin sampled"
// REPORT-DAG: top.cg.il3.removed instance 1 test "default": 3 at time 0 - "illegal bin sampled"
// REPORT-DAG: top.cg.il4.removed instance 1 test "default": 3 at time 0 - "illegal bin sampled"
// JSON-DAG: "name":"ig1","comment":""{{[^{]*}}"covered":0,"total":0,{{[^{]*}}"bins":[{{[{]}}"id":{{[0-9]+}},"template_bin":{{[0-9]+}},"name":"ordinary",{{[^}]*}}"flags":128,{{[^}]*}}"count":0,{{[^}]*}}"excluded":true,"contributing":false
// JSON-DAG: "name":"ig2","comment":""{{[^{]*}}"covered":0,"total":0,{{[^{]*}}"bins":[{{[{]}}"id":{{[0-9]+}},"template_bin":{{[0-9]+}},"name":"ordinary",{{[^}]*}}"flags":128,{{[^}]*}}"count":0,{{[^}]*}}"excluded":true,"contributing":false
// JSON-DAG: "name":"ig3","comment":""{{[^{]*}}"covered":1,"total":1,{{[^{]*}}"bins":[{{[{]}}"id":{{[0-9]+}},"template_bin":{{[0-9]+}},"name":"ordinary",{{[^}]*}}"flags":0,{{[^}]*}}"count":4,{{[^}]*}}"excluded":false,"contributing":true
// JSON-DAG: "name":"ig4","comment":""{{[^{]*}}"covered":0,"total":0,{{[^{]*}}"bins":[{{[{]}}"id":{{[0-9]+}},"template_bin":{{[0-9]+}},"name":"ordinary",{{[^}]*}}"flags":128,{{[^}]*}}"count":0,{{[^}]*}}"excluded":true,"contributing":false
// JSON-DAG: "name":"il1","comment":""{{[^{]*}}"covered":0,"total":0,{{[^{]*}}"bins":[{{[{]}}"id":{{[0-9]+}},"template_bin":{{[0-9]+}},"name":"ordinary",{{[^}]*}}"flags":128,{{[^}]*}}"count":0,{{[^}]*}}"excluded":true,"contributing":false
// JSON-DAG: "name":"il2","comment":""{{[^{]*}}"covered":0,"total":0,{{[^{]*}}"bins":[{{[{]}}"id":{{[0-9]+}},"template_bin":{{[0-9]+}},"name":"ordinary",{{[^}]*}}"flags":128,{{[^}]*}}"count":0,{{[^}]*}}"excluded":true,"contributing":false
// JSON-DAG: "name":"il3","comment":""{{[^{]*}}"covered":1,"total":1,{{[^{]*}}"bins":[{{[{]}}"id":{{[0-9]+}},"template_bin":{{[0-9]+}},"name":"ordinary",{{[^}]*}}"flags":0,{{[^}]*}}"count":4,{{[^}]*}}"excluded":false,"contributing":true
// JSON-DAG: "name":"il4","comment":""{{[^{]*}}"covered":0,"total":0,{{[^{]*}}"bins":[{{[{]}}"id":{{[0-9]+}},"template_bin":{{[0-9]+}},"name":"ordinary",{{[^}]*}}"flags":128,{{[^}]*}}"count":0,{{[^}]*}}"excluded":true,"contributing":false
