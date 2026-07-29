// RUN: %split-file %s %t
// RUN: obelisk -O2 --native-scheduler=aot -emit-llvm %t/design.sv \
// RUN:   -o %t/design.ll
// RUN: FileCheck %s --check-prefix=LLVM < %t/design.ll
// RUN: obelisk -O0 --native-scheduler=aot -emit-llvm %t/design.sv \
// RUN:   -o %t/design-o0.ll
// RUN: FileCheck %s --check-prefix=O0 < %t/design-o0.ll
// RUN: obelisk -O2 --static-specialization=off --native-scheduler=aot \
// RUN:   -emit-llvm %t/design.sv -o %t/design-off.ll
// RUN: FileCheck %s --check-prefix=OFF < %t/design-off.ll
// RUN: obelisk -O0 --static-specialization=on --native-scheduler=aot \
// RUN:   -emit-llvm %t/design.sv -o %t/design-on.ll
// RUN: FileCheck %s --check-prefix=ON < %t/design-on.ll
// RUN: obelisk --native-scheduler=aot %t/design.sv -o %t/native
// RUN: obelisk --native-scheduler=aot --execution-tier=bytecode \
// RUN:   %t/design.sv -o %t/bytecode
// RUN: %t/native > %t/native.out
// RUN: diff -u %t/expected.txt %t/native.out
// RUN: env OBELISK_RT_SIGNAL_DIAGNOSTICS=1 %t/bytecode \
// RUN:   > %t/bytecode.out 2> %t/bytecode.diag
// RUN: diff -u %t/expected.txt %t/bytecode.out
// RUN: FileCheck %s --check-prefix=DIAG < %t/bytecode.diag
// RUN: obelisk -O2 --native-scheduler=aot -emit-llvm %t/wide.sv \
// RUN:   -o %t/wide.ll
// RUN: FileCheck %s --check-prefix=WIDE < %t/wide.ll
// RUN: obelisk -O2 --native-scheduler=aot %t/wide.sv -o %t/wide
// RUN: %t/wide > %t/wide.out
// RUN: diff -u %t/wide-expected.txt %t/wide.out
// RUN: obelisk -O0 --native-scheduler=auto -emit-llvm %t/string.sv \
// RUN:   -o %t/string.ll
// RUN: FileCheck %s --check-prefix=HYBRID < %t/string.ll
// RUN: obelisk --native-scheduler=auto --execution-tier=bytecode \
// RUN:   %t/string.sv -o %t/string
// RUN: %t/string > %t/string.out
// RUN: diff -u %t/string-expected.txt %t/string.out

// LLVM-DAG: @__obelisk_aot_nba_roots_v1
// LLVM-DAG: @__obelisk_aot_nba_sites_v1
// LLVM-DAG: call i32 @obelisk_rt_v1_static_nba_claim
// LLVM-DAG: define i32 @__obelisk_aot_static_nba_commit_v1
// LLVM-DAG: call i32 @obelisk_rt_v1_static_nba_commit_roots
// LLVM-DAG: call i32 @obelisk_rt_v1_scheduler_run_aot_nodes
// LLVM-DAG: @__obelisk_aot_static_actor_roots_v1

// O0-NOT: @__obelisk_aot_nba_roots_v1
// O0-NOT: call i32 @obelisk_rt_v1_static_nba_claim
// O0: call i32 @obelisk_rt_v1_scheduler_nba

// OFF-NOT: @__obelisk_aot_nba_roots_v1
// OFF-NOT: call i32 @obelisk_rt_v1_static_nba_claim
// OFF: call i32 @obelisk_rt_v1_scheduler_nba

// ON: @__obelisk_aot_nba_roots_v1
// ON: call i32 @obelisk_rt_v1_static_nba_claim

// DIAG: aot_nba_stages={{[1-9][0-9]*}}
// DIAG-SAME: aot_nba_commits={{[1-9][0-9]*}}
// DIAG-SAME: aot_fallbacks=0

// WIDE: @__obelisk_aot_nba_accumulator_0 = internal global [104 x i8]
// WIDE-NOT: call i32 @obelisk_rt_v1_native_state_load_plane
// WIDE: load i40, ptr {{.*}}@__obelisk_state_value
// WIDE: store i32 -1, ptr {{.*}}@__obelisk_aot_nba_accumulator_0
// WIDE-NOT: call i32 @obelisk_rt_v1_scheduler_static_nba
// WIDE-NOT: call i32 @obelisk_rt_v1_native_state_load_plane

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
//--- wide.sv
module native_aot_generated_wide_nba;
  bit clock;
  bit [31:0] values [0:7];

  always @(posedge clock) begin
    values[0] <= values[0] + 1;
    if (values[0][0])
      values[0] <= 32'h55;
    values[1] <= values[0];
  end

  initial begin
    values[0] = 1;
    #1 clock = 1;
    #1;
    $display("values=%08h,%08h", values[0], values[1]);
    $finish;
  end
endmodule

//--- wide-expected.txt
values=00000055,00000001
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
