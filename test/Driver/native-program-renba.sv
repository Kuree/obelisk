// RUN: talos -O3 %s -o %t.native
// RUN: %t.native > %t.native.out
// RUN: FileCheck %s < %t.native.out
// RUN: talos -O3 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode | diff -u %t.native.out -

module program_renba_top;
  logic [3:0] value;
  program_renba program_instance(value);

  initial begin
    value = 0;
    @(value);
    $display("design-wake=%0d", value);
  end
endmodule

program program_renba(ref logic [3:0] value);
  initial begin
    $display("program-before=%0d", value);
    value <= 7;
    #0;
    $display("program-after-zero=%0d", value);
    // Keep the program alive through the current slot's Re-NBA commit; an
    // implicit 24.7 $exit at this point would correctly end the simulation.
    #1;
  end
endprogram

// CHECK: program-before=0
// CHECK-NEXT: program-after-zero=0
// CHECK-NEXT: design-wake=7
