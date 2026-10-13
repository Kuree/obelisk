// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' \
// RUN:   --test-obelisk-print-bytecode -o /dev/null 2>&1 \
// RUN:   | FileCheck %s

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @readmem {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 function hierarchy "top.read"
    simulation.func @read(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        -> (!simulation.logic<13>, i32, i64)
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      %fd = arith.constant 2147483648 : i32
      simulation.publication.batch_begin %ctx
      %data, %kind, %address =
          simulation.file.readmem_token %ctx, %fd {radix = #simulation.radix<hex>} :
          (!simulation.context, i32) -> (!simulation.logic<13>, i32, i64)
      simulation.publication.batch_end %ctx
      simulation.return %data, %kind, %address :
          !simulation.logic<13>, i32, i64
    }
  }
}

// The interpreter publishes each store itself, so the publication batch that
// brackets a $readmem encodes no instruction.
// CHECK: intrinsic 0: id=0x00010113 inputs=2 outputs=3 flags=0
// CHECK-NOT: intrinsic 1:
