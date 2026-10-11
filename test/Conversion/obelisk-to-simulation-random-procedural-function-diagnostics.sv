// RUN: %split-file %s %t
// RUN: %obelisk --std=1800-2023 -emit-obelisk %t/reject.sv | not obelisk-opt '--test-obelisk-edit-attributes=op=obelisk.sv.symbol.formal_argument match={name = "const_ref_value"} set={direction = 3 : i32, is_const}' '--test-obelisk-edit-attributes=op=obelisk.sv.symbol.variable match={name = "const_temp"} set={is_const}' '--test-obelisk-edit-attributes=op=obelisk.sv.symbol.variable match={name = "static_result"} set={lifetime = 1 : i32}' '--test-obelisk-edit-attributes=op=obelisk.sv.symbol.variable match={name = "static_fast"} set={lifetime = 1 : i32}' '--lower-obelisk-to-sim=opt-level=0' -o /dev/null 2>&1 | FileCheck %s --check-prefix=REJECT
// RUN: %if compiled-sampler %{ %obelisk --std=1800-2023 -emit-obelisk %t/candidate.sv | not obelisk-opt '--lower-obelisk-to-sim=opt-level=0' -o /dev/null 2>&1 | FileCheck %s --check-prefix=STAGED %}

// IEEE 1800-2023 18.5.11 forbids constraint functions with side effects or
// persistent local updates: a write to a class property (line 7), a static
// local (16), a const-ref formal (24) and a const local (33) are rejected.
// A static return variable read
// before its first assignment (41, and 48 when the result is returned by
// fallthrough) cannot be replaced by an automatic zero, and an unbounded loop
// (57) remains an implementation boundary. The const and static-result cases
// are retargeted by attribute edits because the source frontend rejects them.
// REJECT-DAG: reject.sv:7:30: error: constraint function id must be a side-effect-free expression function or an admitted automatic statement body
// REJECT-DAG: reject.sv:16:30: error: constraint function id must be a side-effect-free expression function or an admitted automatic statement body
// REJECT-DAG: reject.sv:24:30: error: constraint function id must be a side-effect-free expression function or an admitted automatic statement body
// REJECT-DAG: reject.sv:33:30: error: constraint function id must be a side-effect-free expression function or an admitted automatic statement body
// REJECT-DAG: reject.sv:41:30: error: constraint function static_result must be a side-effect-free expression function or an admitted automatic statement body
// REJECT-DAG: reject.sv:48:30: error: constraint function static_fast must be a side-effect-free expression function or an admitted automatic statement body
// REJECT-DAG: reject.sv:57:30: error: constraint function id must be a side-effect-free expression function or an admitted automatic statement body

// IEEE 1800-2023 18.5.11 and 18.5.12: when the state error (null handle
// read) is selected by the candidate value of y, the compiled sampler cannot
// decide it before solving and reports that staged execution is required.
// STAGED: error: candidate-conditioned constraint function evaluation errors require staged function execution

//--- reject.sv
class NonLocal;
  rand bit x, y;
  function automatic bit id(bit value);
    x = 0;
    return value;
  endfunction
  constraint relation { x == id(y); }
endclass
class StaticLocal;
  rand bit x, y;
  function automatic bit id(bit value);
    static bit temp;
    temp = value;
    return value;
  endfunction
  constraint relation { x == id(y); }
endclass
class ConstRef;
  rand bit x, y;
  function automatic bit id(bit const_ref_value);
    const_ref_value = 0;
    return const_ref_value;
  endfunction
  constraint relation { x == id(y); }
endclass
class ConstLocal;
  rand bit x, y;
  function automatic bit id(bit value);
    bit const_temp = 0;
    const_temp = value;
    return value;
  endfunction
  constraint relation { x == id(y); }
endclass
class StaticResult;
  rand bit x, y;
  function automatic bit static_result(bit value);
    static_result = static_result ^ value;
    return static_result;
  endfunction
  constraint relation { x == static_result(y); }
endclass
class StaticFast;
  rand bit x, y;
  function automatic bit static_fast(bit value);
    static_fast = static_fast ^ value;
  endfunction
  constraint relation { x == static_fast(y); }
endclass
class Loop;
  rand bit x, y;
  function automatic bit id(bit value);
    while (value)
      ;
    return value;
  endfunction
  constraint relation { x == id(y); }
endclass
module top;
  NonLocal a = new; StaticLocal b = new; ConstRef c = new; ConstLocal d = new; StaticResult e = new; StaticFast f = new; Loop g = new;
  initial begin
    void'(a.randomize()); void'(b.randomize()); void'(c.randomize()); void'(d.randomize());
    void'(e.randomize()); void'(f.randomize()); void'(g.randomize());
  end
endmodule

//--- candidate.sv
class D;
  bit v;
endclass
class Cand;
  rand bit x, y;
  D n;
  function automatic bit id(bit value);
    if (value) return n.v;
    return 0;
  endfunction
  constraint relation { x == id(y); }
endclass
module top;
  Cand h = new;
  initial void'(h.randomize());
endmodule
