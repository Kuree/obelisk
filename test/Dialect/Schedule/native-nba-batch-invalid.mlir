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
// -----
#batch = #schedule.nba_batch_layout<plan = @plan, value = @value, unknown = @unknown, dirty = @dirty, summary = @summary, ingress = @ingress, bitCount = 256, rootCount = 1, capacity = 130>
#consumer = #schedule.nba_consumer<low = 0, width = 1, bit = 130>
#root = #schedule.nba_commit_layout<offset = 0, width = 8, accumulator = @accumulator, slots = [], consumers = [#consumer]>
module {
  llvm.func @bad_consumer(%ctx: !llvm.ptr, %region: i32, %changed: !llvm.ptr) {
    %r = schedule.nba.batch %ctx, %region, %changed {
      // expected-error @+1 {{requires consumers within root and ready capacity}}
      schedule.nba.commit 0 {layout = #root}
      schedule.nba.yield
    } {layout = #batch} : (!llvm.ptr, i32, !llvm.ptr) -> i32
    llvm.return
  }
}
// -----
#batch = #schedule.nba_batch_layout<plan = @plan, value = @value, unknown = @unknown, dirty = @dirty, summary = @summary, ingress = @ingress, bitCount = 256, rootCount = 1, capacity = 130>
#root = #schedule.nba_commit_layout<offset = 250, width = 8, accumulator = @accumulator, slots = [], consumers = []>
module {
  llvm.func @bad_range(%ctx: !llvm.ptr, %region: i32, %changed: !llvm.ptr) {
    %r = schedule.nba.batch %ctx, %region, %changed {
      // expected-error @+1 {{commit range must lie within the batch state layout}}
      schedule.nba.commit 0 {layout = #root}
      schedule.nba.yield
    } {layout = #batch} : (!llvm.ptr, i32, !llvm.ptr) -> i32
    llvm.return
  }
}
// -----
#batch = #schedule.nba_batch_layout<plan = @plan, value = @value, unknown = @unknown, dirty = @dirty, summary = @summary, ingress = @ingress, bitCount = 256, rootCount = 1, capacity = 130>
#slot = #schedule.nba_slot_layout<offset = @offset, value = @staged_value, unknown = @staged_unknown, valid = @valid, width = 5, constantOffset = 4 : i64>
#root = #schedule.nba_commit_layout<offset = 0, width = 8, slots = [#slot], consumers = []>
module {
  llvm.func @bad_constant_target(%ctx: !llvm.ptr, %region: i32, %changed: !llvm.ptr) {
    %r = schedule.nba.batch %ctx, %region, %changed {
      // expected-error @+1 {{constant slot target must lie within its root}}
      schedule.nba.commit 0 {layout = #root}
      schedule.nba.yield
    } {layout = #batch} : (!llvm.ptr, i32, !llvm.ptr) -> i32
    llvm.return
  }
}
// -----
#batch = #schedule.nba_batch_layout<plan = @plan, value = @value, unknown = @unknown, dirty = @dirty, summary = @summary, ingress = @ingress, bitCount = 256, rootCount = 1, capacity = 130>
#slot = #schedule.nba_slot_layout<offset = @offset, value = @staged_value, unknown = @staged_unknown, valid = @valid, width = 5, constantOffset = -1 : i64>
#root = #schedule.nba_commit_layout<offset = 0, width = 8, slots = [#slot], consumers = []>
module {
  llvm.func @bad_constant_target(%ctx: !llvm.ptr, %region: i32, %changed: !llvm.ptr) {
    %r = schedule.nba.batch %ctx, %region, %changed {
      // expected-error @+1 {{constant slot target must lie within its root}}
      schedule.nba.commit 0 {layout = #root}
      schedule.nba.yield
    } {layout = #batch} : (!llvm.ptr, i32, !llvm.ptr) -> i32
    llvm.return
  }
}
// -----
#batch = #schedule.nba_batch_layout<plan = @plan, value = @value, unknown = @unknown, dirty = @dirty, summary = @summary, ingress = @ingress, bitCount = 256, rootCount = 1, capacity = 130>
#slot = #schedule.nba_slot_layout<offset = @offset, value = @staged_value, unknown = @staged_unknown, valid = @valid, width = 5, constantOffset = 18446744073709551616 : i128>
#root = #schedule.nba_commit_layout<offset = 0, width = 8, slots = [#slot], consumers = []>
module {
  llvm.func @bad_constant_target(%ctx: !llvm.ptr, %region: i32, %changed: !llvm.ptr) {
    %r = schedule.nba.batch %ctx, %region, %changed {
      // expected-error @+1 {{constant slot target must lie within its root}}
      schedule.nba.commit 0 {layout = #root}
      schedule.nba.yield
    } {layout = #batch} : (!llvm.ptr, i32, !llvm.ptr) -> i32
    llvm.return
  }
}
