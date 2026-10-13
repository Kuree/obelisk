// RUN: obelisk-opt %s --pass-pipeline='builtin.module(encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' | mlir-translate --mlir-to-llvmir | opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O3>' | %llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: rm -rf %t.d && mkdir -p %t.d/valid %t.d/bad-bin %t.d/bad-hex && cp %S/Inputs/readmem-wide.hex %S/Inputs/readmem-wide.bin %t.d/valid && cp %S/Inputs/readmem-wide.hex %t.d/bad-bin && cp %S/Inputs/readmem-invalid.bin %t.d/bad-bin/readmem-wide.bin && cp %S/Inputs/readmem-wide.bin %t.d/bad-hex && cp %S/Inputs/readmem-invalid.hex %t.d/bad-hex/readmem-wide.hex
// RUN: cd %t.d/valid && %t.exe | FileCheck %s
// RUN: cd %t.d/bad-bin && not %t.exe 2>&1 | FileCheck %s --check-prefix=INVALID
// RUN: cd %t.d/bad-hex && not %t.exe 2>&1 | FileCheck %s --check-prefix=INVALID

// LRM 21.4: retain low destination bits for overlong words, preserve X/Z,
// ignore underscores and comments, and leave address markers as separate
// tokens. Exercise both sides of the 64/128-bit parsing boundaries and a
// token longer than the wide parser's local buffer.
// CHECK: READMEM WIDE PASS
// CHECK-NOT: FAIL
// INVALID: invalid character in $readmem input
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @readmem_wide {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %mode = simulation.bytes.constant "r"
      %data_kind = arith.constant 1 : i32
      %address_kind = arith.constant 2 : i32
      %eof_kind = arith.constant 0 : i32
      %expected_address = arith.constant 291 : i64
      %ok0 = arith.constant true
      %hex_path = simulation.bytes.constant "readmem-wide.hex"
      %hex_fd = simulation.file.open %ctx, %hex_path, %mode : (!simulation.context, !simulation.bytes, !simulation.bytes) -> i32
      %word1, %kind1, %address1 = simulation.file.readmem_token %ctx, %hex_fd {radix = #simulation.radix<hex>} : (!simulation.context, i32) -> (!simulation.logic<64>, i32, i64)
      %expected1 = simulation.logic.constant 81985529216486895 : i64, 0 : i64 : !simulation.logic<64>
      %equal1 = simulation.logic.compare case_eq %word1, %expected1 : (!simulation.logic<64>, !simulation.logic<64>) -> i1
      %data1 = arith.cmpi eq, %kind1, %data_kind : i32
      %valid1 = arith.andi %equal1, %data1 : i1
      %ok1 = arith.andi %ok0, %valid1 : i1
      %word2, %kind2, %address2 = simulation.file.readmem_token %ctx, %hex_fd {radix = #simulation.radix<hex>} : (!simulation.context, i32) -> (!simulation.logic<65>, i32, i64)
      %expected2 = simulation.logic.constant 1311768467463790320 : i65, 0 : i65 : !simulation.logic<65>
      %equal2 = simulation.logic.compare case_eq %word2, %expected2 : (!simulation.logic<65>, !simulation.logic<65>) -> i1
      %data2 = arith.cmpi eq, %kind2, %data_kind : i32
      %valid2 = arith.andi %equal2, %data2 : i1
      %ok2 = arith.andi %ok1, %valid2 : i1
      %word3, %kind3, %address3 = simulation.file.readmem_token %ctx, %hex_fd {radix = #simulation.radix<hex>} : (!simulation.context, i32) -> (!simulation.logic<127>, i32, i64)
      %expected3 = simulation.logic.constant 150385901603114748695279975582733614302 : i127, 148873535527910577783601077231070216192 : i127 : !simulation.logic<127>
      %equal3 = simulation.logic.compare case_eq %word3, %expected3 : (!simulation.logic<127>, !simulation.logic<127>) -> i1
      %data3 = arith.cmpi eq, %kind3, %data_kind : i32
      %valid3 = arith.andi %equal3, %data3 : i1
      %ok3 = arith.andi %ok2, %valid3 : i1
      %word4, %kind4, %address4 = simulation.file.readmem_token %ctx, %hex_fd {radix = #simulation.radix<hex>} : (!simulation.context, i32) -> (!simulation.logic<128>, i32, i64)
      %expected4 = simulation.logic.constant 19944327616755004386252608954954123691 : i128, 338953138925153547590542576490549084160 : i128 : !simulation.logic<128>
      %equal4 = simulation.logic.compare case_eq %word4, %expected4 : (!simulation.logic<128>, !simulation.logic<128>) -> i1
      %data4 = arith.cmpi eq, %kind4, %data_kind : i32
      %valid4 = arith.andi %equal4, %data4 : i1
      %ok4 = arith.andi %ok3, %valid4 : i1
      %word5, %kind5, %address5 = simulation.file.readmem_token %ctx, %hex_fd {radix = #simulation.radix<hex>} : (!simulation.context, i32) -> (!simulation.logic<129>, i32, i64)
      %expected5 = simulation.logic.constant 560465074928604528057322882828794701071 : i129, 255 : i129 : !simulation.logic<129>
      %equal5 = simulation.logic.compare case_eq %word5, %expected5 : (!simulation.logic<129>, !simulation.logic<129>) -> i1
      %data5 = arith.cmpi eq, %kind5, %data_kind : i32
      %valid5 = arith.andi %equal5, %data5 : i1
      %ok5 = arith.andi %ok4, %valid5 : i1
      %word6, %kind6, %address6 = simulation.file.readmem_token %ctx, %hex_fd {radix = #simulation.radix<hex>} : (!simulation.context, i32) -> (!simulation.logic<128>, i32, i64)
      %expected6 = simulation.logic.constant 1512366075204170947332355369683137040 : i128, 0 : i128 : !simulation.logic<128>
      %equal6 = simulation.logic.compare case_eq %word6, %expected6 : (!simulation.logic<128>, !simulation.logic<128>) -> i1
      %data6 = arith.cmpi eq, %kind6, %data_kind : i32
      %valid6 = arith.andi %equal6, %data6 : i1
      %ok6 = arith.andi %ok5, %valid6 : i1
      %word7, %kind7, %address7 = simulation.file.readmem_token %ctx, %hex_fd {radix = #simulation.radix<hex>} : (!simulation.context, i32) -> (!simulation.logic<128>, i32, i64)
      %kind_ok7 = arith.cmpi eq, %kind7, %address_kind : i32
      %addr_ok7 = arith.cmpi eq, %address7, %expected_address : i64
      %valid7 = arith.andi %kind_ok7, %addr_ok7 : i1
      %ok7 = arith.andi %ok6, %valid7 : i1
      %word8, %kind8, %address8 = simulation.file.readmem_token %ctx, %hex_fd {radix = #simulation.radix<hex>} : (!simulation.context, i32) -> (!simulation.logic<128>, i32, i64)
      %kind_ok8 = arith.cmpi eq, %kind8, %eof_kind : i32
      %ok8 = arith.andi %ok7, %kind_ok8 : i1
      simulation.file.close %ctx, %hex_fd : (!simulation.context, i32) -> ()
      %bin_path = simulation.bytes.constant "readmem-wide.bin"
      %bin_fd = simulation.file.open %ctx, %bin_path, %mode : (!simulation.context, !simulation.bytes, !simulation.bytes) -> i32
      %word9, %kind9, %address9 = simulation.file.readmem_token %ctx, %bin_fd {radix = #simulation.radix<binary>} : (!simulation.context, i32) -> (!simulation.logic<64>, i32, i64)
      %expected9 = simulation.logic.constant 12297829382473034405 : i64, 12 : i64 : !simulation.logic<64>
      %equal9 = simulation.logic.compare case_eq %word9, %expected9 : (!simulation.logic<64>, !simulation.logic<64>) -> i1
      %data9 = arith.cmpi eq, %kind9, %data_kind : i32
      %valid9 = arith.andi %equal9, %data9 : i1
      %ok9 = arith.andi %ok8, %valid9 : i1
      %word10, %kind10, %address10 = simulation.file.readmem_token %ctx, %bin_fd {radix = #simulation.radix<binary>} : (!simulation.context, i32) -> (!simulation.logic<65>, i32, i64)
      %expected10 = simulation.logic.constant 24595658764946068826 : i65, 12 : i65 : !simulation.logic<65>
      %equal10 = simulation.logic.compare case_eq %word10, %expected10 : (!simulation.logic<65>, !simulation.logic<65>) -> i1
      %data10 = arith.cmpi eq, %kind10, %data_kind : i32
      %valid10 = arith.andi %equal10, %data10 : i1
      %ok10 = arith.andi %ok9, %valid10 : i1
      %word11, %kind11, %address11 = simulation.file.readmem_token %ctx, %bin_fd {radix = #simulation.radix<binary>} : (!simulation.context, i32) -> (!simulation.logic<127>, i32, i64)
      %expected11 = simulation.logic.constant 71349528547938710081030159622790108853 : i127, 131722206550040695534209525457458662499 : i127 : !simulation.logic<127>
      %equal11 = simulation.logic.compare case_eq %word11, %expected11 : (!simulation.logic<127>, !simulation.logic<127>) -> i1
      %data11 = arith.cmpi eq, %kind11, %data_kind : i32
      %valid11 = arith.andi %equal11, %data11 : i1
      %ok11 = arith.andi %ok10, %valid11 : i1
      %word12, %kind12, %address12 = simulation.file.readmem_token %ctx, %bin_fd {radix = #simulation.radix<binary>} : (!simulation.context, i32) -> (!simulation.logic<128>, i32, i64)
      %expected12 = simulation.logic.constant 204169420152563078078024764459060926873 : i128, 272225893536750770770699685945414569164 : i128 : !simulation.logic<128>
      %equal12 = simulation.logic.compare case_eq %word12, %expected12 : (!simulation.logic<128>, !simulation.logic<128>) -> i1
      %data12 = arith.cmpi eq, %kind12, %data_kind : i32
      %valid12 = arith.andi %equal12, %data12 : i1
      %ok12 = arith.andi %ok11, %valid12 : i1
      %word13, %kind13, %address13 = simulation.file.readmem_token %ctx, %bin_fd {radix = #simulation.radix<binary>} : (!simulation.context, i32) -> (!simulation.logic<129>, i32, i64)
      %expected13 = simulation.logic.constant 136112946768375385385349842972707284582 : i129, 272225893536750770770699685945414569164 : i129 : !simulation.logic<129>
      %equal13 = simulation.logic.compare case_eq %word13, %expected13 : (!simulation.logic<129>, !simulation.logic<129>) -> i1
      %data13 = arith.cmpi eq, %kind13, %data_kind : i32
      %valid13 = arith.andi %equal13, %data13 : i1
      %ok13 = arith.andi %ok12, %valid13 : i1
      %word14, %kind14, %address14 = simulation.file.readmem_token %ctx, %bin_fd {radix = #simulation.radix<binary>} : (!simulation.context, i32) -> (!simulation.logic<128>, i32, i64)
      %expected14 = simulation.logic.constant 1 : i128, 0 : i128 : !simulation.logic<128>
      %equal14 = simulation.logic.compare case_eq %word14, %expected14 : (!simulation.logic<128>, !simulation.logic<128>) -> i1
      %data14 = arith.cmpi eq, %kind14, %data_kind : i32
      %valid14 = arith.andi %equal14, %data14 : i1
      %ok14 = arith.andi %ok13, %valid14 : i1
      %word15, %kind15, %address15 = simulation.file.readmem_token %ctx, %bin_fd {radix = #simulation.radix<binary>} : (!simulation.context, i32) -> (!simulation.logic<128>, i32, i64)
      %kind_ok15 = arith.cmpi eq, %kind15, %address_kind : i32
      %addr_ok15 = arith.cmpi eq, %address15, %expected_address : i64
      %valid15 = arith.andi %kind_ok15, %addr_ok15 : i1
      %ok15 = arith.andi %ok14, %valid15 : i1
      %word16, %kind16, %address16 = simulation.file.readmem_token %ctx, %bin_fd {radix = #simulation.radix<binary>} : (!simulation.context, i32) -> (!simulation.logic<128>, i32, i64)
      %kind_ok16 = arith.cmpi eq, %kind16, %eof_kind : i32
      %ok16 = arith.andi %ok15, %kind_ok16 : i1
      simulation.file.close %ctx, %bin_fd : (!simulation.context, i32) -> ()
      cf.cond_br %ok16, ^pass, ^fail
    ^pass:
      %message = simulation.bytes.constant "READMEM WIDE PASS"
      %stdout = arith.constant 1 : i32
      simulation.display %ctx to %stdout(%message) newline = true radix = <decimal> flags = [0] : !simulation.bytes
      simulation.return
    ^fail:
      %zero = arith.constant 0 : i32
      simulation.fatal %ctx, %zero
      simulation.return
    }
  }
}
