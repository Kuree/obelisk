// RUN: %obelisk -fno-lto --std=1800-2023 -O0 --target=native \
// RUN:   -o %t.native %s
// RUN: %t.native > %t.native.txt
// RUN: %obelisk -fno-lto --std=1800-2023 -O3 --execution-tier=bytecode \
// RUN:   -o %t.bytecode %s
// RUN: %t.bytecode > %t.bytecode.txt
// RUN: diff -u %t.native.txt %t.bytecode.txt
// RUN: FileCheck %s < %t.native.txt

module top;
  bit [1:0] sampled;

  covergroup cg(input int count);
    cp: coverpoint sampled {
      bins values[count] = {[0:3]};
    }
  endgroup

  cg one;
  cg four;
  int covered;
  int total;

  initial begin
    one = new(1);
    four = new(4);
    sampled = 0;
    one.sample();
    $display("one %.6f %0d %0d",
             one.get_inst_coverage(covered, total), covered, total);
    $display("four %.6f %0d %0d",
             four.get_inst_coverage(covered, total), covered, total);
    // The type percentage is the equally weighted average of a 100% instance
    // and a 0% instance. Its optional outputs must describe that same 50%, not
    // the contradictory raw physical-bin sum 1/5.
    $display("type %.6f %0d %0d",
             cg::get_coverage(covered, total), covered, total);
    $finish;
  end
endmodule

// CHECK: one 100.000000 1 1
// CHECK-NEXT: four 0.000000 0 4
// CHECK-NEXT: type 50.000000 1 2
