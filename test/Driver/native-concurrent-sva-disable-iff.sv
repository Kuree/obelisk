// RUN: obelisk --std=1800-2023 -O0 --native-scheduler=generic %s -o %t.generic
// RUN: obelisk --std=1800-2023 -O3 --native-scheduler=generic %s -o %t.generic-o3
// RUN: obelisk --std=1800-2023 -O0 --execution-tier=bytecode %s -o %t.bytecode
// RUN: obelisk --std=1800-2023 -O3 --execution-tier=bytecode %s -o %t.bytecode-o3
// RUN: obelisk --std=1800-2023 -O3 --native-scheduler=aot %s -o %t.aot
// RUN: %t.generic > %t.generic.out 2>&1
// RUN: %t.generic-o3 > %t.generic-o3.out 2>&1
// RUN: %t.bytecode > %t.bytecode.out 2>&1
// RUN: %t.bytecode-o3 > %t.bytecode-o3.out 2>&1
// RUN: %t.aot > %t.aot.out 2>&1
// RUN: diff -u %t.generic.out %t.generic-o3.out
// RUN: diff -u %t.generic.out %t.bytecode.out
// RUN: diff -u %t.generic.out %t.bytecode-o3.out
// RUN: diff -u %t.generic.out %t.aot.out
// RUN: FileCheck %s < %t.aot.out
// RUN: obelisk --std=1800-2023 -O0 -emit-sim %s | FileCheck %s --check-prefix=SIM
// RUN: obelisk --std=1800-2023 -O3 --native-scheduler=aot -emit-llvm %s | FileCheck %s --check-prefix=AOT --implicit-check-not=obelisk_rt_v1_scheduler_run_aot_nodes

