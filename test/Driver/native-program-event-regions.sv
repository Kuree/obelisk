// RUN: talos -O3 %s -o %t.native
// RUN: %t.native > %t.native.out
// RUN: FileCheck %s < %t.native.out
// RUN: talos -O3 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode | diff -u %t.native.out -

program region_program;
  initial begin
    $display("program-reactive");
    #0;
    $display("program-re-inactive");
  end
endprogram

module region_design;
  initial begin
    $display("design-active");
    #0;
    $display("design-inactive");
  end
endmodule

// CHECK: design-active
// CHECK-NEXT: design-inactive
// CHECK-NEXT: program-reactive
// CHECK-NEXT: program-re-inactive
