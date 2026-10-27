// REQUIRES: native-macho
// RUN: env PATH=/nonexistent SDKROOT=/nonexistent DEVELOPER_DIR=/nonexistent %talos %s -o %t.exe
// RUN: llvm-readobj --file-headers --macho-version-min %t.exe | FileCheck %s --check-prefix=MACHO
// RUN: codesign --verify %t.exe && %t.exe | FileCheck %s --check-prefix=OUTPUT
// RUN: %talos -O0 %s -o %t.o0 && %t.o0 | FileCheck %s --check-prefix=OUTPUT
// RUN: %talos -flto %s -o %t.lto && codesign --verify %t.lto && %t.lto | FileCheck %s --check-prefix=OUTPUT
// RUN: %talos -c %s -o %t.o && llvm-readobj --file-headers %t.o | FileCheck %s --check-prefix=OBJECT
// RUN: %talos -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM

module macos_native;
  logic [7:0] value;
  initial begin
    value = 40;
    $display("first=%0d", value);
    #1;
    value = value + 2;
    $display("second=%0d", value);
  end
  final $display("final=%0d", value);
endmodule

// MACHO: Format: Mach-O arm64
// MACHO: FileType: Executable
// MACHO: PIE
// MACHO: Cmd: LC_BUILD_VERSION
// MACHO: Platform: macos
// MACHO-NEXT: Version: {{[0-9]+(\.[0-9]+)*$}}
// OBJECT: Format: Mach-O arm64
// OBJECT: FileType: Relocatable
// LLVM: target triple = "aarch64-apple-macosx{{[0-9.]+}}"
// OUTPUT: first=40
// OUTPUT-NEXT: second=42
// OUTPUT-NEXT: final=42
