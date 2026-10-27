// RUN: talos -emit-slang %s | talos-opt --test-talos-native-backend='scheduler=auto sim-opt-level=3 stop-after=lowering' '--test-talos-set-module-attributes=attrs="{schedule.native_scheduler = 1 : i32}"' -o %t.sim.mlir
// RUN: talos-opt %t.sim.mlir --pass-pipeline='builtin.module(simulation.design(talos-sim-plan-native-partitions),convert-talos-sim-processes-to-llvm-coroutines)' -o %t.mlir
// RUN: FileCheck %s --implicit-check-not=gc_designate --implicit-check-not=gc_collect_pending --implicit-check-not=schedule.gc.collect < %t.mlir

// A UVM-shaped producer/consumer testbench: a mailbox of transaction handles,
// forked workers that capture handles across clock waits, handles held
// across delays, a static registry and a linked history. The source is
// lowered as `talos -O3 --vpi=off` lowers it, whose backend resolves the
// auto scheduler to generic for this design, and replayed through that
// build's backend passes without GC polls.

// Mailbox traffic, forks and delays keep this design on the runtime
// scheduler: no generated periodic loop proves a cycle, so nothing designates
// collections and allocation keeps collecting under its precise roots.
// CHECK: llvm.call @talos_rt_v1_gc_managed_root_range_push(
// CHECK: llvm.call @talos_rt_v1_gc_managed_root_range_pop(
// CHECK: llvm.func @main(

class packet;
  int id;
  int payload[4];
  packet prev;
  function new(int id, packet prev);
    this.id = id;
    this.prev = prev;
    foreach (payload[i]) payload[i] = id + i;
  endfunction
  function int sum();
    int s = 0;
    foreach (payload[i]) s += payload[i];
    return s;
  endfunction
endclass

class registry;
  static packet last;
  static int count;
  static function void record(packet p);
    last = p;
    count++;
  endfunction
endclass

module tb2;
  logic clk = 0;
  always #5 clk = ~clk;
  mailbox #(packet) box = new();
  int checksum;
  int history;

  // Producer: one packet per cycle, linked to the previous one, and a
  // zero-time burst of short-lived packets at time 0.
  initial begin
    automatic packet tail = null;
    for (int i = 0; i < 200; i++) begin
      automatic packet scratch = new(i, null);
      checksum += 0 * scratch.sum();
    end
    repeat (16) begin
      @(posedge clk);
      tail = new(registry::count, tail);
      registry::record(tail);
      box.put(tail);
    end
  end

  // Consumer: holds the handle across a delay before using it, and forks a
  // worker that keeps its own captured handle across a clock wait.
  initial begin
    repeat (16) begin
      automatic packet got;
      box.get(got);
      #2;
      checksum += got.sum();
      fork
        automatic packet held = got;
        begin
          @(negedge clk);
          history += held.id;
        end
      join_none
    end
    #40;
    begin
      automatic int depth = 0;
      automatic packet walk = registry::last;
      while (walk != null) begin
        depth++;
        walk = walk.prev;
      end
      $display("checksum=%0d history=%0d depth=%0d last=%0d", checksum, history,
               depth, registry::last.id);
    end
    $finish;
  end
endmodule
