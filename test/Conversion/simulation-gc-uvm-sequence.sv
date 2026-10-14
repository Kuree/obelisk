// RUN: obelisk -emit-slang %s | obelisk-opt --test-obelisk-native-backend='scheduler=auto sim-opt-level=3 stop-after=lowering' -o %t.sim.mlir
// RUN: obelisk-opt %t.sim.mlir --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-suspension)),encode-obelisk-sim-to-bytecode{vpi=off prune-native=true},simulation.design(obelisk-sim-optimize-native-regions),simulation.design(obelisk-sim-plan-native-partitions),convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.mlir
// RUN: FileCheck %s --implicit-check-not=schedule.gc.collect --implicit-check-not=runtime.gc.collect < %t.mlir
// RUN: FileCheck %s --check-prefix=ROOTS < %t.mlir

// A UVM-shaped, class-based testbench driven by a periodic clock: components
// built at time 0 (with a zero-time burst of short-lived transactions), a
// sequence that creates one transaction per clock and sends it through
// virtual calls, and a scoreboard queue of handles that outlives the
// transactions it drops. The source is lowered as `obelisk -O3 --vpi=off`
// lowers it and replayed through that build's backend passes without GC
// polls.

// The generated periodic loop designates collections once the clock is
// proven periodic, and keeps a single collection point per clock cycle
// (IEEE 1800-2023 8.29 leaves the timing of reclamation to the
// implementation): the rising edge of the clock, ahead of that edge's
// evaluation. Every other designated point, at the falling edge and at slot
// ends, is gone, and none remains unlowered.
// CHECK-LABEL: llvm.func @__obelisk_aot_schedule_run_v1(
// CHECK: llvm.call @obelisk_rt_v1_scheduler_prepare_periodic_aot(
// CHECK: llvm.call @obelisk_rt_v1_gc_designate(
// CHECK: %[[GC:.*]] = llvm.call @obelisk_rt_v1_gc_collect_pending(
// CHECK-NEXT: llvm.br ^[[COLLECTED:bb[0-9]+]](%[[GC]] : i32)
// CHECK-NEXT: ^[[COLLECTED]](%[[STATUS:.*]]: i32):
// CHECK-NEXT: llvm.icmp "eq" %[[STATUS]]
// CHECK-NOT: llvm.call @obelisk_rt_v1_gc_collect_pending(
// CHECK: llvm.func @main(

// Collections designated at slot boundaries do not change the precise roots
// that class activations still register around their own allocations.
// ROOTS: llvm.call @obelisk_rt_v1_gc_managed_root_range_push(
// ROOTS: llvm.call @obelisk_rt_v1_gc_managed_root_range_pop(

class item;
  rand bit [7:0] data;
  int id;
  function new(int id); this.id = id; data = id * 3; endfunction
endclass

class component;
  string name;
  function new(string name); this.name = name; endfunction
  virtual function void consume(item t); endfunction
endclass

class scoreboard extends component;
  item seen[$];
  int total;
  function new(string name); super.new(name); endfunction
  virtual function void consume(item t);
    seen.push_back(t);
    total += t.data;
    if (seen.size() > 4) void'(seen.pop_front());
  endfunction
endclass

class env;
  component sinks[2];
  function new();
    automatic scoreboard sb = new("sb");
    sinks[0] = sb;
    sinks[1] = new("null");
  endfunction
endclass

module tb;
  logic clk = 0;
  always #5 clk = ~clk;
  env e;
  int cycle;
  initial begin
    // Build phase: zero-time allocation burst.
    e = new();
    for (int i = 0; i < 64; i++) begin
      automatic item scratch = new(i);
      e.sinks[1].consume(scratch);
    end
    repeat (20) begin
      automatic item t;
      @(posedge clk);
      t = new(cycle);
      cycle++;
      foreach (e.sinks[s]) e.sinks[s].consume(t);
    end
    begin
      automatic scoreboard sb;
      $cast(sb, e.sinks[0]);
      $display("total=%0d kept=%0d last=%0d", sb.total, sb.seen.size(), sb.seen[$].id);
    end
    $finish;
  end
endmodule
