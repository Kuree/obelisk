// The default links the prebuilt object runtime archive. Opting into LTO
// must produce the same simulator behavior.

// RUN: obelisk -O3 -flto %s -o %t.lto
// RUN: %t.lto > %t.lto.out
// RUN: obelisk -O3 %s -o %t.nolto
// RUN: %t.nolto > %t.nolto.out
// RUN: diff -u %t.lto.out %t.nolto.out
// RUN: FileCheck %s --check-prefix=STDOUT < %t.nolto.out

// Explicit -fno-lto selects the default, and the last of the pair wins.
// RUN: obelisk -O3 -fno-lto %s -o %t.explicit
// RUN: %t.explicit > %t.explicit.out
// RUN: diff -u %t.nolto.out %t.explicit.out
// RUN: obelisk -O3 -fno-lto -flto %s -o %t.relto
// RUN: %t.relto > %t.relto.out
// RUN: diff -u %t.lto.out %t.relto.out
// RUN: obelisk -O3 -flto -fno-lto %s -o %t.restored
// RUN: %t.restored > %t.restored.out
// RUN: diff -u %t.nolto.out %t.restored.out

// The bytecode tier also defaults to linking the object runtime archive.
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode > %t.bytecode.out
// RUN: diff -u %t.lto.out %t.bytecode.out
// RUN: obelisk -O3 -flto --execution-tier=bytecode %s -o %t.bytecode-lto
// RUN: %t.bytecode-lto > %t.bytecode-lto.out
// RUN: diff -u %t.bytecode.out %t.bytecode-lto.out

// -O0 does not run LTO even when requested.
// RUN: obelisk -O0 -flto %s -o %t.o0
// RUN: %t.o0 > %t.o0.out
// RUN: diff -u %t.lto.out %t.o0.out

// A command file can opt into LTO.
// RUN: echo "-flto" > %t.flags.f
// RUN: obelisk -O3 -f %t.flags.f %s -o %t.viaflags
// RUN: %t.viaflags > %t.viaflags.out
// RUN: diff -u %t.lto.out %t.viaflags.out

module no_lto;
  int accumulator;
  initial begin
    for (int index = 0; index < 8; index++)
      accumulator += index * index;
    $display("accumulator=%0d", accumulator);
    #5 $display("time=%0t", $time);
    $finish;
  end
endmodule

// STDOUT: accumulator=140
// STDOUT: time=5
