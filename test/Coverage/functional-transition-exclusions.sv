// RUN: %obelisk --std=1800-2023 -O0 --target=native \
// RUN:   --coverage=functional -o %t.native %s
// RUN: not %t.native --coverage-output=%t.native.obcov \
// RUN:   --coverage-test=transition-exclusions 2> %t.native.err
// RUN: obelisk-cov report --format=text %t.native.obcov -o %t.native.txt
// RUN: %obelisk --std=1800-2023 -O3 --execution-tier=bytecode \
// RUN:   --coverage=functional -o %t.bytecode %s
// RUN: not %t.bytecode --coverage-output=%t.bytecode.obcov \
// RUN:   --coverage-test=transition-exclusions 2> %t.bytecode.err
// RUN: obelisk-cov report --format=text %t.bytecode.obcov -o %t.bytecode.txt
// RUN: diff -u %t.native.txt %t.bytecode.txt
// RUN: diff -u %t.native.err %t.bytecode.err
// RUN: FileCheck %s < %t.native.txt
// RUN: FileCheck %s --check-prefix=ERROR < %t.native.err
// RUN: %python -c "import sys; s=open(sys.argv[1]).read(); assert s.count('ERROR: functional coverage illegal bin') == 5 and s.count(\"'top.cg.cp.illegal_state'\") == 2" %t.native.err

module top;
  bit [3:0] sampled;
  bit [3:0] other_sampled;
  bit [3:0] symbolic_sampled;
  bit [3:0] union_sampled;

  covergroup cg;
    cp: coverpoint sampled {
      bins removed = (1 => 2 => 3 => 4);
      bins partial = (0 => 2 => 3), (0 => 5 => 6);
      bins ignored_state_bridge = (7 => 2 => 8);
      bins illegal_removed = (8 => 9 => 10 => 11);
      bins illegal_state_bridge = (12 => 10 => 13);
      bins array_removed = (14 => 4 => 5 => 13);
      bins symbolic_partial = (0 => 6 => 7), (0 => 8 => 7);
      bins wildcard_removed = (9 => 12 => 14);
      ignore_bins ignored_state = {2};
      illegal_bins illegal_state = {10};
      ignore_bins cut[] = (2 => 3), (4 => 5);
      illegal_bins bad = (9 => 10);
      ignore_bins symbolic_cut = ([5:6] => 7);
      wildcard illegal_bins wildcard_bad = (4'b100? => 4'b110?);
    }
    // This independent coverpoint deliberately uses the more general
    // wildcard transition language. Exclusions on cp do not constrain it.
    other_cp: coverpoint other_sampled {
      wildcard bins unaffected = (4'b000? => 4'b001?);
    }
  endgroup

  cg cov;
  bit [2:0] default_sampled;
  covergroup default_cg;
    default_cp: coverpoint default_sampled {
      bins removed = (1 => 2 => 3);
      ignore_bins ignored = (1 => 2);
      bins other = default sequence;
    }
  endgroup
  default_cg default_cov;

  // An exclusion that completes before the containing ordinary word must
  // taint that live match without deleting the portions of the symbolic bin
  // that remain reachable.
  covergroup symbolic_cg;
    symbolic_cp: coverpoint symbolic_sampled {
      bins partial_range = ([0:1] => [4:5] => [6:7] => 15);
      wildcard bins wildcard_partial = (4'b10?? => 4'b11?? => 0);
      ignore_bins middle_cut = (1 => 4 => 6);
      wildcard illegal_bins wildcard_middle = (4'b100? => 4'b110?);
    }
  endgroup
  symbolic_cg symbolic_cov;

  // These two exclusions jointly cover the wildcard ordinary language. The
  // post-distribution subtraction must recognize the union exactly and mark
  // the ordinary bin empty without enumerating its values.
  covergroup union_cg;
    union_cp: coverpoint union_sampled {
      wildcard bins removed_by_union = (4'b000? => 4'b01??);
      ignore_bins low_half = (0 => [4:7]);
      ignore_bins high_half = (1 => [4:7]);
    }
  endgroup
  union_cg union_cov;

  task automatic take(input bit [3:0] value);
    sampled = value;
    cov.sample();
  endtask

  initial begin
    cov = new;
    default_cov = new;
    symbolic_cov = new;
    union_cov = new;

    other_sampled = 0;
    take(1);
    other_sampled = 2;
    take(2); take(3); take(4); take(15);
    take(0); take(2); take(3); take(15);
    take(0); take(5); take(6); take(15);
    take(7); take(2); take(8); take(15);
    take(8); take(9); take(10); take(11); take(15);
    take(12); take(10); take(13);
    take(15); take(14); take(4); take(5); take(13);
    take(15); take(0); take(6); take(7);
    take(15); take(0); take(8); take(7);
    take(15); take(9); take(12); take(14);

    // Completion of an ignored sequence prevents default-sequence fallback.
    // The following unrelated transition increments it exactly once.
    default_sampled = 1; default_cov.sample();
    default_sampled = 2; default_cov.sample();
    default_sampled = 4; default_cov.sample();

    // The first word contains middle_cut and must not increment partial_range.
    // The second word is in the surviving symbolic language and increments it
    // exactly once.
    symbolic_sampled = 1; symbolic_cov.sample();
    symbolic_sampled = 4; symbolic_cov.sample();
    symbolic_sampled = 6; symbolic_cov.sample();
    symbolic_sampled = 15; symbolic_cov.sample();
    symbolic_sampled = 14; symbolic_cov.sample();
    symbolic_sampled = 0; symbolic_cov.sample();

    symbolic_sampled = 5; symbolic_cov.sample();
    symbolic_sampled = 7; symbolic_cov.sample();
    symbolic_sampled = 15; symbolic_cov.sample();
    symbolic_sampled = 14; symbolic_cov.sample();
    symbolic_sampled = 8; symbolic_cov.sample();
    symbolic_sampled = 12; symbolic_cov.sample();
    symbolic_sampled = 0; symbolic_cov.sample();
    symbolic_sampled = 14; symbolic_cov.sample();
    symbolic_sampled = 10; symbolic_cov.sample();
    symbolic_sampled = 14; symbolic_cov.sample();
    symbolic_sampled = 0; symbolic_cov.sample();
    $finish;
  end
