// RUN: %obelisk --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: %t | FileCheck %s

// IEEE 4.4.2.8-9 and 10.4.2: program NBAs commit in Re-NBA after Re-Inactive.
module top;
  program_cov program_instance();
endmodule
program program_cov;
  bit value;
  covergroup cg;
    cp: coverpoint value { bins one = {1}; }
  endgroup
  cg cov;
  initial begin
    cov = new;
    cov.option.weight <= 16;
    cov.option.weight[3:0] <= 3;
    cov.option.comment <= "program rhs";
    cov.option.comment[0] <= "P";
    cg::type_option.weight <= 3;
    cg::type_option.merge_instances <= 0;
    cg::type_option.goal[3:0] <= 3;
    #0 $display("re-inactive %0d %0d", cov.option.weight,
                cg::type_option.weight);
    #1 $display("re-nba %0d %0d %0d %0d %s", cov.option.weight,
                cg::type_option.weight, cg::type_option.merge_instances,
                cg::type_option.goal,
                cov.option.comment);
    $finish;
  end
endprogram

// CHECK: re-inactive 1 1
// CHECK-NEXT: re-nba 19 3 0 99 Program rhs
