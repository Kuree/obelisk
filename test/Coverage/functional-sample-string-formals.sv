// RUN: %obelisk -fno-lto --std=1800-2023 -O0 --target=native \
// RUN:   --coverage=functional -o %t.native %s
// RUN: %t.native --coverage-output=%t.native.obcov \
// RUN:   --coverage-test=sample-string > %t.native.out
// RUN: %obelisk -fno-lto --std=1800-2023 -O3 --execution-tier=bytecode \
// RUN:   --coverage=functional -o %t.bytecode %s
// RUN: %t.bytecode --coverage-output=%t.bytecode.obcov \
// RUN:   --coverage-test=sample-string > %t.bytecode.out
// RUN: diff -u %t.native.out %t.bytecode.out
// RUN: FileCheck %s < %t.native.out

module top;
  string alias_value;

  covergroup cg with function sample(input string tag,
                                     ref string alias_arg,
                                     input string fallback = "ready");
    cp: coverpoint tag.len()
        iff (alias_arg != "skip" && fallback == "ready") {
      bins three = {3};
      bins four = {4};
    }
  endgroup

  cg cov;
  int covered;
  int total;
  real percentage;
  initial begin
    cov = new;
    alias_value = "go";
    cov.sample("abc", alias_value);
    alias_value = "skip";
    cov.sample("four", alias_value);
    alias_value = "go";
    cov.sample("four", alias_value, "not-ready");
    percentage = cov.cp.get_inst_coverage(covered, total);
    $display("sample strings %.6f %0d %0d", percentage, covered, total);
    $finish;
  end
endmodule

// CHECK: sample strings 50.000000 1 2
