"""Exercise private capture ownership across sparse leaf and summary words."""
import argparse

parser = argparse.ArgumentParser()
parser.add_argument("--canonical", action="store_true")
parser.add_argument("--helper", action="store_true")
parser.add_argument("--finish", action="store_true")
args = parser.parse_args()

# More than 64 admitted roots, both sides of leaf boundaries, a large gap,
# and both sides of the 4096-root summary-page boundary.
roots = list(range(66)) + [127, 128, 4095, 4096]
words = sorted({root // 64 for root in roots})
leaf = {word: sum(1 << (root % 64) for root in roots if root // 64 == word)
        for word in words}
pages = {page: sum(1 << (word % 64) for word in words if word // 64 == page)
         for page in (0, 1)}
query_expected = 0
for value in list(leaf.values()) + list(pages.values()):
    query_expected ^= value
expected = sum(i + 1 + (128 if i % 5 == 0 else 0) for i in range(len(roots)))

if args.helper:
    print('#include <stdint.h>')
    print('#include <stdio.h>')
    for root in roots:
        print(f'extern unsigned char acc{root}[136];')
    print('static unsigned char *const captures[] = {')
    print(', '.join(f'acc{root}' for root in roots) + '};')
    print('''uint32_t obelisk_rt_v1_static_nba_specialization_guard_write(
        void *context, uint32_t root) { return 1; }
uint32_t obelisk_rt_v1_scheduler_execute_aot_actor(void *context, uint32_t slot) {
  uint64_t sum = 0;
  for (unsigned i = 0; i != sizeof(captures) / sizeof(*captures); ++i) {
    unsigned char *p = captures[i];
    if (*(uint32_t *)(p + 96) != 1 || *(uint32_t *)(p + 100) != 2 ||
        *(uint64_t *)(p + 64) != UINT32_MAX) return 1;
    sum += *(uint64_t *)p + 128 * *(uint64_t *)(p + 32);
  }''')
    print(f'  return sum == {expected} ? 0 : 1;\n}}')
    print('''uint32_t obelisk_rt_v1_scheduler_prepare_eval_nba_local_write(
        void *context, uint32_t local, uint32_t *region) {
  *region = UINT32_MAX;
  return local == 2 ? 0 : 1;
}''')
    raise SystemExit

records = ', '.join(
    f'{{root = array<i64: {root + 1}, {root}, {root + 1}, 0, 32, '
    f'4294967295, 2, {int(i == 0)}>, accumulator = @acc{root}, '
    f'sites = array<i64: {i + 1}>}}' for i, root in enumerate(roots))
certificate = '' if args.canonical else (
    f', schedule.eval.transaction_roots = [{records}]')
print('module attributes {llvm.data_layout = '
      '"e-m:e-p:64:64-i64:64-n8:16:32:64-S128", '
      'schedule.eval.runtime_calendar' + certificate + '} {')
for root in roots:
    print(f'  llvm.mlir.global @acc{root}(dense<0> : tensor<136xi8>) '
          ': !llvm.array<136 x i8>')
print('''  llvm.mlir.global @__obelisk_aot_nba_dirty_roots_v1(dense<0> : tensor<520xi8>) : !llvm.array<520 x i8>
  llvm.mlir.global @__obelisk_aot_nba_dirty_summary_v1(dense<0> : tensor<16xi8>) : !llvm.array<16 x i8>
  llvm.mlir.global internal @__obelisk_current_context() : !llvm.ptr {
    %null = llvm.mlir.zero : !llvm.ptr
    llvm.return %null : !llvm.ptr
  }
  llvm.func @__obelisk_eval_write_guard_0_v1()
  llvm.func @obelisk_rt_v1_scheduler_execute_aot_actor(!llvm.ptr, i32) -> i32
  llvm.func @obelisk_rt_v1_scheduler_prepare_eval_nba(!llvm.ptr, !llvm.ptr) -> i32
''')
for i, root in enumerate(roots):
    print(f'  llvm.func @capture{root}() {{')
    print(f'    %acc = llvm.mlir.addressof @acc{root} : !llvm.ptr')
    for name, offset in [('xz', 32), ('mask', 64), ('valid', 96),
                         ('region', 100), ('transient', 104)]:
        print(f'    %{name} = llvm.getelementptr %acc[{offset}] '
              ': (!llvm.ptr) -> !llvm.ptr, i8')
    for name, value, width in [('value', i + 1, 64), ('unknown', int(i % 5 == 0), 64),
                                ('bits', 4294967295, 64), ('one', 1, 32),
                                ('nba', 2, 32), ('zero', 0, 64)]:
        print(f'    %{name} = llvm.mlir.constant({value} : i{width}) : i{width}')
    for name, pointer, width in [('value', 'acc', 64), ('unknown', 'xz', 64),
                                  ('bits', 'mask', 64), ('one', 'valid', 32),
                                  ('nba', 'region', 32), ('zero', 'transient', 64)]:
        print(f'    llvm.store %{name}, %{pointer} : i{width}, !llvm.ptr')
    print('    %dirty = llvm.mlir.addressof @__obelisk_aot_nba_dirty_roots_v1 : !llvm.ptr')
    print(f'    %leaf = llvm.getelementptr %dirty[{root // 64 * 8}] '
          ': (!llvm.ptr) -> !llvm.ptr, i8')
    role = f'schedule.eval.pending_dirty_root = {root} : i32'
    print(f'    %old = llvm.load %leaf {{{role}}} : !llvm.ptr -> i64')
    print(f'    %bit = llvm.mlir.constant({1 << (root % 64)} : i64) : i64')
    print('    %marked = llvm.or %old, %bit : i64')
    print(f'    llvm.store %marked, %leaf {{{role}, '
          f'schedule.eval.pending_stage_spec = array<i64: {root}, {root + 1}, '
          f'32, 4294967295, 2, {i + 1}>}} : i64, !llvm.ptr')
    print('    %summary = llvm.mlir.addressof @__obelisk_aot_nba_dirty_summary_v1 : !llvm.ptr')
    print(f'    %page = llvm.getelementptr %summary[{root // 4096 * 8}] '
          ': (!llvm.ptr) -> !llvm.ptr, i8')
    role = f'schedule.eval.pending_summary_root = {root} : i32'
    print(f'    %prior = llvm.load %page {{{role}}} : !llvm.ptr -> i64')
    print(f'    %pagebit = llvm.mlir.constant({1 << ((root // 64) % 64)} : i64) : i64')
    print('    %markedpage = llvm.or %prior, %pagebit : i64')
    print(f'    llvm.store %markedpage, %page {{{role}}} : i64, !llvm.ptr')
    print('    llvm.return\n  }')
    print(f'  llvm.func @consume{root}() -> i64 {{')
    print(f'    %acc = llvm.mlir.addressof @acc{root} : !llvm.ptr')
    print('    %valid = llvm.getelementptr %acc[96] : (!llvm.ptr) -> !llvm.ptr, i8')
    print(f'    %ready = llvm.load %valid {{schedule.eval.pending_dirty_root = {root} : i32}} : !llvm.ptr -> i32')
    print('    %xz = llvm.getelementptr %acc[32] : (!llvm.ptr) -> !llvm.ptr, i8')
    print('    %value = llvm.load %acc : !llvm.ptr -> i64')
    print('    %unknown = llvm.load %xz : !llvm.ptr -> i64')
    print('''    %scale = llvm.mlir.constant(128 : i64) : i64
    %zero = llvm.mlir.constant(0 : i32) : i32
    %zero64 = llvm.mlir.constant(0 : i64) : i64
    %weighted = llvm.mul %unknown, %scale : i64
    %sum = llvm.add %value, %weighted : i64
    %present = llvm.icmp "ne" %ready, %zero : i32
    %result = llvm.select %present, %sum, %zero64 : i1, i64
    llvm.store %zero, %valid : i32, !llvm.ptr
    llvm.return %result : i64
  }''')
print('''  llvm.func @canonical_only() -> i64 {
    %summary = llvm.mlir.addressof @__obelisk_aot_nba_dirty_summary_v1 : !llvm.ptr
    %bits = llvm.load %summary {schedule.eval.pending_runtime_root = 5000 : i32} : !llvm.ptr -> i64
    llvm.return %bits : i64
  }''')
print('  llvm.func @query() -> i64 {')
print('    %zero = llvm.mlir.constant(0 : i64) : i64')
previous = 'zero'
for i, (name, offset) in enumerate(
        [('roots', word * 8) for word in words] + [('summary', page * 8) for page in pages]):
    symbol = '__obelisk_aot_nba_dirty_' + name + '_v1'
    print(f'    %base{i} = llvm.mlir.addressof @{symbol} : !llvm.ptr')
    print(f'    %ptr{i} = llvm.getelementptr %base{i}[{offset}] : (!llvm.ptr) -> !llvm.ptr, i8')
    print(f'    %bits{i} = llvm.load %ptr{i} : !llvm.ptr -> i64')
    print(f'    %sum{i} = llvm.xor %{previous}, %bits{i} : i64')
    previous = f'sum{i}'
print(f'    llvm.return %{previous} : i64\n  }}')
print('''  llvm.func @pending4096() -> i64 {
    %dirty = llvm.mlir.addressof @__obelisk_aot_nba_dirty_roots_v1 : !llvm.ptr
    %leaf = llvm.getelementptr %dirty[512] : (!llvm.ptr) -> !llvm.ptr, i8
    %bits = llvm.load %leaf {schedule.eval.pending_dirty_root = 4096 : i32} : !llvm.ptr -> i64
    %one = llvm.mlir.constant(1 : i64) : i64
    %result = llvm.and %bits, %one : i64
    llvm.return %result : i64
  }''')
print('''  llvm.func @__obelisk_eval_dispatch_v1() -> i64 attributes {schedule.eval.call_closure_root} {
    %null = llvm.mlir.zero : !llvm.ptr
    %slot = llvm.mlir.constant(0 : i32) : i32
    %zero = llvm.mlir.constant(0 : i64) : i64''')
for root in roots:
    print(f'    llvm.call @capture{root}() : () -> ()')
print('    %canonical = llvm.call @canonical_only() : () -> i64')
print('    %before = llvm.call @query() : () -> i64')
print(f'    %expectedQuery = llvm.mlir.constant({query_expected} : i64) : i64')
print('    %queryOK = llvm.icmp "eq" %before, %expectedQuery : i64')
print('    %last = llvm.call @pending4096() : () -> i64')
print('    %one64 = llvm.mlir.constant(1 : i64) : i64')
print('    %lastOK = llvm.icmp "eq" %last, %one64 : i64')
if args.finish:
    print('    %one32 = llvm.mlir.constant(1 : i64) : i64')
    print('    %out = llvm.alloca %one32 x i32 : (i64) -> !llvm.ptr')
    print('    %status = llvm.call @obelisk_rt_v1_scheduler_prepare_eval_nba(%null, %out) : (!llvm.ptr, !llvm.ptr) -> i32')
else:
    print('    %status = llvm.call @obelisk_rt_v1_scheduler_execute_aot_actor(%null, %slot) : (!llvm.ptr, i32) -> i32')
print('    %zero32 = llvm.mlir.constant(0 : i32) : i32')
print('    %cutOK = llvm.icmp "eq" %status, %zero32 : i32')
previous = 'zero'
for i, root in enumerate(roots):
    print(f'    %value{i} = llvm.call @consume{root}() : () -> i64')
    print(f'    %sum{i} = llvm.add %{previous}, %value{i} : i64')
    previous = f'sum{i}'
for i, (name, offset) in enumerate(
        [('roots', word * 8) for word in words] + [('summary', page * 8) for page in pages]):
    symbol = '__obelisk_aot_nba_dirty_' + name + '_v1'
    print(f'    %base{i} = llvm.mlir.addressof @{symbol} : !llvm.ptr')
    print(f'    %ptr{i} = llvm.getelementptr %base{i}[{offset}] : (!llvm.ptr) -> !llvm.ptr, i8')
    print(f'    llvm.store %zero, %ptr{i} : i64, !llvm.ptr')
print('''    %after = llvm.call @query() : () -> i64
    %empty = llvm.icmp "eq" %after, %zero : i64
    %validCut = llvm.and %queryOK, %cutOK : i1
    %validLast = llvm.and %validCut, %lastOK : i1
    %valid = llvm.and %validLast, %empty : i1''')
print('    %failure = llvm.mlir.constant(-1 : i64) : i64')
print(f'    %result = llvm.select %valid, %{previous}, %failure : i1, i64')
print('    llvm.return %result : i64\n  }')
print('''  llvm.func @main() -> i32 {
    %first = llvm.call @__obelisk_eval_dispatch_v1() : () -> i64
    %second = llvm.call @__obelisk_eval_dispatch_v1() : () -> i64''')
print(f'    %expected = llvm.mlir.constant({0 if args.finish else expected} : i64) : i64')
print('''    %one = llvm.mlir.constant(1 : i32) : i32
    %zero = llvm.mlir.constant(0 : i32) : i32
    %firstOK = llvm.icmp "eq" %first, %expected : i64
    %secondOK = llvm.icmp "eq" %second, %expected : i64
    %reentryOK = llvm.and %firstOK, %secondOK : i1''')
if not args.finish:
    # These calls are outside the eval closure. Exercise the canonical wrappers
    # for one root in the last sparse word, including a second empty consume.
    print('''    llvm.call @capture4096() : () -> ()
    %pending = llvm.call @pending4096() : () -> i64
    %cold = llvm.call @consume4096() : () -> i64
    %again = llvm.call @consume4096() : () -> i64
    %one64 = llvm.mlir.constant(1 : i64) : i64
    %zero64 = llvm.mlir.constant(0 : i64) : i64''')
    print(f'    %coldExpected = llvm.mlir.constant({len(roots)} : i64) : i64')
    print('''    %pendingOK = llvm.icmp "eq" %pending, %one64 : i64
    %coldOK = llvm.icmp "eq" %cold, %coldExpected : i64
    %emptyOK = llvm.icmp "eq" %again, %zero64 : i64
    %coldCaptureOK = llvm.and %pendingOK, %coldOK : i1
    %coldOnceOK = llvm.and %coldCaptureOK, %emptyOK : i1
    %ok = llvm.and %reentryOK, %coldOnceOK : i1''')
else:
    print('    %ok = llvm.and %firstOK, %secondOK : i1')
print('''    %status = llvm.select %ok, %zero, %one : i1, i32
    llvm.return %status : i32
  }
}''')
