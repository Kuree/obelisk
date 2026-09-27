// RUN: obelisk -O0 --vpi=off %s -o %t.o0.native
// RUN: %t.o0.native | FileCheck %s
// RUN: obelisk -O0 --vpi=off --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode | FileCheck %s
// RUN: obelisk -O3 --vpi=off %s -o %t.o3.native
// RUN: %t.o3.native | FileCheck %s
// RUN: obelisk -O3 --vpi=off --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode | FileCheck %s
// RUN: obelisk -emit-sim -O0 --vpi=off %s | FileCheck %s --check-prefix=LIFETIME

// IEEE 1800-2017 20.17: all sixteen PLA tasks share the same ascending
// memory/input/output order. Synchronous calls update only when invoked;
// asynchronous calls install a persistent relation before returning.
module pla_tasks;
  logic [1:3] array_memory [1:2];
  logic [1:3] plane_memory [1:2];
  logic [1:3] input_terms;
  logic [1:2] sync_aa, sync_ap, sync_na, sync_np;
  logic [1:2] sync_oa, sync_op, sync_noa, sync_nop;
  logic [1:2] async_aa, async_ap, async_na, async_np;
  logic [1:2] async_oa, async_op, async_noa, async_nop;

  logic [1:3] reactive_memory [1:2];
  logic [1:3] a, b;
  logic [1:2] reactive_output;
  logic [1:2] synchronous_only;

  task automatic check_automatic_scope;
    logic [1:3] memory [1:2];
    logic [1:3] left, right;
    logic [1:2] result;
    begin
      memory[1] = 3'b101;
      memory[2] = 3'b010;
      left = 3'b100;
      right = 3'b000;
      $async$or$array(memory, left ^ right, result);
      $display("automatic initial=%b", result);
      right = 3'b110;
      #1 $display("automatic expression=%b", result);
    end
  endtask

  initial begin
    array_memory[1] = 3'b101;
    array_memory[2] = 3'b010;
    plane_memory[1] = 3'b10?;
    plane_memory[2] = 3'b01z;
    input_terms = 3'b110;

    $sync$and$array(array_memory, input_terms, sync_aa);
    $sync$and$plane(plane_memory, input_terms, sync_ap);
    $sync$nand$array(array_memory, input_terms, sync_na);
    $sync$nand$plane(plane_memory, input_terms, sync_np);
    $sync$or$array(array_memory, input_terms, sync_oa);
    $sync$or$plane(plane_memory, input_terms, sync_op);
    $sync$nor$array(array_memory, input_terms, sync_noa);
    $sync$nor$plane(plane_memory, input_terms, sync_nop);
    $display("sync=%b %b %b %b %b %b %b %b", sync_aa, sync_ap,
             sync_na, sync_np, sync_oa, sync_op, sync_noa, sync_nop);

    $async$and$array(array_memory, input_terms, async_aa);
    $async$and$plane(plane_memory, input_terms, async_ap);
    $async$nand$array(array_memory, input_terms, async_na);
    $async$nand$plane(plane_memory, input_terms, async_np);
    $async$or$array(array_memory, input_terms, async_oa);
    $async$or$plane(plane_memory, input_terms, async_op);
    $async$nor$array(array_memory, input_terms, async_noa);
    $async$nor$plane(plane_memory, input_terms, async_nop);
    $display("async=%b %b %b %b %b %b %b %b", async_aa, async_ap,
             async_na, async_np, async_oa, async_op, async_noa, async_nop);

    // Array memory admits only 0 and 1. Nonconforming X/Z symbols do not
    // include an input term; only an exact known 1 does.
    array_memory[1] = 3'bxz1;
    array_memory[2] = 3'bxz0;
    input_terms = 3'b001;
    $sync$and$array(array_memory, input_terms, sync_aa);
    $sync$nand$array(array_memory, input_terms, sync_na);
    $sync$or$array(array_memory, input_terms, sync_oa);
    $sync$nor$array(array_memory, input_terms, sync_noa);
    $display("array xz=%b %b %b %b", sync_aa, sync_na, sync_oa,
             sync_noa);

    // Plane X is the worst case and Z is don't-care. An all-Z product is 1;
    // an all-Z sum is 0.
    plane_memory[1] = 3'bxzz;
    plane_memory[2] = 3'bzzz;
    input_terms = 3'b000;
    $sync$and$plane(plane_memory, input_terms, sync_ap);
    $sync$nand$plane(plane_memory, input_terms, sync_np);
    $sync$or$plane(plane_memory, input_terms, sync_op);
    $sync$nor$plane(plane_memory, input_terms, sync_nop);
    $display("plane xz=%b %b %b %b", sync_ap, sync_np, sync_op,
             sync_nop);

    reactive_memory[1] = 3'b101;
    reactive_memory[2] = 3'b010;
    a = 3'b100;
    b = 3'b000;
    $async$or$array(reactive_memory, a ^ b, reactive_output);
    $display("reactive initial=%b", reactive_output);
    b = 3'b110;
    #1 $display("reactive expression=%b", reactive_output);
    reactive_memory[1] = 3'b010;
    #1 $display("reactive memory=%b", reactive_output);

    $sync$or$array(reactive_memory, a ^ b, synchronous_only);
    b = 3'b000;
    reactive_memory[1] = 3'b101;
    #1 $display("sync unchanged=%b", synchronous_only);
    $sync$or$array(reactive_memory, a ^ b, synchronous_only);
    $display("sync recalled=%b", synchronous_only);

    check_automatic_scope();
    $finish;
  end
endmodule

// CHECK: sync=01 00 10 11 11 11 00 00
// CHECK-NEXT: async=01 00 10 11 11 11 00 00
// CHECK-NEXT: array xz=11 00 10 01
// CHECK-NEXT: plane xz=x1 x0 x0 x1
// CHECK-NEXT: reactive initial=10
// CHECK-NEXT: reactive expression=01
// CHECK-NEXT: reactive memory=11
// CHECK-NEXT: sync unchanged=11
// CHECK-NEXT: sync recalled=10
// CHECK-NEXT: automatic initial=10
// CHECK-NEXT: automatic expression=01

// The persistent child owns stable references to every automatic local it
// rereads or writes, so the process-frame lifetime extends past the call.
// LIFETIME-LABEL: simulation.func private @unit_0.fork
// LIFETIME-COUNT-4: simulation.automatic_reference_capture
// LIFETIME-SAME: schedule.detached_controls
// LIFETIME-SAME: schedule.prime_on_spawn
