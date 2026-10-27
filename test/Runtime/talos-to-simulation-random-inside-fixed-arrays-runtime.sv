// RUN: %talos --std=1800-2023 -emit-talos %s | talos-opt '--lower-talos-to-sim=opt-level=0' | FileCheck %s --check-prefix=PLAN
// RUN: %talos --std=1800-2023 -O0 -o %t.exe %s
// RUN: %t.exe --seed=13 | FileCheck %s --check-prefix=RUNTIME
// REQUIRES: compiled-sampler

// IEEE 1800-2023 11.4.13 and 18.5.11: fixed unpacked members of an inside
// set contribute their singular leaves, with declaration coordinates, numeric
// comparison context and RHS wildcard symbols. Class and scope plans, nested
// descending and negative-bound arrays, candidate arrays inside the set,
// frozen X/Z, live state and modes, case-inside, checker-only and failed
// calls and 130-bit selectors are exercised.
// PLAN-COUNT-3: simulation.random.solve_wide{{.*}}SAMP
// RUNTIME: inside fixed arrays valid 1

class C;
  rand bit [3:0] v;
  rand bit [3:0] pair [2];                 // candidate array inside the set
  rand bit [7:0] sel;
  rand bit [129:0] wide_v;
  bit [3:0] desc [3:1][1:0] = '{ '{4'd1, 4'd2}, '{4'd3, 4'd4}, '{4'd5, 4'd6} };
  int neg [-2:-1] = '{-5, 7};                 // negative bounds
  logic [3:0] frozen [2] = '{4'b1x00, 4'b0011};
  bit [129:0] wide_set [2] = '{130'h2_0000_0000_0000_0000_0000_0000_0000_0005, 130'h7};
  bit [3:0] state = 4'd9;                    // live state used as a set member
  bit mode = 1;

  function bit in_desc(bit [3:0] a);
    return a inside {desc};
  endfunction
  function bit [3:0] fcase(bit [3:0] a);
    case (a) inside
      desc: return 4'd1;
      frozen: return 4'd2;
      default: return 4'd3;
    endcase
  endfunction
  function bit in_wide(bit [129:0] a);
    return a inside {wide_set};
  endfunction

  constraint members {
    v inside {desc};
    pair[0] inside {desc};
    pair[1] inside {pair[0], state};
    sel inside {neg};                     // int leaves in a numeric comparison context
    wide_v inside {wide_set};
  }
endclass

module top;
  C obj;
  bit [3:0] scope_set [1:0][2:1] = '{ '{4'd10, 4'd11}, '{4'd12, 4'd13} };
  bit [3:0] sx;
  bit [129:0] swx;
  int valid, ok;
  int seen_state;
  bit [3:0] sv, sp0, sp1;
  bit [129:0] sw;
  int calls;
  bit a1, a2;
  initial begin
    obj = new;
    valid = 1;
    seen_state = 0;
    repeat (200) begin
      ok = obj.randomize();
      valid &= ok;
      valid &= obj.v >= 1 && obj.v <= 6;
      valid &= obj.pair[0] >= 1 && obj.pair[0] <= 6;
      valid &= obj.pair[1] == obj.pair[0] || obj.pair[1] == obj.state;
      seen_state |= obj.pair[1] == obj.state;
      // int leaves -5 and 7 are compared as 32-bit values; sel is 8 bits wide
      // so only 7 is representable without truncation (-5 would be 251).
      valid &= obj.sel == 8'd7;
      valid &= obj.wide_v == obj.wide_set[0] || obj.wide_v == obj.wide_set[1];
      valid &= obj.in_desc(obj.v) && obj.fcase(obj.v) == 1 && obj.in_wide(obj.wide_v);
    end
    valid &= seen_state != 0;
    // Disabling a candidate array leaves it frozen at its current value.
    obj.pair.rand_mode(0);
    sp0 = obj.pair[0]; sp1 = obj.pair[1];
    repeat (50) begin
      valid &= obj.randomize();
      valid &= obj.pair[0] == sp0 && obj.pair[1] == sp1;
    end
    obj.pair.rand_mode(1);
    // Live state changes follow the next call.
    obj.state = 4'd13;
    repeat (50) begin
      valid &= obj.randomize();
      valid &= obj.pair[1] == obj.pair[0] || obj.pair[1] == 4'd13;
    end
    // Four-state frozen leaves act as wildcards on the right-hand side.
    valid &= obj.fcase(4'b1100) == 4'd2 && obj.fcase(4'b1000) == 4'd2;
    valid &= obj.fcase(4'b0011) == 4'd1 && obj.fcase(4'd9) == 3;
    // Scope plans expand the same leaves for std::randomize.
    repeat (50) begin
      valid &= std::randomize(sx, swx) with { sx inside {scope_set}; swx inside {obj.wide_set}; };
      valid &= sx >= 10 && sx <= 13 && (swx == obj.wide_set[0] || swx == obj.wide_set[1]);
    end
    // Checker-only and failed calls keep the fields.
    sv = obj.v; sw = obj.wide_v;
    obj.v = 4'd15;
    valid &= obj.randomize(null) == 0 && obj.v == 4'd15;
    obj.v = sv;
    valid &= obj.randomize(null) == 1;
    $display("inside fixed arrays valid %0d", valid);
  end
endmodule
