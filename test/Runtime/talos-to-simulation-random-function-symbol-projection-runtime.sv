// REQUIRES: compiled-sampler
// RUN: %talos --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s

// IEEE 1800-2023 11.4.4, 11.4.6, 12.5.1, 18.5.11 and 18.8:
// The compiler relation combines frozen X/Z symbols with an active function
// argument in either operand. Check every retained argument-domain value,
// exact normal/casez/casex matching, ==? RHS-only wildcards and live modes.
// Lower outputs follow the independent implicit argument-priority stage.
// CHECK: mixed function symbol projection PASS

class C;
  rand logic [3:0] a;
  rand logic [3:0] b;
  rand bit normal, zm, xm;
  rand bit [3:0] symbols;
  bit active_left;

  constraint relation {
    if (active_left) a inside {0, 9, 10, 15};
    else b inside {0, 9, 10, 15};
    normal == normal_case(a, b);
    zm == z_case(a, b);
    xm == x_case(a, b);
    symbols == compare_symbols(a, b);
  }

  function bit normal_case(logic [3:0] v, logic [3:0] label_v);
    case (v)
      label_v: return 1;
      default: return 0;
    endcase
  endfunction

  function bit z_case(logic [3:0] v, logic [3:0] label_v);
    casez (v)
      label_v: return 1;
      default: return 0;
    endcase
  endfunction

  function bit x_case(logic [3:0] v, logic [3:0] label_v);
    casex (v)
      label_v: return 1;
      default: return 0;
    endcase
  endfunction

  function bit [3:0] compare_symbols(logic [3:0] v, logic [3:0] label_v);
    return {v === label_v, v !== label_v, v ==? label_v, v !=? label_v};
  endfunction
endclass

module top;
  C c;
  logic [3:0] wanted;
  bit expected_x, expected_z;
  bit [3:0] expected_symbols;

  function automatic logic [3:0] domain_value(int item);
    case (item)
      0: return 0;
      1: return 9;
      2: return 10;
      default: return 15;
    endcase
  endfunction

  initial begin
    c = new;
    c.a.rand_mode(0);
    c.a = 4'b10xz;
    for (int item = 0; item < 4; item++) begin
      wanted = domain_value(item);
      if (!c.randomize() with { b == wanted; })
        $fatal(1, "mixed frozen-left solve failed");
      expected_x = c.b == 9 || c.b == 10;
      expected_symbols = expected_x ? 4 : 5;
      if (c.normal != 0 || c.zm != 0 || c.xm != expected_x ||
          c.symbols != expected_symbols || c.a !== 4'b10xz || c.b !== wanted)
        $fatal(1, "frozen-left X/Z relation mismatch");
    end

    c.a = 4'b10z1;
    for (int item = 0; item < 4; item++) begin
      wanted = domain_value(item);
      if (!c.randomize() with { b == wanted; })
        $fatal(1, "mixed frozen-Z solve failed");
      expected_z = c.b == 9;
      expected_symbols = expected_z ? 4 : 5;
      if (c.normal != 0 || c.zm != expected_z || c.xm != expected_z ||
          c.symbols != expected_symbols || c.a !== 4'b10z1 || c.b !== wanted)
        $fatal(1, "frozen-Z relation mismatch");
    end

    c.b.rand_mode(0);
    c.a.rand_mode(1);
    c.active_left = 1;
    c.b = 4'b10xz;
    for (int item = 0; item < 4; item++) begin
      wanted = domain_value(item);
      if (!c.randomize() with { a == wanted; })
        $fatal(1, "mixed frozen-right solve failed");
      expected_x = c.a == 9 || c.a == 10;
      expected_symbols = expected_x ? 6 : 5;
      if (c.normal != 0 || c.zm != 0 || c.xm != expected_x ||
          c.symbols != expected_symbols || c.b !== 4'b10xz || c.a !== wanted)
        $fatal(1, "frozen-right wildcard relation mismatch");
    end

    if (!c.randomize(null))
      $fatal(1, "mixed symbol checker failed");
    c.symbols = 0;
    if (c.randomize(null))
      $fatal(1, "mixed symbol checker accepted mismatch");
    $display("mixed function symbol projection PASS");
    $finish;
  end
endmodule
