// RUN: %obelisk --std=1800-2023 -O0 -o %t %s
// RUN: %t +ntb_random_seed=13 | FileCheck %s
// REQUIRES: compiled-sampler

// IEEE 1800-2023 18.5.11 admits ordinary case statements in constraint
// functions, evaluated under the 12.5 rules. Every result below is constrained
// to a function of the rand argument; the constrained value must equal the
// same function called procedurally, for each of the 256 argument values and
// for four state selections of the four-state selector.
// CHECK: case mismatches 0 of 260
// CHECK-NEXT: first-match results 10 99

// A constraint-function call makes its rand arguments solve first (18.5.9).
// The result constraint then contradicts the argument about half the time.
// Failed calls roll back the object, and the failure count stays in a band
// around half of 256 draws.
// CHECK-NEXT: function priority valid 1
// CHECK-NEXT: function priority failures 1

class C;
  rand bit [7:0] a;
  rand bit [7:0] r_order, r_fall, r_mixed, r_signed, r_fill, r_loop,
                 r_body;

  // First matching item wins, labels may be grouped, default is last.
  function bit [7:0] f_order(bit [7:0] v);
    case (v)
      8'd1, 8'd2: return 8'd10;
      8'd2: return 8'd20;
      8'd3: return 8'd30;
      default: return 8'd99;
    endcase
  endfunction

  // An unmatched case without default and an empty item fall through to the
  // statement after the case.
  function bit [7:0] f_fall(bit [7:0] v);
    case (v)
      8'd5: return 8'd50;
      8'd6: ;
    endcase
    return v + 8'd1;
  endfunction

  // One unsigned operand makes the context unsigned: the narrow signed
  // selector zero-extends, so 8'hFF never matches and 8'h0F does.
  function bit [7:0] f_mixed(bit [7:0] v);
    bit signed [3:0] s;
    s = v[3:0];
    case (s)
      8'hFF: return 8'd1;
      8'h0F: return 8'd2;
      default: return 8'd3;
    endcase
  endfunction

  // An all-signed context sign-extends the selector.
  function bit [7:0] f_signed(bit [7:0] v);
    bit signed [3:0] s;
    s = v[3:0];
    case (s)
      -8'sd1: return 8'd1;
      8'sd5: return 8'd2;
      default: return 8'd3;
    endcase
  endfunction

  // Unbased literals fill the whole case context.
  function bit [7:0] f_fill(bit [7:0] v);
    case (v)
      '1: return 8'd1;
      '0: return 8'd2;
      default: return 8'd3;
    endcase
  endfunction

  // Case equality keeps X and Z distinct and does not match a partly
  // unknown selector against a known label. The four-state selector is a
  // state variable captured when randomize() starts.
  logic [1:0] st;
  rand bit [7:0] r_state;
  function bit [7:0] f_state();
    case (st)
      2'bxx: return 8'd1;
      2'bzz: return 8'd2;
      2'b11: return 8'd3;
      default: return 8'd4;
    endcase
  endfunction

  // break leaves the enclosing loop, return leaves the function, and an
  // unmatched case continues with the next statement of the loop body.
  function bit [7:0] f_loop(bit [7:0] v);
    repeat (3) begin
      case (v[1:0])
        2'd0, 2'd1: break;
        2'd2: return 8'd7;
      endcase
      return 8'd8;
    end
    return 8'd9;
  endfunction

  // A taken item's assignment is visible to a later case, whose labels are
  // arbitrary expressions evaluated in source order.
  function bit [7:0] f_body(bit [7:0] v);
    bit [7:0] t;
    t = v;
    case (v[1:0])
      2'd1: t = t + 8'd100;
      2'd2: t = t + 8'd50;
    endcase
    case (t)
      v + 8'd100, v + 8'd50: return t - v;
      default: return 8'd0;
    endcase
  endfunction

  constraint c_order  { r_order  == f_order(a); }
  constraint c_fall   { r_fall   == f_fall(a); }
  constraint c_mixed  { r_mixed  == f_mixed(a); }
  constraint c_signed { r_signed == f_signed(a); }
  constraint c_fill   { r_fill   == f_fill(a); }
  constraint c_state  { r_state  == f_state(); }
  constraint c_loop   { r_loop   == f_loop(a); }
  constraint c_body   { r_body   == f_body(a); }
endclass

class P;
  rand bit y;
  rand bit x;
  function bit flip(bit v);
    case (v)
      1'b0: return 1'b1;
      default: return 1'b0;
    endcase
  endfunction
  constraint relation { x == flip(y); }
  constraint limit { x == 1'b1; }
endclass

module top;
  C o = new;
  P p = new;
  int bad, ok, failed;
  bit [7:0] dup, none;
  logic [1:0] states[4] = '{2'bxx, 2'bzz, 2'b11, 2'b1x};
  bit valid;
  bit oldx, oldy;
  initial begin
    for (int k = 0; k < 256; k++) begin
      if (!o.randomize() with { a == k; }) bad++;
      else if (o.r_order != o.f_order(k) || o.r_fall != o.f_fall(k) ||
               o.r_mixed != o.f_mixed(k) || o.r_signed != o.f_signed(k) ||
               o.r_fill != o.f_fill(k) || o.r_loop != o.f_loop(k) ||
               o.r_body != o.f_body(k)) bad++;
      if (k == 2) dup = o.r_order;
      if (k == 4) none = o.r_order;
    end
    foreach (states[i]) begin
      o.st = states[i];
      if (!o.randomize() with { a == 0; } || o.r_state != i + 1) bad++;
    end
    $display("case mismatches %0d of 260", bad);
    $display("first-match results %0d %0d", dup, none);
    valid = 1;
    repeat (256) begin
      oldx = p.x;
      oldy = p.y;
      if (p.randomize()) begin
        ok++;
        valid &= p.x == 1'b1 && p.y == 1'b0;
      end else begin
        failed++;
        valid &= p.x == oldx && p.y == oldy;
      end
    end
    $display("function priority valid %0d", valid);
    $display("function priority failures %0d", failed >= 96 && failed <= 160);
  end
endmodule
