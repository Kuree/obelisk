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

module strobe_monitor;
  logic [3:0] value;

  function automatic logic [3:0] read_value();
    return value;
  endfunction

  initial begin
    value = 0;
    $monitor("old-monitor=%0d", value);
    $monitor("monitor=%0d", value);
    value = 1;
    value = 2;
    value <= 3;
    $display("active=%0d", value);
    $strobe("strobe=%0d", value);
    $strobe("pure-call=%0d", read_value());
    #1;
    $monitoroff;
    value = 4;
    #1;
    $monitoron;
    value = 5;
    #1 $finish;
  end

  final $display("final=%0d", value);
endmodule

// CHECK: active=2
// CHECK-NEXT: monitor=3
// CHECK-NEXT: strobe=3
// CHECK-NEXT: pure-call=3
// CHECK-NEXT: monitor=5
// CHECK-NEXT: final=5
// CHECK-NOT: old-monitor
