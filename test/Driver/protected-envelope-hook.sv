// XFAIL: *
// Upstream Slang v11.0 has no decryption or protected-source redaction API.
// Keep this source case visible without downstream Slang patches.
// RUN: %talos -emit-slang %s | FileCheck %s --check-prefix=SLANG
// RUN: %talos -emit-talos %s | talos-opt --verify-each | FileCheck %s --check-prefix=TALOS
// RUN: %talos -O3 %s -o %t.native
// RUN: %t.native | FileCheck %s --check-prefix=OUTPUT
// RUN: %talos -O3 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode | FileCheck %s --check-prefix=OUTPUT

// IEEE 1800-2017 34.2 makes these envelope descriptors effective in source
// order even though they are split across directives before begin_protected.
`pragma protect data_method="x-caesar"
`pragma protect data_keyname="rot13"
`pragma protect encoding=(enctype="raw")
`pragma protect begin_protected
`pragma protect data_block
zbqhyr cebgrpgrq_ubbx;
  `qrsvar CEBGRPGRQ_INYHR 42
  vavgvny ortva
    vs (`CEBGRPGRQ_INYHR == 42) $qvfcynl("CEBGRPG_BX");
    $svavfu;
  raq
raqzbqhyr
`pragma protect end_protected

// SLANG: slang.symbol.definition @{{[^ ]+}} attributes {{.*}}name = "protected_hook"
// TALOS: talos.sv.symbol.definition @{{[^ ]+}} attributes {{.*}}name = "protected_hook"
// TALOS-NOT: data_block
// TALOS-NOT: cebgrpgrq_ubbx
// OUTPUT: PROTECT_OK
