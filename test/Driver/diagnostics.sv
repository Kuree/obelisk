// RUN: not obelisk -D 2>&1 | FileCheck %s --check-prefix=MISSING
// RUN: not obelisk -U 2>&1 | FileCheck %s --check-prefix=MISSING
// RUN: not obelisk -I 2>&1 | FileCheck %s --check-prefix=MISSING
// RUN: not obelisk --include-directory 2>&1 \
// RUN:   | FileCheck %s --check-prefix=MISSING
// RUN: not obelisk -isystem 2>&1 | FileCheck %s --check-prefix=MISSING
// RUN: not obelisk -f 2>&1 | FileCheck %s --check-prefix=MISSING
// RUN: not obelisk --filelist 2>&1 | FileCheck %s --check-prefix=MISSING
// RUN: not obelisk -y 2>&1 | FileCheck %s --check-prefix=MISSING
// RUN: not obelisk -Y 2>&1 | FileCheck %s --check-prefix=MISSING
// RUN: not obelisk -l 2>&1 | FileCheck %s --check-prefix=MISSING
// RUN: not obelisk -G 2>&1 | FileCheck %s --check-prefix=MISSING
// RUN: not obelisk -fno-lto -o 2>&1 | FileCheck %s --check-prefix=MISSING
// RUN: not obelisk -Xslang 2>&1 | FileCheck %s --check-prefix=MISSING

// RUN: not obelisk --max-include-depth=invalid 2>&1 \
// RUN:   | FileCheck %s --check-prefix=BAD-DEPTH
// RUN: not obelisk --max-include-depth=-1 2>&1 \
// RUN:   | FileCheck %s --check-prefix=BAD-DEPTH
// RUN: not obelisk --error-limit=invalid 2>&1 \
// RUN:   | FileCheck %s --check-prefix=BAD-LIMIT
// RUN: not obelisk --error-limit=-1 2>&1 \
// RUN:   | FileCheck %s --check-prefix=BAD-LIMIT

// RUN: not obelisk --std= 2>&1 | FileCheck %s --check-prefix=BAD-STD
// RUN: not obelisk --std=latest 2>&1 \
// RUN:   | FileCheck %s --check-prefix=BAD-STD
// RUN: not obelisk --std=1800-2005 2>&1 \
// RUN:   | FileCheck %s --check-prefix=BAD-STD

// RUN: not obelisk - - 2>&1 | FileCheck %s --check-prefix=DUP-STDIN
// RUN: not obelisk --definitely-unknown 2>&1 \
// RUN:   | FileCheck %s --check-prefix=UNKNOWN

// MISSING: obelisk: error:
// MISSING-SAME: missing argument

// BAD-DEPTH: obelisk: error: invalid value
// BAD-DEPTH-SAME: for --max-include-depth

// BAD-LIMIT: obelisk: error: invalid value
// BAD-LIMIT-SAME: for --error-limit

// BAD-STD: obelisk: error: unsupported SystemVerilog revision

// DUP-STDIN: obelisk: error: standard input may only appear once

// UNKNOWN: obelisk: error:
// UNKNOWN-SAME: unknown argument
