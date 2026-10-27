// RUN: talos -emit-talos %s | talos-opt '--test-talos-set-module-attributes=attrs="{llvm.data_layout = \"e-p:64:64-i64:64-i128:128\"}"' --talos-sim-prepare | FileCheck %s --check-prefix=ALIGN16
// RUN: talos -emit-talos %s | talos-opt '--test-talos-set-module-attributes=attrs="{llvm.data_layout = \"e-p:32:32-i64:64-i128:64\"}"' --talos-sim-prepare | FileCheck %s --check-prefix=ALIGN8

// Deliberately different target ABIs must produce different record offsets
// and array strides even when both compilations run on the same host.
module unpacked_target_layout;
  typedef struct {
    bit flag;
    bit [127:0] payload;
    bit tail;
  } record_t;
  record_t records[2];
  initial begin
    records[1].payload = 128'h123;
    $display("%h", records[1].payload);
    $finish;
  end
endmodule
// ALIGN16: #simulation.storage_layout<size = 48, alignment = 16, stride = 0, offsets = [0, 16, 32]>
// ALIGN16-SAME: #simulation.storage_layout<size = 96, alignment = 16, stride = 48, offsets = []>
// ALIGN8: #simulation.storage_layout<size = 32, alignment = 8, stride = 0, offsets = [0, 8, 24]>
// ALIGN8-SAME: #simulation.storage_layout<size = 64, alignment = 8, stride = 32, offsets = []>
