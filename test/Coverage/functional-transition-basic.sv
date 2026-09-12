// RUN: %obelisk -fno-lto --std=1800-2023 -O0 --target=native \
// RUN:   --coverage=functional -o %t.native %s
// RUN: %t.native --coverage-output=%t.native.obcov --coverage-test=transition \
// RUN:   2> %t.native.err
// RUN: obelisk-cov report --format=text %t.native.obcov -o %t.native.txt
// RUN: FileCheck %s --check-prefix=WARN < %t.native.err
// RUN: %obelisk -fno-lto --std=1800-2023 -O3 --execution-tier=bytecode \
// RUN:   --coverage=functional -o %t.bytecode %s
// RUN: %t.bytecode --coverage-output=%t.bytecode.obcov \
// RUN:   --coverage-test=transition
// RUN: obelisk-cov report --format=text %t.bytecode.obcov -o %t.bytecode.txt
// RUN: diff -u %t.native.txt %t.bytecode.txt
// RUN: FileCheck %s < %t.native.txt

module top;
  bit [2:0] sampled;

  covergroup cg(int base);
    cp: coverpoint sampled {
      bins path = (0 => 1 => 2);
      bins overlap = (1 => 1);
      bins alternatives = (3 => 4), (5 => 6);
      bins configured = (base => base + 1);
    }
  endgroup

  cg cov, second;
  bit [1:0] narrow;
  covergroup narrow_cg;
    narrow_cp: coverpoint narrow {
      bins empty_singleton = (4 => 1);
      bins empty_range = ([4:6] => 1);
      bins mixed = (4 => 1), (2 => 3);
      bins clipped = ([3:5] => 0);
    }
  endgroup
  narrow_cg narrow_cov;
  bit [2:0] repeated;
  covergroup repeat_cg(int low, int high);
    repeat_cp: coverpoint repeated {
      bins exact = (5 [* 3]);
      bins ranged = (6 [* 2:3]);
      bins configured = (1 => 2 [* low:high] => 3);
    }
  endgroup
  repeat_cg repeat_cov;
  covergroup goto_cg(int low, int high);
    goto_cp: coverpoint repeated {
      bins exact = (1 => 2 [-> 3] => 3);
      bins ranged = (4 [-> low:high] => 5);
      bins terminal = (6 [-> 2:3]);
    }
  endgroup
  goto_cg goto_cov;
  covergroup nonconsecutive_cg(int low, int high);
    nonconsecutive_cp: coverpoint repeated {
      bins exact = (1 => 2 [= 3] => 3);
      bins ranged = (4 [= low:high] => 5);
      bins terminal = (6 [= 2:3]);
    }
  endgroup
  nonconsecutive_cg nonconsecutive_cov;
  initial begin
    cov = new(6);
    second = new(0);
    narrow_cov = new;
    repeat_cov = new(2, 3);
    goto_cov = new(2, 3);
    nonconsecutive_cov = new(2, 3);

    sampled = 0; cov.sample();
    sampled = 1; cov.sample();
    sampled = 2; cov.sample();

    // Three adjacent 1 samples contain two overlapping (1 => 1) matches.
    sampled = 1; cov.sample();
    sampled = 1; cov.sample();
    sampled = 1; cov.sample();

    sampled = 3; cov.sample();
    sampled = 4; cov.sample();
    sampled = 5; cov.sample();
    sampled = 6; cov.sample();
    sampled = 7; cov.sample();

    sampled = 0; second.sample();
    sampled = 1; second.sample();

    narrow = 2; narrow_cov.sample();
    narrow = 3; narrow_cov.sample();
    narrow = 3; narrow_cov.sample();
    narrow = 0; narrow_cov.sample();

    repeated = 5; repeat_cov.sample();
    repeated = 5; repeat_cov.sample();
    repeated = 5; repeat_cov.sample();
    repeated = 5; repeat_cov.sample();
    repeated = 6; repeat_cov.sample();
    repeated = 6; repeat_cov.sample();
    repeated = 6; repeat_cov.sample();
    repeated = 1; repeat_cov.sample();
    repeated = 2; repeat_cov.sample();
    repeated = 2; repeat_cov.sample();
    repeated = 3; repeat_cov.sample();

    // Goto repetitions ignore nonmatching samples before and between the
    // required occurrences. The item following the repetition must match on
    // the sample immediately after the selected last occurrence.
    repeated = 1; goto_cov.sample();
    repeated = 2; goto_cov.sample();
    repeated = 0; goto_cov.sample();
    repeated = 2; goto_cov.sample();
    repeated = 2; goto_cov.sample();
    repeated = 0; goto_cov.sample();
    repeated = 3; goto_cov.sample(); // Does not complete across the gap.

    repeated = 1; goto_cov.sample();
    repeated = 0; goto_cov.sample();
    repeated = 2; goto_cov.sample();
    repeated = 0; goto_cov.sample();
    repeated = 2; goto_cov.sample();
    repeated = 7; goto_cov.sample();
    repeated = 2; goto_cov.sample();
    repeated = 3; goto_cov.sample();

    repeated = 4; goto_cov.sample();
    repeated = 0; goto_cov.sample();
    repeated = 4; goto_cov.sample();
    repeated = 5; goto_cov.sample();
    // Both the two- and three-occurrence paths complete on this final 5, but
    // the ranged bin is incremented at most once for the sample.
    repeated = 4; goto_cov.sample();
    repeated = 0; goto_cov.sample();
    repeated = 4; goto_cov.sample();
    repeated = 0; goto_cov.sample();
    repeated = 4; goto_cov.sample();
    repeated = 5; goto_cov.sample();

    repeated = 6; goto_cov.sample();
    repeated = 0; goto_cov.sample();
    repeated = 6; goto_cov.sample();
    repeated = 0; goto_cov.sample();
    repeated = 6; goto_cov.sample();

    // Nonconsecutive repetition also permits gaps after the selected final
    // occurrence, but seeing the repetition value again invalidates that
    // successor-wait candidate.
    repeated = 1; nonconsecutive_cov.sample();
    repeated = 2; nonconsecutive_cov.sample();
    repeated = 0; nonconsecutive_cov.sample();
    repeated = 2; nonconsecutive_cov.sample();
    repeated = 2; nonconsecutive_cov.sample();
    repeated = 0; nonconsecutive_cov.sample();
    repeated = 2; nonconsecutive_cov.sample();
    repeated = 0; nonconsecutive_cov.sample();
    repeated = 3; nonconsecutive_cov.sample(); // Invalidated by the fourth 2.

    repeated = 1; nonconsecutive_cov.sample();
    repeated = 0; nonconsecutive_cov.sample();
    repeated = 2; nonconsecutive_cov.sample();
    repeated = 0; nonconsecutive_cov.sample();
    repeated = 2; nonconsecutive_cov.sample();
    repeated = 7; nonconsecutive_cov.sample();
    repeated = 2; nonconsecutive_cov.sample();
    repeated = 0; nonconsecutive_cov.sample();
    repeated = 7; nonconsecutive_cov.sample();
    repeated = 3; nonconsecutive_cov.sample();

    repeated = 4; nonconsecutive_cov.sample();
    repeated = 0; nonconsecutive_cov.sample();
    repeated = 4; nonconsecutive_cov.sample();
    repeated = 0; nonconsecutive_cov.sample();
    repeated = 4; nonconsecutive_cov.sample();
    repeated = 0; nonconsecutive_cov.sample();
    repeated = 5; nonconsecutive_cov.sample();
    repeated = 4; nonconsecutive_cov.sample();
    repeated = 0; nonconsecutive_cov.sample();
    repeated = 4; nonconsecutive_cov.sample();
    repeated = 0; nonconsecutive_cov.sample();
    repeated = 5; nonconsecutive_cov.sample();

    repeated = 6; nonconsecutive_cov.sample();
    repeated = 0; nonconsecutive_cov.sample();
    repeated = 6; nonconsecutive_cov.sample();
    repeated = 0; nonconsecutive_cov.sample();
    repeated = 7; nonconsecutive_cov.sample();
    repeated = 0; nonconsecutive_cov.sample();
    repeated = 6; nonconsecutive_cov.sample();
    $finish;
  end
