// RUN: obelisk -fno-lto -O0 --vpi=off %s -o %t.o0.native
// RUN: %t.o0.native | FileCheck %s
// RUN: obelisk -fno-lto -O0 --vpi=off --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode | FileCheck %s
// RUN: obelisk -fno-lto -O3 --vpi=off %s -o %t.o3.native
// RUN: %t.o3.native | FileCheck %s
// RUN: obelisk -fno-lto -O3 --vpi=off --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode | FileCheck %s

// Exact IEEE 1800 Clause 28 source-strength forwarding conformance. SV is
// retained here because the primitive/source binding itself is under test;
// the directed topology runtime is covered independently by hand-authored
// Simulation MLIR.
module mos_source_strength_forwarding;
  logic supply_enabled = 1;
  logic ncontrol = 1;
  logic pcontrol = 0;
  wire source;
  assign (supply1, supply0) source = supply_enabled ? 1'b1 : 1'bz;
  assign (pull1, pull0) source = 1'b1;

  wire nout, pout, cout, rnout, rpout, rcout;
  nmos n0(nout, source, ncontrol);
  pmos p0(pout, source, pcontrol);
  cmos c0(cout, source, ncontrol, pcontrol);
  rnmos rn0(rnout, source, ncontrol);
  rpmos rp0(rpout, source, pcontrol);
  rcmos rc0(rcout, source, ncontrol, pcontrol);

  // A destination-local supply driver cannot flow backward across a MOS
  // device into the source terminal.
  wire blocked;
  assign (supply0, supply1) blocked = 1'b0;
  nmos nb(blocked, source, ncontrol);

  initial begin
    #1;
    $display("supply %v %v %v %v %v %v %v %v", source, nout, pout, cout,
             rnout, rpout, rcout, blocked);
    supply_enabled = 0;
    #1;
    $display("strength-only %v %v %v %v %v %v %v %v", source, nout, pout,
             cout, rnout, rpout, rcout, blocked);
    ncontrol = 1'bx;
    pcontrol = 1'bx;
    #1;
    $display("uncertain %v %v %v %v %v %v", nout, pout, cout, rnout, rpout,
             rcout);
    ncontrol = 1;
    pcontrol = 0;
    force source = 0;
    #1;
    $display("forced %v %v %v %v", source, nout, rnout, blocked);
    release source;
    #1;
    $display("released %v %v %v", source, nout, rnout);
  end
endmodule

// CHECK: supply Su1 St1 St1 St1 Pu1 Pu1 Pu1 Su0
// CHECK: strength-only Pu1 Pu1 Pu1 Pu1 We1 We1 We1 Su0
// CHECK: uncertain PuH PuH PuH WeH WeH WeH
// CHECK: forced Su0 St0 Pu0 Su0
// CHECK: released Pu1 Pu1 We1
