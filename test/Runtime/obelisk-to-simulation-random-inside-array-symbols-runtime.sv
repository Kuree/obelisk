// RUN: %obelisk --std=1800-2023 -emit-obelisk %s | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=PLAN
// RUN: %obelisk --std=1800-2023 -O0 -o %t.exe %s
// RUN: %t.exe --seed=13 | FileCheck %s --check-prefix=RUNTIME
// REQUIRES: compiled-sampler

// IEEE 1800-2023 11.4.13, 12.5.4 and 18.5.11: native function execution and
// expanded constraint plans agree on nested array membership. RHS wildcard
// bits may consume frozen X/Z; unmatched left unknowns yield X. A known match
// absorbs X, a two-state return coerces X, and case-inside takes only known
// true comparisons before considering its next label.
// PLAN-COUNT-4: simulation.random.solve{{.*}}SAMP
// RUNTIME: inside array symbols valid 1

class C;
  rand bit [3:0] v;
  rand bit hit;
  rand bit [1:0] pick;
  logic [3:0] left = 4'b1x00;
  logic [3:0] frozen [2][2] = '{ '{4'b10zz, 4'b0101}, '{4'bx001, 4'b1111} };
  bit [3:0] known [2][2] = '{ '{4'd1, 4'd2}, '{4'd3, 4'd4} };

  // Four-state membership: a known match absorbs unknown comparisons, a
  // comparison against unknown left bits yields X when nothing matches.
  function logic member4(logic [3:0] a);
    return a inside {frozen};
  endfunction
  // A two-state return coerces X to 0.
  function bit member2(logic [3:0] a);
    return a inside {frozen};
  endfunction
  // Case-inside takes only known-true comparisons before the next label.
  function bit [1:0] label(logic [3:0] a);
    case (a) inside
      frozen: return 2'd2;
      default: return 2'd3;
    endcase
  endfunction

  constraint plan {
    v inside {frozen};
    hit == member2(v);
    pick == label(v);
  }
endclass

class D;
  rand bit [3:0] v;
  bit [3:0] known [2][2] = '{ '{4'd1, 4'd2}, '{4'd3, 4'd4} };
  constraint plan { v inside {known}; }
endclass

module top;
  C obj;
  D dobj;
  int valid;
  logic r;
  initial begin
    obj = new;
    dobj = new;
    valid = 1;
    repeat (200) begin
      valid &= obj.randomize();
      // RHS wildcards: 10zz, 0101, x001 and 1111 are the admitted values.
      valid &= obj.v[3:2] == 2'b10 || obj.v == 4'b0101 || obj.v[2:0] == 3'b001 || obj.v == 4'b1111;
      valid &= obj.hit == 1 && obj.pick == obj.label(obj.v);
      valid &= dobj.randomize() && dobj.v >= 1 && dobj.v <= 4;
    end
    // Pinned four-state results.
    valid &= obj.member4(4'b1000) === 1'b1;          // matches 10zz
    valid &= obj.member4(4'b0101) === 1'b1;
    valid &= obj.member4(4'b0010) === 1'b0;          // no unknown comparison
    valid &= obj.member4(4'b0x01) === 1'bx;          // x vs known 0101 / x001: unknown
    valid &= obj.member4(4'b10x1) === 1'b1;          // match absorbs unknown
    valid &= obj.member2(4'b0x01) == 1'b0;           // two-state return coerces X
        valid &= obj.label(4'b1011) == 2'd2 && obj.label(4'b1111) == 2'd2;
    valid &= obj.label(4'b101x) == 2'd2 && obj.label(4'b0x01) == 2'd3;
    valid &= obj.label(4'd7) == 2'd3 && obj.label(4'bxxxx) == 2'd3;
    // Unmatched left operand with an unknown bit yields X (10zz is undecided).
    r = obj.left inside {obj.frozen};
    valid &= r === 1'bx;
    $display("inside array symbols valid %0d", valid);
  end
endmodule
