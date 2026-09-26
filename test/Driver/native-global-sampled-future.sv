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

module l18_future_runtime;
  logic gclk = 0, a = 0, endpoint = 0, disabled = 0;
  logic same_enable = 1;
  int passes = 0, fails = 0;
  int same_passes = 0;
  int phase = 1;
  global clocking gcb @(posedge gclk); endclocking

  future_all: assert property (@(posedge endpoint) disable iff (disabled)
      ((phase == 1) && $future_gclk(a) &&
       $rising_gclk(a) && !$falling_gclk(a) &&
       !$steady_gclk(a) && $changing_gclk(a)) ||
      ((phase == 2) && !$rising_gclk(a) && $falling_gclk(a) &&
       !$steady_gclk(a) && $changing_gclk(a)) ||
      ((phase == 3) && $rising_gclk(a) && !$falling_gclk(a)) ||
      ((phase == 4) && !$rising_gclk(a) && !$falling_gclk(a) &&
       $steady_gclk(a) && !$changing_gclk(a)) ||
      ((phase == 5) && $future_gclk(a) && $rising_gclk(a) &&
       !$steady_gclk(a) && $changing_gclk(a)) ||
      ((phase == 6) && $future_gclk(a) && !$rising_gclk(a) &&
       !$falling_gclk(a) && $steady_gclk(a) && !$changing_gclk(a)))
    begin passes++; $display("future pass"); end
  else begin fails++; $display("future FAIL"); end

  same_event: assert property (@(posedge gclk) disable iff (!same_enable)
      $future_gclk(a))
    begin same_passes++; $display("same signal pass"); end
  else begin fails++; $display("same signal FAIL"); end

  initial begin
    // The assertion endpoint and a global edge share this occurrence. The
    // completed attempt must wait for the strictly later global occurrence.
    #1 begin endpoint = 1; gclk = 1; end
    #1 begin endpoint = 0; gclk = 0; a = 1; same_enable = 0; end
    if (passes != 0 || fails != 0) $display("same-tick FAIL");
    #1 gclk = 1;
    #1 gclk = 0;
    if (passes != 1 || same_passes != 1 || fails != 0)
      $display("next-tick FAIL");

    // X to zero is a falling global transition; current X is not zero.
    #1 begin phase = 2; a = 1'bx; end
    #1 endpoint = 1;
    #1 begin endpoint = 0; a = 0; end
    #1 gclk = 1;
    #1 gclk = 0;
    if (passes != 2 || fails != 0) $display("x-falling FAIL");

    // Z to one is a rising global transition; current Z is not one.
    #1 begin phase = 3; a = 1'bz; end
    #1 endpoint = 1;
    #1 begin endpoint = 0; a = 1; end
    #1 gclk = 1;
    #1 gclk = 0;
    if (passes != 3 || fails != 0) $display("z-rising FAIL");

    // Case stability treats X as a value rather than Boolean unknown.
    #1 begin phase = 4; a = 1'bx; end
    #1 endpoint = 1;
    #1 endpoint = 0;
    #1 gclk = 1;
    #1 gclk = 0;
    if (passes != 4 || fails != 0) $display("x-steady FAIL");

    // Two endpoint attempts coexist before one global tick. Each keeps its
    // own current value: 0->1 rises for phase 5 while 1->1 is steady for 6.
    #1 begin phase = 5; a = 0; end
    #1 endpoint = 1;
    #1 begin endpoint = 0; phase = 6; a = 1; end
    #1 endpoint = 1;
    #1 endpoint = 0;
    if (passes != 4 || fails != 0) $display("overlap-early FAIL");
    #1 gclk = 1;
    #1 gclk = 0;
    if (passes != 6 || fails != 0) $display("overlap-resolve FAIL");

    // Active updates that cause the endpoint come after the assertion's
    // Preponed sample. Both the ordinary phase guard and the transition's
    // current value must therefore retain phase 5 / value 0 here.
    #1 begin phase = 5; a = 0; end
    #1 begin endpoint = 1; phase = 99; a = 1; end
    #1 endpoint = 0;
    #1 gclk = 1;
    #1 gclk = 0;
    if (passes != 7 || fails != 0) $display("preponed-endpoint FAIL");

    // Disable after the endpoint cannot cancel the detached completed
    // attempt. It does prevent a new attempt at a later endpoint.
    #1 begin phase = 1; a = 0; end
    #1 endpoint = 1;
    #1 begin endpoint = 0; disabled = 1; a = 1; end
    #1 gclk = 1;
    #1 gclk = 0;
    if (passes != 8 || fails != 0) $display("late-disable FAIL");
    #1 begin endpoint = 1; a = 0; end
    #1 begin endpoint = 0; a = 1; end
    #1 gclk = 1;
    #1 gclk = 0;
    if (passes != 8 || fails != 0) $display("endpoint-disable FAIL");
    $display("DONE %0d %0d", passes, fails);
    $finish;
  end
endmodule

// OUTPUT-COUNT-1: same signal pass
// OUTPUT-COUNT-8: future pass
// OUTPUT: DONE 8 0
// OUTPUT-NOT: FAIL
