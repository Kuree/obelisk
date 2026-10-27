// RUN: talos -emit-slang %s | FileCheck %s --check-prefix=SLANG
// RUN: talos -emit-talos %s | FileCheck %s --check-prefix=TALOS

module covergroup_metadata;
  covergroup cg with function sample(input logic [2:0] value);
    cp: coverpoint value iff (value != 0) {
      bins explicit_values = {1, 2};
      bins explicit_range = {[3:4]};
      bins fallback = default;
    }
  endgroup
  cg c;
  initial begin
    c = new;
    $display("%f", c.get_inst_coverage());
  end
endmodule

// SLANG: slang.type.covergroup_type
// SLANG-SAME: constructor_argument_count = 0
// SLANG-SAME: has_coverage_event = false
// SLANG-SAME: sample_formal_count = 1
// SLANG: slang.symbol.covergroup_body
// SLANG-SAME: option_count = 0
// SLANG: slang.symbol.coverpoint
// SLANG-SAME: has_iff = true
// SLANG-SAME: option_count = 0
// SLANG: slang.symbol.coverage_bin
// SLANG-SAME: is_default = false
// SLANG-SAME: value_count = 2
// SLANG: slang.expression.call attributes {{.*}}callee_name = "get_inst_coverage"
// SLANG-SAME: defaulted_arguments = array<i64: 1, 1>

// TALOS: talos.sv.type.covergroup_type
// TALOS-SAME: constructor_argument_count = 0
// TALOS-SAME: has_coverage_event = false
// TALOS-SAME: sample_formal_count = 1
// TALOS: talos.sv.symbol.coverpoint
// TALOS-SAME: has_iff = true
// TALOS: talos.sv.symbol.coverage_bin
// TALOS-SAME: value_count = 2
// TALOS: talos.sv.expression.new_covergroup
// TALOS-SAME: argument_count = 0
// TALOS: talos.sv.expression.call attributes {{.*}}callee_name = "get_inst_coverage"
// TALOS-SAME: defaulted_arguments = array<i64: 1, 1>
