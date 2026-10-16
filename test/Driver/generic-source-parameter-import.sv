// RUN: obelisk --std=1800-2023 -emit-slang %s -o /dev/null
// RUN: obelisk --std=1800-2023 -emit-obelisk %s -o /dev/null

// Importing source reflection must not force default binding in an unused
// generic class or diagnose an overridden dependent type default.
class unused_generic #(type T, type U = T);
endclass

class overridden_default #(type T = int, type U = T[3:0]);
endclass

class unused_value_generic #(int P, int Q = P + 2);
endclass

class overridden_value_default #(int P = 1, int Q = $clog2(P), int R = '1);
endclass

module generic_source_parameter_import;
  overridden_default #(real, int) value;
  overridden_value_default #(9, 4, 3) value_defaults;
endmodule
