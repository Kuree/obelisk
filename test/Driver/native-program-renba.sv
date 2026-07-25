// RUN: obelisk -O0 %s -o %t.o0.native
// RUN: %t.o0.native > %t.o0.native.out
// RUN: obelisk -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode > %t.o0.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o0.bytecode.out
// RUN: obelisk -O3 %s -o %t.o3.native
// RUN: %t.o3.native > %t.o3.native.out
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode > %t.o3.bytecode.out
// RUN: diff -u %t.o3.native.out %t.o3.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o3.native.out
// RUN: FileCheck %s < %t.o3.native.out

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
  end
endprogram

// CHECK: program-before=0
// CHECK-NEXT: program-after-zero=0
// CHECK-NEXT: design-wake=7
