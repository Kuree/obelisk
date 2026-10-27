// XFAIL: *
// RUN: %split-file %s %t
// RUN: talos --std=1800-2023 -emit-talos %t/input.sv -o - | FileCheck %s

// IEEE 1800-2023 19.5 requires an untyped coverpoint to retain the expression's
// self-determined type. Slang v11.0 routes this inference through
// Expression::bindImplicitParam, which forces four-state flat vectors and
// discards declared ranges and packed dimensions. Keep the legal implicit
// declarations as a failing probe; coverage lowering does not repair its AST.
// CHECK-DAG: talos.sv.symbol.iterator @{{[^ ]*}}cu attributes {{.*}}semantic_type = !talos.ranged_packed_array<0 : 64 x !talos.integral<1, false, false, 0 : 0, bit>>
// CHECK-DAG: talos.sv.symbol.iterator @{{[^ ]*}}cn attributes {{.*}}semantic_type = !talos.ranged_packed_array<6 : -58 x !talos.integral<1, false, false, 0 : 0, bit>>
// CHECK-DAG: talos.sv.symbol.iterator @{{[^ ]*}}cp attributes {{.*}}semantic_type = !talos.ranged_packed_array<1 : 0 x !talos.ranged_packed_array<3 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>>

//--- input.sv
module inferred_types;
  bit [0:64] ascending;
  bit [6:-58] negative;
  bit [1:0][3:0] packed_value;
  covergroup cg;
    cu: coverpoint ascending;
    cn: coverpoint negative;
    cp: coverpoint packed_value;
    product: cross cu, cn, cp {
      bins selected = product with (cu[0] == cn[-58] && cp[0] == 0);
    }
  endgroup
  cg cov = new;
endmodule
