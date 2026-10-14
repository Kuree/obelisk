// RUN: %obelisk --std=1800-2023 -O0 -DREQUEST_COVERAGE_DATABASE -o %t.database %S/automatic-dump.sv
// RUN: %t.database --coverage-output=%t.database.obcov
// RUN: obelisk-cov inspect %t.database.obcov | FileCheck %s --check-prefix=DATABASE-INSPECT
// RUN: mkdir -p %t.dump-output-directory
// RUN: not --status=4 %t.database --coverage-output=%t.dump-output-directory 2>&1 | FileCheck %s --check-prefix=DUMP-IO
// RUN: %obelisk --std=1800-2023 -O0 --coverage=line -DDUMP_FATAL -o %t.dump-fatal %S/automatic-dump.sv
// RUN: not --status=19 %t.dump-fatal --coverage-output=%t.dump-output-directory 2>&1 | FileCheck %s --check-prefix=DUMP-FATAL

// A $get_coverage call requests the functional database, which is dumped at
// exit. A failed dump is a nonzero exit with a diagnostic, including after
// $fatal, where the message of the fatal itself still precedes it.

// DATABASE-INSPECT: format: obcov
// DATABASE-INSPECT: codec-version: 2
// DATABASE-INSPECT: runs: 1
// DATABASE-INSPECT: line-points: 0
// DATABASE-INSPECT: functional-types: 1
// DATABASE-INSPECT: functional-item-templates: 1
// DATABASE-INSPECT: functional-bin-templates: 1
// DATABASE-INSPECT: functional-configurations: 1
// DATABASE-INSPECT: resolved-functional-items: 1
// DATABASE-INSPECT: resolved-functional-bins: 1
// DATABASE-INSPECT: resolved-instances: 1
// DUMP-IO: error: failed to write coverage database
// DUMP-IO-SAME: I/O error (rename:
// DUMP-IO-SAME: (status 4)
// DUMP-FATAL: expected simulation failure before coverage dump
// DUMP-FATAL: error: failed to write coverage database
// DUMP-FATAL-SAME: I/O error (rename:
