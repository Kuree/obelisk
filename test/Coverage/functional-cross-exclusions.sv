// RUN: %obelisk --std=1800-2023 -O0 --target=native \
// RUN:   --coverage=functional -o %t.native %s
// RUN: not %t.native --coverage-output=%t.native.obcov \
// RUN:   --coverage-test=cross-exclusions > %t.native.out 2> %t.native.err
// RUN: %obelisk --std=1800-2023 -O3 --execution-tier=bytecode \
// RUN:   --coverage=functional -o %t.bytecode %s
// RUN: not %t.bytecode --coverage-output=%t.bytecode.obcov \
// RUN:   --coverage-test=cross-exclusions > %t.bytecode.out 2> %t.bytecode.err
// RUN: diff -u %t.native.out %t.bytecode.out
// RUN: diff -u %t.native.err %t.bytecode.err
// RUN: FileCheck %s --check-prefix=QUERY < %t.native.out
// RUN: FileCheck %s --check-prefix=ERROR < %t.native.err
// RUN: %python -c "import sys; s=open(sys.argv[1]).read(); assert s.count('ERROR: functional coverage illegal bin') == 2" %t.native.err
// RUN: obelisk-cov report --format=text %t.native.obcov -o %t.txt
// RUN: FileCheck %s --check-prefix=REPORT < %t.txt
// RUN: obelisk-cov report --format=json %t.native.obcov -o %t.json
// RUN: %python -c "import json,sys; d=json.load(open(sys.argv[1])); p=next(i for g in d['functional_instance_groups'] for i in g['items'] if i['name']=='product'); b={v['name']:v for v in p['bins']}; assert b['ordinary']['count']==3 and b['ignored']['excluded'] and b['illegal']['excluded']; assert p['automatic_total']==2 and sum(v['count'] for v in p['automatic_bins'])==1; assert len(d['sparse_cross_tuples'])==1; q=d['illegal_bin_diagnostics']; assert len(q)==2 and all(v['count']==1 for v in q) and any(v['bin']==b['illegal']['id'] for v in q)" %t.json
// RUN: not %t.native --coverage-output=%t.zeta.obcov --coverage-test=zeta \
// RUN:   > /dev/null 2> /dev/null
// RUN: not %t.native --coverage-output=%t.alpha.obcov --coverage-test=alpha \
// RUN:   > /dev/null 2> /dev/null
// RUN: obelisk-cov report --format=text %t.zeta.obcov %t.alpha.obcov \
// RUN:   -o %t.merged.txt
// RUN: FileCheck %s --check-prefix=ORDER < %t.merged.txt

module top;
  bit [1:0] a;
  bit [1:0] b;
  bit ignore_on;
  bit illegal_on;

  covergroup cg;
    a_point: coverpoint a {
      bins zero = {0};
      bins one = {1};
      bins two = {2};
    }
    b_point: coverpoint b {
      bins zero = {0};
      bins one = {1};
      bins one_overlap = {1};
      bins two = {2};
    }
    product: cross a_point, b_point {
      bins ordinary = binsof(a_point.zero);
      ignore_bins ignored = binsof(b_point.zero) iff (ignore_on);
      illegal_bins illegal = binsof(b_point) intersect {1} iff (illegal_on);
    }
  endgroup

  cg cov;
  bit [1:0] array_value;
  covergroup array_cg(input int high);
    cp: coverpoint array_value {
      illegal_bins bad[] = {[1:high]};
    }
  endgroup
  array_cg low_config;
  array_cg high_config;
  int covered;
  int total;
  real percentage;

  initial begin
    cov = new;
    low_config = new(1);
    high_config = new(2);

    // Disabled exclusion guards do not suppress an overlapping ordinary bin.
    a = 0;
    b = 0;
    cov.sample();
    b = 1;
    cov.sample();

    // 19.6.2: ignore wins over an overlapping ordinary bin.
    b = 0;
    ignore_on = 1;
    cov.sample();

    // 19.6.3: illegal wins over an overlapping ordinary bin and is retained
    // as one run-time diagnostic hit even though b=1 hits two overlapping
    // target bins selected by the illegal cross bin.
    b = 1;
    illegal_on = 1;
    cov.sample();

    // An ordinary-only selection increments its explicit bin.
    b = 2;
    cov.sample();

    // A tuple outside every user-defined selector increments the symbolic
    // automatic remainder.
    a = 1;
    cov.sample();

    // Resolved bin IDs are configuration-local.  A diagnostic from the
    // high=2 configuration must name bad[2], never bad[1] from high=1.
    array_value = 2;
    high_config.sample();

    percentage = cov.product.get_inst_coverage(covered, total);
    $display("cross-exclusions %.6f %0d %0d", percentage, covered, total);
    $finish;
  end
endmodule

// QUERY: cross-exclusions 66.666667 2 3
// ERROR-DAG: ERROR: functional coverage illegal bin 'top.cg.product.illegal' sampled at simulation time 0
// ERROR-DAG: ERROR: functional coverage illegal bin 'top.array_cg.cp.bad[2]' sampled at simulation time 0
// REPORT: illegal-bin diagnostics:
// REPORT-DAG: top.array_cg.cp.bad[2] instance 3 test "cross-exclusions": 1 at time 0 - "illegal bin sampled"
// REPORT-DAG: top.cg.product.illegal instance 1 test "cross-exclusions": 1 at time 0 - "illegal bin sampled"
// ORDER: illegal-bin diagnostics:
// ORDER-NEXT: top.array_cg.cp.bad[2] instance 3 test "alpha": 1 at time 0 - "illegal bin sampled"
// ORDER-NEXT: top.cg.product.illegal instance 1 test "alpha": 1 at time 0 - "illegal bin sampled"
// ORDER-NEXT: top.array_cg.cp.bad[2] instance 3 test "zeta": 1 at time 0 - "illegal bin sampled"
// ORDER-NEXT: top.cg.product.illegal instance 1 test "zeta": 1 at time 0 - "illegal bin sampled"
