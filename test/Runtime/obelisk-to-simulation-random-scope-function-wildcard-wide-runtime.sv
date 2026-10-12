// REQUIRES: compiled-sampler
// RUN: %obelisk --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s

// IEEE 1800-2023 11.4.4, 11.4.6, 12.5.1, 18.5.11 and 18.12:
// Scope case matching retains 130-bit value/X/Z planes, unknown bits in
// separate words, unbased context fill and common signedness. Known
// mismatches dominate unmatched unknown bits in ==?. Active argument stages
// and failed lower stages use the same compiler plans at arbitrary width.
// CHECK: scope wildcard case wide symbols PASS

typedef bit [129:0] B130;

module top;
  logic [129:0] value, label_value;
  B130 normal, zm, xm, symbols;
  logic signed [3:0] small_value;
  B130 context_out;
  bit all_signed;

  function B130 normal_case(logic [129:0] v, logic [129:0] label_v);
    case (v)
      label_v: return 130'h200000000000000000000000000000001;
      '1: return 130'h100000000000000000000000000000002;
      default: return 130'h10000000000000003;
    endcase
  endfunction

  function B130 z_case(logic [129:0] v, logic [129:0] label_v);
    casez (v)
      label_v: return 130'h200000000000000000000000000000001;
      '1: return 130'h100000000000000000000000000000002;
      default: return 130'h10000000000000003;
    endcase
  endfunction

  function B130 x_case(logic [129:0] v, logic [129:0] label_v);
    casex (v)
      label_v: return 130'h200000000000000000000000000000001;
      '1: return 130'h100000000000000000000000000000002;
      default: return 130'h10000000000000003;
    endcase
  endfunction

  function B130 compare_symbols(logic [129:0] v, logic [129:0] label_v);
    return {v === label_v, v !== label_v, v ==? label_v, v !=? label_v};
  endfunction

  function B130 context_case(logic signed [3:0] v, bit signed_context);
    if (signed_context) begin
      casez (v)
        -130'sd1: return 1;
        default: return 2;
      endcase
    end else begin
      casex (v)
        130'hf: return 3;
        130'h3ffffffffffffffffffffffffffffffff: return 4;
        default: return 5;
      endcase
    end
  endfunction

  function automatic void check_values(int n, int z, int x, int s);
    logic [129:0] before_value = value;
    logic [129:0] before_label = label_value;
    B130 result_1 = 130'h200000000000000000000000000000001;
    B130 result_2 = 130'h100000000000000000000000000000002;
    B130 result_3 = 130'h10000000000000003;
    B130 expected_n = n == 1 ? result_1 : n == 2 ? result_2 : result_3;
    B130 expected_z = z == 1 ? result_1 : z == 2 ? result_2 : result_3;
    B130 expected_x = x == 1 ? result_1 : x == 2 ? result_2 : result_3;
    if (!randomize(normal, zm, xm, symbols) with {
          normal == normal_case(value, label_value);
          zm == z_case(value, label_value);
          xm == x_case(value, label_value);
          symbols == compare_symbols(value, label_value);
        })
      $fatal(1, "wide case solve failed");
    if (normal != expected_n || zm != expected_z || xm != expected_x ||
        symbols != B130'(s))
      $fatal(1, "wide case symbol mismatch");
    if (value !== before_value || label_value !== before_label)
      $fatal(1, "wide state symbols changed");
  endfunction

  initial begin
    value = '0;
    label_value = '0;
    value[129] = 1'bx;
    label_value[129] = 1'bz;
    check_values(3, 1, 1, 6);
    value[129] = 1'bz;
    label_value[129] = 1'bx;
    check_values(3, 1, 1, 6);
    value[129] = 1'bx;
    label_value[129] = 1'b1;
    check_values(3, 3, 1, 4);
    value[129] = 1'bx;
    label_value[129] = 1'bx;
    check_values(1, 1, 1, 10);
    value = 'x;
    label_value = 'z;
    check_values(3, 1, 1, 6);
    value = '1;
    label_value = '0;
    check_values(2, 2, 2, 5);
    value = '0;
    label_value = '0;
    value[65] = 1'bx;
    label_value[65] = 1'bz;
    check_values(3, 1, 1, 6);
    value[64] = 1'b1;
    check_values(3, 3, 3, 5);

    small_value = -1;
    all_signed = 0;
    if (!randomize(context_out) with { context_out == context_case(small_value, all_signed); } ||
        context_out != 3)
      $fatal(1, "unsigned wildcard context sign extended");
    all_signed = 1;
    if (!randomize(context_out) with { context_out == context_case(small_value, all_signed); } ||
        context_out != 1)
      $fatal(1, "signed wildcard context did not sign extend");

    value = '0;
    label_value = '0;
    if (!randomize(value, normal, zm, xm, symbols) with {
          value == 130'h200000000000000000000000000000000;
          normal == normal_case(value, label_value);
          zm == z_case(value, label_value);
          xm == x_case(value, label_value);
          symbols == compare_symbols(value, label_value);
        })
      $fatal(1, "wide active function arguments failed");
    if (normal != 130'h10000000000000003 || zm != normal || xm != normal ||
        symbols != 5)
      $fatal(1, "wide active function result mismatch");
    if (randomize(value, normal) with {
          value == 130'h200000000000000000000000000000000;
          normal == normal_case(value, label_value);
          normal == 0;
        })
      $fatal(1, "wide lower stage accepted mismatch");
    if (value !== 130'h200000000000000000000000000000000 ||
        normal != 130'h10000000000000003)
      $fatal(1, "wide failed stage committed");
    $display("scope wildcard case wide symbols PASS");
    $finish;
  end
endmodule
