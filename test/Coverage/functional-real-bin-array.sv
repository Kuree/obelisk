// RUN: %obelisk --std=1800-2023 -O0 --target=native \
// RUN:   --coverage=functional -o %t.native %s
// RUN: %t.native --coverage-output=%t.native.obcov --coverage-test=real-array
// RUN: obelisk-cov report --format=text %t.native.obcov -o %t.native.txt
// RUN: %obelisk --std=1800-2023 -O3 --execution-tier=bytecode \
// RUN:   --coverage=functional -o %t.bytecode %s
// RUN: %t.bytecode --coverage-output=%t.bytecode.obcov \
// RUN:   --coverage-test=real-array
// RUN: obelisk-cov report --format=text %t.bytecode.obcov -o %t.bytecode.txt
// RUN: diff -u %t.native.txt %t.bytecode.txt
// RUN: FileCheck %s < %t.native.txt

module top;
  real sampled;

  covergroup cg;
    type_option.real_interval = 1.0;
    open_point: coverpoint sampled {
      bins open[] = {[1.0:3.0], 7.5, [8.5+/-0.1]};
    }
    merged_point: coverpoint sampled {
      bins merged[] = {[1.0:3.0], [1.0:3.0]};
    }
    fixed_point: coverpoint sampled {
      bins fixed[5] = {[1.0:3.0], 7.0};
    }
    unbounded_point: coverpoint sampled {
      bins unbounded[] = {3.0, [0.75:$]};
    }
    fine_point: coverpoint sampled {
      type_option.real_interval = 0.01;
      bins fine[] = {[0.75:0.78]};
    }
  endgroup

  real decimal_sampled;
  covergroup decimal_cg;
    decimal_point: coverpoint decimal_sampled {
      type_option.real_interval = 0.1;
      bins decimal[] = {[0.0:1.0]};
    }
  endgroup

  real sliver_sampled;
  covergroup sliver_cg;
    sliver_point: coverpoint sliver_sampled {
      type_option.real_interval = 1.0;
      bins sliver[] = {[0.0:10.000000000000002]};
    }
  endgroup

  cg cov;
  decimal_cg decimal_cov;
  sliver_cg sliver_cov;
  initial begin
    cov = new;
    decimal_cov = new;
    sliver_cov = new;
    sampled = 0.75;  cov.sample();
    sampled = 0.755; cov.sample();
    sampled = 0.765; cov.sample();
    sampled = 0.775; cov.sample();
    sampled = 1.5;   cov.sample();
    sampled = 2.0;   cov.sample();
    sampled = 3.0;   cov.sample();
    sampled = 7.0;   cov.sample();
    sampled = 7.5;   cov.sample();
    sampled = 8.5;   cov.sample();
    for (int i = 0; i < 10; i++) begin
      decimal_sampled = (i + 0.5) / 10.0;
      decimal_cov.sample();
      sliver_sampled = i + 0.5;
      sliver_cov.sample();
    end
    sliver_sampled = 10.000000000000002;
    sliver_cov.sample();
    $finish;
  end
endmodule

// CHECK: functional: 100.00% (35/35)
// CHECK-DAG: coverpoint open_point: 4/4 (100.00%)
// CHECK-DAG: bin open[1.0:2.0): 1 [covered]
// CHECK-DAG: bin open[2.0:3.0]: 2 [covered]
// CHECK-DAG: bin open[7.5]: 1 [covered]
// CHECK-DAG: bin open[8.4:8.6]: 1 [covered]
// CHECK-DAG: coverpoint merged_point: 2/2 (100.00%)
// CHECK-DAG: bin merged[1.0:2.0): 1 [covered]
// CHECK-DAG: bin merged[2.0:3.0]: 2 [covered]
// CHECK-DAG: coverpoint fixed_point: 3/3 (100.00%)
// CHECK-DAG: bin fixed[0]: 1 [covered]
// CHECK-DAG: bin fixed[1]: 2 [covered]
// CHECK-DAG: bin fixed[2]: 1 [covered]
// CHECK-DAG: bin fixed[3]: 0 [excluded]
// CHECK-DAG: bin fixed[4]: 0 [excluded]
// CHECK-DAG: coverpoint unbounded_point: 2/2 (100.00%)
// CHECK-DAG: bin unbounded[3.0]: 1 [covered]
// CHECK-DAG: bin unbounded[0.75:$]: 10 [covered]
// CHECK-DAG: coverpoint fine_point: 3/3 (100.00%)
// CHECK-DAG: bin fine[0.75:0.76): 2 [covered]
// CHECK-DAG: bin fine[0.76:0.77): 1 [covered]
// CHECK-DAG: bin fine[0.77:0.78]: 1 [covered]
// CHECK-DAG: coverpoint decimal_point: 10/10 (100.00%)
// CHECK-DAG: coverpoint sliver_point: 11/11 (100.00%)
