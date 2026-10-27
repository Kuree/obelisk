// RUN: talos -emit-slang %s 2>/dev/null | FileCheck %s --check-prefix=SLANG
// RUN: talos -emit-talos %s 2>/dev/null | FileCheck %s --check-prefix=TALOS

parameter int CU_IMPLICIT = 1;

package metadata_pkg;
  typedef logic [7:0] inherited_t;

  parameter int IMPLICIT = 2;
  localparam inherited_t TYPEDEF_RANGE = 8'h3c;
  localparam logic [9:4] DIRECT_RANGE = 6'h2a;
  localparam logic [0:0] SINGLE_RANGE = 1'b1;
  localparam inherited_t [3:0] NAMED_DIRECT = 32'h1234_abcd;
endpackage

// Compilation-unit and package parameters are local even when declared with
// the `parameter` keyword. Direct packed dimensions, including [0:0], retain
// separate source provenance; built-in and typedef-provided ranges do not.
// SLANG-DAG: slang.symbol.parameter @{{[^ ]+}} attributes {constant_value = "1", hierarchical_name = "CU_IMPLICIT", is_local_param, name = "CU_IMPLICIT"
// SLANG-DAG: slang.symbol.parameter @{{[^ ]+}} attributes {constant_value = "2", hierarchical_name = "metadata_pkg::IMPLICIT", is_local_param, name = "IMPLICIT"
// SLANG-DAG: slang.symbol.parameter @{{[^ ]+}} attributes {constant_value = "8'd60", hierarchical_name = "metadata_pkg::TYPEDEF_RANGE", is_local_param, name = "TYPEDEF_RANGE"
// SLANG-DAG: slang.symbol.parameter @{{[^ ]+}} attributes {constant_value = "6'b101010", has_explicit_range, hierarchical_name = "metadata_pkg::DIRECT_RANGE", is_local_param, name = "DIRECT_RANGE"
// SLANG-DAG: slang.symbol.parameter @{{[^ ]+}} attributes {constant_value = "1'b1", has_explicit_range, hierarchical_name = "metadata_pkg::SINGLE_RANGE", is_local_param, name = "SINGLE_RANGE"
// SLANG-DAG: slang.symbol.parameter @{{[^ ]+}} attributes {constant_value = "32'd305441741", has_explicit_range, hierarchical_name = "metadata_pkg::NAMED_DIRECT", is_local_param, name = "NAMED_DIRECT"
// TALOS-DAG: talos.sv.symbol.parameter @{{[^ ]+}} attributes {constant_value = "1", hierarchical_name = "CU_IMPLICIT", is_local_param, name = "CU_IMPLICIT"
// TALOS-DAG: talos.sv.symbol.parameter @{{[^ ]+}} attributes {constant_value = "2", hierarchical_name = "metadata_pkg::IMPLICIT", is_local_param, name = "IMPLICIT"
// TALOS-DAG: talos.sv.symbol.parameter @{{[^ ]+}} attributes {constant_value = "8'd60", hierarchical_name = "metadata_pkg::TYPEDEF_RANGE", is_local_param, name = "TYPEDEF_RANGE"
// TALOS-DAG: talos.sv.symbol.parameter @{{[^ ]+}} attributes {constant_value = "6'b101010", has_explicit_range, hierarchical_name = "metadata_pkg::DIRECT_RANGE", is_local_param, name = "DIRECT_RANGE"
// TALOS-DAG: talos.sv.symbol.parameter @{{[^ ]+}} attributes {constant_value = "1'b1", has_explicit_range, hierarchical_name = "metadata_pkg::SINGLE_RANGE", is_local_param, name = "SINGLE_RANGE"
// TALOS-DAG: talos.sv.symbol.parameter @{{[^ ]+}} attributes {constant_value = "32'd305441741", has_explicit_range, hierarchical_name = "metadata_pkg::NAMED_DIRECT", is_local_param, name = "NAMED_DIRECT"
