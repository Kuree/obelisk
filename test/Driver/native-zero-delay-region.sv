// RUN: obelisk -fno-lto --native-scheduler=aot %s -o %t.native
// RUN: env OBELISK_RT_SIGNAL_DIAGNOSTICS=1 %t.native > %t.native.out 2> %t.diag
// RUN: obelisk -fno-lto --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode > %t.bytecode.out
// RUN: diff -u %t.native.out %t.bytecode.out
// RUN: FileCheck %s < %t.native.out
// RUN: FileCheck %s --check-prefix=DIAG < %t.diag

module native_zero_delay_region;
  logic trigger;
  logic nba_value;

  initial begin
    trigger = 0;
    nba_value = 0;
    nba_value <= 1;
    #0;
    $display("inactive nba_value=%0d", nba_value);
  end

  initial begin
    trigger = 1;
    $display("active");
  end
endmodule

// CHECK: active
// CHECK-NEXT: inactive nba_value=0
// DIAG: aot_fallbacks=1
