// RUN: not talos --std=1800-2023 -emit-talos %s -o /dev/null 2>&1 | FileCheck %s --implicit-check-not=error:

// IEEE 19.7 and 19.7.1 limit these options to coverage definitions. Unary
// and compound updates use the same checks as simple assignment.
module top;
  bit value;
  covergroup cg;
    cp: coverpoint value;
  endgroup
  cg cov;
  initial begin
    cov = new;
    // CHECK: [[@LINE+1]]:{{[0-9]+}}: error: the 'auto_bin_max' option cannot be modified outside of the covergroup definition
    cov.option.auto_bin_max++;
    // CHECK: [[@LINE+1]]:{{[0-9]+}}: error: the 'detect_overlap' option cannot be modified outside of the covergroup definition
    cov.option.detect_overlap += 1;
    // CHECK: [[@LINE+1]]:{{[0-9]+}}: error: the 'cross_retain_auto_bins' option cannot be modified outside of the covergroup definition
    cov.option.cross_retain_auto_bins = 1;
    // CHECK: [[@LINE+1]]:{{[0-9]+}}: error: the 'per_instance' option cannot be modified outside of the covergroup definition
    cov.option.per_instance++;
    // CHECK: [[@LINE+1]]:{{[0-9]+}}: error: the 'get_inst_coverage' option cannot be modified outside of the covergroup definition
    cov.option.get_inst_coverage--;
    // CHECK: [[@LINE+1]]:{{[0-9]+}}: error: the 'strobe' option cannot be modified outside of the covergroup definition
    cg::type_option.strobe += 1;
    // CHECK: [[@LINE+1]]:{{[0-9]+}}: error: the 'real_interval' option cannot be modified outside of the covergroup definition
    cg::type_option.real_interval++;
  end
endmodule
