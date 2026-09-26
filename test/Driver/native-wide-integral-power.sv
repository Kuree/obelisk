// RUN: obelisk -O0 --top=top -emit-sim %s -o - | FileCheck %s --check-prefix=SIM
// RUN: obelisk -O0 --top=top %s -o %t.o0.native
// RUN: obelisk -O0 --execution-tier=bytecode --top=top %s -o %t.o0.bytecode
// RUN: obelisk -O3 --top=top %s -o %t.o3.native
// RUN: obelisk -O3 --execution-tier=bytecode --top=top %s -o %t.o3.bytecode
// RUN: %t.o0.native > %t.o0.native.out
// RUN: %t.o0.bytecode > %t.o0.bytecode.out
// RUN: %t.o3.native > %t.o3.native.out
// RUN: %t.o3.bytecode > %t.o3.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o0.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o3.native.out
// RUN: diff -u %t.o0.native.out %t.o3.bytecode.out
// RUN: FileCheck %s < %t.o0.native.out

module top;
  logic [4095:0] base, exponent, result;
  logic [127:0] narrow_base, narrow_result;
  logic [31:0] narrow_exponent;
  logic signed [7:0] signed_base, signed_exponent, signed_result;

  initial begin
    // IEEE 1800-2017 11.4.4: exponentiation preserves the exponent's
    // self-determined packed width. This width guards against compiler IR
    // growth proportional to the exponent width.
    base = 4096'd3;
    exponent = 4096'd5;
    result = base ** exponent;
    assert (result == 4096'd243);

    // The exponent is self-determined in both width directions.
    narrow_base = 128'd3;
    narrow_result = narrow_base ** exponent;
    assert (narrow_result == 128'd243);
    narrow_exponent = 32'd5;
    result = base ** narrow_exponent;
    assert (result == 4096'd243);

    exponent = '0;
    result = base ** exponent;
    assert (result == 4096'd1);

    exponent = 'x;
    result = base ** exponent;
    assert (result === {4096{1'bx}});

    // IEEE 1800-2017 11.4.4, Table 11-4: a signed negative exponent
    // selects its result from the base and, for -1, the exponent parity.
    signed_base = -1;
    signed_exponent = -3;
    signed_result = signed_base ** signed_exponent;
    assert (signed_result == -1);
    signed_exponent = -2;
    signed_result = signed_base ** signed_exponent;
    assert (signed_result == 1);
    signed_base = 2;
    signed_result = signed_base ** signed_exponent;
    assert (signed_result == 0);
    signed_base = 0;
    signed_result = signed_base ** signed_exponent;
    assert (signed_result === 8'hxx);
    $display("wide integral power passed");
  end
endmodule

// SIM-COUNT-9: obelisk_sim.logic.power
// CHECK: wide integral power passed
