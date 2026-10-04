// RUN: obelisk-opt %s --split-input-file --verify-diagnostics
module {
  llvm.func @bad() {
    // expected-error @+1 {{requires a nonempty CFG without entry arguments}}
    schedule.ready.region {
    ^entry(%arg: i64):
      schedule.ready.yield
    }
    llvm.return
  }
}
// -----
module {
  llvm.func @bad() {
    // expected-error @+1 {{does not permit nested regions}}
    schedule.ready.region {
      schedule.ready.region {
        schedule.ready.yield
      }
      schedule.ready.yield
    }
    llvm.return
  }
}
// -----
module {
  llvm.func @bad() {
    // expected-error @+1 {{requires non-suspending branches or ready.yield}}
    schedule.ready.region {
      llvm.unreachable
    }
    llvm.return
  }
}
