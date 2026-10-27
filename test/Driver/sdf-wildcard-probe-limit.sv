// RUN: %split-file %s %t
// RUN: cd %t && not %test-talos --test-max-sdf-application-work=8 -emit-slang design.sv -o %t/design.mlir 2> %t/err
// RUN: FileCheck %s < %t/err
// RUN: test ! -e %t/design.mlir

// Production application (not parser coverage): bounded empty wildcard CELLs
// still probe the elaborated instance inventory. Charge that Clause 32.4 work
// even though no IOPATH match or delay update occurs. Four CELLs probe four
// instances each against a lowered limit of eight.
// CHECK-COUNT-1: error: static SDF annotation application-work resource limit exceeded
// CHECK-NOT: warning:

//--- design.sv
`timescale 1fs / 1fs
module sdf_wildcard_probe_leaf;
endmodule
module sdf_wildcard_probe_resource;
  sdf_wildcard_probe_leaf leaf_0();
  sdf_wildcard_probe_leaf leaf_1();
  sdf_wildcard_probe_leaf leaf_2();
  sdf_wildcard_probe_leaf leaf_3();
  initial $sdf_annotate("probes.sdf");
endmodule
//--- probes.sdf
(DELAYFILE
  (SDFVERSION "4.0")
  (TIMESCALE 1fs)
  (CELL (CELLTYPE "sdf_wildcard_probe_leaf") (INSTANCE *))
  (CELL (CELLTYPE "sdf_wildcard_probe_leaf") (INSTANCE *))
  (CELL (CELLTYPE "sdf_wildcard_probe_leaf") (INSTANCE *))
  (CELL (CELLTYPE "sdf_wildcard_probe_leaf") (INSTANCE *))
)
