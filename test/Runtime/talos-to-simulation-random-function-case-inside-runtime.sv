// RUN: %talos --std=1800-2023 -emit-talos %s | talos-opt '--lower-talos-to-sim=opt-level=0' | FileCheck %s --check-prefix=PLAN
// RUN: %talos --std=1800-2023 -O0 -o %t.exe %s
// RUN: %t.exe --seed=13 | FileCheck %s --check-prefix=RUNTIME
// REQUIRES: compiled-sampler

// IEEE 1800-2023 12.5.4, 11.4.13 and 18.5.11: constraint functions with
// case-inside use asymmetric wildcard membership, inclusive ranges with live
// state bounds and first-match control flow. Unknown comparisons skip their
// item, selected X/Z returns survive nested matching, and the common context
// covers signed narrow operands and 130-bit patterns. Failed and checker-only
// calls preserve fields; dollar endpoints work in ordinary membership. Every
// randomize call keeps a compiled sampling plan.
// PLAN-COUNT-2: simulation.random.solve{{.*}}SAMP
// RUNTIME: case inside valid 1

class C;
  rand bit [3:0] a;
  rand bit [3:0] out;
  rand bit [1:0] nested;
  rand bit [3:0] wide_out;
  rand bit [129:0] w;
  rand bit signed [3:0] sa;
  rand bit [3:0] sout;
  bit [3:0] wanted_a = 4'd9;
  bit [3:0] lo = 4'd2, hi = 4'd5;
  bit signed [3:0] sneg = -4'sd1;

  // First match wins; the range uses live state captures as bounds and the
  // wildcard pattern on the right-hand side matches any bit value.
  function bit [3:0] pick(bit [3:0] v);
    case (v) inside
      4'b10??: return 4'd1;
      [lo:hi]: return 4'd2;
      4'b01??: return 4'd3;
      default: return 4'd4;
    endcase
  endfunction

  // Unknown comparison results never select an item; the selected X/Z
  // return value is matched by an enclosing case-inside.
  function logic [3:0] unk(logic [3:0] v);
    case (v) inside
      4'b1zzz: return 4'b10xz;
      4'b0z1?: return 4'bxxxx;
      default: return 4'b0001;
    endcase
  endfunction
  function bit [1:0] nest(bit [3:0] v);
    case (unk(v)) inside
      4'b10zx: return 2'd1;
      4'b1001: return 2'd2;
      default: return 2'd3;
    endcase
  endfunction

  // Signed narrow selector: the common context includes every label.
  function bit [3:0] sgn(bit signed [3:0] v);
    case (v) inside
      4'sd3: return 4'd7;
      [-4'sd2:-4'sd1]: return 4'd8;
      default: return 4'd9;
    endcase
  endfunction

  function bit [3:0] wide(bit [129:0] v);
    case (v) inside
      130'h3ffffffffffffffffffffffffffffffff: return 4'd5;
      130'b1zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz: return 4'd6;
      [130'h3:130'h7]: return 4'd7;
      default: return 4'd8;
    endcase
  endfunction

  constraint relation {
    a == wanted_a;
    out == pick(a);
    nested == nest(a);
    wide_out == wide(w);
    sa inside {4'sd3, -4'sd2, -4'sd1, 4'sd1};
    sout == sgn(sa);
    w inside {130'h3ffffffffffffffffffffffffffffffff, 130'h5, 130'h1, 130'h2_0000_0000_0000_0000_0000_0000_0000_0000};
  }
endclass

module top;
  C obj;
  int valid;
  bit [3:0] av, sv_out;
  logic [3:0] lv;
  function bit open(int v);
    return v inside {[10:$]};
  endfunction
  function bit open_low(int v);
    return v inside {[$:3]};
  endfunction
  initial begin
    obj = new;
    valid = 1;
    // Sweep the selector across every 4-bit value; the compiled plan must
    // agree with native execution of the same function.
    for (int i = 0; i < 16; i++) begin
      obj.wanted_a = i[3:0];
      valid &= obj.randomize();
      valid &= obj.a == i[3:0];
      valid &= obj.out == obj.pick(obj.a);
      valid &= obj.nested == obj.nest(obj.a);
      valid &= obj.wide_out == obj.wide(obj.w);
      valid &= obj.sout == obj.sgn(obj.sa) && obj.sout != 4'd0;
    end
    // Pinned results of the LRM first-match rule.
    obj.wanted_a = 4'b1001; void'(obj.randomize()); valid &= obj.out == 1;
    obj.wanted_a = 4'd3;    void'(obj.randomize()); valid &= obj.out == 2;
    obj.wanted_a = 4'd6;    void'(obj.randomize()); valid &= obj.out == 3;
    obj.wanted_a = 4'd0;    void'(obj.randomize()); valid &= obj.out == 4;
    obj.lo = 4'd6; obj.hi = 4'd7; obj.wanted_a = 4'd5;
    void'(obj.randomize()); valid &= obj.out == 3;
    obj.wanted_a = 4'd7; void'(obj.randomize()); valid &= obj.out == 2;
    // Failed and checker-only calls preserve the random fields.
    obj.wanted_a = 4'd3; void'(obj.randomize());
    av = obj.a;
    obj.out = ~obj.pick(obj.a);
    sv_out = obj.out;
    valid &= obj.randomize(null) == 0;
    valid &= obj.a == av && obj.out == sv_out;
    obj.wanted_a = 4'd15; void'(obj.randomize());
    valid &= obj.randomize(null) == 1;
    // Dollar endpoints are open bounds in ordinary membership.
    valid &= open(10) && open(1000) && !open(9) && open_low(3) && open_low(-5) && !open_low(4);
    valid &= obj.sgn(4'sd3) == 7 && obj.sgn(-4'sd2) == 8 && obj.sgn(4'sd1) == 9;
    $display("case inside valid %0d", valid);
  end
endmodule
