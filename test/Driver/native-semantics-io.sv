// RUN: obelisk -O0 %s -o %t.o0
// RUN: obelisk -O3 %s -o %t.o3
// RUN: %t.o0 | tee %t.o0.out | FileCheck %s
// RUN: %t.o3 | tee %t.o3.out | FileCheck %s
// RUN: diff -u %t.o0.out %t.o3.out

module native_io;
  int descriptor;
  initial begin
    descriptor = $fopen("/dev/stdout", "w");
    $fdisplay(descriptor, "native file io");
    $fclose(descriptor);
    $display("io=%0d", descriptor != 0);
  end
endmodule

// CHECK: native file io
// CHECK-NEXT: io=1
