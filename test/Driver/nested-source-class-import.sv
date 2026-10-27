// RUN: talos -emit-slang %s -o /dev/null
// RUN: talos -emit-talos %s -o /dev/null

// Source inventory must not elaborate unused nested signatures, defaults, or
// fields. Realized nested classes must retain resolvable lexical references.
class G #(type T);
  class Inner;
    int value;
    T dependent;
    function T inspect(); return dependent; endfunction
    class Deep #(type U);
      typedef U Alias;
      U missing;
    endclass
  endclass
  class Unused;
    T dependent;
    function T inspect(); return dependent; endfunction
  endclass
endclass
module top;
  G#(int)::Inner a;
  G#(bit)::Inner b;
  G#(int)::Inner::Deep#(byte) d;
endmodule
