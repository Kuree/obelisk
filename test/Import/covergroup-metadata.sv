// RUN: obelisk -emit-slang %s | FileCheck %s --check-prefix=SLANG
// RUN: obelisk -emit-obelisk %s | FileCheck %s --check-prefix=OBELISK

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

// OBELISK: obelisk.sv.type.covergroup_type
// OBELISK-SAME: constructor_argument_count = 0
// OBELISK-SAME: has_coverage_event = false
// OBELISK-SAME: sample_formal_count = 1
// OBELISK: obelisk.sv.symbol.coverpoint
// OBELISK-SAME: has_iff = true
// OBELISK: obelisk.sv.symbol.coverage_bin
// OBELISK-SAME: value_count = 2
// OBELISK: obelisk.sv.expression.new_covergroup
// OBELISK-SAME: argument_count = 0
// OBELISK: obelisk.sv.expression.call attributes {{.*}}callee_name = "get_inst_coverage"
// OBELISK-SAME: defaulted_arguments = array<i64: 1, 1>
