// RUN: obelisk -O0 %s -o %t.o0.native
// RUN: %t.o0.native > %t.o0.native.out
// RUN: obelisk -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode > %t.o0.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o0.bytecode.out
// RUN: obelisk -O3 %s -o %t.o3.native
// RUN: %t.o3.native > %t.o3.native.out
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode > %t.o3.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o3.native.out
// RUN: diff -u %t.o0.native.out %t.o3.bytecode.out
// RUN: FileCheck %s < %t.o0.native.out

module native_covergroups;
  logic gate;
  covergroup cg with function sample(input logic [3:0] value,
                                     input bit [1:0] aux);
    cp_value: coverpoint value iff (gate) {
      bins low = {1, 2};
      bins overlap = {[2:3]};
      bins other = default;
    }
    cp_aux: coverpoint aux {
      bins zero = {0};
      bins one = {1};
    }
  endgroup

  cg a;
  cg b;
  int covered;
  int total;

  initial begin
    gate = 1;
    $display("type_empty %.6f %0d %0d",
             cg::get_coverage(covered, total), covered, total);
    a = new;
    $display("initial %.6f %0d %0d",
             a.get_inst_coverage(covered, total), covered, total);
    a.sample(2, 0);
    $display("overlap %.6f %0d %0d",
             a.get_inst_coverage(covered, total), covered, total);
    a.sample(4, 1);
    $display("default %.6f %0d %0d",
             a.get_inst_coverage(covered, total), covered, total);
    a.stop();
    a.sample(1, 1);
    a.start();
    a.sample('x, 1);
    $display("controlled %.6f %0d %0d",
             a.get_inst_coverage(covered, total), covered, total);

    b = new;
    gate = 0;
    b.sample(1, 3);
    gate = 'x;
    b.sample(1, 3);
    gate = 1;
    b.sample('x, 3);
    b.sample('z, 3);
    $display("suppressed %.6f %0d %0d",
             b.get_inst_coverage(covered, total), covered, total);
    $display("zero_outputs %.6f %.6f",
             b.get_inst_coverage(), cg::get_coverage());
    b.sample(1, 0);
    b.sample(1, 0);
    $display("repeated %.6f %0d %0d",
             b.get_inst_coverage(covered, total), covered, total);
    $display("two_instances %.6f %0d %0d",
             cg::get_coverage(covered, total), covered, total);
    a = new;
    $display("retained %.6f %0d %0d",
             cg::get_coverage(covered, total), covered, total);
  end
endmodule

// CHECK: type_empty 0.000000 0 0
// CHECK-NEXT: initial 0.000000 0 5
// CHECK-NEXT: overlap 58.333333 3 5
// CHECK-NEXT: default 100.000000 5 5
// CHECK-NEXT: controlled 100.000000 5 5
// CHECK-NEXT: suppressed 0.000000 0 5
// CHECK-NEXT: zero_outputs 0.000000 50.000000
// CHECK-NEXT: repeated 41.666667 2 5
// CHECK-NEXT: two_instances 70.833333 7 10
// CHECK-NEXT: retained 47.222222 7 15
