// RUN: obelisk -O0 --vpi=off %s -o %t.o0.native
// RUN: %t.o0.native | FileCheck %s
// RUN: obelisk -O0 --vpi=off --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode | FileCheck %s
// RUN: obelisk -O3 --vpi=off %s -o %t.o3.native
// RUN: %t.o3.native | FileCheck %s
// RUN: obelisk -O3 --vpi=off --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode | FileCheck %s

// IEEE 1800-2017 28.7-28.8: MOS switches preserve an enabled source's Z,
// treat Z controls as unknown, and CMOS conducts when either transistor is
// definitely on. Resistive variants have the same four-state truth table.
module mos_primitives;
  logic data;
  logic ncontrol;
  logic pcontrol;
  wire nout, pout, cout;
  wire rnout, rpout, rcout;

  nmos n0(nout, data, ncontrol);
  pmos p0(pout, data, pcontrol);
  cmos c0(cout, data, ncontrol, pcontrol);
  rnmos rn0(rnout, data, ncontrol);
  rpmos rp0(rpout, data, pcontrol);
  rcmos rc0(rcout, data, ncontrol, pcontrol);

  task automatic check_outputs(logic expected_n, logic expected_p,
                               logic expected_c);
    #1;
    if (nout !== expected_n || pout !== expected_p || cout !== expected_c ||
        rnout !== expected_n || rpout !== expected_p || rcout !== expected_c)
      $fatal(0, "MOS mismatch: %b %b %b / %b %b %b", nout, pout, cout,
             rnout, rpout, rcout);
  endtask

  initial begin
    data = 0; ncontrol = 0; pcontrol = 1; check_outputs('z, 'z, 'z);
    data = 0; ncontrol = 1; pcontrol = 0; check_outputs(0, 0, 0);
    data = 1; ncontrol = 1; pcontrol = 0; check_outputs(1, 1, 1);
    data = 'x; ncontrol = 1; pcontrol = 0; check_outputs('x, 'x, 'x);
    data = 'z; ncontrol = 1; pcontrol = 0; check_outputs('z, 'z, 'z);

    data = 0; ncontrol = 'x; pcontrol = 'z; check_outputs('x, 'x, 'x);
    data = 1; ncontrol = 'z; pcontrol = 'x; check_outputs('x, 'x, 'x);
    data = 'z; ncontrol = 'x; pcontrol = 'x; check_outputs('z, 'z, 'z);

    data = 1; ncontrol = 1; pcontrol = 1; check_outputs(1, 'z, 1);
    data = 0; ncontrol = 0; pcontrol = 0; check_outputs('z, 0, 0);
    data = 1; ncontrol = 'x; pcontrol = 0; check_outputs('x, 1, 1);
    data = 0; ncontrol = 1; pcontrol = 'x; check_outputs(0, 'x, 0);
    $display("MOS PASS");
  end
endmodule

// CHECK: MOS PASS
