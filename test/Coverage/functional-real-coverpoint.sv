// RUN: %obelisk --std=1800-2023 -O0 --target=native \
// RUN:   --coverage=functional -o %t.native %s
// RUN: %t.native --coverage-output=%t.native.obcov --coverage-test=real-point \
// RUN:   > %t.native.out 2> %t.native.err
// RUN: obelisk-cov report --format=text %t.native.obcov -o %t.native.txt
// RUN: obelisk-cov report --format=json %t.native.obcov -o %t.native.json
// RUN: %obelisk --std=1800-2023 -O3 --execution-tier=bytecode \
// RUN:   --coverage=functional -o %t.bytecode %s
// RUN: %t.bytecode --coverage-output=%t.bytecode.obcov \
// RUN:   --coverage-test=real-point > %t.bytecode.out 2> %t.bytecode.err
// RUN: obelisk-cov report --format=text %t.bytecode.obcov -o %t.bytecode.txt
// RUN: diff -u %t.native.out %t.bytecode.out
// RUN: diff -u %t.native.err %t.bytecode.err
// RUN: diff -u %t.native.txt %t.bytecode.txt
// RUN: FileCheck %s < %t.native.txt
// RUN: %python -c "import sys; lines=[l for l in open(sys.argv[1]) if 'functional coverpoint' in l]; assert len(lines)==1 and \"cg.overlap'\" in lines[0] and \"bins 'left' and 'right' overlap\" in lines[0]" %t.native.err
// RUN: %python -c "import json,struct,sys; d=json.load(open(sys.argv[1])); o=[x for c in d['functional_configurations'] for x in c['options'] if x['option']==11]; assert len(o)==2 and all(x['value_kind']==5 for x in o); assert sorted(struct.unpack('<d',int(x['value']).to_bytes(8,'little'))[0] for x in o)==[0.1,0.25]" %t.native.json

module top;
  real sampled;

  covergroup cg;
    type_option.real_interval = 0.25;
    option.detect_overlap = 1;
    cp: coverpoint sampled {
      type_option.real_interval = 0.1;
      option.detect_overlap = 0;
      bins exact = {1.5};
      bins interval = {[2.0:3.0]};
      bins absolute = {[4.0+/-0.5]};
      bins relative = {[5.0+%-10.0]};
      bins low = {[$:0.75]};
      bins high = {[1.25:$]};
      bins other = default;
    }
    overlap: coverpoint sampled {
      bins left = {[$:1.0]};
      bins right = {[0.5:$]};
    }
    quiet: coverpoint sampled {
      option.detect_overlap = 0;
      bins left = {[0.0:1.0]};
      bins right = {[0.5:2.0]};
    }
  endgroup

  cg cov;
  initial begin
    cov = new;
    sampled = -100.0;
    cov.sample();
    sampled = 0.75;
    cov.sample();
    sampled = 1.5;
    cov.sample();
    sampled = 2.5;
    cov.sample();
    sampled = 4.5;
    cov.sample();
    sampled = 5.5;
    cov.sample();
    sampled = 10.0;
    cov.sample();
    $finish;
  end
endmodule

// CHECK: functional: 100.00% (10/10)
// CHECK-DAG: coverpoint cp: 6/6 (100.00%)
// CHECK-DAG: bin exact: 1 [covered]
// CHECK-DAG: bin interval: 1 [covered]
// CHECK-DAG: bin absolute: 1 [covered]
// CHECK-DAG: bin relative: 2 [covered]
// CHECK-DAG: bin low: 2 [covered]
// CHECK-DAG: bin high: 5 [covered]
// CHECK-DAG: bin other: 0 [excluded]
// CHECK-DAG: coverpoint overlap: 2/2 (100.00%)
// CHECK-DAG: coverpoint quiet: 2/2 (100.00%)
