// RUN: %obelisk --std=1800-2023 -O0 -o %t %s
// RUN: %t +ntb_random_seed=13 | FileCheck %s
// REQUIRES: compiled-sampler

// The case rules of the object test (IEEE 1800-2023 18.5.11 and 12.5) also
// hold for design-scope functions called from the inline constraint of
// std::randomize (18.12). Each result variable is constrained to a function of
// the random argument and must equal the procedural call, for each of the 256
// argument values and for four state selections of the four-state selector.
// CHECK: case mismatches 0 of 260
// CHECK-NEXT: first-match results 10 99

// Scope calls use the same argument-first priority as object calls (18.5.9):
// the result constraint contradicts the argument about half the time, and a
// failed std::randomize commits no variable. The failure count stays in a
// band around half of 256 draws.
// CHECK-NEXT: function priority valid 1
// CHECK-NEXT: function priority failures 1

module top;
  bit [7:0] a;
  bit [7:0] r_order, r_fall, r_mixed, r_signed, r_fill, r_state, r_loop, r_body;
  logic [1:0] st;

  // First matching item wins, labels may be grouped, default is last.
  function automatic bit [7:0] f_order(bit [7:0] v);
    case (v)
      8'd1, 8'd2: return 8'd10;
      8'd2: return 8'd20;
      8'd3: return 8'd30;
      default: return 8'd99;
    endcase
  endfunction

  // An unmatched case without default and an empty item fall through to the
  // statement after the case.
  function automatic bit [7:0] f_fall(bit [7:0] v);
    case (v)
      8'd5: return 8'd50;
      8'd6: ;
    endcase
    return v + 8'd1;
  endfunction

  // One unsigned operand makes the context unsigned: the narrow signed
  // selector zero-extends, so 8'hFF never matches and 8'h0F does.
  function automatic bit [7:0] f_mixed(bit [7:0] v);
    bit signed [3:0] s;
    s = v[3:0];
    case (s)
      8'hFF: return 8'd1;
      8'h0F: return 8'd2;
      default: return 8'd3;
    endcase
  endfunction

  // An all-signed context sign-extends the selector.
  function automatic bit [7:0] f_signed(bit [7:0] v);
    bit signed [3:0] s;
    s = v[3:0];
    case (s)
      -8'sd1: return 8'd1;
      8'sd5: return 8'd2;
      default: return 8'd3;
    endcase
  endfunction

  // Unbased literals fill the whole case context.
  function automatic bit [7:0] f_fill(bit [7:0] v);
    case (v)
      '1: return 8'd1;
      '0: return 8'd2;
      default: return 8'd3;
    endcase
  endfunction

  // Case equality keeps X and Z distinct and does not match a partly
  // unknown selector against a known label. The four-state selector is a
  // state variable captured when randomize() starts.
  function automatic bit [7:0] f_state();
    case (st)
      2'bxx: return 8'd1;
      2'bzz: return 8'd2;
      2'b11: return 8'd3;
      default: return 8'd4;
    endcase
  endfunction

  // break leaves the enclosing loop, return leaves the function, and an
  // unmatched case continues with the next statement of the loop body.
  function automatic bit [7:0] f_loop(bit [7:0] v);
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
  function automatic bit [7:0] f_body(bit [7:0] v);
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

  function automatic bit flip(bit v);
    case (v)
      1'b0: return 1'b1;
      default: return 1'b0;
    endcase
  endfunction

  int bad, failed;
  bit [7:0] dup, none;
  logic [1:0] states[4] = '{2'bxx, 2'bzz, 2'b11, 2'b1x};
  bit valid;
  bit x, y, oldx, oldy;
  initial begin
    for (int k = 0; k < 256; k++) begin
      if (!std::randomize(a, r_order, r_fall, r_mixed, r_signed, r_fill,
                          r_loop, r_body) with {
            a == k;
            r_order == f_order(a);
            r_fall == f_fall(a);
            r_mixed == f_mixed(a);
            r_signed == f_signed(a);
            r_fill == f_fill(a);
            r_loop == f_loop(a);
            r_body == f_body(a);
          }) bad++;
      else if (r_order != f_order(k) || r_fall != f_fall(k) ||
               r_mixed != f_mixed(k) || r_signed != f_signed(k) ||
               r_fill != f_fill(k) || r_loop != f_loop(k) ||
               r_body != f_body(k)) bad++;
      if (k == 2) dup = r_order;
      if (k == 4) none = r_order;
    end
    foreach (states[i]) begin
      st = states[i];
      if (!std::randomize(r_state) with { r_state == f_state(); } ||
          r_state != i + 1) bad++;
    end
    $display("case mismatches %0d of 260", bad);
    $display("first-match results %0d %0d", dup, none);
    valid = 1;
    repeat (256) begin
      oldx = x;
      oldy = y;
      if (std::randomize(y, x) with { x == flip(y); x == 1'b1; }) begin
        valid &= x == 1'b1 && y == 1'b0;
      end else begin
        failed++;
        valid &= x == oldx && y == oldy;
      end
    end
    $display("function priority valid %0d", valid);
    $display("function priority failures %0d", failed >= 96 && failed <= 160);
  end
endmodule
