// RUN: %obelisk -fno-lto --std=1800-2023 -O0 --target=native \
// RUN:   --coverage=functional -o %t.native %s
// RUN: %t.native --coverage-output=%t.native.obcov \
// RUN:   --coverage-test=computed-events > %t.native.out
// RUN: %obelisk -fno-lto --std=1800-2023 -O3 --execution-tier=bytecode \
// RUN:   --coverage=functional -o %t.bytecode %s
// RUN: %t.bytecode --coverage-output=%t.bytecode.obcov \
// RUN:   --coverage-test=computed-events > %t.bytecode.out
// RUN: diff -u %t.native.out %t.bytecode.out
// RUN: FileCheck %s --check-prefix=QUERY < %t.native.out
// RUN: obelisk-cov report --format=text %t.native.obcov -o %t.native.txt
// RUN: obelisk-cov report --format=text %t.bytecode.obcov -o %t.bytecode.txt
// RUN: diff -u %t.native.txt %t.bytecode.txt
// RUN: FileCheck %s --check-prefix=REPORT < %t.native.txt

module top;
  bit [1:0] full_pair;
  bit [1:0] xor_pair;
  bit [1:0] edge_pair;
  bit [1:0] or_pair;
  bit [1:0] iff_pair;
  bit [1:0] repeat_pair;
  bit [1:0] strobe_pair;
  bit gate;

  covergroup full_change @(full_pair);
    point: coverpoint 1'b1 { bins hit = {1}; }
  endgroup

  covergroup computed_change @(xor_pair[0] ^ xor_pair[1]);
    point: coverpoint 1'b1 { bins hit = {1}; }
  endgroup

  covergroup vector_edge @(posedge edge_pair);
    point: coverpoint 1'b1 { bins hit = {1}; }
  endgroup

  covergroup or_dedup @(or_pair[0] or or_pair[1]);
    point: coverpoint 1'b1 {
      option.at_least = 2;
      bins hit = {1};
    }
  endgroup

  covergroup computed_iff @(posedge (iff_pair[0] ^ iff_pair[1]) iff gate);
    point: coverpoint 1'b1 { bins hit = {1}; }
  endgroup

  covergroup repeat_same_slot @(repeat_pair[0] ^ repeat_pair[1]);
    point: coverpoint 1'b1 {
      option.at_least = 2;
      bins hit = {1};
    }
  endgroup

  covergroup computed_strobe @(strobe_pair[0] ^ strobe_pair[1]);
    type_option.strobe = 1;
    point: coverpoint 1'b1 {
      option.at_least = 2;
      bins hit = {1};
    }
  endgroup

  full_change full_coverage;
  computed_change computed_coverage;
  vector_edge edge_coverage;
  or_dedup or_coverage;
  computed_iff iff_coverage;
  repeat_same_slot repeat_coverage;
  computed_strobe strobe_coverage;
  int covered;
  int total;
  real percentage;

  initial begin
    full_coverage = new;
    computed_coverage = new;
    edge_coverage = new;
    or_coverage = new;
    iff_coverage = new;
    repeat_coverage = new;
    strobe_coverage = new;

    // A change event compares the complete result, not just its LSB.
    full_pair = 2'b10;
    percentage = full_coverage.get_inst_coverage(covered, total);
    $display("full change %.6f %0d %0d", percentage, covered, total);

    // One atomic publication changes both inputs but leaves XOR unchanged.
    xor_pair = 2'b11;
    percentage = computed_coverage.get_inst_coverage(covered, total);
    $display("xor unchanged %.6f %0d %0d", percentage, covered, total);
    xor_pair = 2'b01;
    percentage = computed_coverage.get_inst_coverage(covered, total);
    $display("xor changed %.6f %0d %0d", percentage, covered, total);

    // Explicit edges use only the expression result's packed LSB.
    edge_pair = 2'b10;
    percentage = edge_coverage.get_inst_coverage(covered, total);
    $display("vector msb %.6f %0d %0d", percentage, covered, total);
    edge_pair = 2'b11;
    percentage = edge_coverage.get_inst_coverage(covered, total);
    $display("vector lsb %.6f %0d %0d", percentage, covered, total);

    // Both OR clauses observe one publication, so they trigger one sample.
    or_pair = 2'b11;
    percentage = or_coverage.get_inst_coverage(covered, total);
    $display("or dedup once %.6f %0d %0d", percentage, covered, total);
    or_pair = 2'b00;
    percentage = or_coverage.get_inst_coverage(covered, total);
    $display("or dedup twice %.6f %0d %0d", percentage, covered, total);

    // iff is evaluated only when the computed primary event occurs.
    gate = 0;
    iff_pair = 2'b01;
    gate = 1;
    percentage = iff_coverage.get_inst_coverage(covered, total);
    $display("iff event instant %.6f %0d %0d", percentage, covered, total);
    iff_pair = 2'b00;
    iff_pair = 2'b01;
    percentage = iff_coverage.get_inst_coverage(covered, total);
    $display("iff admitted %.6f %0d %0d", percentage, covered, total);

    // Separate publications in one time slot remain separate nonstrobe events.
    repeat_pair = 2'b01;
    repeat_pair = 2'b00;
    percentage = repeat_coverage.get_inst_coverage(covered, total);
    $display("repeat same slot %.6f %0d %0d", percentage, covered, total);

    // strobe coalesces within a numeric time and samples again at the next.
    strobe_pair = 2'b01;
    strobe_pair = 2'b00;
    #1;
    percentage = strobe_coverage.get_inst_coverage(covered, total);
    $display("strobe coalesced %.6f %0d %0d", percentage, covered, total);
    strobe_pair = 2'b01;
    #1;
    percentage = strobe_coverage.get_inst_coverage(covered, total);
    $display("strobe next time %.6f %0d %0d", percentage, covered, total);
    $finish;
  end
endmodule

// QUERY: full change 100.000000 1 1
// QUERY-NEXT: xor unchanged 0.000000 0 1
// QUERY-NEXT: xor changed 100.000000 1 1
// QUERY-NEXT: vector msb 0.000000 0 1
// QUERY-NEXT: vector lsb 100.000000 1 1
// QUERY-NEXT: or dedup once 0.000000 0 1
// QUERY-NEXT: or dedup twice 100.000000 1 1
// QUERY-NEXT: iff event instant 0.000000 0 1
// QUERY-NEXT: iff admitted 100.000000 1 1
// QUERY-NEXT: repeat same slot 100.000000 1 1
// QUERY-NEXT: strobe coalesced 0.000000 0 1
// QUERY-NEXT: strobe next time 100.000000 1 1
// REPORT: functional: 100.00% (7/7)
