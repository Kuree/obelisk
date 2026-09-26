// RUN: obelisk -O0 --vpi=off %s -o %t.native
// RUN: %t.native > %t.native.out
// RUN: obelisk -O0 --vpi=off --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode > %t.bytecode.out
// RUN: diff -u %t.bytecode.out %t.native.out
// RUN: FileCheck %s < %t.native.out

// IEEE 1800-2017 12.6: pattern variables are local to their matching item.
// Separate items may reuse a name even when their captured types differ.
module tagged_pattern_binding_identity;
  typedef union tagged {
    bit [59:0] Narrow;
    bit [89:0] Wide;
  } tagged_t;

  tagged_t value;
  bit [89:0] result;

  initial begin
    value = tagged Narrow(60'hfed_cba9_8765_4321);
    case (value) matches
      tagged Narrow .payload: result = payload;
      tagged Wide .payload: result = payload;
    endcase
    assert (result == 90'h0000000_0fedcba987654321);

    value = tagged Wide(90'h2de_adbeef_cafebabe_123456);
    case (value) matches
      tagged Narrow .payload: result = payload;
      tagged Wide .payload: result = payload;
    endcase
    assert (result == 90'h2de_adbeef_cafebabe_123456);
    $display("tagged pattern identities passed");
  end

  // CHECK: tagged pattern identities passed
endmodule
