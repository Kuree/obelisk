// RUN: obelisk -O0 %s -o %t.native
// RUN: %t.native | FileCheck %s
// RUN: obelisk -O0 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode | FileCheck %s

module native_string_format;
  string text;
  initial begin
    text = $sformatf("d=%0d h=%h s=%s %%", -12, 8'h2a, "ok");
    $display("f:%s", text);
    $sformat(text, "b=%b o=%o", 4'b1010, 6'o17);
    $display("t:%s", text);
    text = $psprintf("x=%x", 8'hbc);
    $display("p:%s", text);
    // CHECK: f:d=-12 h=2a s=ok %
    // CHECK: t:b=1010 o=17
    // CHECK: p:x=bc
  end
endmodule
