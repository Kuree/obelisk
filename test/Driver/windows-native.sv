// REQUIRES: windows
// RUN: %talos %s -o %t.exe
// RUN: llvm-readobj --file-headers --coff-imports %t.exe | FileCheck %s --check-prefix=%coff_machine
// RUN: %t.exe | FileCheck %s --check-prefix=OUTPUT
// RUN: %talos -O0 %s -o %t.o0.exe
// RUN: %t.o0.exe | FileCheck %s --check-prefix=OUTPUT
// RUN: %talos -flto %s -o %t.lto.exe
// RUN: %t.lto.exe | FileCheck %s --check-prefix=OUTPUT
// RUN: %talos -c %s -o %t.obj && llvm-readobj --file-headers %t.obj | FileCheck %s --check-prefix=%coff_machine

module windows_native;
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

// IMAGE_FILE_MACHINE_AMD64: Format: COFF-x86-64
// IMAGE_FILE_MACHINE_AMD64: Machine: IMAGE_FILE_MACHINE_AMD64
// IMAGE_FILE_MACHINE_ARM64: Format: COFF-ARM64
// IMAGE_FILE_MACHINE_ARM64: Machine: IMAGE_FILE_MACHINE_ARM64
// OUTPUT: first=40
// OUTPUT-NEXT: second=42
// OUTPUT-NEXT: final=42
