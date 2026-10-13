// RUN: obelisk -O3 --vpi=off --native-scheduler=generic -emit-llvm %s -o - | FileCheck %s --check-prefix=IR --implicit-check-not=obelisk_rt_v1_file_scan_dynamic --implicit-check-not=obelisk_rt_v1_scan_dynamic_validate --implicit-check-not=obelisk_rt_v1_string_scan_dynamic
// RUN: obelisk -O3 --vpi=off --execution-tier=bytecode -emit-llvm %s -o - | FileCheck %s --check-prefix=IR --implicit-check-not=obelisk_rt_v1_file_scan_dynamic --implicit-check-not=obelisk_rt_v1_scan_dynamic_validate --implicit-check-not=obelisk_rt_v1_string_scan_dynamic
// RUN: obelisk -O3 --vpi=off --native-scheduler=aot -emit-llvm %s -o - | FileCheck %s --check-prefix=IR --implicit-check-not=obelisk_rt_v1_file_scan_dynamic --implicit-check-not=obelisk_rt_v1_scan_dynamic_validate --implicit-check-not=obelisk_rt_v1_string_scan_dynamic
// RUN: obelisk -O3 --vpi=off --native-scheduler=generic %s -o %t.generic
// RUN: %t.generic > %t.generic.out
// RUN: FileCheck %s --check-prefix=OUTPUT < %t.generic.out
// RUN: obelisk -O3 --vpi=off --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode | diff -u %t.generic.out -

// A design with no dynamic formatted scan must not acquire even declarations
// for the feature ABI, regardless of execution tier or scheduler selection.
module scan_dynamic_pay_for_play;
  bit clock;
  int value;

  always @(posedge clock)
    value <= value + 1;

  initial begin
    value = 41;
    #1 clock = 1;
    #1 $display("value=%0d", value);
    $finish;
  end
endmodule

// IR: define
// OUTPUT: value=42
