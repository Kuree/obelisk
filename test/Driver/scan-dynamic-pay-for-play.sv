// RUN: obelisk -O3 --vpi=off --native-scheduler=generic -emit-llvm %s -o %t.generic.ll
// RUN: obelisk -O3 --vpi=off --execution-tier=bytecode -emit-llvm %s -o %t.bytecode.ll
// RUN: obelisk -O3 --vpi=off --native-scheduler=auto -emit-llvm %s -o %t.auto.ll
// RUN: obelisk -O3 --vpi=off --native-scheduler=aot -emit-llvm %s -o %t.aot.ll
// RUN: FileCheck %s --check-prefix=IR --implicit-check-not=obelisk_rt_v1_file_scan_dynamic \
// RUN:   --implicit-check-not=obelisk_rt_v1_scan_dynamic_validate \
// RUN:   --implicit-check-not=obelisk_rt_v1_string_scan_dynamic < %t.generic.ll
// RUN: FileCheck %s --check-prefix=IR --implicit-check-not=obelisk_rt_v1_file_scan_dynamic \
// RUN:   --implicit-check-not=obelisk_rt_v1_scan_dynamic_validate \
// RUN:   --implicit-check-not=obelisk_rt_v1_string_scan_dynamic < %t.bytecode.ll
// RUN: FileCheck %s --check-prefix=IR --implicit-check-not=obelisk_rt_v1_file_scan_dynamic \
// RUN:   --implicit-check-not=obelisk_rt_v1_scan_dynamic_validate \
// RUN:   --implicit-check-not=obelisk_rt_v1_string_scan_dynamic < %t.auto.ll
// RUN: FileCheck %s --check-prefix=IR --implicit-check-not=obelisk_rt_v1_file_scan_dynamic \
// RUN:   --implicit-check-not=obelisk_rt_v1_scan_dynamic_validate \
// RUN:   --implicit-check-not=obelisk_rt_v1_string_scan_dynamic < %t.aot.ll
// RUN: obelisk -fno-lto -O3 --vpi=off --native-scheduler=generic %s -o %t.generic
// RUN: obelisk -fno-lto -O3 --vpi=off --execution-tier=bytecode %s -o %t.bytecode
// RUN: obelisk -fno-lto -O3 --vpi=off --native-scheduler=auto %s -o %t.auto
// RUN: obelisk -fno-lto -O3 --vpi=off --native-scheduler=aot %s -o %t.aot
// RUN: %t.generic > %t.generic.out
// RUN: %t.bytecode > %t.bytecode.out
// RUN: %t.auto > %t.auto.out
// RUN: %t.aot > %t.aot.out
// RUN: diff -u %t.generic.out %t.bytecode.out
// RUN: diff -u %t.generic.out %t.auto.out
// RUN: diff -u %t.generic.out %t.aot.out
// RUN: FileCheck %s --check-prefix=OUTPUT < %t.generic.out

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
