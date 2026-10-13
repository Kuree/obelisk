// RUN: obelisk -O3 -Wno-nonstandard-string-concat %s -o %t.native
// RUN: %t.native > %t.native.out 2> %t.native.err
// RUN: FileCheck %s < %t.native.out
// RUN: obelisk -O3 -Wno-nonstandard-string-concat --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode 2> %t.bytecode.err | diff -u %t.native.out -
// RUN: diff -u %t.native.err %t.bytecode.err

module native_managed_strings;
  typedef logic [39:0] packed_string_t;
  string static_text = "static";

  function automatic string decorate(input string value);
    return {"[", value, "]"};
  endfunction

  function automatic string append_bang(ref string value);
    value += "!";
    return value;
  endfunction

  initial begin
    string text;
    string copy;
    string numeric;
    string choice;
    string format;
    logic ambiguous;
    packed_string_t packed_value;
    integer parsed_decimal;
    integer parsed_hex;
    integer parsed_oct;
    integer parsed_bin;
    real parsed_real;

    text = "Hello";
    copy = {text, " ", "WORLD"};
    $display("value=%s len=%0d first=%c", copy, copy.len(), copy.getc(0));
    $display("lower=%s upper=%s cmp=%0d icmp=%0d",
             copy.tolower(), text.toupper(),
             text.compare("Hello"), text.icompare("hELLo"));
    $display("substr=%s decorated=%s", copy.substr(6, 10), decorate(text));
    choice = "ref";
    choice = append_bang(choice);
    $display("ref=%s", choice);

    copy.putc(0, "h");
    copy[1] = "A";
    copy[99] = "Z";
    copy[2] = 0;
    copy += "!";
    packed_value = packed_string_t'(text);
    $display("putc=%s packed=%h static=%s", copy, packed_value, static_text);
    choice = text == "Hello" ? "yes" : "no";
    case (choice)
      "yes": $display("conditional=%s case=yes", choice);
      default: $display("conditional=%s case=no", choice);
    endcase
    ambiguous = 1'bx;
    $display("ambiguous=%b", ambiguous);
    choice = ambiguous ? "same" : "same";
    $display("ambiguous-equal=%s", choice);
    choice = ambiguous ? "left" : "right";
    $display("ambiguous-packed-len=%0d", choice.len());
    choice = ambiguous ? text : copy;
    $display("ambiguous-string-len=%0d", choice.len());
    format = "dynamic=%s";
    $display(format, text);

    numeric = " -1_23";
    parsed_decimal = numeric.atoi();
    numeric = "3.25";
    parsed_real = numeric.atoreal();
    $display("atoi=%0d real=%g", parsed_decimal, parsed_real);
    numeric = "f_f";
    parsed_hex = numeric.atohex();
    numeric = "17";
    parsed_oct = numeric.atooct();
    numeric = "101";
    parsed_bin = numeric.atobin();
    $display("hex=%0d oct=%0d bin=%0d",
             parsed_hex, parsed_oct, parsed_bin);
    numeric.itoa(-42);
    $write("itoa=%s ", numeric);
    numeric.hextoa(255);
    $write("hex=%s ", numeric);
    numeric.octtoa(9);
    $write("oct=%s ", numeric);
    numeric.bintoa(5);
    $write("bin=%s ", numeric);
    numeric.realtoa(3.25);
    $display("real=%s", numeric);
  end
endmodule

// CHECK: value=Hello WORLD len=11 first=H
// CHECK-NEXT: lower=hello world upper=HELLO cmp=0 icmp=0
// CHECK-NEXT: substr=WORLD decorated=[Hello]
// CHECK-NEXT: ref=ref!
// CHECK-NEXT: putc=hAllo WORLD! packed=48656c6c6f static=static
// CHECK-NEXT: conditional=yes case=yes
// CHECK-NEXT: ambiguous=x
// CHECK-NEXT: ambiguous-equal=same
// CHECK-NEXT: ambiguous-packed-len=4
// CHECK-NEXT: ambiguous-string-len=0
// CHECK-NEXT: dynamic=Hello
// CHECK-NEXT: atoi=-123 real=3.25
// CHECK-NEXT: hex=255 oct=15 bin=5
// CHECK-NEXT: itoa=-42 hex=ff oct=11 bin=101 real=3.25
