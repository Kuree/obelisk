// RUN: %obelisk --std=1800-2023 -O0 --target=native \
// RUN:   --coverage=functional -o %t.native %s
// RUN: %t.native --coverage-output=%t.native.obcov --coverage-test=bin-with
// RUN: obelisk-cov report --format=text %t.native.obcov -o %t.native.txt
// RUN: %obelisk --std=1800-2023 -O3 --execution-tier=bytecode \
// RUN:   --coverage=functional -o %t.bytecode %s
// RUN: %t.bytecode --coverage-output=%t.bytecode.obcov \
// RUN:   --coverage-test=bin-with
// RUN: obelisk-cov report --format=text %t.bytecode.obcov -o %t.bytecode.txt
// RUN: diff -u %t.native.txt %t.bytecode.txt
// RUN: FileCheck %s < %t.native.txt

module top;
  bit [3:0] sampled;
  logic signed [3:0] signed_sampled;

  function automatic bit is_odd(bit [3:0] value);
    return value[0];
  endfunction

  covergroup filter_first(input bit [3:0] bound);
    ordered: coverpoint sampled {
      bins pre[2] = {[0:5]} with (item >= bound);
    }
    duplicate_order: coverpoint sampled {
      // 19.5.1 retains both occurrences of 3. B=2, so 4 belongs to bin 1.
      bins duplicate[2] = {3, 3, 4, 5, 6} with (item <= 15);
    }
    function_call: coverpoint sampled {
      bins odds[] = {[0:5]} with (is_odd(item));
    }
  endgroup

  covergroup distribute_first(input bit [3:0] bound);
    type_option.distribute_first = 1;
    ordered: coverpoint sampled {
      bins post[2] = {[0:5]} with (item >= bound);
    }
  endgroup

  covergroup signed_filter;
    signed_cp: coverpoint signed_sampled {
      bins negative[] = {[-3:2]} with (item < 0);
      bins nonnegative[] = {[-3:2]} with (item >= 0);
    }
  endgroup

  filter_first filter_cov;
  distribute_first distribute_cov;
  signed_filter signed_cov;
  initial begin
    filter_cov = new(2);
    distribute_cov = new(2);
    signed_cov = new;
    sampled = 3;
    filter_cov.sample();
    distribute_cov.sample();
    signed_sampled = -2;
    signed_cov.sample();
    signed_sampled = 1;
    signed_cov.sample();
    sampled = 4;
    filter_cov.sample();
    distribute_cov.sample();
    $finish;
  end
endmodule

// Filter-before-distribute selects {2,3,4,5}, then partitions {2,3}/{4,5}.
// CHECK-DAG: bin pre[0]: 1 [covered]
// CHECK-DAG: bin pre[1]: 1 [covered]
// Distribute-before-filter partitions {0,1,2}/{3,4,5}, then filters.
// CHECK-DAG: bin post[0]: 0 [uncovered]
// CHECK-DAG: bin post[1]: 2 [covered]
// Duplicate source occurrences remain in the occurrence stream.
// CHECK-DAG: bin duplicate[0]: 1 [covered]
// CHECK-DAG: bin duplicate[1]: 1 [covered]
// The ordinary constructor expression lowering handles item-dependent calls.
// CHECK-DAG: bin odds[1]: 0 [uncovered]
// CHECK-DAG: bin odds[3]: 1 [covered]
// CHECK-DAG: bin odds[5]: 0 [uncovered]
// Signed source ranges retain their mathematical negative and positive
// domains through construction-time item binding.
// CHECK-DAG: bin negative[-2]: 1 [covered]
// CHECK-DAG: bin nonnegative[1]: 1 [covered]
