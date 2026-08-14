// RUN: obelisk -fno-lto -O0 %s -o %t.o0.native
// RUN: %t.o0.native > %t.o0.native.out
// RUN: obelisk -fno-lto -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode > %t.o0.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o0.bytecode.out
// RUN: obelisk -fno-lto -O3 %s -o %t.o3.native
// RUN: %t.o3.native > %t.o3.native.out
// RUN: obelisk -fno-lto -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode > %t.o3.bytecode.out
// RUN: diff -u %t.o3.native.out %t.o3.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o3.native.out
// RUN: FileCheck %s --implicit-check-not=unreachable-param-wait < %t.o3.native.out
// RUN: obelisk -emit-sim %s | FileCheck %s --check-prefix=SIM

package constant_pkg;
  parameter int PKG_VALUE = 17;
endpackage

module constant_leaf #(parameter int INSTANCE_VALUE = -1);
  initial $display("instance=%0d", INSTANCE_VALUE);
endmodule

module native_elaborated_constants;
  typedef struct packed {
    logic [3:0] high;
    logic [3:0] low;
  } packed_t;
  parameter int SIGNED_VALUE = -3;
  parameter logic [5:0] FOUR_STATE = 6'b10xz01;
  parameter packed_t PACKED_VALUE = 8'b10xz1010;
  parameter real REAL_VALUE = 1.25;
  localparam integer LOCAL_VALUE = 42;
  typedef enum logic [2:0] { ENUM_VALUE = 3'b101 } enum_t;
  specify
    specparam SPEC_VALUE = 9;
  endspecify
  logic parameter_only_comb;
  logic parameter_only_latch;

  always_comb parameter_only_comb = SIGNED_VALUE == -3;
  always_latch
    if (SIGNED_VALUE == -3)
      parameter_only_latch = 1'b1;

  constant_leaf #(.INSTANCE_VALUE(11)) first();
  constant_leaf #(.INSTANCE_VALUE(23)) second();

  function automatic int read_package_parameter();
    return constant_pkg::PKG_VALUE;
  endfunction

  initial begin
    $display("signed=%0d four=%b real=%0.2f local=%0d enum=%b spec=%0d pkg=%0d hier=%0d call=%0d",
             SIGNED_VALUE, FOUR_STATE, REAL_VALUE, LOCAL_VALUE, ENUM_VALUE,
             SPEC_VALUE, constant_pkg::PKG_VALUE, first.INSTANCE_VALUE,
             read_package_parameter());
    $display("packed=%b", PACKED_VALUE);
    fork
      $display("fork-signed=%0d", SIGNED_VALUE);
    join
  end

  initial begin
    wait ((SIGNED_VALUE + LOCAL_VALUE) == 39);
    $display("constant-wait=ready");
  end

  initial begin
    wait (SIGNED_VALUE == 0);
    $display("unreachable-param-wait");
  end

  for (genvar g = 0; g < 2; ++g) begin : generated
    initial $display("genvar=%0d", g);
  end

  initial begin
    #1;
    $display("constant-controls comb=%b latch=%b",
             parameter_only_comb, parameter_only_latch);
    $finish;
  end
endmodule

// CHECK-DAG: instance=11
// CHECK-DAG: instance=23
// CHECK-DAG: signed=-3 four=10xz01 real=1.25 local=42 enum=101 spec=9 pkg=17 hier=11 call=17
// CHECK-DAG: packed=10xz1010
// CHECK-DAG: fork-signed=-3
// CHECK-DAG: constant-wait=ready
// CHECK-DAG: constant-controls comb=1 latch=1
// CHECK-DAG: genvar=0
// CHECK-DAG: genvar=1

// The frozen boundary preserves signed, real, and four-state payloads before
// later optimization is allowed to fold them.
// SIM: arith.constant -3 : i32
// SIM: arith.constant 1.250000e+00 : f64
// SIM: obelisk_sim.logic.constant -27 : i6, 12 : i6
