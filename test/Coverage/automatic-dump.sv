// RUN: %obelisk -fno-lto --target=native --coverage=line -o %t.native %s
// RUN: %t.native --coverage-output=%t.native.obcov --coverage-test=smoke
// RUN: obelisk-cov inspect %t.native.obcov \
// RUN:   | FileCheck %s --check-prefix=LINE-INSPECT
// RUN: %obelisk -fno-lto --execution-tier=bytecode --coverage=line -o %t.bytecode %s
// RUN: %t.bytecode --coverage-output=%t.bytecode.obcov --coverage-test=smoke
// RUN: obelisk-cov inspect %t.bytecode.obcov \
// RUN:   | FileCheck %s --check-prefix=LINE-INSPECT
// RUN: %t.native --coverage-output=%t.suppressed.obcov --no-coverage-dump
// RUN: test ! -e %t.suppressed.obcov
// RUN: %obelisk -fno-lto --target=native -o %t.functional-only.native %s
// RUN: %t.functional-only.native \
// RUN:   --coverage-output=%t.functional-only.native.obcov
// RUN: test ! -e %t.functional-only.native.obcov
// RUN: %obelisk -fno-lto --execution-tier=bytecode \
// RUN:   -o %t.functional-only.bytecode %s
// RUN: %t.functional-only.bytecode \
// RUN:   --coverage-output=%t.functional-only.bytecode.obcov
// RUN: test ! -e %t.functional-only.bytecode.obcov
// RUN: %obelisk -fno-lto --target=native -DREQUEST_COVERAGE_DATABASE \
// RUN:   -o %t.database-request.native %s
// RUN: %t.database-request.native \
// RUN:   --coverage-output=%t.database-request.native.obcov
// RUN: obelisk-cov inspect %t.database-request.native.obcov \
// RUN:   | FileCheck %s --check-prefix=DATABASE-INSPECT
// RUN: %obelisk -fno-lto --execution-tier=bytecode \
// RUN:   -DREQUEST_COVERAGE_DATABASE -o %t.database-request.bytecode %s
// RUN: %t.database-request.bytecode \
// RUN:   --coverage-output=%t.database-request.bytecode.obcov
// RUN: obelisk-cov inspect %t.database-request.bytecode.obcov \
// RUN:   | FileCheck %s --check-prefix=DATABASE-INSPECT
// RUN: %python -c "import os,subprocess,sys; os.makedirs(sys.argv[2],exist_ok=True); p=subprocess.run([sys.argv[1],'--coverage-output='+sys.argv[2]],text=True,capture_output=True); assert p.returncode == 4, (p.returncode,p.stderr); print(p.stderr,end='')" %t.native %t.dump-output-directory 2>&1 | FileCheck %s --check-prefix=DUMP-IO
// RUN: %obelisk -fno-lto --target=native --coverage=line -DDUMP_FATAL \
// RUN:   -o %t.dump-fatal %s
// RUN: %python -c "import os,subprocess,sys; os.makedirs(sys.argv[2],exist_ok=True); p=subprocess.run([sys.argv[1],'--coverage-output='+sys.argv[2]],text=True,capture_output=True); assert p.returncode == 19, (p.returncode,p.stderr); print(p.stderr,end='')" %t.dump-fatal %t.dump-fatal-output-directory 2>&1 | FileCheck %s --check-prefix=DUMP-FATAL

module top;
  bit value;
  covergroup cg;
    cp: coverpoint value {
      bins zero = {0};
    }
  endgroup
  cg c;
`ifdef REQUEST_COVERAGE_DATABASE
  real global_coverage;
`endif

  initial begin
    c = new;
    c.sample();
`ifdef REQUEST_COVERAGE_DATABASE
    global_coverage = $get_coverage();
`endif
`ifdef DUMP_FATAL
    $fatal(1, "expected simulation failure before coverage dump");
`else
    $finish;
`endif
  end
endmodule

// LINE-INSPECT: format: obcov
// LINE-INSPECT: codec-version: 1
// LINE-INSPECT: runs: 1
// LINE-INSPECT: line-points: 3
// LINE-INSPECT: functional-types: 1
// LINE-INSPECT: functional-configurations: 0
// LINE-INSPECT: resolved-instances: 0
// DATABASE-INSPECT: format: obcov
// DATABASE-INSPECT: codec-version: 1
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
// DUMP-FATAL: expected simulation failure before coverage dump
// DUMP-FATAL: error: failed to write coverage database
// DUMP-FATAL-SAME: I/O error (rename:

// RUN: obelisk-cov report --format=text %t.native.obcov | FileCheck %s --check-prefix=LINE
// RUN: obelisk-cov report --format=text %t.bytecode.obcov | FileCheck %s --check-prefix=LINE
// LINE: line: 100.00% (3/3), partial 0
// LINE: functional: unavailable
