// RUN: talos-opt %s --split-input-file --verify-diagnostics
module {
  llvm.func @bad(%base: !llvm.ptr) {
    // expected-error @+1 {{ready word must name a leaf within capacity}}
    %value = schedule.ready.read %base {capacity = 64 : i64, word = 1 : i64} : (!llvm.ptr) -> i64
    llvm.return
  }
}
// -----
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
// -----
module {
  llvm.func @bad() -> i32 {
    %zero = arith.constant 0 : i64
    %status = schedule.ready.region {
      // expected-error @+1 {{operands must match the ready.region results}}
      schedule.ready.yield %zero : i64
    } : i32
    llvm.return %status : i32
  }
}
