// RUN: obelisk --std=1800-2023 -O0 --native-scheduler=generic %s -o %t.generic-o0
// RUN: obelisk --std=1800-2023 -O3 --native-scheduler=generic %s -o %t.generic-o3
// RUN: obelisk --std=1800-2023 -O3 %s -o %t.auto-o3
// RUN: obelisk --std=1800-2023 -O0 --execution-tier=bytecode %s -o %t.bytecode-o0
// RUN: obelisk --std=1800-2023 -O3 --execution-tier=bytecode %s -o %t.bytecode-o3
// RUN: %t.generic-o0 > %t.generic-o0.out
// RUN: %t.generic-o3 > %t.generic-o3.out
// RUN: %t.auto-o3 > %t.auto-o3.out
// RUN: %t.bytecode-o0 > %t.bytecode-o0.out
// RUN: %t.bytecode-o3 > %t.bytecode-o3.out
// RUN: diff -u %t.generic-o0.out %t.generic-o3.out
// RUN: diff -u %t.generic-o0.out %t.auto-o3.out
// RUN: diff -u %t.generic-o0.out %t.bytecode-o0.out
// RUN: diff -u %t.generic-o0.out %t.bytecode-o3.out
// RUN: FileCheck %s --check-prefix=OUTPUT < %t.generic-o0.out

module l18_past_runtime;
  logic gclk = 0, a = 0, endpoint = 0;
  int phase = 1, passes = 0, fails = 0;
  global clocking gcb @(posedge gclk); endclocking

  always @(posedge endpoint)
    $display("dbg %0d past=%b rose=%b fell=%b stable=%b changed=%b a=%b",
             phase, $past_gclk(a), $rose_gclk(a), $fell_gclk(a),
             $stable_gclk(a), $changed_gclk(a), a);

  past_all: assert property (@(posedge endpoint)
      ((phase == 1) && !$past_gclk(a) && $rose_gclk(a) && !$fell_gclk(a) &&
       !$stable_gclk(a) && $changed_gclk(a)) ||
      ((phase == 2) && ($past_gclk(a) === 1'bx) &&
       !$rose_gclk(a) && $fell_gclk(a) &&
       !$stable_gclk(a) && $changed_gclk(a)) ||
      ((phase == 3) && ($past_gclk(a) === 1'bz) &&
       $rose_gclk(a) && !$fell_gclk(a) &&
       !$stable_gclk(a) && $changed_gclk(a)) ||
      ((phase == 4) && ($past_gclk(a) === 1'bx) &&
       !$rose_gclk(a) && !$fell_gclk(a) &&
       $stable_gclk(a) && !$changed_gclk(a)))
    begin passes++; $display("past pass"); end
  else begin fails++; $display("past FAIL phase %0d", phase); end

  task automatic global_sample(input logic value);
    #1 a = value;
    #1 gclk = 1;
    #1 gclk = 0;
  endtask

  task automatic endpoint_sample(input logic value, input int next_phase);
    #1 begin a = value; phase = next_phase; end
    #1 endpoint = 1;
    #1 endpoint = 0;
  endtask

  initial begin
    global_sample(0); endpoint_sample(1, 1);
    global_sample(1'bx); endpoint_sample(0, 2);
    global_sample(1'bz); endpoint_sample(1, 3);
    global_sample(1'bx); endpoint_sample(1'bx, 4);
    #1;
    if (passes != 4 || fails != 0) $display("past summary FAIL");
    $display("DONE %0d %0d", passes, fails);
    $finish;
  end
endmodule

// OUTPUT: dbg 1 past=0 rose=1 fell=0 stable=0 changed=1 a=1
// OUTPUT: past pass
// OUTPUT: dbg 2 past=x rose=0 fell=1 stable=0 changed=1 a=0
// OUTPUT: past pass
// OUTPUT: dbg 3 past=z rose=1 fell=0 stable=0 changed=1 a=1
// OUTPUT: past pass
// OUTPUT: dbg 4 past=x rose=0 fell=0 stable=1 changed=0 a=x
// OUTPUT: past pass
// OUTPUT: DONE 4 0
// OUTPUT-NOT: FAIL
