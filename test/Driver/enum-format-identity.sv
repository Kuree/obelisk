// RUN: obelisk -O0 --vpi=off %s -o %t.native
// RUN: %t.native > %t.native.out
// RUN: obelisk -O0 --vpi=off --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode > %t.bytecode.out
// RUN: diff -u %t.bytecode.out %t.native.out
// RUN: FileCheck %s < %t.native.out

// IEEE 1800-2017 21.2.1.7: %p prints a valid enumeration value using its
// declared name. A dynamic format still has to retain that enum identity.
module enum_format_identity;
  typedef enum logic [59:0] {
    FIRST = 60'h1,
    LARGE = 60'h1234_4567_abcd
  } value_t;

  value_t value;
  string format;
  string text;

  initial begin
    value = LARGE;
    format = "%p";
    text = $sformatf(format, value);
    $display("named=%s", text);

    format = "%0h";
    text = $sformatf(format, value);
    $display("numeric=%s", text);

    value = value_t'(60'h11);
    format = "%p";
    text = $sformatf(format, value);
    $display("unnamed=%s", text);

    // `%s` enum mnemonics are a widely implemented compatibility extension;
    // retaining them costs no extra runtime lookup once %p identity exists.
    value = FIRST;
    format = "%s";
    text = $sformatf(format, value);
    $display("string=%s", text);

    // CHECK: named=LARGE
    // CHECK: numeric=12344567abcd
    // CHECK: unnamed=17
    // CHECK: string=FIRST
  end
endmodule
