// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-suspension)),encode-obelisk-sim-to-bytecode{vpi=off prune-native=true},simulation.design(obelisk-sim-optimize-native-regions),simulation.design(obelisk-sim-plan-native-partitions),convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.mlir
// RUN: FileCheck %s --implicit-check-not=schedule.gc.collect --implicit-check-not=runtime.gc.collect < %t.mlir
// RUN: FileCheck %s --check-prefix=ROOTS < %t.mlir

// A UVM-shaped, class-based testbench driven by a periodic clock: components
// built at time 0 (with a zero-time burst of short-lived transactions), a
// sequence that creates one transaction per clock and sends it through
// virtual calls, and a scoreboard queue of handles that outlives the
// transactions it drops. The input is the native backend's view of this
// source (`obelisk -O3 --vpi=off`), replayed through the same passes:
//
// | // A UVM-shaped testbench: components built at time 0, a sequence that creates
// | // one transaction object per clock, a driver reached through a virtual call,
// | // and a scoreboard queue of handles that outlives the transactions it drops.
// | class item;
// |   rand bit [7:0] data;
// |   int id;
// |   function new(int id); this.id = id; data = id * 3; endfunction
// | endclass
// |
// | class component;
// |   string name;
// |   function new(string name); this.name = name; endfunction
// |   virtual function void consume(item t); endfunction
// | endclass
// |
// | class scoreboard extends component;
// |   item seen[$];
// |   int total;
// |   function new(string name); super.new(name); endfunction
// |   virtual function void consume(item t);
// |     seen.push_back(t);
// |     total += t.data;
// |     if (seen.size() > 4) void'(seen.pop_front());
// |   endfunction
// | endclass
// |
// | class env;
// |   component sinks[2];
// |   function new();
// |     automatic scoreboard sb = new("sb");
// |     sinks[0] = sb;
// |     sinks[1] = new("null");
// |   endfunction
// | endclass
// |
// | module tb;
// |   logic clk = 0;
// |   always #5 clk = ~clk;
// |   env e;
// |   int cycle;
// |   initial begin
// |     // Build phase: zero-time allocation burst.
// |     e = new();
// |     for (int i = 0; i < 64; i++) begin
// |       automatic item scratch = new(i);
// |       e.sinks[1].consume(scratch);
// |     end
// |     repeat (20) begin
// |       automatic item t;
// |       @(posedge clk);
// |       t = new(cycle);
// |       cycle++;
// |       foreach (e.sinks[s]) e.sinks[s].consume(t);
// |     end
// |     begin
// |       automatic scoreboard sb;
// |       $cast(sb, e.sinks[0]);
// |       $display("total=%0d kept=%0d last=%0d", sb.total, sb.seen.size(), sb.seen[$].id);
// |     end
// |     $finish;
// |   end
// | endmodule

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