module native_concurrent_sva_disable_iff;
  logic clk = 0;
  logic explicit_disable = 1, rst_n = 0, other_disable = 0;
  logic held = 1;
  logic cancel_a = 0, cancel_b = 0, unrelated_a = 0, unrelated_b = 0;
  logic x_hit = 0, z_hit = 0;
  logic recover_a = 0, recover_b = 0;
  logic before_hit = 0, nba_hit = 0, queued_hit = 0;
  logic complex_a = 0, complex_b = 1;
  int initial_explicit = 0, initial_default = 0;
  int canceled_explicit = 0, canceled_default = 0, unrelated = 0;
  int x_explicit = 0, x_default = 0, z_explicit = 0, z_default = 0;
  int recovered_explicit = 0, recovered_default = 0;
  int before_explicit = 0, before_default = 0;
  int nba_explicit = 0, nba_default = 0;
  int reset_driver = 0, queued_explicit = 0, queued_default = 0;
  int queued_unrelated = 0, queued_complex = 0;

  default clocking cb @(posedge clk); endclocking
  default disable iff (!rst_n);

  cover property (disable iff (explicit_disable) held)
    initial_explicit = initial_explicit + 1;
  cover property (held) initial_default = initial_default + 1;
  cover property (disable iff (explicit_disable) cancel_a ##1 cancel_b)
    canceled_explicit = canceled_explicit + 1;
  cover property (cancel_a ##1 cancel_b)
    canceled_default = canceled_default + 1;
  cover property (disable iff (other_disable) unrelated_a ##1 unrelated_b)
    unrelated = unrelated + 1;
  cover property (disable iff (explicit_disable) x_hit)
    x_explicit = x_explicit + 1;
  cover property (x_hit) x_default = x_default + 1;
  cover property (disable iff (explicit_disable) z_hit)
    z_explicit = z_explicit + 1;
  cover property (z_hit) z_default = z_default + 1;
  cover property (disable iff (explicit_disable) recover_a ##1 recover_b)
    recovered_explicit = recovered_explicit + 1;
  cover property (recover_a ##1 recover_b)
    recovered_default = recovered_default + 1;
  cover property (disable iff (explicit_disable) before_hit)
    before_explicit = before_explicit + 1;
  cover property (before_hit) before_default = before_default + 1;
  cover property (disable iff (explicit_disable) nba_hit)
    nba_explicit = nba_explicit + 1;
  cover property (nba_hit) nba_default = nba_default + 1;

  // This callback is source-ordered before the following callbacks. Its
  // Reactive reset transition must invalidate results that were already
  // queued by Observed evaluation, without affecting an unrelated disable.
  cover property (disable iff (other_disable) queued_hit) begin
    reset_driver = reset_driver + 1;
    explicit_disable = 1;
    rst_n = 0;
    complex_a = 1;
    explicit_disable = 0;
    rst_n = 1;
    complex_a = 0;
  end
  cover property (disable iff (explicit_disable) queued_hit)
    queued_explicit = queued_explicit + 1;
  cover property (queued_hit) queued_default = queued_default + 1;
  cover property (disable iff (other_disable) queued_hit)
    queued_unrelated = queued_unrelated + 1;
  cover property (disable iff (complex_a && complex_b) queued_hit)
    queued_complex = queued_complex + 1;

  initial begin
    // Initially true disables remain effective across multiple clocks.
    #1 clk = 1;
    #1 clk = 0;
    #1 clk = 1;
    #1 begin
      clk = 0;
      held = 0;
      explicit_disable = 0;
      rst_n = 1;
      cancel_a = 1;
      unrelated_a = 1;
    end

    // Start two bounded attempts, then cancel them without a clock. The
    // unrelated attempt has its own false disable and must survive.
    #1 clk = 1;
    #1 clk = 0;
    #1 begin explicit_disable = 1; rst_n = 0; end
    #1 begin
      explicit_disable = 0;
      rst_n = 1;
      cancel_b = 1;
      unrelated_b = 1;
    end
    #1 clk = 1;
    #1 begin
      clk = 0;
      cancel_a = 0;
      cancel_b = 0;
      unrelated_a = 0;
      unrelated_b = 0;
    end

    // X and Z are not true disable conditions.
    #1 begin explicit_disable = 1'bx; rst_n = 1'bx; x_hit = 1; end
    #1 clk = 1;
    #1 begin
      clk = 0;
      x_hit = 0;
      explicit_disable = 1'bz;
      rst_n = 1'bz;
      z_hit = 1;
    end
    #1 clk = 1;
    #1 begin
      clk = 0;
      z_hit = 0;
      explicit_disable = 0;
      rst_n = 1;
      recover_a = 1;
    end

    // Deassertion permits fresh attempts after the canceled generation.
    #1 clk = 1;
    #1 begin clk = 0; recover_a = 0; recover_b = 1; end
    #1 clk = 1;
    #1 begin clk = 0; recover_b = 0; end

    // A disable that rises before the clock in the same Active evaluation
    // suppresses the sampled result.
    #1 before_hit = 1;
    #1 begin
      explicit_disable = 1;
      rst_n = 0;
      clk = 1;
    end
    #1 begin
      clk = 0;
      before_hit = 0;
      explicit_disable = 0;
      rst_n = 1;
      nba_hit = 1;
    end

    // An NBA disable after the clock event but before Observed also suppresses
    // the result because disable iff is unsampled.
    #1 begin
      explicit_disable <= 1;
      rst_n <= 0;
      clk = 1;
    end
    #1 begin
      clk = 0;
      nba_hit = 0;
      explicit_disable = 0;
      rst_n = 1;
    end

    // Queue all four results in Observed. The first Reactive action pulses the
    // two real disables; only the unrelated fourth action may survive. Repeat
    // the pulse after the cancellation observers rearm. The multi-signal
    // disable also proves each source-ordered publication observes canonical
    // state through that transition, not the generated plane's future value.
    #1 queued_hit = 1;
    #1 clk = 1;
    #1 begin clk = 0; queued_hit = 0; end
    #1 queued_hit = 1;
    #1 clk = 1;
    #1 begin clk = 0; queued_hit = 0; end
    #1 $display("initial=%0d%0d cancel=%0d%0d unrelated=%0d x=%0d%0d z=%0d%0d recover=%0d%0d before=%0d%0d nba=%0d%0d queued=%0d%0d%0d%0d complex=%0d",
                initial_explicit, initial_default,
                canceled_explicit, canceled_default, unrelated,
                x_explicit, x_default, z_explicit, z_default,
                recovered_explicit, recovered_default,
                before_explicit, before_default, nba_explicit, nba_default,
                reset_driver,
                queued_explicit, queued_default, queued_unrelated,
                queued_complex);
    $finish;
  end
endmodule

// CHECK: initial=00 cancel=00 unrelated=1 x=11 z=11 recover=11 before=00 nba=00 queued=2002 complex=0
// SIM-DAG: obelisk_sim.concurrent_cancel
// SIM-DAG: obelisk_sim.concurrent_cancel_observer
// SIM-DAG: obelisk_sim.suspend.observe
// SIM-DAG: obelisk_sim.concurrent_report
// SIM-DAG: home_region = 8 : i32
// SIM-DAG: home_region = 10 : i32
// AOT: call i32 @obelisk_rt_v1_scheduler_add_aot
// AOT: call i32 @obelisk_rt_v1_scheduler_run(