endmodule

// CHECK: functional: 100.00% (7/7)
// CHECK-DAG: coverpoint other_cp: 1/1 (100.00%)
// CHECK-DAG: bin unaffected: 1 [covered]
// CHECK-DAG: coverpoint cp: 4/4 (100.00%)
// CHECK-DAG: bin removed: 0 [excluded]
// CHECK-DAG: bin partial: 1 [covered]
// CHECK-DAG: bin ignored_state_bridge: 1 [covered]
// CHECK-DAG: bin illegal_removed: 0 [excluded]
// CHECK-DAG: bin illegal_state_bridge: 1 [covered]
// CHECK-DAG: bin array_removed: 0 [excluded]
// CHECK-DAG: bin symbolic_partial: 1 [covered]
// CHECK-DAG: bin wildcard_removed: 0 [excluded]
// CHECK-DAG: bin ignored_state: 0 [excluded]
// CHECK-DAG: bin illegal_state: 0 [excluded]
// CHECK-DAG: bin cut[2=>3]: 0 [excluded]
// CHECK-DAG: bin cut[4=>5]: 0 [excluded]
// CHECK-DAG: bin bad: 0 [excluded]
// CHECK-DAG: bin symbolic_cut: 0 [excluded]
// CHECK-DAG: bin wildcard_bad: 0 [excluded]
// CHECK-DAG: coverpoint symbolic_cp: 2/2 (100.00%)
// CHECK-DAG: bin partial_range: 1 [covered]
// CHECK-DAG: bin wildcard_partial: 1 [covered]
// CHECK-DAG: bin middle_cut: 0 [excluded]
// CHECK-DAG: bin wildcard_middle: 0 [excluded]
// CHECK-DAG: coverpoint union_cp: 0/0 (0.00%)
// CHECK-DAG: bin removed_by_union: 0 [excluded]
// CHECK-DAG: bin low_half: 0 [excluded]
// CHECK-DAG: bin high_half: 0 [excluded]
// CHECK-DAG: coverpoint default_cp: 0/0 (0.00%)
// CHECK-DAG: bin removed: 0 [excluded]
// CHECK-DAG: bin ignored: 0 [excluded]
// CHECK-DAG: bin other: 1 [excluded]
// CHECK: illegal-bin diagnostics:
// CHECK-DAG: top.cg.cp.illegal_state instance 1 test "transition-exclusions": 2 at time 0 - "illegal bin sampled"
// CHECK-DAG: top.cg.cp.bad instance 1 test "transition-exclusions": 1 at time 0 - "illegal bin sampled"
// CHECK-DAG: top.cg.cp.wildcard_bad instance 1 test "transition-exclusions": 1 at time 0 - "illegal bin sampled"
// CHECK-DAG: top.symbolic_cg.symbolic_cp.wildcard_middle instance 3 test "transition-exclusions": 1 at time 0 - "illegal bin sampled"
// ERROR-DAG: ERROR: functional coverage illegal bin 'top.cg.cp.illegal_state' sampled at simulation time 0
// ERROR-DAG: ERROR: functional coverage illegal bin 'top.cg.cp.bad' sampled at simulation time 0
// ERROR-DAG: ERROR: functional coverage illegal bin 'top.cg.cp.wildcard_bad' sampled at simulation time 0
// ERROR-DAG: ERROR: functional coverage illegal bin 'top.symbolic_cg.symbolic_cp.wildcard_middle' sampled at simulation time 0
