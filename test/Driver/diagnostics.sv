// RUN: not talos --max-include-depth=invalid 2>&1 | FileCheck %s --check-prefix=BAD-DEPTH
// RUN: not talos --max-include-depth=-1 2>&1 | FileCheck %s --check-prefix=BAD-DEPTH
// RUN: not talos --error-limit=invalid 2>&1 | FileCheck %s --check-prefix=BAD-LIMIT
// RUN: not talos --error-limit=-1 2>&1 | FileCheck %s --check-prefix=BAD-LIMIT
// RUN: not talos - - 2>&1 | FileCheck %s --check-prefix=DUP-STDIN
// RUN: not talos --definitely-unknown 2>&1 | FileCheck %s --check-prefix=UNKNOWN

// BAD-DEPTH: talos: error: invalid value
// BAD-DEPTH-SAME: for --max-include-depth

// BAD-LIMIT: talos: error: invalid value
// BAD-LIMIT-SAME: for --error-limit

// DUP-STDIN: talos: error: standard input may only appear once

// UNKNOWN: talos: error:
// UNKNOWN-SAME: unknown argument
