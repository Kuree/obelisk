// RUN: %protect-obelisk -emit-slang %s | FileCheck %s --check-prefix=SLANG
// RUN: %protect-obelisk -emit-obelisk %s | obelisk-opt --verify-each | FileCheck %s --check-prefix=OBELISK
// RUN: %protect-obelisk -fno-lto -O0 %s -o %t.native-o0
// RUN: %t.native-o0 | FileCheck %s --check-prefix=OUTPUT
// RUN: %protect-obelisk -fno-lto -O3 %s -o %t.native-o3
// RUN: %t.native-o3 | FileCheck %s --check-prefix=OUTPUT
// RUN: %protect-obelisk -fno-lto -O0 --execution-tier=bytecode %s -o %t.bytecode-o0
// RUN: %t.bytecode-o0 | FileCheck %s --check-prefix=OUTPUT
// RUN: %protect-obelisk -fno-lto -O3 --execution-tier=bytecode %s -o %t.bytecode-o3
// RUN: %t.bytecode-o3 | FileCheck %s --check-prefix=OUTPUT
// RUN: %protect-obelisk -fno-lto -O0 --native-scheduler=aot %s -o %t.aot-o0
// RUN: %t.aot-o0 | FileCheck %s --check-prefix=OUTPUT
// RUN: %protect-obelisk -fno-lto -O3 --native-scheduler=aot %s -o %t.aot-o3
// RUN: %t.aot-o3 | FileCheck %s --check-prefix=OUTPUT
// RUN: not obelisk -emit-slang %s 2>&1 | FileCheck %s --check-prefix=NO-PROVIDER
// RUN: not %protect-obelisk --test-max-protect-bytes=8 -emit-slang %s 2>&1 | FileCheck %s --check-prefix=SIZE-LIMIT
// RUN: not %protect-obelisk --test-max-protect-count=0 -emit-slang %s 2>&1 | FileCheck %s --check-prefix=COUNT-LIMIT

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

// SLANG: slang.symbol.definition attributes {{.*}}name = "protected_hook"
// OBELISK: obelisk.sv.symbol.definition attributes {{.*}}name = "protected_hook"
// OBELISK-NOT: data_block
// OBELISK-NOT: cebgrpgrq_ubbx
// OUTPUT: PROTECT_OK
// NO-PROVIDER: error: protected envelope rejected (provider unavailable)
// SIZE-LIMIT: error: protected envelope rejected (resource limit)
// SIZE-LIMIT-NOT: protected_hook
// COUNT-LIMIT: error: protected envelope rejected (resource limit)
// COUNT-LIMIT-NOT: cebgrpgrq_ubbx
