// RUN: talos -emit-slang %s | talos-opt --convert-slang-to-talos -o %t.talos.mlir
// RUN: talos-opt %t.talos.mlir '--lower-talos-to-sim=opt-level=0' > %t.threaded.mlir
// RUN: talos-opt %t.talos.mlir '--lower-talos-to-sim=opt-level=0' --mlir-disable-threading | diff -u - %t.threaded.mlir
// RUN: talos-opt %t.talos.mlir '--lower-talos-to-sim=workers=2 vpi=read' | FileCheck %s --check-prefix=OPTIONS
// RUN: FileCheck %s --check-prefix=SIM < %t.threaded.mlir
// RUN: FileCheck %s --check-prefix=FINAL < %t.threaded.mlir
// RUN: FileCheck %s --check-prefix=SCCP --implicit-check-not='arith.constant 123456789' < %t.threaded.mlir
// RUN: FileCheck %s --check-prefix=COPYBACK < %t.threaded.mlir

// OPTIONS: #schedule.graph<version = 1, vpi = read, workers = 2

// The finest precision (1ps, from sim_fast_timescale) is the design's tick.
// SIM: simulation.design @design attributes {{.*}}time_precision_fs = 1000 : i64
// SIM-DAG: simulation.scope.decl 0
// SIM-DAG: simulation.storage.decl
// SIM-DAG: simulation.net.decl
// SIM-DAG: simulation.driver.decl
// SIM-DAG: simulation.func @__talos_root
// SIM-DAG: simulation.spawn
// SIM-DAG: simulation.func private @unit_
// SIM-DAG: simulation.call @unit_
// SIM-DAG: simulation.ref.extract
// SIM-DAG: simulation.logic.replicate
// SIM-DAG: simulation.logic.compare slt
// `#5` is 5ns under 1ns units and 50ns under 10ns units, in 1ps ticks.
// SIM-DAG: simulation.time.constant 5000{{$}}
// SIM-DAG: simulation.time.constant 50000{{$}}
// SIM-DAG: simulation.logic.binary and
// SIM-DAG: simulation.logic.unary logical_not
// SIM-DAG: arith.cmpi eq
// SIM-DAG: simulation.logic.shift left
// SIM-DAG: -> (!simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>) attributes {{.*}}entry_kind = 8 : i32
// SIM-DAG: {{%.*}}:3 = simulation.call
// SIM-DAG: simulation.ref.store {{%.*}}#1
// SIM-DAG: simulation.ref.store {{%.*}}#2
// Private aggregate reads become SSA value selections after promotion.
// SIM-DAG: simulation.aggregate.extract
// SIM-DAG: simulation.packed.flatten
// SIM-DAG: cf.cond_br
// SIM-DAG: simulation.nba.enqueue
// SIM-DAG: simulation.suspend.change
// SIM-DAG: simulation.suspend.edge posedge
// SIM-DAG: simulation.suspend.any
// SIM-DAG: simulation.suspend.delay {{.*}} to ^

// FINAL-NOT: talos.sv.
// FINAL-NOT: simulation.bindings
// FINAL-NOT: simulation.delay_scale
// FINAL-NOT: time_unit_fs
// FINAL-NOT: simulation.ref.alloc
// FINAL-NOT: simulation.func @unit_

// A constant argument folds through the identity function, and the function
// nobody calls is dropped with its constant.
// SCCP: simulation.func private @[[IDENTITY:unit_[0-9]+]](%arg0: !simulation.context{{.*}}) -> i1 attributes
// SCCP: %[[IDENTITY_RESULT:.*]] = arith.constant true
// SCCP: simulation.return %[[IDENTITY_RESULT]] : i1
// SCCP: simulation.func private @[[CALLER:unit_[0-9]+]]({{.*}}!simulation.ref<i1>
// SCCP: simulation.call @[[IDENTITY]]
// SCCP: %[[FOLDED:.*]] = arith.constant {simulation.rematerialized} true
// SCCP: simulation.ref.store %[[FOLDED]]