#loc = loc("tb.sv":36:1)
#loc1 = loc("tb.sv":4:1)
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu", obelisk.coverage.language_version = 2023 : i32, obelisk.native.closed_executable, obelisk.native.optimization_level = 3 : i32, schedule.native_scheduler = 0 : i32, schedule.native_scheduler.auto_requested} {
  simulation.design @design attributes {compute_graph = #schedule.graph<version = 1, vpi = off, workers = 1, nodes = [#schedule.fragment<id = 0, function = @__obelisk_root, block = 0, region = active, action = terminate, tier = native, cost = 10, lane = 0, twoState = true, effects = [#schedule.effect<effect = write, resource = storage, target = descriptor, descriptor = 0, formal = 0, low = 0, width = 1, dynamic = false, deferred = false, trigger = none>]>, #schedule.fragment<id = 1, function = @unit_7, block = 0, region = active, action = continue, tier = native, cost = 1, lane = 0, twoState = true, effects = []>, #schedule.fragment<id = 2, function = @unit_7, block = 1, region = active, action = suspend_delay, tier = native, cost = 1, lane = 0, twoState = true, effects = []>, #schedule.fragment<id = 3, function = @unit_7, block = 2, region = active, action = continue, tier = native, cost = 7, lane = 0, twoState = false, effects = [#schedule.effect<effect = read, resource = storage, target = descriptor, descriptor = 0, formal = 0, low = 0, width = 1, dynamic = false, deferred = false, trigger = none>, #schedule.effect<effect = write, resource = storage, target = descriptor, descriptor = 0, formal = 0, low = 0, width = 1, dynamic = false, deferred = false, trigger = none>]>, #schedule.fragment<id = 4, function = @unit_8, block = 0, region = active, action = continue, tier = native, cost = 27, lane = 0, twoState = true, effects = [#schedule.effect<effect = write, resource = storage, target = descriptor, descriptor = 1, formal = 0, low = 0, width = 64, dynamic = false, deferred = false, trigger = none>]>, #schedule.fragment<id = 5, function = @unit_8, block = 1, region = active, action = continue, tier = native, cost = 1, lane = 0, twoState = true, effects = []>, #schedule.fragment<id = 6, function = @unit_8, block = 2, region = active, action = continue, tier = native, cost = 21, lane = 0, twoState = true, effects = [#schedule.effect<effect = read, resource = storage, target = descriptor, descriptor = 1, formal = 0, low = 0, width = 64, dynamic = false, deferred = false, trigger = none>]>, #schedule.fragment<id = 7, function = @unit_8, block = 3, region = active, action = continue, tier = native, cost = 1, lane = 0, twoState = true, effects = []>, #schedule.fragment<id = 8, function = @unit_8, block = 4, region = active, action = continue, tier = native, cost = 9, lane = 0, twoState = true, effects = [#schedule.effect<effect = write, resource = storage, target = descriptor, descriptor = 3, formal = 0, low = 0, width = 32, dynamic = false, deferred = false, trigger = none>, #schedule.effect<effect = write, resource = storage, target = descriptor, descriptor = 4, formal = 0, low = 0, width = 64, dynamic = false, deferred = false, trigger = none>]>, #schedule.fragment<id = 9, function = @unit_8, block = 5, region = active, action = continue, tier = native, cost = 1, lane = 0, twoState = true, effects = []>, #schedule.fragment<id = 10, function = @unit_8, block = 6, region = active, action = continue, tier = native, cost = 11, lane = 0, twoState = true, effects = [#schedule.effect<effect = read, resource = storage, target = descriptor, descriptor = 1, formal = 0, low = 0, width = 64, dynamic = false, deferred = false, trigger = none>]>, #schedule.fragment<id = 11, function = @unit_8, block = 7, region = active, action = continue, tier = native, cost = 23, lane = 0, twoState = true, effects = [#schedule.effect<effect = read, resource = storage, target = descriptor, descriptor = 2, formal = 0, low = 0, width = 32, dynamic = false, deferred = false, trigger = none>, #schedule.effect<effect = write, resource = storage, target = descriptor, descriptor = 2, formal = 0, low = 0, width = 32, dynamic = false, deferred = false, trigger = none>]>, #schedule.fragment<id = 12, function = @unit_8, block = 8, region = active, action = continue, tier = native, cost = 1, lane = 0, twoState = true, effects = []>, #schedule.fragment<id = 13, function = @unit_8, block = 9, region = active, action = continue, tier = native, cost = 11, lane = 0, twoState = true, effects = [#schedule.effect<effect = read, resource = storage, target = descriptor, descriptor = 1, formal = 0, low = 0, width = 64, dynamic = false, deferred = false, trigger = none>]>, #schedule.fragment<id = 14, function = @unit_8, block = 10, region = active, action = terminate, tier = native, cost = 19, lane = 0, twoState = true, effects = [#schedule.effect<effect = write, resource = storage, target = descriptor, descriptor = 3, formal = 0, low = 0, width = 32, dynamic = false, deferred = false, trigger = none>]>, #schedule.fragment<id = 15, function = @unit_8, block = 11, region = active, action = terminate, tier = native, cost = 7, lane = 0, twoState = true, effects = [#schedule.effect<effect = write, resource = storage, target = descriptor, descriptor = 3, formal = 0, low = 0, width = 32, dynamic = false, deferred = false, trigger = none>]>, #schedule.fragment<id = 16, function = @unit_8, block = 12, region = active, action = suspend_edge, tier = native, cost = 1, lane = 0, twoState = true, effects = [#schedule.effect<effect = watch, resource = storage, target = descriptor, descriptor = 0, formal = 0, low = 0, width = 1, dynamic = false, deferred = false, trigger = posedge>]>, #schedule.fragment<id = 17, function = @unit_8, block = 13, region = active, action = continue, tier = native, cost = 6, lane = 0, twoState = true, effects = [#schedule.effect<effect = read, resource = storage, target = descriptor, descriptor = 3, formal = 0, low = 0, width = 32, dynamic = false, deferred = false, trigger = none>]>, #schedule.fragment<id = 18, function = @unit_8, block = 14, region = active, action = terminate, tier = native, cost = 0, lane = 0, twoState = true, effects = []>, #schedule.fragment<id = 19, function = @unit_8, block = 15, region = active, action = continue, tier = native, cost = 4, lane = 0, twoState = true, effects = [#schedule.effect<effect = read, resource = storage, target = descriptor, descriptor = 4, formal = 0, low = 0, width = 64, dynamic = false, deferred = false, trigger = none>]>], edges = [#schedule.edge<source = 0, target = 1, kind = spawn>, #schedule.edge<source = 0, target = 3, kind = conflict, resource = <effect = write, resource = unknown, target = unknown, descriptor = 0, formal = 0, low = 0, width = 0, dynamic = false, deferred = false, trigger = none>>, #schedule.edge<source = 0, target = 4, kind = spawn>, #schedule.edge<source = 0, target = 16, kind = sensitivity, resource = <effect = watch, resource = storage, target = descriptor, descriptor = 0, formal = 0, low = 0, width = 1, dynamic = false, deferred = false, trigger = posedge>>, #schedule.edge<source = 1, target = 2, kind = process_order>, #schedule.edge<source = 2, target = 3, kind = resume>, #schedule.edge<source = 3, target = 2, kind = process_order>, #schedule.edge<source = 3, target = 16, kind = sensitivity, resource = <effect = watch, resource = storage, target = descriptor, descriptor = 0, formal = 0, low = 0, width = 1, dynamic = false, deferred = false, trigger = posedge>>, #schedule.edge<source = 4, target = 5, kind = process_order>, #schedule.edge<source = 5, target = 6, kind = process_order>, #schedule.edge<source = 5, target = 7, kind = process_order>, #schedule.edge<source = 6, target = 5, kind = process_order>, #schedule.edge<source = 7, target = 8, kind = process_order>, #schedule.edge<source = 7, target = 10, kind = process_order>, #schedule.edge<source = 8, target = 16, kind = process_order>, #schedule.edge<source = 9, target = 7, kind = process_order>, #schedule.edge<source = 10, target = 14, kind = process_order>, #schedule.edge<source = 10, target = 15, kind = process_order>, #schedule.edge<source = 11, target = 12, kind = process_order>, #schedule.edge<source = 12, target = 9, kind = process_order>, #schedule.edge<source = 12, target = 13, kind = process_order>, #schedule.edge<source = 13, target = 12, kind = process_order>, #schedule.edge<source = 16, target = 17, kind = resume>, #schedule.edge<source = 17, target = 18, kind = process_order>, #schedule.edge<source = 17, target = 19, kind = process_order>, #schedule.edge<source = 19, target = 11, kind = process_order>], regions = [#schedule.region<kind = active, groups = [#schedule.group<fragments = [0], schedule = acyclic, feedback = []>, #schedule.group<fragments = [1], schedule = acyclic, feedback = []>, #schedule.group<fragments = [3], schedule = acyclic, feedback = []>, #schedule.group<fragments = [2], schedule = acyclic, feedback = []>, #schedule.group<fragments = [4], schedule = acyclic, feedback = []>, #schedule.group<fragments = [5], schedule = acyclic, feedback = []>, #schedule.group<fragments = [6], schedule = acyclic, feedback = []>, #schedule.group<fragments = [17], schedule = acyclic, feedback = []>, #schedule.group<fragments = [18], schedule = acyclic, feedback = []>, #schedule.group<fragments = [19], schedule = acyclic, feedback = []>, #schedule.group<fragments = [11], schedule = acyclic, feedback = []>, #schedule.group<fragments = [12], schedule = acyclic, feedback = []>, #schedule.group<fragments = [9], schedule = acyclic, feedback = []>, #schedule.group<fragments = [7], schedule = acyclic, feedback = []>, #schedule.group<fragments = [8], schedule = acyclic, feedback = []>, #schedule.group<fragments = [10], schedule = acyclic, feedback = []>, #schedule.group<fragments = [13], schedule = acyclic, feedback = []>, #schedule.group<fragments = [14], schedule = acyclic, feedback = []>, #schedule.group<fragments = [15], schedule = acyclic, feedback = []>, #schedule.group<fragments = [16], schedule = acyclic, feedback = []>]>, #schedule.region<kind = nba, groups = []>, #schedule.region<kind = observed, groups = []>, #schedule.region<kind = reactive, groups = []>, #schedule.region<kind = postponed, groups = []>]>, schedule.compute_kernels = [#schedule.kernel<id = 0, region = active, schedule = acyclic, lane = 0, cost = 11, loweringReady = true, fragments = [0, 1]>, #schedule.kernel<id = 1, region = active, schedule = acyclic, lane = 0, cost = 7, loweringReady = true, fragments = [3]>, #schedule.kernel<id = 2, region = active, schedule = acyclic, lane = 0, cost = 143, loweringReady = true, fragments = [2, 4, 5, 6, 17, 18, 19, 11, 12, 9, 7, 8, 10, 13, 14, 15]>, #schedule.kernel<id = 3, region = active, schedule = acyclic, lane = 0, cost = 1, loweringReady = true, fragments = [16]>], schedule.nba.change_watched = array<i64>, schedule.nba.private_influence = array<i64: 1, 2, 3, 4>, schedule.nba.transient_observable = array<i64: 0>, schedule.static_specialization = #schedule.static_specialization<version = 1, maxPackedWidth = 64, sourceGraph = <version = 1, vpi = off, workers = 1, nodes = [#schedule.fragment<id = 0, function = @__obelisk_root, block = 0, region = active, action = terminate, tier = native, cost = 10, lane = 0, twoState = true, effects = [#schedule.effect<effect = write, resource = storage, target = descriptor, descriptor = 0, formal = 0, low = 0, width = 1, dynamic = false, deferred = false, trigger = none>]>, #schedule.fragment<id = 1, function = @unit_7, block = 0, region = active, action = continue, tier = native, cost = 1, lane = 0, twoState = true, effects = []>, #schedule.fragment<id = 2, function = @unit_7, block = 1, region = active, action = suspend_delay, tier = native, cost = 1, lane = 0, twoState = true, effects = []>, #schedule.fragment<id = 3, function = @unit_7, block = 2, region = active, action = continue, tier = native, cost = 7, lane = 0, twoState = false, effects = [#schedule.effect<effect = read, resource = storage, target = descriptor, descriptor = 0, formal = 0, low = 0, width = 1, dynamic = false, deferred = false, trigger = none>, #schedule.effect<effect = write, resource = storage, target = descriptor, descriptor = 0, formal = 0, low = 0, width = 1, dynamic = false, deferred = false, trigger = none>]>, #schedule.fragment<id = 4, function = @unit_8, block = 0, region = active, action = continue, tier = native, cost = 27, lane = 0, twoState = true, effects = [#schedule.effect<effect = write, resource = storage, target = descriptor, descriptor = 1, formal = 0, low = 0, width = 64, dynamic = false, deferred = false, trigger = none>]>, #schedule.fragment<id = 5, function = @unit_8, block = 1, region = active, action = continue, tier = native, cost = 1, lane = 0, twoState = true, effects = []>, #schedule.fragment<id = 6, function = @unit_8, block = 2, region = active, action = continue, tier = native, cost = 21, lane = 0, twoState = true, effects = [#schedule.effect<effect = read, resource = storage, target = descriptor, descriptor = 1, formal = 0, low = 0, width = 64, dynamic = false, deferred = false, trigger = none>]>, #schedule.fragment<id = 7, function = @unit_8, block = 3, region = active, action = continue, tier = native, cost = 1, lane = 0, twoState = true, effects = []>, #schedule.fragment<id = 8, function = @unit_8, block = 4, region = active, action = continue, tier = native, cost = 9, lane = 0, twoState = true, effects = [#schedule.effect<effect = write, resource = storage, target = descriptor, descriptor = 3, formal = 0, low = 0, width = 32, dynamic = false, deferred = false, trigger = none>, #schedule.effect<effect = write, resource = storage, target = descriptor, descriptor = 4, formal = 0, low = 0, width = 64, dynamic = false, deferred = false, trigger = none>]>, #schedule.fragment<id = 9, function = @unit_8, block = 5, region = active, action = continue, tier = native, cost = 1, lane = 0, twoState = true, effects = []>, #schedule.fragment<id = 10, function = @unit_8, block = 6, region = active, action = continue, tier = native, cost = 11, lane = 0, twoState = true, effects = [#schedule.effect<effect = read, resource = storage, target = descriptor, descriptor = 1, formal = 0, low = 0, width = 64, dynamic = false, deferred = false, trigger = none>]>, #schedule.fragment<id = 11, function = @unit_8, block = 7, region = active, action = continue, tier = native, cost = 23, lane = 0, twoState = true, effects = [#schedule.effect<effect = read, resource = storage, target = descriptor, descriptor = 2, formal = 0, low = 0, width = 32, dynamic = false, deferred = false, trigger = none>, #schedule.effect<effect = write, resource = storage, target = descriptor, descriptor = 2, formal = 0, low = 0, width = 32, dynamic = false, deferred = false, trigger = none>]>, #schedule.fragment<id = 12, function = @unit_8, block = 8, region = active, action = continue, tier = native, cost = 1, lane = 0, twoState = true, effects = []>, #schedule.fragment<id = 13, function = @unit_8, block = 9, region = active, action = continue, tier = native, cost = 11, lane = 0, twoState = true, effects = [#schedule.effect<effect = read, resource = storage, target = descriptor, descriptor = 1, formal = 0, low = 0, width = 64, dynamic = false, deferred = false, trigger = none>]>, #schedule.fragment<id = 14, function = @unit_8, block = 10, region = active, action = terminate, tier = native, cost = 19, lane = 0, twoState = true, effects = [#schedule.effect<effect = write, resource = storage, target = descriptor, descriptor = 3, formal = 0, low = 0, width = 32, dynamic = false, deferred = false, trigger = none>]>, #schedule.fragment<id = 15, function = @unit_8, block = 11, region = active, action = terminate, tier = native, cost = 7, lane = 0, twoState = true, effects = [#schedule.effect<effect = write, resource = storage, target = descriptor, descriptor = 3, formal = 0, low = 0, width = 32, dynamic = false, deferred = false, trigger = none>]>, #schedule.fragment<id = 16, function = @unit_8, block = 12, region = active, action = suspend_edge, tier = native, cost = 1, lane = 0, twoState = true, effects = [#schedule.effect<effect = watch, resource = storage, target = descriptor, descriptor = 0, formal = 0, low = 0, width = 1, dynamic = false, deferred = false, trigger = posedge>]>, #schedule.fragment<id = 17, function = @unit_8, block = 13, region = active, action = continue, tier = native, cost = 6, lane = 0, twoState = true, effects = [#schedule.effect<effect = read, resource = storage, target = descriptor, descriptor = 3, formal = 0, low = 0, width = 32, dynamic = false, deferred = false, trigger = none>]>, #schedule.fragment<id = 18, function = @unit_8, block = 14, region = active, action = terminate, tier = native, cost = 0, lane = 0, twoState = true, effects = []>, #schedule.fragment<id = 19, function = @unit_8, block = 15, region = active, action = continue, tier = native, cost = 4, lane = 0, twoState = true, effects = [#schedule.effect<effect = read, resource = storage, target = descriptor, descriptor = 4, formal = 0, low = 0, width = 64, dynamic = false, deferred = false, trigger = none>]>], edges = [#schedule.edge<source = 0, target = 1, kind = spawn>, #schedule.edge<source = 0, target = 3, kind = conflict, resource = <effect = write, resource = unknown, target = unknown, descriptor = 0, formal = 0, low = 0, width = 0, dynamic = false, deferred = false, trigger = none>>, #schedule.edge<source = 0, target = 4, kind = spawn>, #schedule.edge<source = 0, target = 16, kind = sensitivity, resource = <effect = watch, resource = storage, target = descriptor, descriptor = 0, formal = 0, low = 0, width = 1, dynamic = false, deferred = false, trigger = posedge>>, #schedule.edge<source = 1, target = 2, kind = process_order>, #schedule.edge<source = 2, target = 3, kind = resume>, #schedule.edge<source = 3, target = 2, kind = process_order>, #schedule.edge<source = 3, target = 16, kind = sensitivity, resource = <effect = watch, resource = storage, target = descriptor, descriptor = 0, formal = 0, low = 0, width = 1, dynamic = false, deferred = false, trigger = posedge>>, #schedule.edge<source = 4, target = 5, kind = process_order>, #schedule.edge<source = 5, target = 6, kind = process_order>, #schedule.edge<source = 5, target = 7, kind = process_order>, #schedule.edge<source = 6, target = 5, kind = process_order>, #schedule.edge<source = 7, target = 8, kind = process_order>, #schedule.edge<source = 7, target = 10, kind = process_order>, #schedule.edge<source = 8, target = 16, kind = process_order>, #schedule.edge<source = 9, target = 7, kind = process_order>, #schedule.edge<source = 10, target = 14, kind = process_order>, #schedule.edge<source = 10, target = 15, kind = process_order>, #schedule.edge<source = 11, target = 12, kind = process_order>, #schedule.edge<source = 12, target = 9, kind = process_order>, #schedule.edge<source = 12, target = 13, kind = process_order>, #schedule.edge<source = 13, target = 12, kind = process_order>, #schedule.edge<source = 16, target = 17, kind = resume>, #schedule.edge<source = 17, target = 18, kind = process_order>, #schedule.edge<source = 17, target = 19, kind = process_order>, #schedule.edge<source = 19, target = 11, kind = process_order>], regions = [#schedule.region<kind = active, groups = [#schedule.group<fragments = [0], schedule = acyclic, feedback = []>, #schedule.group<fragments = [1], schedule = acyclic, feedback = []>, #schedule.group<fragments = [3], schedule = acyclic, feedback = []>, #schedule.group<fragments = [2], schedule = acyclic, feedback = []>, #schedule.group<fragments = [4], schedule = acyclic, feedback = []>, #schedule.group<fragments = [5], schedule = acyclic, feedback = []>, #schedule.group<fragments = [6], schedule = acyclic, feedback = []>, #schedule.group<fragments = [17], schedule = acyclic, feedback = []>, #schedule.group<fragments = [18], schedule = acyclic, feedback = []>, #schedule.group<fragments = [19], schedule = acyclic, feedback = []>, #schedule.group<fragments = [11], schedule = acyclic, feedback = []>, #schedule.group<fragments = [12], schedule = acyclic, feedback = []>, #schedule.group<fragments = [9], schedule = acyclic, feedback = []>, #schedule.group<fragments = [7], schedule = acyclic, feedback = []>, #schedule.group<fragments = [8], schedule = acyclic, feedback = []>, #schedule.group<fragments = [10], schedule = acyclic, feedback = []>, #schedule.group<fragments = [13], schedule = acyclic, feedback = []>, #schedule.group<fragments = [14], schedule = acyclic, feedback = []>, #schedule.group<fragments = [15], schedule = acyclic, feedback = []>, #schedule.group<fragments = [16], schedule = acyclic, feedback = []>]>, #schedule.region<kind = nba, groups = []>, #schedule.region<kind = observed, groups = []>, #schedule.region<kind = reactive, groups = []>, #schedule.region<kind = postponed, groups = []>]>, roots = [#schedule.static_state_root<descriptor = 0, width = 1, direct = true, guarded = false, nba = false>, #schedule.static_state_root<descriptor = 1, width = 64, direct = false, guarded = false, nba = false>, #schedule.static_state_root<descriptor = 2, width = 32, direct = true, guarded = false, nba = false>, #schedule.static_state_root<descriptor = 3, width = 32, direct = true, guarded = false, nba = false>, #schedule.static_state_root<descriptor = 4, width = 64, direct = true, guarded = false, nba = false>], actorRoots = [#schedule.static_actor_root<function = @__obelisk_root, descriptor = 0, read = false, write = true>, #schedule.static_actor_root<function = @unit_7, descriptor = 0, read = true, write = true>, #schedule.static_actor_root<function = @unit_8, descriptor = 0, read = true, write = false>, #schedule.static_actor_root<function = @unit_8, descriptor = 2, read = true, write = true>, #schedule.static_actor_root<function = @unit_8, descriptor = 3, read = true, write = true>, #schedule.static_actor_root<function = @unit_8, descriptor = 4, read = true, write = true>], nbaRoots = []>, schedule.static_superstep = #schedule.static_superstep<version = 1, sourceGraph = <version = 1, vpi = off, workers = 1, nodes = [#schedule.fragment<id = 0, function = @__obelisk_root, block = 0, region = active, action = terminate, tier = native, cost = 10, lane = 0, twoState = true, effects = [#schedule.effect<effect = write, resource = storage, target = descriptor, descriptor = 0, formal = 0, low = 0, width = 1, dynamic = false, deferred = false, trigger = none>]>, #schedule.fragment<id = 1, function = @unit_7, block = 0, region = active, action = continue, tier = native, cost = 1, lane = 0, twoState = true, effects = []>, #schedule.fragment<id = 2, function = @unit_7, block = 1, region = active, action = suspend_delay, tier = native, cost = 1, lane = 0, twoState = true, effects = []>, #schedule.fragment<id = 3, function = @unit_7, block = 2, region = active, action = continue, tier = native, cost = 7, lane = 0, twoState = false, effects = [#schedule.effect<effect = read, resource = storage, target = descriptor, descriptor = 0, formal = 0, low = 0, width = 1, dynamic = false, deferred = false, trigger = none>, #schedule.effect<effect = write, resource = storage, target = descriptor, descriptor = 0, formal = 0, low = 0, width = 1, dynamic = false, deferred = false, trigger = none>]>, #schedule.fragment<id = 4, function = @unit_8, block = 0, region = active, action = continue, tier = native, cost = 27, lane = 0, twoState = true, effects = [#schedule.effect<effect = write, resource = storage, target = descriptor, descriptor = 1, formal = 0, low = 0, width = 64, dynamic = false, deferred = false, trigger = none>]>, #schedule.fragment<id = 5, function = @unit_8, block = 1, region = active, action = continue, tier = native, cost = 1, lane = 0, twoState = true, effects = []>, #schedule.fragment<id = 6, function = @unit_8, block = 2, region = active, action = continue, tier = native, cost = 21, lane = 0, twoState = true, effects = [#schedule.effect<effect = read, resource = storage, target = descriptor, descriptor = 1, formal = 0, low = 0, width = 64, dynamic = false, deferred = false, trigger = none>]>, #schedule.fragment<id = 7, function = @unit_8, block = 3, region = active, action = continue, tier = native, cost = 1, lane = 0, twoState = true, effects = []>, #schedule.fragment<id = 8, function = @unit_8, block = 4, region = active, action = continue, tier = native, cost = 9, lane = 0, twoState = true, effects = [#schedule.effect<effect = write, resource = storage, target = descriptor, descriptor = 3, formal = 0, low = 0, width = 32, dynamic = false, deferred = false, trigger = none>, #schedule.effect<effect = write, resource = storage, target = descriptor, descriptor = 4, formal = 0, low = 0, width = 64, dynamic = false, deferred = false, trigger = none>]>, #schedule.fragment<id = 9, function = @unit_8, block = 5, region = active, action = continue, tier = native, cost = 1, lane = 0, twoState = true, effects = []>, #schedule.fragment<id = 10, function = @unit_8, block = 6, region = active, action = continue, tier = native, cost = 11, lane = 0, twoState = true, effects = [#schedule.effect<effect = read, resource = storage, target = descriptor, descriptor = 1, formal = 0, low = 0, width = 64, dynamic = false, deferred = false, trigger = none>]>, #schedule.fragment<id = 11, function = @unit_8, block = 7, region = active, action = continue, tier = native, cost = 23, lane = 0, twoState = true, effects = [#schedule.effect<effect = read, resource = storage, target = descriptor, descriptor = 2, formal = 0, low = 0, width = 32, dynamic = false, deferred = false, trigger = none>, #schedule.effect<effect = write, resource = storage, target = descriptor, descriptor = 2, formal = 0, low = 0, width = 32, dynamic = false, deferred = false, trigger = none>]>, #schedule.fragment<id = 12, function = @unit_8, block = 8, region = active, action = continue, tier = native, cost = 1, lane = 0, twoState = true, effects = []>, #schedule.fragment<id = 13, function = @unit_8, block = 9, region = active, action = continue, tier = native, cost = 11, lane = 0, twoState = true, effects = [#schedule.effect<effect = read, resource = storage, target = descriptor, descriptor = 1, formal = 0, low = 0, width = 64, dynamic = false, deferred = false, trigger = none>]>, #schedule.fragment<id = 14, function = @unit_8, block = 10, region = active, action = terminate, tier = native, cost = 19, lane = 0, twoState = true, effects = [#schedule.effect<effect = write, resource = storage, target = descriptor, descriptor = 3, formal = 0, low = 0, width = 32, dynamic = false, deferred = false, trigger = none>]>, #schedule.fragment<id = 15, function = @unit_8, block = 11, region = active, action = terminate, tier = native, cost = 7, lane = 0, twoState = true, effects = [#schedule.effect<effect = write, resource = storage, target = descriptor, descriptor = 3, formal = 0, low = 0, width = 32, dynamic = false, deferred = false, trigger = none>]>, #schedule.fragment<id = 16, function = @unit_8, block = 12, region = active, action = suspend_edge, tier = native, cost = 1, lane = 0, twoState = true, effects = [#schedule.effect<effect = watch, resource = storage, target = descriptor, descriptor = 0, formal = 0, low = 0, width = 1, dynamic = false, deferred = false, trigger = posedge>]>, #schedule.fragment<id = 17, function = @unit_8, block = 13, region = active, action = continue, tier = native, cost = 6, lane = 0, twoState = true, effects = [#schedule.effect<effect = read, resource = storage, target = descriptor, descriptor = 3, formal = 0, low = 0, width = 32, dynamic = false, deferred = false, trigger = none>]>, #schedule.fragment<id = 18, function = @unit_8, block = 14, region = active, action = terminate, tier = native, cost = 0, lane = 0, twoState = true, effects = []>, #schedule.fragment<id = 19, function = @unit_8, block = 15, region = active, action = continue, tier = native, cost = 4, lane = 0, twoState = true, effects = [#schedule.effect<effect = read, resource = storage, target = descriptor, descriptor = 4, formal = 0, low = 0, width = 64, dynamic = false, deferred = false, trigger = none>]>], edges = [#schedule.edge<source = 0, target = 1, kind = spawn>, #schedule.edge<source = 0, target = 3, kind = conflict, resource = <effect = write, resource = unknown, target = unknown, descriptor = 0, formal = 0, low = 0, width = 0, dynamic = false, deferred = false, trigger = none>>, #schedule.edge<source = 0, target = 4, kind = spawn>, #schedule.edge<source = 0, target = 16, kind = sensitivity, resource = <effect = watch, resource = storage, target = descriptor, descriptor = 0, formal = 0, low = 0, width = 1, dynamic = false, deferred = false, trigger = posedge>>, #schedule.edge<source = 1, target = 2, kind = process_order>, #schedule.edge<source = 2, target = 3, kind = resume>, #schedule.edge<source = 3, target = 2, kind = process_order>, #schedule.edge<source = 3, target = 16, kind = sensitivity, resource = <effect = watch, resource = storage, target = descriptor, descriptor = 0, formal = 0, low = 0, width = 1, dynamic = false, deferred = false, trigger = posedge>>, #schedule.edge<source = 4, target = 5, kind = process_order>, #schedule.edge<source = 5, target = 6, kind = process_order>, #schedule.edge<source = 5, target = 7, kind = process_order>, #schedule.edge<source = 6, target = 5, kind = process_order>, #schedule.edge<source = 7, target = 8, kind = process_order>, #schedule.edge<source = 7, target = 10, kind = process_order>, #schedule.edge<source = 8, target = 16, kind = process_order>, #schedule.edge<source = 9, target = 7, kind = process_order>, #schedule.edge<source = 10, target = 14, kind = process_order>, #schedule.edge<source = 10, target = 15, kind = process_order>, #schedule.edge<source = 11, target = 12, kind = process_order>, #schedule.edge<source = 12, target = 9, kind = process_order>, #schedule.edge<source = 12, target = 13, kind = process_order>, #schedule.edge<source = 13, target = 12, kind = process_order>, #schedule.edge<source = 16, target = 17, kind = resume>, #schedule.edge<source = 17, target = 18, kind = process_order>, #schedule.edge<source = 17, target = 19, kind = process_order>, #schedule.edge<source = 19, target = 11, kind = process_order>], regions = [#schedule.region<kind = active, groups = [#schedule.group<fragments = [0], schedule = acyclic, feedback = []>, #schedule.group<fragments = [1], schedule = acyclic, feedback = []>, #schedule.group<fragments = [3], schedule = acyclic, feedback = []>, #schedule.group<fragments = [2], schedule = acyclic, feedback = []>, #schedule.group<fragments = [4], schedule = acyclic, feedback = []>, #schedule.group<fragments = [5], schedule = acyclic, feedback = []>, #schedule.group<fragments = [6], schedule = acyclic, feedback = []>, #schedule.group<fragments = [17], schedule = acyclic, feedback = []>, #schedule.group<fragments = [18], schedule = acyclic, feedback = []>, #schedule.group<fragments = [19], schedule = acyclic, feedback = []>, #schedule.group<fragments = [11], schedule = acyclic, feedback = []>, #schedule.group<fragments = [12], schedule = acyclic, feedback = []>, #schedule.group<fragments = [9], schedule = acyclic, feedback = []>, #schedule.group<fragments = [7], schedule = acyclic, feedback = []>, #schedule.group<fragments = [8], schedule = acyclic, feedback = []>, #schedule.group<fragments = [10], schedule = acyclic, feedback = []>, #schedule.group<fragments = [13], schedule = acyclic, feedback = []>, #schedule.group<fragments = [14], schedule = acyclic, feedback = []>, #schedule.group<fragments = [15], schedule = acyclic, feedback = []>, #schedule.group<fragments = [16], schedule = acyclic, feedback = []>]>, #schedule.region<kind = nba, groups = []>, #schedule.region<kind = observed, groups = []>, #schedule.region<kind = reactive, groups = []>, #schedule.region<kind = postponed, groups = []>]>, actors = [@__obelisk_root, @unit_7, @unit_8]>, schedule.three_tier_schedule = #schedule.three_tier_schedule<version = 1, sourceGraph = <version = 1, vpi = off, workers = 1, nodes = [#schedule.fragment<id = 0, function = @__obelisk_root, block = 0, region = active, action = terminate, tier = native, cost = 10, lane = 0, twoState = true, effects = [#schedule.effect<effect = write, resource = storage, target = descriptor, descriptor = 0, formal = 0, low = 0, width = 1, dynamic = false, deferred = false, trigger = none>]>, #schedule.fragment<id = 1, function = @unit_7, block = 0, region = active, action = continue, tier = native, cost = 1, lane = 0, twoState = true, effects = []>, #schedule.fragment<id = 2, function = @unit_7, block = 1, region = active, action = suspend_delay, tier = native, cost = 1, lane = 0, twoState = true, effects = []>, #schedule.fragment<id = 3, function = @unit_7, block = 2, region = active, action = continue, tier = native, cost = 7, lane = 0, twoState = false, effects = [#schedule.effect<effect = read, resource = storage, target = descriptor, descriptor = 0, formal = 0, low = 0, width = 1, dynamic = false, deferred = false, trigger = none>, #schedule.effect<effect = write, resource = storage, target = descriptor, descriptor = 0, formal = 0, low = 0, width = 1, dynamic = false, deferred = false, trigger = none>]>, #schedule.fragment<id = 4, function = @unit_8, block = 0, region = active, action = continue, tier = native, cost = 27, lane = 0, twoState = true, effects = [#schedule.effect<effect = write, resource = storage, target = descriptor, descriptor = 1, formal = 0, low = 0, width = 64, dynamic = false, deferred = false, trigger = none>]>, #schedule.fragment<id = 5, function = @unit_8, block = 1, region = active, action = continue, tier = native, cost = 1, lane = 0, twoState = true, effects = []>, #schedule.fragment<id = 6, function = @unit_8, block = 2, region = active, action = continue, tier = native, cost = 21, lane = 0, twoState = true, effects = [#schedule.effect<effect = read, resource = storage, target = descriptor, descriptor = 1, formal = 0, low = 0, width = 64, dynamic = false, deferred = false, trigger = none>]>, #schedule.fragment<id = 7, function = @unit_8, block = 3, region = active, action = continue, tier = native, cost = 1, lane = 0, twoState = true, effects = []>, #schedule.fragment<id = 8, function = @unit_8, block = 4, region = active, action = continue, tier = native, cost = 9, lane = 0, twoState = true, effects = [#schedule.effect<effect = write, resource = storage, target = descriptor, descriptor = 3, formal = 0, low = 0, width = 32, dynamic = false, deferred = false, trigger = none>, #schedule.effect<effect = write, resource = storage, target = descriptor, descriptor = 4, formal = 0, low = 0, width = 64, dynamic = false, deferred = false, trigger = none>]>, #schedule.fragment<id = 9, function = @unit_8, block = 5, region = active, action = continue, tier = native, cost = 1, lane = 0, twoState = true, effects = []>, #schedule.fragment<id = 10, function = @unit_8, block = 6, region = active, action = continue, tier = native, cost = 11, lane = 0, twoState = true, effects = [#schedule.effect<effect = read, resource = storage, target = descriptor, descriptor = 1, formal = 0, low = 0, width = 64, dynamic = false, deferred = false, trigger = none>]>, #schedule.fragment<id = 11, function = @unit_8, block = 7, region = active, action = continue, tier = native, cost = 23, lane = 0, twoState = true, effects = [#schedule.effect<effect = read, resource = storage, target = descriptor, descriptor = 2, formal = 0, low = 0, width = 32, dynamic = false, deferred = false, trigger = none>, #schedule.effect<effect = write, resource = storage, target = descriptor, descriptor = 2, formal = 0, low = 0, width = 32, dynamic = false, deferred = false, trigger = none>]>, #schedule.fragment<id = 12, function = @unit_8, block = 8, region = active, action = continue, tier = native, cost = 1, lane = 0, twoState = true, effects = []>, #schedule.fragment<id = 13, function = @unit_8, block = 9, region = active, action = continue, tier = native, cost = 11, lane = 0, twoState = true, effects = [#schedule.effect<effect = read, resource = storage, target = descriptor, descriptor = 1, formal = 0, low = 0, width = 64, dynamic = false, deferred = false, trigger = none>]>, #schedule.fragment<id = 14, function = @unit_8, block = 10, region = active, action = terminate, tier = native, cost = 19, lane = 0, twoState = true, effects = [#schedule.effect<effect = write, resource = storage, target = descriptor, descriptor = 3, formal = 0, low = 0, width = 32, dynamic = false, deferred = false, trigger = none>]>, #schedule.fragment<id = 15, function = @unit_8, block = 11, region = active, action = terminate, tier = native, cost = 7, lane = 0, twoState = true, effects = [#schedule.effect<effect = write, resource = storage, target = descriptor, descriptor = 3, formal = 0, low = 0, width = 32, dynamic = false, deferred = false, trigger = none>]>, #schedule.fragment<id = 16, function = @unit_8, block = 12, region = active, action = suspend_edge, tier = native, cost = 1, lane = 0, twoState = true, effects = [#schedule.effect<effect = watch, resource = storage, target = descriptor, descriptor = 0, formal = 0, low = 0, width = 1, dynamic = false, deferred = false, trigger = posedge>]>, #schedule.fragment<id = 17, function = @unit_8, block = 13, region = active, action = continue, tier = native, cost = 6, lane = 0, twoState = true, effects = [#schedule.effect<effect = read, resource = storage, target = descriptor, descriptor = 3, formal = 0, low = 0, width = 32, dynamic = false, deferred = false, trigger = none>]>, #schedule.fragment<id = 18, function = @unit_8, block = 14, region = active, action = terminate, tier = native, cost = 0, lane = 0, twoState = true, effects = []>, #schedule.fragment<id = 19, function = @unit_8, block = 15, region = active, action = continue, tier = native, cost = 4, lane = 0, twoState = true, effects = [#schedule.effect<effect = read, resource = storage, target = descriptor, descriptor = 4, formal = 0, low = 0, width = 64, dynamic = false, deferred = false, trigger = none>]>], edges = [#schedule.edge<source = 0, target = 1, kind = spawn>, #schedule.edge<source = 0, target = 3, kind = conflict, resource = <effect = write, resource = unknown, target = unknown, descriptor = 0, formal = 0, low = 0, width = 0, dynamic = false, deferred = false, trigger = none>>, #schedule.edge<source = 0, target = 4, kind = spawn>, #schedule.edge<source = 0, target = 16, kind = sensitivity, resource = <effect = watch, resource = storage, target = descriptor, descriptor = 0, formal = 0, low = 0, width = 1, dynamic = false, deferred = false, trigger = posedge>>, #schedule.edge<source = 1, target = 2, kind = process_order>, #schedule.edge<source = 2, target = 3, kind = resume>, #schedule.edge<source = 3, target = 2, kind = process_order>, #schedule.edge<source = 3, target = 16, kind = sensitivity, resource = <effect = watch, resource = storage, target = descriptor, descriptor = 0, formal = 0, low = 0, width = 1, dynamic = false, deferred = false, trigger = posedge>>, #schedule.edge<source = 4, target = 5, kind = process_order>, #schedule.edge<source = 5, target = 6, kind = process_order>, #schedule.edge<source = 5, target = 7, kind = process_order>, #schedule.edge<source = 6, target = 5, kind = process_order>, #schedule.edge<source = 7, target = 8, kind = process_order>, #schedule.edge<source = 7, target = 10, kind = process_order>, #schedule.edge<source = 8, target = 16, kind = process_order>, #schedule.edge<source = 9, target = 7, kind = process_order>, #schedule.edge<source = 10, target = 14, kind = process_order>, #schedule.edge<source = 10, target = 15, kind = process_order>, #schedule.edge<source = 11, target = 12, kind = process_order>, #schedule.edge<source = 12, target = 9, kind = process_order>, #schedule.edge<source = 12, target = 13, kind = process_order>, #schedule.edge<source = 13, target = 12, kind = process_order>, #schedule.edge<source = 16, target = 17, kind = resume>, #schedule.edge<source = 17, target = 18, kind = process_order>, #schedule.edge<source = 17, target = 19, kind = process_order>, #schedule.edge<source = 19, target = 11, kind = process_order>], regions = [#schedule.region<kind = active, groups = [#schedule.group<fragments = [0], schedule = acyclic, feedback = []>, #schedule.group<fragments = [1], schedule = acyclic, feedback = []>, #schedule.group<fragments = [3], schedule = acyclic, feedback = []>, #schedule.group<fragments = [2], schedule = acyclic, feedback = []>, #schedule.group<fragments = [4], schedule = acyclic, feedback = []>, #schedule.group<fragments = [5], schedule = acyclic, feedback = []>, #schedule.group<fragments = [6], schedule = acyclic, feedback = []>, #schedule.group<fragments = [17], schedule = acyclic, feedback = []>, #schedule.group<fragments = [18], schedule = acyclic, feedback = []>, #schedule.group<fragments = [19], schedule = acyclic, feedback = []>, #schedule.group<fragments = [11], schedule = acyclic, feedback = []>, #schedule.group<fragments = [12], schedule = acyclic, feedback = []>, #schedule.group<fragments = [9], schedule = acyclic, feedback = []>, #schedule.group<fragments = [7], schedule = acyclic, feedback = []>, #schedule.group<fragments = [8], schedule = acyclic, feedback = []>, #schedule.group<fragments = [10], schedule = acyclic, feedback = []>, #schedule.group<fragments = [13], schedule = acyclic, feedback = []>, #schedule.group<fragments = [14], schedule = acyclic, feedback = []>, #schedule.group<fragments = [15], schedule = acyclic, feedback = []>, #schedule.group<fragments = [16], schedule = acyclic, feedback = []>]>, #schedule.region<kind = nba, groups = []>, #schedule.region<kind = observed, groups = []>, #schedule.region<kind = reactive, groups = []>, #schedule.region<kind = postponed, groups = []>]>, ownerCount = 5, triggers = [#schedule.trigger_group<id = 0, key = <resource = storage, descriptor = 0, low = 0, width = 1, edge = posedge>>], kernels = [#schedule.scheduled_kernel<id = 0, owner = 1, readyBit = 0, tier = tier1, region = active, schedule = acyclic, shared = true, loweringReady = true, twoStateEligible = true, promotionRoots = [#schedule.inductive_root<resource = storage, descriptor = 0>], fragments = [0, 1]>, #schedule.scheduled_kernel<id = 1, owner = 2, readyBit = 0, tier = tier1, region = active, schedule = acyclic, shared = true, loweringReady = true, twoStateEligible = false, promotionRoots = [], fragments = [3]>, #schedule.scheduled_kernel<id = 2, owner = 3, readyBit = 0, tier = tier1, region = active, schedule = acyclic, shared = true, loweringReady = true, twoStateEligible = true, promotionRoots = [#schedule.inductive_root<resource = storage, descriptor = 1>, #schedule.inductive_root<resource = storage, descriptor = 2>, #schedule.inductive_root<resource = storage, descriptor = 3>, #schedule.inductive_root<resource = storage, descriptor = 4>], fragments = [2, 4, 5, 6, 17, 18, 19, 11, 12, 9, 7, 8, 10, 13, 14, 15]>, #schedule.scheduled_kernel<id = 3, owner = 0, readyBit = 0, tier = tier1, region = active, schedule = acyclic, shared = false, loweringReady = true, twoStateEligible = true, promotionRoots = [#schedule.inductive_root<resource = storage, descriptor = 0>], fragments = [16]>], roots = [#schedule.scheduled_root<resource = storage, descriptor = 0, low = 0, width = 1, owner = 4, tier = tier1>, #schedule.scheduled_root<resource = storage, descriptor = 1, low = 0, width = 64, owner = 3, tier = tier1>, #schedule.scheduled_root<resource = storage, descriptor = 2, low = 0, width = 32, owner = 3, tier = tier1>, #schedule.scheduled_root<resource = storage, descriptor = 3, low = 0, width = 32, owner = 3, tier = tier1>, #schedule.scheduled_root<resource = storage, descriptor = 4, low = 0, width = 64, owner = 3, tier = tier1>], ingress = [#schedule.scheduler_ingress<trigger = 0, owner = 0, readyBit = 0, fragment = 16>]>, time_precision_fs = 1000000 : i64} {
    simulation.class.decl @__obelisk_class_s21_component id 1 {debug_name = "component", is_abstract = false, is_final = false, is_interface = false, simulation.constraint_mode_field = @__obelisk_class_s21_component_field___obelisk_constraint_mode, simulation.random_increment_field = @__obelisk_class_s21_component_field___obelisk_rng_increment, simulation.random_mode_field = @__obelisk_class_s21_component_field___obelisk_rand_mode, simulation.random_state_field = @__obelisk_class_s21_component_field___obelisk_rng_state}
    simulation.class.field @__obelisk_class_s21_component_field_0 of @__obelisk_class_s21_component at 0 : !simulation.string {debug_name = "name", is_static = false, is_weak = false}
    simulation.class.field @__obelisk_class_s21_component_field___obelisk_rng_state of @__obelisk_class_s21_component at 1 : i64 {debug_name = "__obelisk_rng_state", is_static = false, is_weak = false}
    simulation.class.field @__obelisk_class_s21_component_field___obelisk_rng_increment of @__obelisk_class_s21_component at 2 : i64 {debug_name = "__obelisk_rng_increment", is_static = false, is_weak = false}
    simulation.class.field @__obelisk_class_s21_component_field___obelisk_rand_mode of @__obelisk_class_s21_component at 3 : i64 {debug_name = "__obelisk_rand_mode", is_static = false, is_weak = false}
    simulation.class.field @__obelisk_class_s21_component_field___obelisk_constraint_mode of @__obelisk_class_s21_component at 4 : i64 {debug_name = "__obelisk_constraint_mode", is_static = false, is_weak = false}
    simulation.class.decl @__obelisk_class_s63_env id 2 {debug_name = "env", is_abstract = false, is_final = false, is_interface = false, simulation.constraint_mode_field = @__obelisk_class_s63_env_field___obelisk_constraint_mode, simulation.random_increment_field = @__obelisk_class_s63_env_field___obelisk_rng_increment, simulation.random_mode_field = @__obelisk_class_s63_env_field___obelisk_rand_mode, simulation.random_state_field = @__obelisk_class_s63_env_field___obelisk_rng_state}
    simulation.class.field @__obelisk_class_s63_env_field_0 of @__obelisk_class_s63_env at 0 : !simulation.unpacked_array<0 : 1 x !simulation.class_handle<@__obelisk_class_s21_component>, #simulation.storage_layout<size = 16, alignment = 8, stride = 8, offsets = []>> {debug_name = "sinks", is_static = false, is_weak = false}
    simulation.class.field @__obelisk_class_s63_env_field___obelisk_rng_state of @__obelisk_class_s63_env at 1 : i64 {debug_name = "__obelisk_rng_state", is_static = false, is_weak = false}
    simulation.class.field @__obelisk_class_s63_env_field___obelisk_rng_increment of @__obelisk_class_s63_env at 2 : i64 {debug_name = "__obelisk_rng_increment", is_static = false, is_weak = false}
    simulation.class.field @__obelisk_class_s63_env_field___obelisk_rand_mode of @__obelisk_class_s63_env at 3 : i64 {debug_name = "__obelisk_rand_mode", is_static = false, is_weak = false}
    simulation.class.field @__obelisk_class_s63_env_field___obelisk_constraint_mode of @__obelisk_class_s63_env at 4 : i64 {debug_name = "__obelisk_constraint_mode", is_static = false, is_weak = false}
    simulation.class.decl @__obelisk_class_s3_item id 3 {debug_name = "item", is_abstract = false, is_final = false, is_interface = false, random_variable_references = [#simulation.random_variable_reference<target = @__obelisk_class_s3_item_field_0>], simulation.constraint_mode_field = @__obelisk_class_s3_item_field___obelisk_constraint_mode, simulation.random_increment_field = @__obelisk_class_s3_item_field___obelisk_rng_increment, simulation.random_mode_field = @__obelisk_class_s3_item_field___obelisk_rand_mode, simulation.random_state_field = @__obelisk_class_s3_item_field___obelisk_rng_state}
    simulation.class.field @__obelisk_class_s3_item_field_0 of @__obelisk_class_s3_item at 0 : !simulation.packed_array<7 : 0 x i1> {debug_name = "data", is_static = false, is_weak = false, simulation.random_mode_index = 0 : i64, simulation.random_variable_kind = 1 : i32, simulation.random_variable_signed = false}
    simulation.class.field @__obelisk_class_s3_item_field_1 of @__obelisk_class_s3_item at 1 : i32 {debug_name = "id", is_static = false, is_weak = false}
    simulation.class.field @__obelisk_class_s3_item_field___obelisk_rng_state of @__obelisk_class_s3_item at 2 : i64 {debug_name = "__obelisk_rng_state", is_static = false, is_weak = false}
    simulation.class.field @__obelisk_class_s3_item_field___obelisk_rng_increment of @__obelisk_class_s3_item at 3 : i64 {debug_name = "__obelisk_rng_increment", is_static = false, is_weak = false}
    simulation.class.field @__obelisk_class_s3_item_field___obelisk_rand_mode of @__obelisk_class_s3_item at 4 : i64 {debug_name = "__obelisk_rand_mode", is_static = false, is_weak = false}
    simulation.class.field @__obelisk_class_s3_item_field___obelisk_constraint_mode of @__obelisk_class_s3_item at 5 : i64 {debug_name = "__obelisk_constraint_mode", is_static = false, is_weak = false}
    simulation.class.decl @__obelisk_class_s41_scoreboard id 4 extends @__obelisk_class_s21_component {debug_name = "scoreboard", is_abstract = false, is_final = false, is_interface = false}
    simulation.class.field @__obelisk_class_s41_scoreboard_field_0 of @__obelisk_class_s41_scoreboard at 0 : !simulation.queue<!simulation.class_handle<@__obelisk_class_s3_item>, 0> {debug_name = "seen", is_static = false, is_weak = false}
    simulation.class.field @__obelisk_class_s41_scoreboard_field_1 of @__obelisk_class_s41_scoreboard at 1 : i32 {debug_name = "total", is_static = false, is_weak = false}
    simulation.scope.decl 0 hierarchy "\\$root " debug "$root" {dpi_precision_femtoseconds = 1000000 : i64, dpi_unit_femtoseconds = 1000000 : i64}
    simulation.vpi_definition.decl @__obelisk_vpi_definition_0 type 32 name "tb" definition_loc #loc
    simulation.scope.decl 1 parent 0 hierarchy "tb" debug "tb" source_definition "tb" vpi_kind 32 definition @__obelisk_vpi_definition_0 {dpi_precision_femtoseconds = 1000000 : i64, dpi_unit_femtoseconds = 1000000 : i64}
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_0 id 0 type 600 in 0 ordinal 0 hierarchy "$unit" debug "" {definition_loc = #loc1, is_compilation_unit, vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 9 : i32, value = "$unit">, #simulation.vpi_property<selector = 600 : i32, value = true>, #simulation.vpi_property<selector = 602 : i32, value = true>]>}
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_1 id 1 type 652 in 0 parent @__obelisk_vpi_anchor_0 ordinal 0 hierarchy "item" debug "item" {backing = #simulation.vpi_backing<kind = class, symbol = @__obelisk_class_s3_item>}
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_2 id 2 type 20 in 0 parent @__obelisk_vpi_anchor_1 ordinal 0 hierarchy "item::new" debug "new" {backing = #simulation.vpi_backing<kind = code_unit, id = 881602538978738836 : i64>, vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = true>]>}
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_3 id 3 type 20 in 0 parent @__obelisk_vpi_anchor_1 ordinal 1 hierarchy "item::randomize" debug "randomize" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = true>]>}
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_4 id 4 type 20 in 0 parent @__obelisk_vpi_anchor_1 ordinal 2 hierarchy "item::pre_randomize" debug "pre_randomize" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = true>]>}
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_5 id 5 type 20 in 0 parent @__obelisk_vpi_anchor_1 ordinal 3 hierarchy "item::post_randomize" debug "post_randomize" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = true>]>}
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_6 id 6 type 20 in 0 parent @__obelisk_vpi_anchor_1 ordinal 4 hierarchy "item::get_randstate" debug "get_randstate" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = true>]>}
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_7 id 7 type 20 in 0 parent @__obelisk_vpi_anchor_1 ordinal 5 hierarchy "item::set_randstate" debug "set_randstate" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = true>]>}
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_8 id 8 type 20 in 0 parent @__obelisk_vpi_anchor_1 ordinal 6 hierarchy "item::srandom" debug "srandom" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = true>]>}
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_9 id 9 type 20 in 0 parent @__obelisk_vpi_anchor_1 ordinal 7 hierarchy "item::rand_mode" debug "rand_mode" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = true>]>}
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_10 id 10 type 20 in 0 parent @__obelisk_vpi_anchor_1 ordinal 8 hierarchy "item::constraint_mode" debug "constraint_mode" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = true>]>}
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_11 id 11 type 652 in 0 parent @__obelisk_vpi_anchor_0 ordinal 1 hierarchy "component" debug "component" {backing = #simulation.vpi_backing<kind = class, symbol = @__obelisk_class_s21_component>}
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_12 id 12 type 20 in 0 parent @__obelisk_vpi_anchor_11 ordinal 0 hierarchy "component::new" debug "new" {backing = #simulation.vpi_backing<kind = code_unit, id = 5523547644211444218 : i64>, vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = true>]>}
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_13 id 13 type 20 in 0 parent @__obelisk_vpi_anchor_11 ordinal 1 hierarchy "component::consume" debug "consume" {backing = #simulation.vpi_backing<kind = code_unit, id = 4219309298153710882 : i64>, vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = true>]>}
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_14 id 14 type 20 in 0 parent @__obelisk_vpi_anchor_11 ordinal 2 hierarchy "component::randomize" debug "randomize" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = true>]>}
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_15 id 15 type 20 in 0 parent @__obelisk_vpi_anchor_11 ordinal 3 hierarchy "component::pre_randomize" debug "pre_randomize" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = true>]>}
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_16 id 16 type 20 in 0 parent @__obelisk_vpi_anchor_11 ordinal 4 hierarchy "component::post_randomize" debug "post_randomize" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = true>]>}
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_17 id 17 type 20 in 0 parent @__obelisk_vpi_anchor_11 ordinal 5 hierarchy "component::get_randstate" debug "get_randstate" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = true>]>}
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_18 id 18 type 20 in 0 parent @__obelisk_vpi_anchor_11 ordinal 6 hierarchy "component::set_randstate" debug "set_randstate" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = true>]>}
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_19 id 19 type 20 in 0 parent @__obelisk_vpi_anchor_11 ordinal 7 hierarchy "component::srandom" debug "srandom" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = true>]>}
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_20 id 20 type 20 in 0 parent @__obelisk_vpi_anchor_11 ordinal 8 hierarchy "component::rand_mode" debug "rand_mode" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = true>]>}
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_21 id 21 type 20 in 0 parent @__obelisk_vpi_anchor_11 ordinal 9 hierarchy "component::constraint_mode" debug "constraint_mode" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = true>]>}
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_22 id 22 type 652 in 0 parent @__obelisk_vpi_anchor_0 ordinal 2 hierarchy "scoreboard" debug "scoreboard" {backing = #simulation.vpi_backing<kind = class, symbol = @__obelisk_class_s41_scoreboard>}
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_23 id 23 type 20 in 0 parent @__obelisk_vpi_anchor_22 ordinal 0 hierarchy "scoreboard::new" debug "new" {backing = #simulation.vpi_backing<kind = code_unit, id = 5917059831331404219 : i64>, vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = true>]>}
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_24 id 24 type 20 in 0 parent @__obelisk_vpi_anchor_22 ordinal 1 hierarchy "scoreboard::consume" debug "consume" {backing = #simulation.vpi_backing<kind = code_unit, id = 3671452087059334011 : i64>, vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = true>]>}
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_25 id 25 type 20 in 0 parent @__obelisk_vpi_anchor_22 ordinal 2 hierarchy "scoreboard::randomize" debug "randomize" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = true>]>}
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_26 id 26 type 20 in 0 parent @__obelisk_vpi_anchor_22 ordinal 3 hierarchy "scoreboard::pre_randomize" debug "pre_randomize" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = true>]>}
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_27 id 27 type 20 in 0 parent @__obelisk_vpi_anchor_22 ordinal 4 hierarchy "scoreboard::post_randomize" debug "post_randomize" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = true>]>}
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_28 id 28 type 20 in 0 parent @__obelisk_vpi_anchor_22 ordinal 5 hierarchy "scoreboard::get_randstate" debug "get_randstate" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = true>]>}
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_29 id 29 type 20 in 0 parent @__obelisk_vpi_anchor_22 ordinal 6 hierarchy "scoreboard::set_randstate" debug "set_randstate" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = true>]>}
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_30 id 30 type 20 in 0 parent @__obelisk_vpi_anchor_22 ordinal 7 hierarchy "scoreboard::srandom" debug "srandom" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = true>]>}
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_31 id 31 type 20 in 0 parent @__obelisk_vpi_anchor_22 ordinal 8 hierarchy "scoreboard::rand_mode" debug "rand_mode" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = true>]>}
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_32 id 32 type 20 in 0 parent @__obelisk_vpi_anchor_22 ordinal 9 hierarchy "scoreboard::constraint_mode" debug "constraint_mode" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = true>]>}
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_33 id 33 type 652 in 0 parent @__obelisk_vpi_anchor_0 ordinal 3 hierarchy "env" debug "env" {backing = #simulation.vpi_backing<kind = class, symbol = @__obelisk_class_s63_env>}
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_34 id 34 type 20 in 0 parent @__obelisk_vpi_anchor_33 ordinal 0 hierarchy "env::new" debug "new" {backing = #simulation.vpi_backing<kind = code_unit, id = 7321998490713755758 : i64>, vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = true>]>}
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_35 id 35 type 20 in 0 parent @__obelisk_vpi_anchor_33 ordinal 1 hierarchy "env::randomize" debug "randomize" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = true>]>}
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_36 id 36 type 20 in 0 parent @__obelisk_vpi_anchor_33 ordinal 2 hierarchy "env::pre_randomize" debug "pre_randomize" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = true>]>}
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_37 id 37 type 20 in 0 parent @__obelisk_vpi_anchor_33 ordinal 3 hierarchy "env::post_randomize" debug "post_randomize" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = true>]>}
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_38 id 38 type 20 in 0 parent @__obelisk_vpi_anchor_33 ordinal 4 hierarchy "env::get_randstate" debug "get_randstate" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = true>]>}
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_39 id 39 type 20 in 0 parent @__obelisk_vpi_anchor_33 ordinal 5 hierarchy "env::set_randstate" debug "set_randstate" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = true>]>}
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_40 id 40 type 20 in 0 parent @__obelisk_vpi_anchor_33 ordinal 6 hierarchy "env::srandom" debug "srandom" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = true>]>}
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_41 id 41 type 20 in 0 parent @__obelisk_vpi_anchor_33 ordinal 7 hierarchy "env::rand_mode" debug "rand_mode" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = true>]>}
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_42 id 42 type 20 in 0 parent @__obelisk_vpi_anchor_33 ordinal 8 hierarchy "env::constraint_mode" debug "constraint_mode" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = true>]>}
    simulation.vpi_object.anchor @__obelisk_vpi_anchor_43 id 43 type 32 in 1 ordinal 1 hierarchy "tb" debug "tb" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>, vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 7 : i32, value = true>, #simulation.vpi_property<selector = 9 : i32, value = "tb">, #simulation.vpi_property<selector = 600 : i32, value = true>]>}
    simulation.storage.decl 0 in 1 : !simulation.logic<1> design hierarchy "tb.clk" debug "clk" {obelisk.coverage.source_authored, observability = 0 : i32, vpi_type = #simulation.vpi_type<kind = logic, isSigned = false, isFourState = true, range = [0, 0], children = [], childNames = []>}
    simulation.storage.decl 1 in 1 : !simulation.class_handle<@__obelisk_class_s63_env> design hierarchy "tb.e" debug "e" {obelisk.coverage.source_authored, observability = 0 : i32, vpi_type = #simulation.vpi_type<kind = class, isSigned = false, isFourState = false, symbol = @__obelisk_class_s63_env, range = [], children = [], childNames = []>}
    simulation.storage.decl 2 in 1 : i32 design hierarchy "tb.cycle" debug "cycle" {obelisk.coverage.source_authored, observability = 0 : i32, vpi_type = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>}
    simulation.code_unit.decl 832639515527371617 in 0 root_initializer hierarchy "__obelisk_root" debug "root initializer" {internal}
    simulation.code_unit.decl 881602538978738836 in 0 function hierarchy "item::new" debug "new"
    simulation.code_unit.decl 5523547644211444218 in 0 function hierarchy "component::new" debug "new"
    simulation.code_unit.decl 4219309298153710882 in 0 function hierarchy "component::consume" debug "consume"
    simulation.code_unit.decl 5917059831331404219 in 0 function hierarchy "scoreboard::new" debug "new"
    simulation.code_unit.decl 3671452087059334011 in 0 function hierarchy "scoreboard::consume" debug "consume"
    simulation.code_unit.decl 7321998490713755758 in 0 function hierarchy "env::new" debug "new"
    simulation.code_unit.decl 7001318926139008662 in 1 function hierarchy "tb.clk.$static_initializer" debug "clk"
    simulation.code_unit.decl 6445594425527109800 in 1 always hierarchy "tb.$code_unit_189" debug ""
    simulation.code_unit.decl 6971149988550550115 in 1 initial hierarchy "tb.$code_unit_214" debug ""
    simulation.code_unit.decl 8169953458695494743 in 1 observer hierarchy "tb.$code_unit_214.$observer.248.primary" debug "" {internal}
    simulation.func @__obelisk_root(%arg0: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 832639515527371617 : i64, domain = 0 : i32, effect_summary = [#schedule.effect<effect = write, resource = storage, target = descriptor, descriptor = 0, formal = 0, low = 0, width = 1, dynamic = false, deferred = false, trigger = none>], entry_kind = 0 : i32, fragment_abi = #schedule.fragment_abi<version = 1, fragments = [0]>, home_region = 2 : i32} {
      simulation.call @unit_6(%arg0) : (!simulation.context) -> ()
      %0 = simulation.context.storage %arg0[0] : !simulation.ref<!simulation.logic<1>>
      %1 = simulation.spawn @unit_7(%arg0, %0) : !simulation.context, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      %2 = simulation.context.storage %arg0[1] : !simulation.ref<!simulation.class_handle<@__obelisk_class_s63_env>>
      %3 = simulation.context.storage %arg0[2] : !simulation.ref<i32>
      %4 = simulation.spawn @unit_8(%arg0, %0, %2, %3) : !simulation.context, !simulation.ref<!simulation.logic<1>>, !simulation.ref<!simulation.class_handle<@__obelisk_class_s63_env>>, !simulation.ref<i32> -> !simulation.process
      simulation.return
    }
    simulation.func private @unit_0(%arg0: !simulation.context {simulation.capture_kind = 0 : i32}, %arg1: !simulation.class_handle<@__obelisk_class_s3_item> {simulation.capture_kind = 1 : i32}, %arg2: i32 {simulation.capture_kind = 1 : i32}) attributes {code_unit_id = 881602538978738836 : i64, domain = 0 : i32, effect_summary = [], entry_kind = 8 : i32, fragment_abi = #schedule.fragment_abi<version = 1, fragments = []>, home_region = 2 : i32, simulation.hierarchical_name = "item::new"} {
      %c3_i32 = arith.constant 3 : i32
      %0 = simulation.class.field_ref %arg1[@__obelisk_class_s3_item_field_1] : !simulation.class_handle<@__obelisk_class_s3_item> -> !simulation.managed_ref<i32, @__obelisk_class_s3_item>
      simulation.managed.store %arg2 to %0 : i32, !simulation.managed_ref<i32, @__obelisk_class_s3_item>
      %1 = arith.muli %arg2, %c3_i32 : i32
      %2 = arith.trunci %1 : i32 to i8
      %3 = simulation.packed.unflatten %2 : (i8) -> !simulation.packed_array<7 : 0 x i1>
      %4 = simulation.class.field_ref %arg1[@__obelisk_class_s3_item_field_0] : !simulation.class_handle<@__obelisk_class_s3_item> -> !simulation.managed_ref<!simulation.packed_array<7 : 0 x i1>, @__obelisk_class_s3_item>
      simulation.managed.store %3 to %4 : !simulation.packed_array<7 : 0 x i1>, !simulation.managed_ref<!simulation.packed_array<7 : 0 x i1>, @__obelisk_class_s3_item>
      simulation.return
    }
    simulation.func private @unit_1(%arg0: !simulation.context {simulation.capture_kind = 0 : i32}, %arg1: !simulation.class_handle<@__obelisk_class_s21_component> {simulation.capture_kind = 1 : i32}, %arg2: !simulation.string {simulation.capture_kind = 1 : i32}) attributes {code_unit_id = 5523547644211444218 : i64, domain = 0 : i32, effect_summary = [], entry_kind = 8 : i32, fragment_abi = #schedule.fragment_abi<version = 1, fragments = []>, home_region = 2 : i32, simulation.hierarchical_name = "component::new"} {
      %0 = simulation.class.field_ref %arg1[@__obelisk_class_s21_component_field_0] : !simulation.class_handle<@__obelisk_class_s21_component> -> !simulation.managed_ref<!simulation.string, @__obelisk_class_s21_component>
      simulation.managed.store %arg2 to %0 : !simulation.string, !simulation.managed_ref<!simulation.string, @__obelisk_class_s21_component>
      simulation.return
    }
    simulation.func private @unit_2(%arg0: !simulation.context {simulation.capture_kind = 0 : i32}, %arg1: !simulation.class_handle<@__obelisk_class_s21_component> {simulation.capture_kind = 1 : i32}, %arg2: !simulation.class_handle<@__obelisk_class_s3_item> {simulation.capture_kind = 1 : i32}) attributes {code_unit_id = 4219309298153710882 : i64, domain = 0 : i32, effect_summary = [], entry_kind = 8 : i32, fragment_abi = #schedule.fragment_abi<version = 1, fragments = []>, home_region = 2 : i32, simulation.hierarchical_name = "component::consume", simulation.void_function} {
      simulation.return
    }
    simulation.func private @unit_3(%arg0: !simulation.context {simulation.capture_kind = 0 : i32}, %arg1: !simulation.class_handle<@__obelisk_class_s41_scoreboard> {simulation.capture_kind = 1 : i32}, %arg2: !simulation.string {simulation.capture_kind = 1 : i32}) attributes {code_unit_id = 5917059831331404219 : i64, domain = 0 : i32, effect_summary = [], entry_kind = 8 : i32, fragment_abi = #schedule.fragment_abi<version = 1, fragments = []>, home_region = 2 : i32, simulation.hierarchical_name = "scoreboard::new"} {
      %0 = simulation.class.cast %arg1 : !simulation.class_handle<@__obelisk_class_s41_scoreboard> to !simulation.class_handle<@__obelisk_class_s21_component>
      simulation.call @unit_1(%arg0, %0, %arg2) : (!simulation.context, !simulation.class_handle<@__obelisk_class_s21_component>, !simulation.string) -> ()
      simulation.return
    }
    simulation.func private @unit_4(%arg0: !simulation.context {simulation.capture_kind = 0 : i32}, %arg1: !simulation.class_handle<@__obelisk_class_s41_scoreboard> {simulation.capture_kind = 1 : i32}, %arg2: !simulation.class_handle<@__obelisk_class_s3_item> {simulation.capture_kind = 1 : i32}) attributes {code_unit_id = 3671452087059334011 : i64, domain = 0 : i32, effect_summary = [], entry_kind = 8 : i32, fragment_abi = #schedule.fragment_abi<version = 1, fragments = []>, home_region = 2 : i32, simulation.hierarchical_name = "scoreboard::consume", simulation.void_function} {
      %c0_i64 = arith.constant 0 : i64
      %c4_i32 = arith.constant 4 : i32
      %0 = simulation.class.field_ref %arg1[@__obelisk_class_s41_scoreboard_field_0] : !simulation.class_handle<@__obelisk_class_s41_scoreboard> -> !simulation.managed_ref<!simulation.queue<!simulation.class_handle<@__obelisk_class_s3_item>, 0>, @__obelisk_class_s41_scoreboard>
      %1 = simulation.managed.load %0 : !simulation.managed_ref<!simulation.queue<!simulation.class_handle<@__obelisk_class_s3_item>, 0>, @__obelisk_class_s41_scoreboard> -> !simulation.queue<!simulation.class_handle<@__obelisk_class_s3_item>, 0>
      %2 = simulation.managed.is_null %1 : (!simulation.queue<!simulation.class_handle<@__obelisk_class_s3_item>, 0>) -> i1
      cf.cond_br %2, ^bb1, ^bb2(%1 : !simulation.queue<!simulation.class_handle<@__obelisk_class_s3_item>, 0>)
    ^bb1:  // pred: ^bb0
      %c0_i64_0 = arith.constant {simulation.rematerialized} 0 : i64
      %3 = simulation.container.create %c0_i64_0 {alignment = 1 : i64, bit_width = 0 : i64, bound = -1 : i64, container_kind = #simulation.container_kind<queue>, element_flags = #simulation.element_flags<none>, element_kind = #simulation.element_kind<class_handle>, trace_kinds = array<i32>, trace_offsets = array<i64>, type_id = 5488838059373496655 : i64, value_size = 8 : i64} : (i64) -> !simulation.queue<!simulation.class_handle<@__obelisk_class_s3_item>, 0>
      cf.br ^bb2(%3 : !simulation.queue<!simulation.class_handle<@__obelisk_class_s3_item>, 0>)
    ^bb2(%4: !simulation.queue<!simulation.class_handle<@__obelisk_class_s3_item>, 0>):  // 2 preds: ^bb0, ^bb1
      %5 = simulation.container.size %4 : (!simulation.queue<!simulation.class_handle<@__obelisk_class_s3_item>, 0>) -> i64
      simulation.container.write %4, %5, %arg2 : (!simulation.queue<!simulation.class_handle<@__obelisk_class_s3_item>, 0>, i64, !simulation.class_handle<@__obelisk_class_s3_item>) -> ()
      cf.cond_br %2, ^bb3, ^bb4
    ^bb3:  // pred: ^bb2
      %6 = simulation.container.clone %4 : (!simulation.queue<!simulation.class_handle<@__obelisk_class_s3_item>, 0>) -> !simulation.queue<!simulation.class_handle<@__obelisk_class_s3_item>, 0>
      simulation.managed.store %6 to %0 : !simulation.queue<!simulation.class_handle<@__obelisk_class_s3_item>, 0>, !simulation.managed_ref<!simulation.queue<!simulation.class_handle<@__obelisk_class_s3_item>, 0>, @__obelisk_class_s41_scoreboard>
      cf.br ^bb4
    ^bb4:  // 2 preds: ^bb2, ^bb3
      %7 = simulation.class.field_ref %arg1[@__obelisk_class_s41_scoreboard_field_1] : !simulation.class_handle<@__obelisk_class_s41_scoreboard> -> !simulation.managed_ref<i32, @__obelisk_class_s41_scoreboard>
      %8 = simulation.managed.load %7 : !simulation.managed_ref<i32, @__obelisk_class_s41_scoreboard> -> i32
      %9 = simulation.class.field_ref %arg2[@__obelisk_class_s3_item_field_0] : !simulation.class_handle<@__obelisk_class_s3_item> -> !simulation.managed_ref<!simulation.packed_array<7 : 0 x i1>, @__obelisk_class_s3_item>
      %10 = simulation.managed.load %9 : !simulation.managed_ref<!simulation.packed_array<7 : 0 x i1>, @__obelisk_class_s3_item> -> !simulation.packed_array<7 : 0 x i1>
      %11 = simulation.packed.flatten %10 : (!simulation.packed_array<7 : 0 x i1>) -> i8
      %12 = arith.extui %11 : i8 to i32
      %13 = arith.addi %8, %12 : i32
      simulation.managed.store %13 to %7 : i32, !simulation.managed_ref<i32, @__obelisk_class_s41_scoreboard>
      %14 = simulation.managed.load %0 : !simulation.managed_ref<!simulation.queue<!simulation.class_handle<@__obelisk_class_s3_item>, 0>, @__obelisk_class_s41_scoreboard> -> !simulation.queue<!simulation.class_handle<@__obelisk_class_s3_item>, 0>
      %15 = simulation.container.size %14 : (!simulation.queue<!simulation.class_handle<@__obelisk_class_s3_item>, 0>) -> i64
      %16 = arith.trunci %15 : i64 to i32
      %c4_i32_1 = arith.constant {simulation.rematerialized} 4 : i32
      %17 = arith.cmpi sgt, %16, %c4_i32_1 : i32
      cf.cond_br %17, ^bb5, ^bb6
    ^bb5:  // pred: ^bb4
      %18 = simulation.managed.load %0 : !simulation.managed_ref<!simulation.queue<!simulation.class_handle<@__obelisk_class_s3_item>, 0>, @__obelisk_class_s41_scoreboard> -> !simulation.queue<!simulation.class_handle<@__obelisk_class_s3_item>, 0>
      %19 = simulation.managed.is_null %18 : (!simulation.queue<!simulation.class_handle<@__obelisk_class_s3_item>, 0>) -> i1
      cf.cond_br %19, ^bb7, ^bb8(%18 : !simulation.queue<!simulation.class_handle<@__obelisk_class_s3_item>, 0>)
    ^bb6:  // 3 preds: ^bb4, ^bb8, ^bb9
      simulation.return
    ^bb7:  // pred: ^bb5
      %c0_i64_2 = arith.constant {simulation.rematerialized} 0 : i64
      %20 = simulation.container.create %c0_i64_2 {alignment = 1 : i64, bit_width = 0 : i64, bound = -1 : i64, container_kind = #simulation.container_kind<queue>, element_flags = #simulation.element_flags<none>, element_kind = #simulation.element_kind<class_handle>, trace_kinds = array<i32>, trace_offsets = array<i64>, type_id = 5488838059373496655 : i64, value_size = 8 : i64} : (i64) -> !simulation.queue<!simulation.class_handle<@__obelisk_class_s3_item>, 0>
      cf.br ^bb8(%20 : !simulation.queue<!simulation.class_handle<@__obelisk_class_s3_item>, 0>)
    ^bb8(%21: !simulation.queue<!simulation.class_handle<@__obelisk_class_s3_item>, 0>):  // 2 preds: ^bb5, ^bb7
      %c0_i64_3 = arith.constant {simulation.rematerialized} 0 : i64
      simulation.queue.delete %21[%c0_i64_3] : !simulation.queue<!simulation.class_handle<@__obelisk_class_s3_item>, 0>
      cf.cond_br %19, ^bb9, ^bb6
    ^bb9:  // pred: ^bb8
      %22 = simulation.container.clone %21 : (!simulation.queue<!simulation.class_handle<@__obelisk_class_s3_item>, 0>) -> !simulation.queue<!simulation.class_handle<@__obelisk_class_s3_item>, 0>
      simulation.managed.store %22 to %0 : !simulation.queue<!simulation.class_handle<@__obelisk_class_s3_item>, 0>, !simulation.managed_ref<!simulation.queue<!simulation.class_handle<@__obelisk_class_s3_item>, 0>, @__obelisk_class_s41_scoreboard>
      cf.br ^bb6
    }
    simulation.func private @unit_5(%arg0: !simulation.context {simulation.capture_kind = 0 : i32}, %arg1: !simulation.class_handle<@__obelisk_class_s63_env> {simulation.capture_kind = 1 : i32}) attributes {code_unit_id = 7321998490713755758 : i64, domain = 0 : i32, effect_summary = [], entry_kind = 8 : i32, fragment_abi = #schedule.fragment_abi<version = 1, fragments = []>, home_region = 2 : i32, simulation.hierarchical_name = "env::new"} {
      %c1_i64 = arith.constant 1 : i64
      %0 = simulation.class.alloc %arg0 : !simulation.context -> !simulation.class_handle<@__obelisk_class_s41_scoreboard>
      %1 = simulation.random.next %arg0 : (!simulation.context) -> i64
      %2 = simulation.random.next %arg0 : (!simulation.context) -> i64
      %3 = arith.ori %2, %c1_i64 : i64
      %4 = simulation.class.field_ref %0[@__obelisk_class_s21_component_field___obelisk_rng_state] : !simulation.class_handle<@__obelisk_class_s41_scoreboard> -> !simulation.managed_ref<i64, @__obelisk_class_s41_scoreboard>
      %5 = simulation.class.field_ref %0[@__obelisk_class_s21_component_field___obelisk_rng_increment] : !simulation.class_handle<@__obelisk_class_s41_scoreboard> -> !simulation.managed_ref<i64, @__obelisk_class_s41_scoreboard>
      simulation.managed.store %1 to %4 : i64, !simulation.managed_ref<i64, @__obelisk_class_s41_scoreboard>
      simulation.managed.store %3 to %5 : i64, !simulation.managed_ref<i64, @__obelisk_class_s41_scoreboard>
      %6 = simulation.string.literal "sb"
      simulation.call @unit_3(%arg0, %0, %6) : (!simulation.context, !simulation.class_handle<@__obelisk_class_s41_scoreboard>, !simulation.string) -> ()
      %7 = simulation.class.cast %0 : !simulation.class_handle<@__obelisk_class_s41_scoreboard> to !simulation.class_handle<@__obelisk_class_s21_component>
      %8 = simulation.class.field_ref %arg1[@__obelisk_class_s63_env_field_0] : !simulation.class_handle<@__obelisk_class_s63_env> -> !simulation.managed_ref<!simulation.unpacked_array<0 : 1 x !simulation.class_handle<@__obelisk_class_s21_component>, #simulation.storage_layout<size = 16, alignment = 8, stride = 8, offsets = []>>, @__obelisk_class_s63_env>
      %9 = simulation.managed.load %8 : !simulation.managed_ref<!simulation.unpacked_array<0 : 1 x !simulation.class_handle<@__obelisk_class_s21_component>, #simulation.storage_layout<size = 16, alignment = 8, stride = 8, offsets = []>>, @__obelisk_class_s63_env> -> !simulation.unpacked_array<0 : 1 x !simulation.class_handle<@__obelisk_class_s21_component>, #simulation.storage_layout<size = 16, alignment = 8, stride = 8, offsets = []>>
      %10 = simulation.aggregate.insert %7 into %9[0] : (!simulation.unpacked_array<0 : 1 x !simulation.class_handle<@__obelisk_class_s21_component>, #simulation.storage_layout<size = 16, alignment = 8, stride = 8, offsets = []>>, !simulation.class_handle<@__obelisk_class_s21_component>) -> !simulation.unpacked_array<0 : 1 x !simulation.class_handle<@__obelisk_class_s21_component>, #simulation.storage_layout<size = 16, alignment = 8, stride = 8, offsets = []>>
      simulation.managed.store %10 to %8 : !simulation.unpacked_array<0 : 1 x !simulation.class_handle<@__obelisk_class_s21_component>, #simulation.storage_layout<size = 16, alignment = 8, stride = 8, offsets = []>>, !simulation.managed_ref<!simulation.unpacked_array<0 : 1 x !simulation.class_handle<@__obelisk_class_s21_component>, #simulation.storage_layout<size = 16, alignment = 8, stride = 8, offsets = []>>, @__obelisk_class_s63_env>
      %11 = simulation.class.alloc %arg0 : !simulation.context -> !simulation.class_handle<@__obelisk_class_s21_component>
      %12 = simulation.random.next %arg0 : (!simulation.context) -> i64
      %13 = simulation.random.next %arg0 : (!simulation.context) -> i64
      %14 = arith.ori %13, %c1_i64 : i64
      %15 = simulation.class.field_ref %11[@__obelisk_class_s21_component_field___obelisk_rng_state] : !simulation.class_handle<@__obelisk_class_s21_component> -> !simulation.managed_ref<i64, @__obelisk_class_s21_component>
      %16 = simulation.class.field_ref %11[@__obelisk_class_s21_component_field___obelisk_rng_increment] : !simulation.class_handle<@__obelisk_class_s21_component> -> !simulation.managed_ref<i64, @__obelisk_class_s21_component>
      simulation.managed.store %12 to %15 : i64, !simulation.managed_ref<i64, @__obelisk_class_s21_component>
      simulation.managed.store %14 to %16 : i64, !simulation.managed_ref<i64, @__obelisk_class_s21_component>
      %17 = simulation.string.literal "null"
      simulation.call @unit_1(%arg0, %11, %17) : (!simulation.context, !simulation.class_handle<@__obelisk_class_s21_component>, !simulation.string) -> ()
      %18 = simulation.managed.load %8 : !simulation.managed_ref<!simulation.unpacked_array<0 : 1 x !simulation.class_handle<@__obelisk_class_s21_component>, #simulation.storage_layout<size = 16, alignment = 8, stride = 8, offsets = []>>, @__obelisk_class_s63_env> -> !simulation.unpacked_array<0 : 1 x !simulation.class_handle<@__obelisk_class_s21_component>, #simulation.storage_layout<size = 16, alignment = 8, stride = 8, offsets = []>>
      %19 = simulation.aggregate.insert %11 into %18[1] : (!simulation.unpacked_array<0 : 1 x !simulation.class_handle<@__obelisk_class_s21_component>, #simulation.storage_layout<size = 16, alignment = 8, stride = 8, offsets = []>>, !simulation.class_handle<@__obelisk_class_s21_component>) -> !simulation.unpacked_array<0 : 1 x !simulation.class_handle<@__obelisk_class_s21_component>, #simulation.storage_layout<size = 16, alignment = 8, stride = 8, offsets = []>>
      simulation.managed.store %19 to %8 : !simulation.unpacked_array<0 : 1 x !simulation.class_handle<@__obelisk_class_s21_component>, #simulation.storage_layout<size = 16, alignment = 8, stride = 8, offsets = []>>, !simulation.managed_ref<!simulation.unpacked_array<0 : 1 x !simulation.class_handle<@__obelisk_class_s21_component>, #simulation.storage_layout<size = 16, alignment = 8, stride = 8, offsets = []>>, @__obelisk_class_s63_env>
      simulation.return
    }
    simulation.func private @unit_6(%arg0: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 7001318926139008662 : i64, domain = 0 : i32, effect_summary = [#schedule.effect<effect = write, resource = storage, target = descriptor, descriptor = 0, formal = 0, low = 0, width = 1, dynamic = false, deferred = false, trigger = none>], entry_kind = 8 : i32, fragment_abi = #schedule.fragment_abi<version = 1, fragments = []>, home_region = 2 : i32, simulation.hierarchical_name = "tb.clk", simulation.void_function} {
      %0 = simulation.logic.constant false, false : !simulation.logic<1>
      %1 = simulation.context.storage %arg0[0] : !simulation.ref<!simulation.logic<1>>
      simulation.ref.store %0 to %1 : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      simulation.return
    }
    simulation.func private @unit_7(%arg0: !simulation.context {simulation.capture_kind = 0 : i32}, %arg1: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {code_unit_id = 6445594425527109800 : i64, domain = 0 : i32, effect_summary = [#schedule.effect<effect = read, resource = storage, target = descriptor, descriptor = 0, formal = 0, low = 0, width = 1, dynamic = false, deferred = false, trigger = none>, #schedule.effect<effect = write, resource = storage, target = descriptor, descriptor = 0, formal = 0, low = 0, width = 1, dynamic = false, deferred = false, trigger = none>], entry_kind = 3 : i32, fragment_abi = #schedule.fragment_abi<version = 1, fragments = [1, 2, 3]>, home_region = 2 : i32, simulation.hierarchical_name = "tb"} {
      %0 = simulation.time.constant 5
      cf.br ^bb1
    ^bb1:  // 2 preds: ^bb0, ^bb2
      %1 = simulation.time.constant 5 {simulation.rematerialized}
      simulation.suspend.delay %1 to ^bb2 {site = #schedule.continuation<id = 3>, timing = #schedule.timing_site<id = 0, kind = calendar>}
    ^bb2:  // pred: ^bb1
      %2 = simulation.ref.load %arg1 : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %3 = simulation.logic.unary bit_not %2 : (!simulation.logic<1>) -> !simulation.logic<1>
      simulation.ref.store %3 to %arg1 : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      cf.br ^bb1
    }
    simulation.storage.decl 3 in 1 : i32 design {observability = 0 : i32}
    simulation.storage.decl 4 in 1 : i64 design {observability = 0 : i32}
    simulation.func private @unit_8(%arg0: !simulation.context {simulation.capture_kind = 0 : i32}, %arg1: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %arg2: !simulation.ref<!simulation.class_handle<@__obelisk_class_s63_env>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %arg3: !simulation.ref<i32> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {code_unit_id = 6971149988550550115 : i64, domain = 0 : i32, effect_summary = [#schedule.effect<effect = read, resource = storage, target = descriptor, descriptor = 1, formal = 0, low = 0, width = 64, dynamic = false, deferred = false, trigger = none>, #schedule.effect<effect = read, resource = storage, target = descriptor, descriptor = 2, formal = 0, low = 0, width = 32, dynamic = false, deferred = false, trigger = none>, #schedule.effect<effect = read, resource = storage, target = descriptor, descriptor = 3, formal = 0, low = 0, width = 32, dynamic = false, deferred = false, trigger = none>, #schedule.effect<effect = read, resource = storage, target = descriptor, descriptor = 4, formal = 0, low = 0, width = 64, dynamic = false, deferred = false, trigger = none>, #schedule.effect<effect = write, resource = storage, target = descriptor, descriptor = 1, formal = 0, low = 0, width = 64, dynamic = false, deferred = false, trigger = none>, #schedule.effect<effect = write, resource = storage, target = descriptor, descriptor = 2, formal = 0, low = 0, width = 32, dynamic = false, deferred = false, trigger = none>, #schedule.effect<effect = write, resource = storage, target = descriptor, descriptor = 3, formal = 0, low = 0, width = 32, dynamic = false, deferred = false, trigger = none>, #schedule.effect<effect = write, resource = storage, target = descriptor, descriptor = 4, formal = 0, low = 0, width = 64, dynamic = false, deferred = false, trigger = none>, #schedule.effect<effect = watch, resource = storage, target = descriptor, descriptor = 0, formal = 0, low = 0, width = 1, dynamic = false, deferred = false, trigger = posedge>], entry_kind = 1 : i32, fragment_abi = #schedule.fragment_abi<version = 1, fragments = [4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19]>, home_region = 2 : i32, schedule.clocked_control, schedule.eval.body = @unit_8.__obelisk_eval_body_0, simulation.hierarchical_name = "tb"} {
      %c0_i32 = arith.constant 0 : i32
      %c1_i64 = arith.constant 1 : i64
      %c64_i32 = arith.constant 64 : i32
      %c1_i32 = arith.constant 1 : i32
      %c0_i64 = arith.constant 0 : i64
      %c2_i64 = arith.constant 2 : i64
      %0 = simulation.bytes.constant "total=%0d kept=%0d last=%0d"
      %c-2147483646_i32 = arith.constant -2147483646 : i32
      %1 = simulation.bytes.constant "$finish: tb.sv:60: simulation time %0t\0A"
      %2 = simulation.bytes.constant "FATAL: tb.sv:57: $cast failed when used as a task"
      %c20_i64 = arith.constant 20 : i64
      %3 = simulation.class.alloc %arg0 : !simulation.context -> !simulation.class_handle<@__obelisk_class_s63_env>
      %4 = simulation.random.next %arg0 : (!simulation.context) -> i64
      %5 = simulation.random.next %arg0 : (!simulation.context) -> i64
      %6 = arith.ori %5, %c1_i64 : i64
      %7 = simulation.class.field_ref %3[@__obelisk_class_s63_env_field___obelisk_rng_state] : !simulation.class_handle<@__obelisk_class_s63_env> -> !simulation.managed_ref<i64, @__obelisk_class_s63_env>
      %8 = simulation.class.field_ref %3[@__obelisk_class_s63_env_field___obelisk_rng_increment] : !simulation.class_handle<@__obelisk_class_s63_env> -> !simulation.managed_ref<i64, @__obelisk_class_s63_env>
      simulation.managed.store %4 to %7 : i64, !simulation.managed_ref<i64, @__obelisk_class_s63_env>
      simulation.managed.store %6 to %8 : i64, !simulation.managed_ref<i64, @__obelisk_class_s63_env>
      simulation.call @unit_5(%arg0, %3) : (!simulation.context, !simulation.class_handle<@__obelisk_class_s63_env>) -> ()
      simulation.ref.store %3 to %arg2 : !simulation.class_handle<@__obelisk_class_s63_env>, !simulation.ref<!simulation.class_handle<@__obelisk_class_s63_env>>
      cf.br ^bb1(%c0_i32 : i32)
    ^bb1(%9: i32):  // 2 preds: ^bb0, ^bb2
      %c64_i32_0 = arith.constant {simulation.rematerialized} 64 : i32
      %10 = arith.cmpi slt, %9, %c64_i32_0 : i32
      %c20_i64_1 = arith.constant {simulation.rematerialized} 20 : i64
      cf.cond_br %10, ^bb2, ^bb3(%c20_i64_1 : i64) {schedule.bounded_loop_header}
    ^bb2:  // pred: ^bb1
      %11 = simulation.class.alloc %arg0 : !simulation.context -> !simulation.class_handle<@__obelisk_class_s3_item>
      %12 = simulation.random.next %arg0 : (!simulation.context) -> i64
      %13 = simulation.random.next %arg0 : (!simulation.context) -> i64
      %c1_i64_2 = arith.constant {simulation.rematerialized} 1 : i64
      %14 = arith.ori %13, %c1_i64_2 : i64
      %15 = simulation.class.field_ref %11[@__obelisk_class_s3_item_field___obelisk_rng_state] : !simulation.class_handle<@__obelisk_class_s3_item> -> !simulation.managed_ref<i64, @__obelisk_class_s3_item>
      %16 = simulation.class.field_ref %11[@__obelisk_class_s3_item_field___obelisk_rng_increment] : !simulation.class_handle<@__obelisk_class_s3_item> -> !simulation.managed_ref<i64, @__obelisk_class_s3_item>
      simulation.managed.store %12 to %15 : i64, !simulation.managed_ref<i64, @__obelisk_class_s3_item>
      simulation.managed.store %14 to %16 : i64, !simulation.managed_ref<i64, @__obelisk_class_s3_item>
      simulation.call @unit_0(%arg0, %11, %9) : (!simulation.context, !simulation.class_handle<@__obelisk_class_s3_item>, i32) -> ()
      %17 = simulation.ref.load %arg2 : !simulation.ref<!simulation.class_handle<@__obelisk_class_s63_env>> -> !simulation.class_handle<@__obelisk_class_s63_env>
      %18 = simulation.class.field_ref %17[@__obelisk_class_s63_env_field_0] : !simulation.class_handle<@__obelisk_class_s63_env> -> !simulation.managed_ref<!simulation.unpacked_array<0 : 1 x !simulation.class_handle<@__obelisk_class_s21_component>, #simulation.storage_layout<size = 16, alignment = 8, stride = 8, offsets = []>>, @__obelisk_class_s63_env>
      %19 = simulation.managed.load %18 : !simulation.managed_ref<!simulation.unpacked_array<0 : 1 x !simulation.class_handle<@__obelisk_class_s21_component>, #simulation.storage_layout<size = 16, alignment = 8, stride = 8, offsets = []>>, @__obelisk_class_s63_env> -> !simulation.unpacked_array<0 : 1 x !simulation.class_handle<@__obelisk_class_s21_component>, #simulation.storage_layout<size = 16, alignment = 8, stride = 8, offsets = []>>
      %20 = simulation.aggregate.extract %19[1] : (!simulation.unpacked_array<0 : 1 x !simulation.class_handle<@__obelisk_class_s21_component>, #simulation.storage_layout<size = 16, alignment = 8, stride = 8, offsets = []>>) -> !simulation.class_handle<@__obelisk_class_s21_component>
      simulation.class.virtual_call %20[@__obelisk_class_s21_component_method_1] slot 0 signature_id 5885352902772247445(%11) : (!simulation.class_handle<@__obelisk_class_s21_component>, !simulation.class_handle<@__obelisk_class_s3_item>) -> ()
      %c1_i32_3 = arith.constant {simulation.rematerialized} 1 : i32
      %21 = arith.addi %9, %c1_i32_3 : i32
      cf.br ^bb1(%21 : i32) {schedule.bounded_loop_latch}
    ^bb3(%22: i64):  // 2 preds: ^bb1, ^bb5
      %c0_i64_4 = arith.constant {simulation.rematerialized} 0 : i64
      %23 = arith.cmpi sgt, %22, %c0_i64_4 : i64
      cf.cond_br %23, ^bb4, ^bb6
    ^bb4:  // pred: ^bb3
      %24 = simulation.context.storage %arg0[4] : !simulation.ref<i64>
      simulation.ref.store %22 to %24 : i64, !simulation.ref<i64>
      %c1_i32_5 = arith.constant 1 : i32
      %25 = simulation.context.storage %arg0[3] : !simulation.ref<i32>
      simulation.ref.store %c1_i32_5 to %25 : i32, !simulation.ref<i32>
      cf.br ^bb12
    ^bb5(%26: i64):  // pred: ^bb8
      %c1_i64_6 = arith.constant {simulation.rematerialized} 1 : i64
      %27 = arith.subi %26, %c1_i64_6 : i64
      cf.br ^bb3(%27 : i64)
    ^bb6:  // pred: ^bb3
      %28 = simulation.ref.load %arg2 : !simulation.ref<!simulation.class_handle<@__obelisk_class_s63_env>> -> !simulation.class_handle<@__obelisk_class_s63_env>
      %29 = simulation.class.field_ref %28[@__obelisk_class_s63_env_field_0] : !simulation.class_handle<@__obelisk_class_s63_env> -> !simulation.managed_ref<!simulation.unpacked_array<0 : 1 x !simulation.class_handle<@__obelisk_class_s21_component>, #simulation.storage_layout<size = 16, alignment = 8, stride = 8, offsets = []>>, @__obelisk_class_s63_env>
      %30 = simulation.managed.load %29 : !simulation.managed_ref<!simulation.unpacked_array<0 : 1 x !simulation.class_handle<@__obelisk_class_s21_component>, #simulation.storage_layout<size = 16, alignment = 8, stride = 8, offsets = []>>, @__obelisk_class_s63_env> -> !simulation.unpacked_array<0 : 1 x !simulation.class_handle<@__obelisk_class_s21_component>, #simulation.storage_layout<size = 16, alignment = 8, stride = 8, offsets = []>>
      %31 = simulation.aggregate.extract %30[0] : (!simulation.unpacked_array<0 : 1 x !simulation.class_handle<@__obelisk_class_s21_component>, #simulation.storage_layout<size = 16, alignment = 8, stride = 8, offsets = []>>) -> !simulation.class_handle<@__obelisk_class_s21_component>
      %32 = simulation.class.cast %31 : !simulation.class_handle<@__obelisk_class_s21_component> to !simulation.class_handle<@__obelisk_class_s41_scoreboard>
      %33 = simulation.class.is_instance %31 is @__obelisk_class_s41_scoreboard : !simulation.class_handle<@__obelisk_class_s21_component>
      %34 = simulation.class.id %31 : !simulation.class_handle<@__obelisk_class_s21_component>
      %c0_i64_7 = arith.constant {simulation.rematerialized} 0 : i64
      %35 = arith.cmpi eq, %34, %c0_i64_7 : i64
      %36 = arith.ori %33, %35 : i1
      cf.cond_br %36, ^bb10, ^bb11
    ^bb7(%37: i64):  // pred: ^bb15
      %38 = simulation.class.alloc %arg0 : !simulation.context -> !simulation.class_handle<@__obelisk_class_s3_item>
      %39 = simulation.random.next %arg0 : (!simulation.context) -> i64
      %40 = simulation.random.next %arg0 : (!simulation.context) -> i64
      %c1_i64_8 = arith.constant {simulation.rematerialized} 1 : i64
      %41 = arith.ori %40, %c1_i64_8 : i64
      %42 = simulation.class.field_ref %38[@__obelisk_class_s3_item_field___obelisk_rng_state] : !simulation.class_handle<@__obelisk_class_s3_item> -> !simulation.managed_ref<i64, @__obelisk_class_s3_item>
      %43 = simulation.class.field_ref %38[@__obelisk_class_s3_item_field___obelisk_rng_increment] : !simulation.class_handle<@__obelisk_class_s3_item> -> !simulation.managed_ref<i64, @__obelisk_class_s3_item>
      simulation.managed.store %39 to %42 : i64, !simulation.managed_ref<i64, @__obelisk_class_s3_item>
      simulation.managed.store %41 to %43 : i64, !simulation.managed_ref<i64, @__obelisk_class_s3_item>
      %44 = simulation.ref.load %arg3 : !simulation.ref<i32> -> i32
      simulation.call @unit_0(%arg0, %38, %44) : (!simulation.context, !simulation.class_handle<@__obelisk_class_s3_item>, i32) -> ()
      %45 = simulation.ref.load %arg3 : !simulation.ref<i32> -> i32
      %c1_i32_9 = arith.constant {simulation.rematerialized} 1 : i32
      %46 = arith.addi %45, %c1_i32_9 : i32
      simulation.ref.store %46 to %arg3 : i32, !simulation.ref<i32>
      %c0_i64_10 = arith.constant {simulation.rematerialized} 0 : i64
      cf.br ^bb8(%c0_i64_10, %37 : i64, i64)
    ^bb8(%47: i64, %48: i64):  // 2 preds: ^bb7, ^bb9
      %c2_i64_11 = arith.constant {simulation.rematerialized} 2 : i64
      %49 = arith.cmpi ult, %47, %c2_i64_11 : i64
      cf.cond_br %49, ^bb9(%48 : i64), ^bb5(%48 : i64) {schedule.bounded_loop_header}
    ^bb9(%50: i64):  // pred: ^bb8
      %c2_i64_12 = arith.constant {simulation.rematerialized} 2 : i64
      %51 = arith.remui %47, %c2_i64_12 : i64
      %52 = arith.trunci %51 : i64 to i32
      %53 = simulation.ref.load %arg2 : !simulation.ref<!simulation.class_handle<@__obelisk_class_s63_env>> -> !simulation.class_handle<@__obelisk_class_s63_env>
      %54 = simulation.class.field_ref %53[@__obelisk_class_s63_env_field_0] : !simulation.class_handle<@__obelisk_class_s63_env> -> !simulation.managed_ref<!simulation.unpacked_array<0 : 1 x !simulation.class_handle<@__obelisk_class_s21_component>, #simulation.storage_layout<size = 16, alignment = 8, stride = 8, offsets = []>>, @__obelisk_class_s63_env>
      %55 = simulation.managed.load %54 : !simulation.managed_ref<!simulation.unpacked_array<0 : 1 x !simulation.class_handle<@__obelisk_class_s21_component>, #simulation.storage_layout<size = 16, alignment = 8, stride = 8, offsets = []>>, @__obelisk_class_s63_env> -> !simulation.unpacked_array<0 : 1 x !simulation.class_handle<@__obelisk_class_s21_component>, #simulation.storage_layout<size = 16, alignment = 8, stride = 8, offsets = []>>
      %56 = arith.extsi %52 : i32 to i65
      %57 = simulation.array.extract_dynamic %55[%56] : (!simulation.unpacked_array<0 : 1 x !simulation.class_handle<@__obelisk_class_s21_component>, #simulation.storage_layout<size = 16, alignment = 8, stride = 8, offsets = []>>, i65) -> !simulation.class_handle<@__obelisk_class_s21_component>
      simulation.class.virtual_call %57[@__obelisk_class_s21_component_method_1] slot 0 signature_id 5885352902772247445(%38) : (!simulation.class_handle<@__obelisk_class_s21_component>, !simulation.class_handle<@__obelisk_class_s3_item>) -> ()
      %c1_i64_13 = arith.constant {simulation.rematerialized} 1 : i64
      %58 = arith.addi %47, %c1_i64_13 : i64
      cf.br ^bb8(%58, %50 : i64, i64) {schedule.bounded_loop_latch}
    ^bb10:  // pred: ^bb6
      %59 = simulation.class.field_ref %32[@__obelisk_class_s41_scoreboard_field_1] : !simulation.class_handle<@__obelisk_class_s41_scoreboard> -> !simulation.managed_ref<i32, @__obelisk_class_s41_scoreboard>
      %60 = simulation.managed.load %59 : !simulation.managed_ref<i32, @__obelisk_class_s41_scoreboard> -> i32
      %61 = simulation.class.field_ref %32[@__obelisk_class_s41_scoreboard_field_0] : !simulation.class_handle<@__obelisk_class_s41_scoreboard> -> !simulation.managed_ref<!simulation.queue<!simulation.class_handle<@__obelisk_class_s3_item>, 0>, @__obelisk_class_s41_scoreboard>
      %62 = simulation.managed.load %61 : !simulation.managed_ref<!simulation.queue<!simulation.class_handle<@__obelisk_class_s3_item>, 0>, @__obelisk_class_s41_scoreboard> -> !simulation.queue<!simulation.class_handle<@__obelisk_class_s3_item>, 0>
      %63 = simulation.container.size %62 : (!simulation.queue<!simulation.class_handle<@__obelisk_class_s3_item>, 0>) -> i64
      %64 = arith.trunci %63 : i64 to i32
      %c1_i64_14 = arith.constant {simulation.rematerialized} 1 : i64
      %65 = arith.subi %63, %c1_i64_14 : i64
      %66 = simulation.container.read %62, %65 : (!simulation.queue<!simulation.class_handle<@__obelisk_class_s3_item>, 0>, i64) -> !simulation.class_handle<@__obelisk_class_s3_item>
      %67 = simulation.class.field_ref %66[@__obelisk_class_s3_item_field_1] : !simulation.class_handle<@__obelisk_class_s3_item> -> !simulation.managed_ref<i32, @__obelisk_class_s3_item>
      %68 = simulation.managed.load %67 : !simulation.managed_ref<i32, @__obelisk_class_s3_item> -> i32
      %c1_i32_15 = arith.constant {simulation.rematerialized} 1 : i32
      %69 = simulation.bytes.constant "total=%0d kept=%0d last=%0d" {simulation.rematerialized}
      simulation.display %arg0 to %c1_i32_15(%69, %60, %64, %68) newline = true radix = <decimal> flags = [0, 1, 1, 1] {library_cell = "work.tb", scope = "tb", time_multiplier = 1 : i64, time_precision = -9 : i32} : !simulation.bytes, i32, i32, i32
      %70 = simulation.time.now %arg0
      %c-2147483646_i32_16 = arith.constant {simulation.rematerialized} -2147483646 : i32
      %71 = simulation.bytes.constant "$finish: tb.sv:60: simulation time %0t\0A" {simulation.rematerialized}
      simulation.display %arg0 to %c-2147483646_i32_16(%71, %70) newline = false radix = <decimal> flags = [0, 0] {scope = "tb", time_multiplier = 1 : i64, time_precision = -9 : i32} : !simulation.bytes, i64
      %c1_i32_17 = arith.constant {simulation.rematerialized} 1 : i32
      simulation.finish %arg0, %c1_i32_17
      %c0_i32_18 = arith.constant 0 : i32
      %72 = simulation.context.storage %arg0[3] : !simulation.ref<i32>
      simulation.ref.store %c0_i32_18 to %72 : i32, !simulation.ref<i32>
      simulation.return
    ^bb11:  // pred: ^bb6
      %c-2147483646_i32_19 = arith.constant {simulation.rematerialized} -2147483646 : i32
      %73 = simulation.bytes.constant "FATAL: tb.sv:57: $cast failed when used as a task" {simulation.rematerialized}
      simulation.display %arg0 to %c-2147483646_i32_19(%73) newline = true radix = <decimal> flags = [0] {scope = "tb", time_multiplier = 1 : i64} : !simulation.bytes
      %c1_i32_20 = arith.constant {simulation.rematerialized} 1 : i32
      simulation.fatal %arg0, %c1_i32_20
      %c0_i32_21 = arith.constant 0 : i32
      %74 = simulation.context.storage %arg0[3] : !simulation.ref<i32>
      simulation.ref.store %c0_i32_21 to %74 : i32, !simulation.ref<i32>
      simulation.return
    ^bb12:  // pred: ^bb4
      simulation.suspend.edge posedge %arg1 to ^bb13 {schedule.procedural_event_wait, site = #schedule.continuation<id = 17>} : !simulation.ref<!simulation.logic<1>>
    ^bb13:  // pred: ^bb12
      %75 = simulation.context.storage %arg0[3] : !simulation.ref<i32>
      %76 = simulation.ref.load %75 : !simulation.ref<i32> -> i32
      %c1_i32_22 = arith.constant 1 : i32
      %77 = arith.cmpi eq, %76, %c1_i32_22 : i32
      cf.cond_br %77, ^bb15, ^bb14
    ^bb14:  // pred: ^bb13
      simulation.return
    ^bb15:  // pred: ^bb13
      %78 = simulation.context.storage %arg0[4] : !simulation.ref<i64>
      %79 = simulation.ref.load %78 : !simulation.ref<i64> -> i64
      cf.br ^bb7(%79 : i64)
    }
    simulation.class.method @__obelisk_class_s21_component_method_1 of @__obelisk_class_s21_component slot 0 signature_id 5885352902772247445 implemented_by @unit_2 : (!simulation.context, !simulation.class_handle<@__obelisk_class_s21_component>, !simulation.class_handle<@__obelisk_class_s3_item>) -> () {debug_name = "consume", is_final = false, is_pure = false, is_static = false, is_task = false, is_virtual = true}
    simulation.class.method @__obelisk_class_s41_scoreboard_method_1 of @__obelisk_class_s41_scoreboard slot 0 signature_id 5885352902772247445 implemented_by @unit_4 : (!simulation.context, !simulation.class_handle<@__obelisk_class_s41_scoreboard>, !simulation.class_handle<@__obelisk_class_s3_item>) -> () {debug_name = "consume", is_final = false, is_pure = false, is_static = false, is_task = false, is_virtual = true}
    simulation.code_unit.decl 1 in 1 function hierarchy "unit_8.__obelisk_eval_body_0" debug "generated native eval body" {internal}
    simulation.func private @unit_8.__obelisk_eval_body_0(%arg0: !simulation.context {simulation.capture_kind = 0 : i32}, %arg1: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %arg2: !simulation.ref<!simulation.class_handle<@__obelisk_class_s63_env>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %arg3: !simulation.ref<i32> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {code_unit_id = 1 : i64, domain = 0 : i32, entry_kind = 8 : i32, home_region = 2 : i32, schedule.eval.borrowed_captures, schedule.eval.continuation = 17 : i32, schedule.eval.raw_captures, schedule.eval.source_owners = [#schedule.source_owner<codeUnit = 6971149988550550115 : i64, continuation = 17 : i32>]} {
      cf.br ^bb1
    ^bb1:  // pred: ^bb0
      %0 = simulation.context.storage %arg0[3] : !simulation.ref<i32>
      %1 = simulation.ref.load %0 : !simulation.ref<i32> -> i32
      %c1_i32 = arith.constant 1 : i32
      %2 = arith.cmpi eq, %1, %c1_i32 : i32
      cf.cond_br %2, ^bb3, ^bb2
    ^bb2:  // pred: ^bb1
      simulation.return
    ^bb3:  // pred: ^bb1
      %3 = simulation.context.storage %arg0[4] : !simulation.ref<i64>
      %4 = simulation.ref.load %3 : !simulation.ref<i64> -> i64
      cf.br ^bb4(%4 : i64)
    ^bb4(%5: i64):  // pred: ^bb3
      %6 = simulation.class.alloc %arg0 : !simulation.context -> !simulation.class_handle<@__obelisk_class_s3_item>
      %7 = simulation.random.next %arg0 : (!simulation.context) -> i64
      %8 = simulation.random.next %arg0 : (!simulation.context) -> i64
      %c1_i64 = arith.constant {simulation.rematerialized} 1 : i64
      %9 = arith.ori %8, %c1_i64 : i64
      %10 = simulation.class.field_ref %6[@__obelisk_class_s3_item_field___obelisk_rng_state] : !simulation.class_handle<@__obelisk_class_s3_item> -> !simulation.managed_ref<i64, @__obelisk_class_s3_item>
      %11 = simulation.class.field_ref %6[@__obelisk_class_s3_item_field___obelisk_rng_increment] : !simulation.class_handle<@__obelisk_class_s3_item> -> !simulation.managed_ref<i64, @__obelisk_class_s3_item>
      simulation.managed.store %7 to %10 : i64, !simulation.managed_ref<i64, @__obelisk_class_s3_item>
      simulation.managed.store %9 to %11 : i64, !simulation.managed_ref<i64, @__obelisk_class_s3_item>
      %12 = simulation.ref.load %arg3 : !simulation.ref<i32> -> i32
      simulation.call @unit_0(%arg0, %6, %12) : (!simulation.context, !simulation.class_handle<@__obelisk_class_s3_item>, i32) -> ()
      %13 = simulation.ref.load %arg3 : !simulation.ref<i32> -> i32
      %c1_i32_0 = arith.constant {simulation.rematerialized} 1 : i32
      %14 = arith.addi %13, %c1_i32_0 : i32
      simulation.ref.store %14 to %arg3 : i32, !simulation.ref<i32>
      %c0_i64 = arith.constant {simulation.rematerialized} 0 : i64
      cf.br ^bb5(%c0_i64, %4 : i64, i64)
    ^bb5(%15: i64, %16: i64):  // 2 preds: ^bb4, ^bb13
      %c2_i64 = arith.constant {simulation.rematerialized} 2 : i64
      %17 = arith.cmpi ult, %15, %c2_i64 : i64
      cf.cond_br %17, ^bb13(%4 : i64), ^bb6(%4 : i64) {schedule.bounded_loop_header}
    ^bb6(%18: i64):  // pred: ^bb5
      %c1_i64_1 = arith.constant {simulation.rematerialized} 1 : i64
      %19 = arith.subi %4, %c1_i64_1 : i64
      cf.br ^bb7(%19 : i64)
    ^bb7(%20: i64):  // pred: ^bb6
      %c0_i64_2 = arith.constant {simulation.rematerialized} 0 : i64
      %21 = arith.cmpi sgt, %19, %c0_i64_2 : i64
      cf.cond_br %21, ^bb11, ^bb8
    ^bb8:  // pred: ^bb7
      %22 = simulation.ref.load %arg2 : !simulation.ref<!simulation.class_handle<@__obelisk_class_s63_env>> -> !simulation.class_handle<@__obelisk_class_s63_env>
      %23 = simulation.class.field_ref %22[@__obelisk_class_s63_env_field_0] : !simulation.class_handle<@__obelisk_class_s63_env> -> !simulation.managed_ref<!simulation.unpacked_array<0 : 1 x !simulation.class_handle<@__obelisk_class_s21_component>, #simulation.storage_layout<size = 16, alignment = 8, stride = 8, offsets = []>>, @__obelisk_class_s63_env>
      %24 = simulation.managed.load %23 : !simulation.managed_ref<!simulation.unpacked_array<0 : 1 x !simulation.class_handle<@__obelisk_class_s21_component>, #simulation.storage_layout<size = 16, alignment = 8, stride = 8, offsets = []>>, @__obelisk_class_s63_env> -> !simulation.unpacked_array<0 : 1 x !simulation.class_handle<@__obelisk_class_s21_component>, #simulation.storage_layout<size = 16, alignment = 8, stride = 8, offsets = []>>
      %25 = simulation.aggregate.extract %24[0] : (!simulation.unpacked_array<0 : 1 x !simulation.class_handle<@__obelisk_class_s21_component>, #simulation.storage_layout<size = 16, alignment = 8, stride = 8, offsets = []>>) -> !simulation.class_handle<@__obelisk_class_s21_component>
      %26 = simulation.class.cast %25 : !simulation.class_handle<@__obelisk_class_s21_component> to !simulation.class_handle<@__obelisk_class_s41_scoreboard>
      %27 = simulation.class.is_instance %25 is @__obelisk_class_s41_scoreboard : !simulation.class_handle<@__obelisk_class_s21_component>
      %28 = simulation.class.id %25 : !simulation.class_handle<@__obelisk_class_s21_component>
      %c0_i64_3 = arith.constant {simulation.rematerialized} 0 : i64
      %29 = arith.cmpi eq, %28, %c0_i64_3 : i64
      %30 = arith.ori %27, %29 : i1
      cf.cond_br %30, ^bb10, ^bb9
    ^bb9:  // pred: ^bb8
      %c-2147483646_i32 = arith.constant {simulation.rematerialized} -2147483646 : i32
      %31 = simulation.bytes.constant "FATAL: tb.sv:57: $cast failed when used as a task" {simulation.rematerialized}
      simulation.display %arg0 to %c-2147483646_i32(%31) newline = true radix = <decimal> flags = [0] {scope = "tb", time_multiplier = 1 : i64} : !simulation.bytes
      %c1_i32_4 = arith.constant {simulation.rematerialized} 1 : i32
      simulation.fatal %arg0, %c1_i32_4
      %c0_i32 = arith.constant 0 : i32
      %32 = simulation.context.storage %arg0[3] : !simulation.ref<i32>
      simulation.ref.store %c0_i32 to %32 : i32, !simulation.ref<i32>
      simulation.return
    ^bb10:  // pred: ^bb8
      %33 = simulation.class.field_ref %26[@__obelisk_class_s41_scoreboard_field_1] : !simulation.class_handle<@__obelisk_class_s41_scoreboard> -> !simulation.managed_ref<i32, @__obelisk_class_s41_scoreboard>
      %34 = simulation.managed.load %33 : !simulation.managed_ref<i32, @__obelisk_class_s41_scoreboard> -> i32
      %35 = simulation.class.field_ref %26[@__obelisk_class_s41_scoreboard_field_0] : !simulation.class_handle<@__obelisk_class_s41_scoreboard> -> !simulation.managed_ref<!simulation.queue<!simulation.class_handle<@__obelisk_class_s3_item>, 0>, @__obelisk_class_s41_scoreboard>
      %36 = simulation.managed.load %35 : !simulation.managed_ref<!simulation.queue<!simulation.class_handle<@__obelisk_class_s3_item>, 0>, @__obelisk_class_s41_scoreboard> -> !simulation.queue<!simulation.class_handle<@__obelisk_class_s3_item>, 0>
      %37 = simulation.container.size %36 : (!simulation.queue<!simulation.class_handle<@__obelisk_class_s3_item>, 0>) -> i64
      %38 = arith.trunci %37 : i64 to i32
      %c1_i64_5 = arith.constant {simulation.rematerialized} 1 : i64
      %39 = arith.subi %37, %c1_i64_5 : i64
      %40 = simulation.container.read %36, %39 : (!simulation.queue<!simulation.class_handle<@__obelisk_class_s3_item>, 0>, i64) -> !simulation.class_handle<@__obelisk_class_s3_item>
      %41 = simulation.class.field_ref %40[@__obelisk_class_s3_item_field_1] : !simulation.class_handle<@__obelisk_class_s3_item> -> !simulation.managed_ref<i32, @__obelisk_class_s3_item>
      %42 = simulation.managed.load %41 : !simulation.managed_ref<i32, @__obelisk_class_s3_item> -> i32
      %c1_i32_6 = arith.constant {simulation.rematerialized} 1 : i32
      %43 = simulation.bytes.constant "total=%0d kept=%0d last=%0d" {simulation.rematerialized}
      simulation.display %arg0 to %c1_i32_6(%43, %34, %38, %42) newline = true radix = <decimal> flags = [0, 1, 1, 1] {library_cell = "work.tb", scope = "tb", time_multiplier = 1 : i64, time_precision = -9 : i32} : !simulation.bytes, i32, i32, i32
      %44 = simulation.time.now %arg0
      %c-2147483646_i32_7 = arith.constant {simulation.rematerialized} -2147483646 : i32
      %45 = simulation.bytes.constant "$finish: tb.sv:60: simulation time %0t\0A" {simulation.rematerialized}
      simulation.display %arg0 to %c-2147483646_i32_7(%45, %44) newline = false radix = <decimal> flags = [0, 0] {scope = "tb", time_multiplier = 1 : i64, time_precision = -9 : i32} : !simulation.bytes, i64
      %c1_i32_8 = arith.constant {simulation.rematerialized} 1 : i32
      simulation.finish %arg0, %c1_i32_8
      %c0_i32_9 = arith.constant 0 : i32
      %46 = simulation.context.storage %arg0[3] : !simulation.ref<i32>
      simulation.ref.store %c0_i32_9 to %46 : i32, !simulation.ref<i32>
      simulation.return
    ^bb11:  // pred: ^bb7
      %47 = simulation.context.storage %arg0[4] : !simulation.ref<i64>
      simulation.ref.store %19 to %47 : i64, !simulation.ref<i64>
      %c1_i32_10 = arith.constant 1 : i32
      %48 = simulation.context.storage %arg0[3] : !simulation.ref<i32>
      simulation.ref.store %c1_i32_10 to %48 : i32, !simulation.ref<i32>
      cf.br ^bb12
    ^bb12:  // pred: ^bb11
      simulation.return
    ^bb13(%49: i64):  // pred: ^bb5
      %c2_i64_11 = arith.constant {simulation.rematerialized} 2 : i64
      %50 = arith.remui %15, %c2_i64_11 : i64
      %51 = arith.trunci %50 : i64 to i32
      %52 = simulation.ref.load %arg2 : !simulation.ref<!simulation.class_handle<@__obelisk_class_s63_env>> -> !simulation.class_handle<@__obelisk_class_s63_env>
      %53 = simulation.class.field_ref %52[@__obelisk_class_s63_env_field_0] : !simulation.class_handle<@__obelisk_class_s63_env> -> !simulation.managed_ref<!simulation.unpacked_array<0 : 1 x !simulation.class_handle<@__obelisk_class_s21_component>, #simulation.storage_layout<size = 16, alignment = 8, stride = 8, offsets = []>>, @__obelisk_class_s63_env>
      %54 = simulation.managed.load %53 : !simulation.managed_ref<!simulation.unpacked_array<0 : 1 x !simulation.class_handle<@__obelisk_class_s21_component>, #simulation.storage_layout<size = 16, alignment = 8, stride = 8, offsets = []>>, @__obelisk_class_s63_env> -> !simulation.unpacked_array<0 : 1 x !simulation.class_handle<@__obelisk_class_s21_component>, #simulation.storage_layout<size = 16, alignment = 8, stride = 8, offsets = []>>
      %55 = arith.extsi %51 : i32 to i65
      %56 = simulation.array.extract_dynamic %54[%55] : (!simulation.unpacked_array<0 : 1 x !simulation.class_handle<@__obelisk_class_s21_component>, #simulation.storage_layout<size = 16, alignment = 8, stride = 8, offsets = []>>, i65) -> !simulation.class_handle<@__obelisk_class_s21_component>
      simulation.class.virtual_call %56[@__obelisk_class_s21_component_method_1] slot 0 signature_id 5885352902772247445(%6) : (!simulation.class_handle<@__obelisk_class_s21_component>, !simulation.class_handle<@__obelisk_class_s3_item>) -> ()
      %c1_i64_12 = arith.constant {simulation.rematerialized} 1 : i64
      %57 = arith.addi %15, %c1_i64_12 : i64
      cf.br ^bb5(%57, %4 : i64, i64) {schedule.bounded_loop_latch}
    }
  }
}

