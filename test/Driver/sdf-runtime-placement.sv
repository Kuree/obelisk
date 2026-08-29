// RUN: cd %S && not obelisk -emit-slang %s -o %t.mlir 2>&1 | FileCheck %s

module sdf_runtime_placement;
  initial
    if (0)
      $sdf_annotate("Inputs/sdf-aot-static.sdf");
  initial begin
    #0 $sdf_annotate("Inputs/sdf-aot-static.sdf");
  end
  initial begin
    #0;
    $sdf_annotate("Inputs/sdf-aot-static.sdf");
  end
  initial begin
    repeat (1)
      $sdf_annotate("Inputs/sdf-aot-static.sdf");
  end
  initial begin
    if (1)
      $sdf_annotate("Inputs/sdf-aot-static.sdf");
  end
endmodule

// CHECK-COUNT-5: error: static $sdf_annotate must be an unconditional, undelayed statement in one initial block
