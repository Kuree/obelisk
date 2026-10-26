// RUN: obelisk -emit-slang %s -o /dev/null
// RUN: obelisk -emit-obelisk %s -o /dev/null

// Both source emit modes accept explicit and implicit method receiver forms.
class C;
  static function void stat(); endfunction
  function void inst(); stat(); endfunction
  function void implicit_call(); inst(); endfunction
endclass
module method_calls;
  C c;
  int q[$];
  initial C::stat();
  initial void'(q.size());
  initial void'(c.randomize());
  initial void'($urandom());
endmodule
