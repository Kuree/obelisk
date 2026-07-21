// RUN: obelisk -emit-obelisk %s > %t.semantic.mlir
// RUN: obelisk-opt %t.semantic.mlir --lower-obelisk-to-sim > %t.threaded.mlir
// RUN: obelisk-opt %t.semantic.mlir --lower-obelisk-to-sim --mlir-disable-threading > %t.single.mlir
// RUN: diff -u %t.single.mlir %t.threaded.mlir
// RUN: FileCheck %s --check-prefix=SIM < %t.threaded.mlir
// RUN: FileCheck %s --check-prefix=FINAL < %t.threaded.mlir
// RUN: obelisk -emit-sim %s | FileCheck %s --check-prefix=DRIVER

module sim_child(output logic ready);
  initial ready = 1'b1;
endmodule

module sim_e2e;
  logic clk;
  logic ready;
  logic [7:0] input_value;
  logic [7:0] input_b;
  logic [7:0] state;
  logic signed [7:0] signed_a;
  logic signed [7:0] signed_b;
  logic signed_lt;
  logic mirror;
  wire [7:0] driven;

  sim_child child(ready);
  assign driven = input_value + input_b;

  function automatic logic [7:0] increment(
      input logic [7:0] value, output logic echoed);
    echoed = value[0];
    increment = value + 1;
  endfunction

  always_comb begin
    automatic logic [7:0] local_value = driven;
    state = local_value + input_b;
  end

  always_comb signed_lt = signed_a < signed_b;
  always_comb mirror = ready;

  always_ff @(posedge clk) begin
    if (ready)
      state <= increment(state, ready);
    else
      state <= 0;
  end

  initial begin
    automatic logic [7:0] delayed = input_value;
    automatic int index = 3;
    state[3:0] = {2{input_value[1:0]}};
    state[index +: 2] = input_value[index -: 2];
    while (state < 4) begin
      state = state + 1;
      if (state == 2)
        continue;
      if (state == 3)
        break;
    end
    case (state)
      0: state = 1;
      1,
      2: state = 3;
      default: state = 0;
    endcase
    #5;
    state = increment(delayed, ready);
  end

  always @(posedge clk or negedge ready)
    state <= input_b;
endmodule

// SIM: obelisk_sim.design @design
// SIM-DAG: obelisk_sim.scope.decl 0
// SIM-DAG: obelisk_sim.storage.decl
// SIM-DAG: obelisk_sim.net.decl
// SIM-DAG: obelisk_sim.driver.decl
// SIM-DAG: obelisk_sim.func @__obelisk_root
// SIM-DAG: obelisk_sim.spawn
// SIM-DAG: obelisk_sim.func @unit_
// SIM-DAG: obelisk_sim.call @unit_
// SIM-DAG: obelisk_sim.ref.extract
// SIM-DAG: obelisk_sim.logic.concat
// SIM-DAG: obelisk_sim.logic.replicate
// SIM-DAG: obelisk_sim.logic.compare slt
// SIM-DAG: obelisk_sim.ref.dyn_extract
// SIM-DAG: obelisk_sim.logic.dyn_extract
// SIM-DAG: cf.cond_br
// SIM-DAG: obelisk_sim.nba.enqueue
// SIM-DAG: obelisk_sim.suspend.change
// SIM-DAG: obelisk_sim.suspend.edge posedge
// SIM-DAG: obelisk_sim.suspend.any
// SIM: obelisk_sim.suspend.delay {{.*}} to ^{{.*}}({{.*}}!obelisk_sim.logic<8>{{.*}})

// FINAL-NOT: obelisk.sv.
// FINAL-NOT: obelisk_sim.bindings
// FINAL-NOT: obelisk_sim.delay_scale
// FINAL-NOT: time_unit_fs
// FINAL-NOT: obelisk_sim.ref.alloc

// DRIVER: obelisk_sim.design @design
// DRIVER: obelisk_sim.func @__obelisk_root
// DRIVER-NOT: obelisk.sv.
