// RUN: %obelisk --std=1800-2023 -O0 --target=native \
// RUN:   --coverage=functional -o %t.native %s
// RUN: not %t.native --coverage-output=%t.native.obcov --coverage-test=bin-iff \
// RUN:   2> %t.native.err
// RUN: obelisk-cov report --format=text %t.native.obcov -o %t.native.txt
// RUN: not %t.native --coverage-load=%t.native.obcov \
// RUN:   --coverage-output=%t.loaded.obcov --coverage-test=bin-iff-loaded \
// RUN:   2> %t.loaded.err
// RUN: obelisk-cov report --format=json %t.loaded.obcov -o %t.loaded.json
// RUN: %python -c "import collections,json,sys; q=json.load(open(sys.argv[1]))['illegal_bin_diagnostics']; assert len(q)==8 and all(v['count']==1 for v in q); assert collections.Counter((v['test'],v['simulation_time']) for v in q) == collections.Counter({('bin-iff',0):2,('bin-iff',10):2,('bin-iff-loaded',0):2,('bin-iff-loaded',10):2}); assert {v['hierarchy'] for v in q} == {'top.cg.cp.illegal','top.cg.cp.illegal_overlap'}" %t.loaded.json
// RUN: %obelisk --std=1800-2023 -O3 --execution-tier=bytecode \
// RUN:   --coverage=functional -o %t.bytecode %s
// RUN: not %t.bytecode --coverage-output=%t.bytecode.obcov \
// RUN:   --coverage-test=bin-iff 2> %t.bytecode.err
// RUN: obelisk-cov report --format=text %t.bytecode.obcov -o %t.bytecode.txt
// RUN: diff -u %t.native.txt %t.bytecode.txt
// RUN: diff -u %t.native.err %t.bytecode.err
// RUN: FileCheck %s < %t.native.txt
// RUN: FileCheck %s --check-prefix=ERROR < %t.native.err
// RUN: %python -c "import sys; s=open(sys.argv[1]).read(); assert s.count('ERROR: functional coverage illegal bin') == 4 and s.count(\"'top.cg.cp.illegal'\") == 2 and s.count(\"'top.cg.cp.illegal_overlap'\") == 2" %t.native.err

module top;
  bit [2:0] sampled;
  bit point_on;
  bit ordinary_on;
  bit ignore_on;
  bit illegal_on;
  bit default_on;

  covergroup cg;
    cp: coverpoint sampled iff (point_on) {
      bins zero = {0} iff (ordinary_on);
      bins one_overlap = {1};
      bins two_overlap = {2};
      bins three = {3} iff (ordinary_on);
      ignore_bins ignored = {1} iff (ignore_on);
      illegal_bins illegal = {2} iff (illegal_on);
      illegal_bins illegal_overlap = {2} iff (illegal_on);
      bins fallback = default iff (default_on);
    }
  endgroup

  cg cov;
  initial begin
    cov = new;
    default_on = 1;

    // The coverpoint guard suppresses the complete sample.
    sampled = 0;
    ordinary_on = 1;
    cov.sample();

    // A false bin iff suppresses the bin increment but does not remove the
    // value from its defined set, so value 0 must not fall into fallback.
    point_on = 1;
    ordinary_on = 0;
    cov.sample();

    // Disabled ignore/illegal guards do not suppress overlapping ordinary
    // bins; enabled guards do.
    sampled = 1;
    cov.sample();
    ignore_on = 1;
    cov.sample();

    sampled = 2;
    cov.sample();
    illegal_on = 1;
    cov.sample();

    // Exercise the coverpoint and ordinary bin guards in both states.
    sampled = 3;
    point_on = 0;
    ordinary_on = 1;
    cov.sample();
    point_on = 1;
    cov.sample();

    // The scalar default bin has its own independent sampling guard.
    sampled = 4;
    default_on = 0;
    cov.sample();
    default_on = 1;
    cov.sample();

    // A later hit of the same illegal bin is a distinct persisted diagnostic.
    #10;
    sampled = 2;
    illegal_on = 1;
    cov.sample();
    $finish;
  end
endmodule

// CHECK: functional: 75.00% (3/4)
// CHECK: coverpoint cp: 3/4 (75.00%)
// CHECK-DAG: bin zero: 0 [uncovered]
// CHECK-DAG: bin one_overlap: 1 [covered]
// CHECK-DAG: bin two_overlap: 1 [covered]
// CHECK-DAG: bin three: 1 [covered]
// CHECK-DAG: bin ignored: 0 [excluded] {ignore}
// ERROR: ERROR: functional coverage illegal bin 'top.cg.cp.illegal' sampled at simulation time 0
// ERROR: ERROR: functional coverage illegal bin 'top.cg.cp.illegal' sampled at simulation time 10
// CHECK-DAG: bin illegal: 0 [excluded] {illegal}
// CHECK-DAG: bin fallback: 1 [excluded] {default}