endmodule

// CHECK: functional: 92.50% (16/19)
// CHECK-DAG: instance $auto$1: 100.00% (4/4)
// CHECK-DAG: coverpoint cp: 4/4 (100.00%)
// CHECK-DAG: bin path: 1 [covered]
// CHECK-DAG: bin overlap: 2 [covered]
// CHECK-DAG: bin alternatives: 2 [covered]
// CHECK-DAG: bin configured: 1 [covered]
// CHECK-DAG: instance $auto$2: 25.00% (1/4)
// CHECK-DAG: coverpoint narrow_cp: 2/2 (100.00%)
// CHECK-DAG: bin empty_singleton: 0 [excluded]
// CHECK-DAG: bin empty_range: 0 [excluded]
// CHECK-DAG: bin mixed: 1 [covered]
// CHECK-DAG: bin clipped: 1 [covered]
// CHECK-DAG: coverpoint repeat_cp: 3/3 (100.00%)
// CHECK-DAG: bin exact: 2 [covered]
// CHECK-DAG: bin ranged: 2 [covered]
// CHECK-DAG: bin configured: 1 [covered]
// CHECK-DAG: coverpoint goto_cp: 3/3 (100.00%)
// CHECK-DAG: bin exact: 1 [covered]
// CHECK-DAG: bin ranged: 2 [covered]
// CHECK-DAG: bin terminal: 2 [covered]
// CHECK-DAG: coverpoint nonconsecutive_cp: 3/3 (100.00%)
// CHECK-DAG: bin exact: 1 [covered]
// CHECK-DAG: bin ranged: 2 [covered]
// CHECK-DAG: bin terminal: 5 [covered]

// WARN: warning: transition bin singleton is outside the effective coverpoint type and is ignored
// WARN: warning: transition bin range is outside the effective coverpoint type and is ignored
// WARN: warning: transition bin range is clipped to the effective coverpoint type