// Input arguments are value-only. Only output and inout results are copied
// back before the function return value is stored.
// COPYBACK: simulation.time.constant 50000
// COPYBACK: simulation.suspend.delay
// COPYBACK: %[[INPUT_VALUE:.*]] = simulation.ref.load %[[INPUT_REF:.*]]
// COPYBACK: %[[CALL:.*]]:3 = simulation.call {{.*}}%[[INPUT_VALUE]]
// COPYBACK-NOT: simulation.ref.store {{.*}} to %[[INPUT_REF]]
// COPYBACK: simulation.ref.store
// COPYBACK-NOT: simulation.ref.store {{.*}} to %[[INPUT_REF]]
// COPYBACK-NEXT: simulation.ref.store %[[CALL]]#2
// COPYBACK-NOT: simulation.ref.store {{.*}} to %[[INPUT_REF]]
// COPYBACK-NEXT: simulation.ref.store %[[CALL]]#0
// COPYBACK-NOT: simulation.ref.store {{.*}} to %[[INPUT_REF]]
// COPYBACK-NEXT: simulation.return

`timescale 1ns/1ps
module sim_child(output logic ready);
  initial ready = 1'b1;
endmodule

module sim_e2e;
  logic clk, ready, ff_echo, init_echo, signed_lt, mirror;
  logic [7:0] input_value, input_b, state, comb_state, work, async_state;
  logic signed [7:0] signed_a, signed_b;
  wire [7:0] driven;

  sim_child child(.ready(ready));
  assign driven = input_value + input_b;

  function automatic logic [7:0] increment(input logic [7:0] value,
                                           output logic echoed);
    echoed = value[0];
    increment = value + 1;
  endfunction

  always_comb begin
    automatic logic [7:0] local_value = driven;
    comb_state = local_value + input_b;
  end
  always_comb signed_lt = signed_a < signed_b;
  always_comb mirror = ready;

  always_ff @(posedge clk)
    if (ready)
      state <= increment(state, ff_echo);
    else
      state <= 0;

  initial begin
    automatic logic [7:0] delayed = input_value;
    automatic int index = 3;
    work[3:0] = {2{input_value[1:0]}};
    work[index +: 2] = input_value[index -: 2];
    while (work < 4) begin
      work = work + 1;
      if (work == 2)
        continue;
      if (work == 3)
        break;
    end
    case (work)
      0: work = 1;
      1, 2: work = 3;
      default: work = 0;
    endcase
    #5;
    work = increment(delayed, init_echo);
  end

  always @(posedge clk or negedge ready) async_state <= input_b;
endmodule

`timescale 10ns/1ns
module sim_expression_and_arguments;
  logic [7:0] lhs, rhs;
  logic logical_result, logical_not_result;
  bit [7:0] two_state;
  bit two_state_not;
  logic output_source, inout_source, input_source, function_result;

  function automatic logic observe(input logic scratch, output logic copied,
                                   inout logic exchanged);
    scratch = ~scratch;
    copied = 1'b1;
    exchanged = ~exchanged;
    return scratch ^ output_source;
  endfunction

  always_comb begin
    rhs = lhs << 1;
    logical_result = lhs && rhs;
    logical_not_result = !lhs;
    two_state_not = !two_state;
  end

  initial begin
    #5;
    function_result = observe(input_source, output_source, inout_source);
  end
endmodule

`timescale 1ns/1ps
module sim_fast_timescale;
  logic [7:0] value;
  initial begin
    value = '0;
    #5;
  end
endmodule

`timescale 10ns/1ns
module sim_sccp_pipeline;
  bit folded_sink;

  function automatic bit fold_identity(input bit value);
    fold_identity = value;
  endfunction

  function automatic int unused_function(input bit value);
    unused_function = 123456789;
  endfunction

  initial folded_sink = fold_identity(1);
endmodule
