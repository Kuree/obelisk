// REQUIRES: compiled-sampler
// RUN: %talos --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s

// IEEE 1800-2023 11.4.4, 11.4.6, 12.5, 12.5.1 and 18.5.11:
// normal case, casez and casex consume exact value, X and Z symbols.
// Wildcards apply to either case operand, while ==? ignores only RHS symbols.
// Frozen arguments, consuming return casts, known mismatches, first-match
// label error guards, active argument stages, modes and failure rollback agree
// with the LRM case semantics.
// CHECK: function wildcard case symbols PASS

typedef bit [3:0] B4;

class C;
  rand logic [3:0] a, b;
  rand B4 normal, zm, xm, symbols;
  bit active_inputs = 0;

  constraint relation {
    if (active_inputs) {
      a == 4'b1001;
      b == 4'b1010;
    }
    normal == normal_case(a, b);
    zm == z_case(a, b);
    xm == x_case(a, b);
    symbols == compare_symbols(a, b);
  }

  function B4 normal_case(logic [3:0] value, logic [3:0] label_value);
    case (value)
      label_value: return 1;
      4'b1001: return 2;
      4'b1010: return 3;
      default: return 4;
    endcase
  endfunction

  function B4 z_case(logic [3:0] value, logic [3:0] label_value);
    casez (value)
      label_value: return 1;
      4'b1001: return 2;
      4'b1010: return 3;
      default: return 4;
    endcase
  endfunction

  function B4 x_case(logic [3:0] value, logic [3:0] label_value);
    casex (value)
      label_value: return 1;
      4'b1001: return 2;
      4'b1010: return 3;
      default: return 4;
    endcase
  endfunction

  function B4 compare_symbols(logic [3:0] value, logic [3:0] label_value);
    return {value === label_value, value !== label_value,
            value ==? label_value, value !=? label_value};
  endfunction

  function void check_values(B4 n, B4 z, B4 x, B4 s);
    logic [3:0] before_a = a;
    logic [3:0] before_b = b;
    if (!this.randomize())
      $fatal(1, "symbol comparison solve failed");
    if (normal != n || zm != z || xm != x || symbols != s)
      $fatal(1, "symbol comparison mismatch: %h %h %h %h", normal, zm, xm,
             symbols);
    if (!active_inputs && (a !== before_a || b !== before_b))
      $fatal(1, "frozen symbols changed");
  endfunction
endclass

class G;
  logic [3:0] a;
  rand B4 out;
  logic [3:0] labels[0:0];
  int bad = 9;
  bit use_x;

  constraint relation {
    if (use_x) out == x_case(a);
    else out == z_case(a);
  }

  function B4 z_case(logic [3:0] value);
    casez (value)
      4'b1zzz: return 1;
      labels[bad]: return 2;
      default: return 3;
    endcase
  endfunction

  function B4 x_case(logic [3:0] value);
    casex (value)
      4'b1zzz: return 1;
      labels[bad]: return 2;
      default: return 3;
    endcase
  endfunction
endclass

module top;
  C c;
  G g;
  B4 before_normal, before_zm, before_xm, before_symbols;

  initial begin
    c = new;
    g = new;
    c.a.rand_mode(0);
    c.b.rand_mode(0);
    c.a = 4'b100x;  c.b = 4'b100z;  c.check_values(4, 1, 1, 6);
    c.a = 4'b100z;  c.b = 4'b100x;  c.check_values(4, 1, 1, 6);
    c.a = 4'b100x;  c.b = 4'b1001;  c.check_values(4, 4, 1, 4);
    c.a = 4'b1001;  c.b = 4'b100x;  c.check_values(2, 2, 1, 6);
    c.a = 4'b10xz;  c.b = 4'b1001;  c.check_values(4, 4, 1, 4);
    c.a = 4'b100x;  c.b = 4'b100x;  c.check_values(1, 1, 1, 10);
    c.a = 4'b100z;  c.b = 4'b100z;  c.check_values(1, 1, 1, 10);
    c.a = 4'b11x;   c.b = 4'b1010;  c.check_values(4, 4, 4, 5);
    c.a = 4'b1001;  c.b = 4'b1010;  c.check_values(2, 2, 2, 5);
    c.a = 4'b1010;  c.b = 4'b1001;  c.check_values(3, 3, 3, 5);
    c.a = 4'bzzzz;  c.b = 4'b101;   c.check_values(4, 1, 1, 4);
    c.a = 4'bxxxx;  c.b = 4'b0;     c.check_values(4, 4, 1, 4);

    c.active_inputs = 1;
    c.a.rand_mode(1);
    c.b.rand_mode(1);
    repeat (4) c.check_values(2, 2, 2, 5);
    if (c.a !== 4'b1001 || c.b !== 4'b1010)
      $fatal(1, "argument stage mismatch");
    before_normal = c.normal;
    before_zm = c.zm;
    before_xm = c.xm;
    before_symbols = c.symbols;
    if (c.randomize() with { normal == 3; })
      $fatal(1, "lower function stage accepted");
    if (c.a !== 4'b1001 || c.b !== 4'b1010 || c.normal != before_normal ||
        c.zm != before_zm || c.xm != before_xm || c.symbols != before_symbols)
      $fatal(1, "failed function stage committed");
    if (!c.randomize(null))
      $fatal(1, "symbol comparison checker failed");
    c.symbols = 0;
    if (c.randomize(null))
      $fatal(1, "symbol comparison checker accepted mismatch");

    g.a = 4'bz000;
    g.use_x = 0;
    if (!g.randomize() || g.out != 1 || g.a !== 4'bz000)
      $fatal(1, "casez did not discard later label error");
    g.a = 4'bx000;
    g.use_x = 1;
    if (!g.randomize() || g.out != 1 || g.a !== 4'bx000)
      $fatal(1, "casex did not discard later label error");
    g.use_x = 0;
    if (g.randomize() || g.out != 1 || g.a !== 4'bx000)
      $fatal(1, "casez hid a label error with X");
    g.relation.constraint_mode(0);
    if (!g.randomize() || g.a !== 4'bx000)
      $fatal(1, "disabled wildcard block failed");
    g.relation.constraint_mode(1);
    g.a = 4'b100x;
    g.use_x = 0;
    if (!g.randomize() || g.out != 1 || g.a !== 4'b100x)
      $fatal(1, "casez label wildcards lost X");
    $display("function wildcard case symbols PASS");
    $finish;
  end
endmodule
