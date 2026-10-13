// RUN: obelisk -emit-slang %s -o %t.slang.mlir
// RUN: obelisk-opt %t.slang.mlir --test-obelisk-native-backend='scheduler=generic stop-after=lowering' | FileCheck %s --check-prefix=SIM
// RUN: obelisk-opt %t.slang.mlir --test-obelisk-native-backend='scheduler=generic' -o %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=LLVM < %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=MULS < %t.llvm.mlir

// IEEE 1800-2023 11.4.4: an exponent keeps its self-determined width in both
// directions.
// SIM: simulation.logic.power %{{.*}}, %{{.*}} : (!simulation.logic<4096>, !simulation.logic<4096>) -> !simulation.logic<4096>
// SIM: simulation.logic.power %{{.*}}, %{{.*}} : (!simulation.logic<128>, !simulation.logic<4096>) -> !simulation.logic<128>
// SIM: simulation.logic.power %{{.*}}, %{{.*}} : (!simulation.logic<4096>, !simulation.logic<32>) -> !simulation.logic<4096>
//
// Native code squares and multiplies in a loop that shifts the exponent, so its
// size does not grow with the exponent width; an unknown operand selects an
// all-X result instead.
// LLVM: %[[ALL:.*]] = llvm.mlir.constant(-1 : i4096) : i4096
// LLVM: %[[UNKNOWN:.*]] = llvm.or %{{.*}}, %{{.*}} : i1
// LLVM-NEXT: llvm.select %[[UNKNOWN]], %{{.*}}, %{{.*}} : i1, i4096
// LLVM-NEXT: llvm.select %[[UNKNOWN]], %{{.*}}, %{{.*}} : i1, i4096
// LLVM-NEXT: llvm.br ^[[HEAD:bb[0-9]+]](
// LLVM-NEXT: ^[[HEAD]](%{{.*}}: i4096, %{{.*}}: i4096, %[[REMAINING:.*]]: i4096):
// LLVM-NEXT: llvm.icmp "ne" %[[REMAINING]], %{{.*}} : i4096
// LLVM-NEXT: llvm.cond_br
// LLVM-NEXT: ^{{bb[0-9]+}}(%[[RESULT:.*]]: i4096, %[[SQUARE:.*]]: i4096, %[[EXPONENT:.*]]: i4096):
// LLVM-NEXT: %[[BIT:.*]] = llvm.trunc %[[EXPONENT]] : i4096 to i1
// LLVM-NEXT: %[[PRODUCT:.*]] = llvm.mul %[[RESULT]], %[[SQUARE]] : i4096
// LLVM-NEXT: %[[NEXT:.*]] = llvm.select %[[BIT]], %[[PRODUCT]], %[[RESULT]] : i1, i4096
// LLVM-NEXT: %[[NEXT_SQUARE:.*]] = llvm.mul %[[SQUARE]], %[[SQUARE]] : i4096
// LLVM-NEXT: %[[SHIFTED:.*]] = llvm.lshr %[[EXPONENT]], %{{.*}} : i4096
// LLVM-NEXT: llvm.br ^[[HEAD]](%[[NEXT]], %[[NEXT_SQUARE]], %[[SHIFTED]] : i4096, i4096, i4096)
// LLVM-NEXT: ^{{bb[0-9]+}}:
// LLVM-NEXT: llvm.select %[[UNKNOWN]], %{{.*}}, %{{.*}} : i1, i4096
// LLVM-NEXT: llvm.select %[[UNKNOWN]], %[[ALL]], %{{.*}} : i1, i4096
// Each of the three powers has exactly that loop's two multiplies.
// MULS-COUNT-6: llvm.mul
// MULS-NOT: llvm.mul

module top;
  logic [4095:0] base, exponent, result;
  logic [127:0] narrow_base, narrow_result;
  logic [31:0] narrow_exponent;
  initial begin
    result = base ** exponent;
    narrow_result = narrow_base ** exponent;
    result = base ** narrow_exponent;
  end
endmodule
