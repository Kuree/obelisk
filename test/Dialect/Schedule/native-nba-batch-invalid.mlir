// RUN: obelisk-opt %s -split-input-file -verify-diagnostics
module {
  llvm.func @duplicate(%ctx: !llvm.ptr, %region: i32, %changed: !llvm.ptr) {
    // expected-error @+1 {{requires distinct roots in increasing order}}
    %r = schedule.nba.batch %ctx, %region, %changed {
      schedule.nba.commit 3
      schedule.nba.commit 3
      schedule.nba.yield
    } : (!llvm.ptr, i32, !llvm.ptr) -> i32
    llvm.return
  }
}
// -----
module {
  llvm.func @empty(%ctx: !llvm.ptr, %region: i32, %changed: !llvm.ptr) {
    // expected-error @+1 {{requires at least one commit}}
    %r = schedule.nba.batch %ctx, %region, %changed {
      schedule.nba.yield
    } : (!llvm.ptr, i32, !llvm.ptr) -> i32
    llvm.return
  }
}
// -----
module {
  llvm.func @effect(%ctx: !llvm.ptr, %region: i32, %changed: !llvm.ptr) {
    // expected-error @+1 {{body permits only nba.commit actions}}
    %r = schedule.nba.batch %ctx, %region, %changed {
      llvm.store %region, %changed : i32, !llvm.ptr
      schedule.nba.commit 1
      schedule.nba.yield
    } : (!llvm.ptr, i32, !llvm.ptr) -> i32
    llvm.return
  }
}
// -----
module {
  llvm.func @outside() {
    // expected-error @+1 {{expects parent op 'schedule.nba.batch'}}
    schedule.nba.commit 1
    llvm.return
  }
}
// -----
module {
  llvm.func @range(%ctx: !llvm.ptr, %region: i32, %changed: !llvm.ptr) {
    %r = schedule.nba.batch %ctx, %region, %changed {
      // expected-error @+1 {{root must be between 0 and UINT32_MAX - 1}}
      schedule.nba.commit 4294967295
      schedule.nba.yield
    } : (!llvm.ptr, i32, !llvm.ptr) -> i32
    llvm.return
  }
}
