// RUN: %obelisk -fno-lto --std=1800-2023 -O0 --target=native \
// RUN:   --coverage=functional -o %t.native %s
// RUN: %t.native --coverage-output=%t.native.obcov \
// RUN:   --coverage-test=cross-bin-iff > %t.native.out
// RUN: %obelisk -fno-lto --std=1800-2023 -O3 --execution-tier=bytecode \
// RUN:   --coverage=functional -o %t.bytecode %s
// RUN: %t.bytecode --coverage-output=%t.bytecode.obcov \
// RUN:   --coverage-test=cross-bin-iff > %t.bytecode.out
// RUN: diff -u %t.native.out %t.bytecode.out
// RUN: FileCheck %s --check-prefix=QUERY < %t.native.out
// RUN: obelisk-cov report --format=json %t.native.obcov -o %t.json
// RUN: %python -c "import json,sys; d=json.load(open(sys.argv[1])); p=next(i for g in d['functional_instance_groups'] for i in g['items'] if i['name']=='product'); e=next(b for b in p['bins'] if b['name']=='selected'); assert e['count']==1; a=p['automatic_bins']; assert p['automatic_total']==2 and sum(b['count'] for b in a)==1; assert len(d['sparse_cross_tuples'])==1" %t.json

module top;
  bit a;
  bit b;
  bit enabled;

  covergroup cg;
    a_point: coverpoint a {
      bins zero = {0};
      bins one = {1};
    }
    b_point: coverpoint b {
      bins zero = {0};
      bins one = {1};
    }
    product: cross a_point, b_point {
      bins selected = binsof(a_point.zero) iff (enabled);
    }
  endgroup

  cg cov;
  int covered;
  int total;
  real percentage;

  initial begin
    cov = new;

    // Clause 19.6: iff guards the explicit bin's counter. The selected tuple
    // remains owned by that bin and must not fall through to an automatic bin.
    a = 0;
    b = 0;
    enabled = 0;
    cov.sample();

    b = 1;
    enabled = 1;
    cov.sample();

    // A tuple outside the explicit selector still contributes to the retained
    // symbolic automatic cross.
    a = 1;
    b = 0;
    cov.sample();

    percentage = cov.product.get_inst_coverage(covered, total);
    $display("cross-bin-iff %.6f %0d %0d", percentage, covered, total);
    $finish;
  end
endmodule

// QUERY: cross-bin-iff 66.666667 2 3
