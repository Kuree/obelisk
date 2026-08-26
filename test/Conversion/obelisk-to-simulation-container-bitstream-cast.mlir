// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(lower-obelisk-to-sim{opt-level=0},obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | mlir-translate --mlir-to-llvmir \
// RUN:   | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a \
// RUN:   %native_support/libc++.a %native_support/libc++abi.a \
// RUN:   %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %llvm_dist/bin/llvm-nm -C --defined-only %t.exe \
// RUN:   | FileCheck %s --check-prefix=BYTECODE-LINK
// RUN: not %t.exe --execution-tier=native 2>&1 | FileCheck %s --check-prefix=FATAL
// RUN: not %t.exe --execution-tier=bytecode 2>&1 | FileCheck %s --check-prefix=FATAL
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | mlir-translate --mlir-to-llvmir \
// RUN:   | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.native.o
// RUN: %llvm_dist/bin/clang++ %t.native.o %native_support/libobelisk_rt.a \
// RUN:   %native_support/libc++.a %native_support/libc++abi.a \
// RUN:   %native_support/libunwind.a -nostdlib++ -lpthread -ldl \
// RUN:   -o %t.native.exe
// RUN: %llvm_dist/bin/llvm-nm -C --defined-only %t.native.exe \
// RUN:   | FileCheck %s --check-prefix=NATIVE-LINK \
// RUN:     --implicit-check-not=container_bitstream_link_anchor \
// RUN:     --implicit-check-not=invokeContainerBitstreamIntrinsic

// BYTECODE-LINK-DAG: obelisk_rt_v1_container_export_bitstream
// BYTECODE-LINK-DAG: obelisk_rt_v1_container_bitstream_link_anchor
// BYTECODE-LINK-DAG: invokeContainerBitstreamIntrinsic
// NATIVE-LINK: obelisk_rt_v1_container_export_bitstream

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk.sv.symbol.definition attributes {
    definition_kind = 0 : i32, hierarchical_name = "top", name = "top",
    node_id = 0 : i64, sym_name = "s0.top"
  } {}
  obelisk.sv.symbol.root attributes {
    hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64,
    sym_name = "s1.$root"
  } {
    obelisk.sv.symbol.compilation_unit attributes {
      hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"
    } {}
    obelisk.sv.symbol.instance attributes {
      hierarchical_name = "top", is_uninstantiated = false, name = "top",
      node_id = 3 : i64, referenced_path = "top",
      referenced_symbol = @s0.top, sym_name = "s3.top"
    } {
      obelisk.sv.symbol.instance_body attributes {
        hierarchical_name = "top", name = "top", node_id = 4 : i64,
        sym_name = "s4.top"
      } {
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "top.source", lifetime = 1 : i32,
          name = "source", node_id = 5 : i64,
          semantic_type = !obelisk.dynarray<!obelisk.integral<8, false, true, 7 : 0, logic>>,
          sym_name = "s5.source"
        } {}
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "top.result", lifetime = 1 : i32,
          name = "result", node_id = 6 : i64,
          semantic_type = !obelisk.integral<24, false, true, 23 : 0, logic>,
          sym_name = "s6.result"
        } {
          obelisk.sv.expression.conversion attributes {
            is_implicit = false, is_signed = false, node_id = 7 : i64,
            semantic_type = !obelisk.integral<24, false, true, 23 : 0, logic>
          } {
            obelisk.sv.expression.named_value attributes {
              is_signed = false, node_id = 8 : i64,
              referenced_path = "top.source",
              referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.source,
              semantic_type = !obelisk.dynarray<!obelisk.integral<8, false, true, 7 : 0, logic>>
            } {}
          }
        }
      }
    }
  }
}

// CHECK-LABEL: obelisk_sim.func private @unit_0
// CHECK: %[[THREE:.*]] = arith.constant 3 : i64
// CHECK: %[[SOURCE:.*]] = obelisk_sim.ref.load
// CHECK: %[[SIZE:.*]] = obelisk_sim.container.size %[[SOURCE]]
// CHECK: %[[MATCHES:.*]] = arith.cmpi eq, %[[SIZE]], %[[THREE]] : i64
// CHECK: cf.cond_br %[[MATCHES]], ^[[ACCEPTED:.*]], ^[[REJECTED:.*]]
// CHECK: ^[[ACCEPTED]]:
// CHECK: obelisk_sim.container.export_bitstream %[[SOURCE]]
// CHECK-NOT: obelisk_sim.logic.dyn_insert
// CHECK: ^[[REJECTED]]:
// CHECK: bit-stream cast source and destination widths differ
// FATAL: bit-stream cast source and destination widths differ
