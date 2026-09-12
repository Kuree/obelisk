// RUN: %obelisk -fno-lto --std=1800-2023 -O0 --target=native \
// RUN:   --coverage=functional -o %t.native %s
// RUN: %t.native --coverage-output=%t.native.obcov \
// RUN:   --coverage-test=integral-open-ranges > %t.native.out
// RUN: obelisk-cov report --format=text %t.native.obcov -o %t.native.txt
// RUN: obelisk-cov report --format=json %t.native.obcov -o %t.native.json
// RUN: %python -c 'import json,sys; d=json.load(open(sys.argv[1])); items={i["name"]:i for g in d["functional_instance_groups"] for i in g["items"]}; bins=lambda n:{b["name"]:b["count"] for b in items[n]["bins"]}; assert bins("four_state_cp")=={"low":2,"high":2}; assert bins("enum_cp")=={"low":2,"high":2}; assert bins("transition_cp")=={"crossing":2,"expanded[0=>14]":0,"expanded[1=>14]":1,"expanded[0=>15]":0,"expanded[1=>15]":1}' %t.native.json
// RUN: %obelisk -fno-lto --std=1800-2023 -O3 --execution-tier=bytecode \
// RUN:   --coverage=functional -o %t.bytecode %s
// RUN: %t.bytecode --coverage-output=%t.bytecode.obcov \
// RUN:   --coverage-test=integral-open-ranges > %t.bytecode.out
// RUN: obelisk-cov report --format=text %t.bytecode.obcov -o %t.bytecode.txt
// RUN: diff -u %t.native.out %t.bytecode.out
// RUN: diff -u %t.native.txt %t.bytecode.txt
// RUN: FileCheck %s --check-prefix=QUERY < %t.native.out
// RUN: FileCheck %s --check-prefix=REPORT < %t.native.txt

// IEEE 1800-2017 6.20.2.1 and 1800-2023 6.20.7, together with Clause 19,
// define [$:expression] as every effective coverpoint value at or below
// expression, and [expression:$] as every value at or above expression. This
// covers signed, unsigned, four-state, wildcard, array, transition,
// cross-intersection, and unbounded-parameter uses through both backends.
module top;
  parameter int OPEN_BASE = $;
  parameter int OPEN = OPEN_BASE;
  bit [3:0] u;
  logic signed [3:0] s;
  logic [3:0] four_state;
  typedef enum logic [3:0] { TWO = 2, THIRTEEN = 13 } nibble_e;
  nibble_e enumeration;

  covergroup cg;
    unsigned_cp: coverpoint u {
      bins low = {[$:2]};
      bins high = {[13:$]};
      bins parameter_low = {[OPEN:1]};
      bins low_array[] = {[$:1]};
      bins high_array[2] = {[14:$]};
      bins selected = {[$:3]} with (!item[0]);
    }
    signed_cp: coverpoint s {
      bins low = {[$:-6]};
      bins high = {[6:$]};
      bins selected = {[$:-6]} with (item < -5);
    }
    four_state_cp: coverpoint four_state {
      bins low = {[$:2]};
      bins high = {[13:$]};
    }
    enum_cp: coverpoint enumeration {
      bins low = {[$:2]};
      bins high = {[13:$]};
    }
    wildcard_cp: coverpoint u {
      wildcard bins low = {[$:4'b00?1]};
      wildcard bins high = {[4'b1?00:$]};
    }
    transition_cp: coverpoint u {
      bins crossing = ([$:1] => [14:$]);
      bins expanded[] = ([$:1] => [14:$]);
    }
    pair: cross unsigned_cp, signed_cp {
      option.cross_retain_auto_bins = 0;
      bins corners = binsof(unsigned_cp) intersect {[$:2]} &&
                     binsof(signed_cp) intersect {[6:$]};
    }
  endgroup

  cg cov;
  int covered;
  int total;
  real percentage;

  initial begin
    cov = new;

    // X and Z are not numeric members of an integral open range.
    u = 8;
    s = 0;
    four_state = 'x;
    enumeration = nibble_e'(8);
    cov.sample();
    four_state = 'z;
    cov.sample();

    u = 0;
    s = -8;
    four_state = 0;
    enumeration = nibble_e'(0);
    cov.sample();
    u = 1;
    s = -7;
    four_state = 2;
    enumeration = TWO;
    cov.sample();
    u = 14;
    s = 6;
    four_state = 13;
    enumeration = THIRTEEN;
    cov.sample();
    u = 2;
    s = 6;
    four_state = 8;
    enumeration = nibble_e'(8);
    cov.sample();
    u = 1;
    s = 0;
    cov.sample();
    u = 15;
    s = 7;
    four_state = 15;
    enumeration = nibble_e'(15);
    cov.sample();

    percentage = cov.unsigned_cp.get_inst_coverage(covered, total);
    $display("unsigned %.6f %0d %0d", percentage, covered, total);
    percentage = cov.signed_cp.get_inst_coverage(covered, total);
    $display("signed %.6f %0d %0d", percentage, covered, total);
    percentage = cov.four_state_cp.get_inst_coverage(covered, total);
    $display("four-state %.6f %0d %0d", percentage, covered, total);
    percentage = cov.enum_cp.get_inst_coverage(covered, total);
    $display("enum %.6f %0d %0d", percentage, covered, total);
    percentage = cov.wildcard_cp.get_inst_coverage(covered, total);
    $display("wildcard %.6f %0d %0d", percentage, covered, total);
    percentage = cov.transition_cp.get_inst_coverage(covered, total);
    $display("transition %.6f %0d %0d", percentage, covered, total);
    percentage = cov.pair.get_inst_coverage(covered, total);
    $display("cross %.6f %0d %0d", percentage, covered, total);
    $finish;
  end
endmodule

// QUERY: unsigned 100.000000 8 8
// QUERY-NEXT: signed 100.000000 3 3
// QUERY-NEXT: four-state 100.000000 2 2
// QUERY-NEXT: enum 100.000000 2 2
// QUERY-NEXT: wildcard 100.000000 2 2
// QUERY-NEXT: transition 60.000000 3 5
// QUERY-NEXT: cross 100.000000 1 1
// REPORT-DAG: coverpoint unsigned_cp: 8/8 (100.00%)
// REPORT-DAG: bin parameter_low: {{[1-9][0-9]*}} [covered]
// REPORT-DAG: bin low_array[0]: {{[1-9][0-9]*}} [covered]
// REPORT-DAG: bin low_array[1]: {{[1-9][0-9]*}} [covered]
// REPORT-DAG: bin high_array[0]: {{[1-9][0-9]*}} [covered]
// REPORT-DAG: bin high_array[1]: {{[1-9][0-9]*}} [covered]
// REPORT-DAG: coverpoint signed_cp: 3/3 (100.00%)
// REPORT-DAG: coverpoint four_state_cp: 2/2 (100.00%)
// REPORT-DAG: coverpoint enum_cp: 2/2 (100.00%)
// REPORT-DAG: coverpoint wildcard_cp: 2/2 (100.00%)
// REPORT-DAG: coverpoint transition_cp: 3/5 (60.00%)
// REPORT-DAG: cross pair: 1/1 (100.00%)
