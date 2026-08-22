// RUN: obelisk -fno-lto -O0 --vpi=off --native-scheduler=generic %s -o %t
// RUN: %t | FileCheck %s --check-prefix=OUTPUT
// RUN: obelisk -O0 --vpi=off --native-scheduler=generic -emit-llvm %s | FileCheck %s --check-prefix=LLVM

module native_net_declaration_delay;
  logic source;
  wire #5 delayed;
  assign delayed = source;

  initial begin
    source = 1'b0;
    #1 source = 1'b1;
    #3 assert (delayed === 1'bz);
    #2 assert (delayed === 1'b1);
    $display("native net delay passed");
  end
endmodule

// OUTPUT: native net delay passed

// A pure native/generic build still embeds the canonical design image used by
// delayed-net resolution and binds the native planes to that image.
// LLVM: @__obelisk_bytecode_image_v1
// LLVM: call i32 @obelisk_rt_v1_native_state_sync
