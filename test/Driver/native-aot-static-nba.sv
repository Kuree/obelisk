// RUN: %split-file %s %t
// RUN: obelisk -O0 --native-scheduler=aot -emit-llvm %t/design.sv \
// RUN:   -o %t/design.ll
// RUN: FileCheck %s --check-prefix=LLVM < %t/design.ll
// RUN: obelisk --native-scheduler=aot %t/design.sv -o %t/native
// RUN: obelisk --native-scheduler=aot --execution-tier=bytecode \
// RUN:   %t/design.sv -o %t/bytecode
// RUN: %t/native > %t/native.out
// RUN: diff -u %t/expected.txt %t/native.out
// RUN: env OBELISK_RT_SIGNAL_DIAGNOSTICS=1 %t/bytecode \
// RUN:   > %t/bytecode.out 2> %t/bytecode.diag
// RUN: diff -u %t/expected.txt %t/bytecode.out
// RUN: FileCheck %s --check-prefix=DIAG < %t/bytecode.diag
// RUN: obelisk -O0 --native-scheduler=auto -emit-llvm %t/string.sv \
// RUN:   -o %t/string.ll
// RUN: FileCheck %s --check-prefix=HYBRID < %t/string.ll
// RUN: obelisk --native-scheduler=auto --execution-tier=bytecode \
// RUN:   %t/string.sv -o %t/string
// RUN: %t/string > %t/string.out
// RUN: diff -u %t/string-expected.txt %t/string.out

// LLVM-DAG: @__obelisk_aot_nba_roots_v1
// LLVM-DAG: @__obelisk_aot_nba_sites_v1
// LLVM-DAG: call i32 @obelisk_rt_v1_scheduler_static_nba
// LLVM-DAG: call i32 @obelisk_rt_v1_scheduler_run_aot_nodes

// DIAG: aot_nba_stages={{[1-9][0-9]*}}
// DIAG-SAME: aot_nba_commits={{[1-9][0-9]*}}
// DIAG-SAME: aot_fallbacks=0

// HYBRID: @__obelisk_aot_schedule_plan_v1
// HYBRID-NOT: call i32 @obelisk_rt_v1_scheduler_static_nba
// HYBRID: call i32 @obelisk_rt_v1_scheduler_string_nba

//--- design.sv
module native_aot_static_nba;
  bit clock;
  logic [7:0] value = 8'h80;

  always_ff @(posedge clock) begin
    value[3:0] <= 4'ha;
    if (value[7])
      value[5:2] <= 4'h3;
  end

  initial begin
    #1 clock = 1;
    #1;
    $display("value=%02h", value);
    $finish;
  end
endmodule

//--- expected.txt
value=8e
//--- string.sv
module native_aot_string_nba;
  string value = "old";

  initial begin
    value <= "new";
    #1;
    $display("value=%s", value);
  end

  initial begin
    #2;
    $finish;
  end
endmodule

//--- string-expected.txt
value=new
