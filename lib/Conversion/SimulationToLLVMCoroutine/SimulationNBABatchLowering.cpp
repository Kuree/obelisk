//===- SimulationNBABatchLowering.cpp - Certified private NBA regions -----===//

#include "SimulationEvalReadySet.h"
#include "mlir/IR/Dominance.h"
#include "mlir/Interfaces/FunctionInterfaces.h"
#include "mlir/Transforms/Mem2Reg.h"
#include "talos/Dialect/Schedule/ScheduleFields.h"
#include "talos/Runtime/Runtime.h"
#include <map>

using namespace mlir;
namespace talos::detail {
namespace {
// Each generated commit uses constant storage offsets and consumer masks.
// Dirty bits predicate bounded runs of roots; no actor runs inside this region.
class BatchLowering {
public:
  explicit BatchLowering(schedule::NativeNBABatchOp op)
      : op(op), layout(op.getLayoutAttr()), b(op), loc(op.getLoc()),
        module(op->getParentOfType<ModuleOp>()), i64(b.getI64Type()),
        i32(b.getI32Type()), ptr(LLVM::LLVMPointerType::get(b.getContext())) {}

  void run() {
    auto function = op->getParentOfType<FunctionOpInterface>();
    Block *entry = op->getBlock();
    done = entry->splitBlock(Block::iterator(op));
    b.setInsertionPointToEnd(entry);
    Value status = llvmConstant(b, loc, i32, TALOS_RT_OK);
    op.getStatus().replaceAllUsesWith(status);
    auto voidType = LLVM::LLVMVoidType::get(b.getContext());
    getOrDeclareLLVMFunction(module, "talos_rt_v1_static_nba_batch_guard",
                             i32, {ptr, ptr, i32});
    getOrDeclareLLVMFunction(module, "talos_rt_v1_static_nba_batch_knownness",
                             voidType, {ptr, i64, i64, i64, i64});
    getOrDeclareLLVMFunction(module, "talos_rt_v1_static_nba_batch_finish",
                             voidType, {ptr, i64, i64, i32});
    Value guard =
        call("talos_rt_v1_static_nba_batch_guard", i32,
             {op.getContext(), address(layout.getPlan()), op.getBarrier()});
    Block *start = block();
    cf::CondBranchOp::create(b, loc, ne(guard, llvmConstant(b, loc, i32, 0)),
                             start, done);
    b.setInsertionPointToStart(start);
    stages = scratch();
    commits = scratch();
    changed = scratch();
    store(c(0), stages);
    store(c(0), commits);
    store(c(0), changed);
    valuePlane = address(layout.getValue());
    unknownPlane = address(layout.getUnknown());
    dirty = address(layout.getDirty());
    summary = address(layout.getSummary());
    std::map<uint32_t, SmallVector<schedule::NativeNBACommitOp>> words;
    for (auto commit : op.getBody().getOps<schedule::NativeNBACommitOp>()) {
      words[commit.getRoot() / 64].push_back(commit);
      for (auto consumer : commit.getLayoutAttr()
                               .getConsumers()
                               .getAsRange<schedule::NBAConsumerAttr>())
        ready.try_emplace(consumer.getBit() / 64);
    }
    for (auto &[word, slot] : ready) {
      slot = scratch();
      store(c(0), slot);
    }
    for (auto &[word, roots] : words) {
      uint64_t eligible = 0;
      for (auto root : roots)
        eligible |= uint64_t{1} << (root.getRoot() % 64);
      Value dirtyAddress = byteGEP(b, loc, dirty, uint64_t(word) * 8);
      Value pending = band(load(dirtyAddress), c(eligible));
      // No admitted root in this word has an update at this barrier. Leave
      // both the leaf and its summary alone, including non-batch owners.
      Block *wordBody = block(), *wordDone = block();
      cf::CondBranchOp::create(b, loc, ne(pending, c(0)), wordBody, wordDone);
      b.setInsertionPointToStart(wordBody);
      Value consumed = scratch();
      store(c(0), consumed);
      // Avoid an indirect branch through a potentially large commit body for
      // every dirty root. Test eight neighboring bits at a time, then visit
      // only that bounded run in ascending root order. Empty runs do no work.
      for (unsigned first = 0; first < 64; first += 8) {
        uint64_t group = eligible & (uint64_t{0xff} << first);
        if (!group)
          continue;
        Block *body = block(), *next = block();
        cf::CondBranchOp::create(b, loc, ne(band(pending, c(group)), c(0)),
                                 body, next);
        b.setInsertionPointToStart(body);
        for (auto root : roots) {
          uint64_t bit = uint64_t{1} << (root.getRoot() % 64);
          if (!(group & bit))
            continue;
          Block *write = block(), *after = block();
          cf::CondBranchOp::create(b, loc, ne(band(pending, c(bit)), c(0)),
                                   write, after);
          b.setInsertionPointToStart(write);
          emitRoot(root.getLayoutAttr());
          // A mixed NBA/Re-NBA accumulator can remain staged after fixed
          // slots commit. Preserve its dirty bit for the later barrier.
          Value retained = c(0);
          if (auto acc = root.getLayoutAttr().getAccumulator()) {
            Value stillPending = load32(nbaFieldGEP(
                b, loc, address(acc),
                offsetof(talos_rt_generated_nba_accumulator_256, valid)));
            // This tests remaining captures after emitRoot, unlike the
            // entry validity test. LRM 4.9.4/10.4.2: consume each capture
            // exactly once, while retaining a later event-region form.
            schedule::set<schedule::Field::EvalPendingDirtyRoot>(
                stillPending.getDefiningOp(),
                b.getI32IntegerAttr(root.getRoot()));
            retained = resize(stillPending, i64);
          }
          store(bor(load(consumed), select(ne(retained, c(0)), c(0), c(bit))),
                consumed);
          cf::BranchOp::create(b, loc, after);
          b.setInsertionPointToStart(after);
        }
        cf::BranchOp::create(b, loc, next);
        b.setInsertionPointToStart(next);
      }
      // No actor or observer callback runs inside a guarded batch. Clear
      // the consumed bits together, preserving other roots and later regions.
      store(band(load(dirtyAddress), bxor(load(consumed), c(UINT64_MAX))),
            dirtyAddress);
      // Clear only this leaf's summary bit, and only if all owners consumed it.
      Value summaryAddress = byteGEP(b, loc, summary, uint64_t(word / 64) * 8);
      Value previous = load(summaryAddress);
      store(select(ne(load(dirtyAddress), c(0)), previous,
                   band(previous, c(~(uint64_t{1} << (word % 64))))),
            summaryAddress);
      cf::BranchOp::create(b, loc, wordDone);
      b.setInsertionPointToStart(wordDone);
    }
    Value pending = c(0);
    if (!ready.empty()) {
      Value ingress = address(layout.getIngress());
      runtime::ReadySetLayout readyLayout(layout.getCapacity());
      for (auto &[word, scratch] : ready) {
        Value mask = load(scratch);
        updateEvalReadyWord(b, loc, ingress, readyLayout, word, mask);
        pending = bor(pending, mask);
      }
    }
    Value wasChanged = load(changed);
    Value prior = load32(op.getChanged());
    LLVM::StoreOp::create(b, loc, bor(prior, resize(ne(wasChanged, c(0)), i32)),
                          op.getChanged(), 4);
    call("talos_rt_v1_static_nba_batch_finish", {},
         {op.getContext(), load(stages), load(commits),
          resize(ne(pending, c(0)), i32)});
    cf::BranchOp::create(b, loc, done);
    op.erase();
    // These slots express region-local accounting and pending consumers, never
    // ABI state. Expose their SSA form now rather than carrying artificial
    // memory traffic through the rest of the MLIR pipeline.
    if (function && !allocations.empty()) {
      b.setInsertionPointToStart(&function.getFunctionBody().front());
      DominanceInfo dominance(function);
      (void)tryToPromoteMemorySlots(allocations, b,
                                    DataLayout::closest(function), dominance);
    }
  }

private:
  Value scratch() {
    Value slot = entryAlloca(b, loc, i64, 1, 8);
    allocations.push_back(
        cast<PromotableAllocationOpInterface>(slot.getDefiningOp()));
    return slot;
  }
  Block *block() {
    auto *result = new Block;
    done->getParent()->getBlocks().insert(Region::iterator(done), result);
    return result;
  }
  Value c(uint64_t n) { return llvmConstant(b, loc, i64, n); }
  Value address(FlatSymbolRefAttr name) {
    return LLVM::AddressOfOp::create(b, loc, ptr, name.getValue());
  }
  Value load(Value addr) { return LLVM::LoadOp::create(b, loc, i64, addr, 8); }
  Value load32(Value addr) {
    return LLVM::LoadOp::create(b, loc, i32, addr, 4);
  }
  void store(Value value, Value addr) {
    LLVM::StoreOp::create(b, loc, value, addr, 8);
  }
  Value band(Value a, Value d) { return arith::AndIOp::create(b, loc, a, d); }
  Value bor(Value a, Value d) { return arith::OrIOp::create(b, loc, a, d); }
  Value bxor(Value a, Value d) { return arith::XOrIOp::create(b, loc, a, d); }
  Value ne(Value a, Value d) {
    return arith::CmpIOp::create(b, loc, arith::CmpIPredicate::ne, a, d);
  }
  Value select(Value p, Value a, Value d) {
    return arith::SelectOp::create(b, loc, p, a, d);
  }
  Value add(Value a, Value d) { return arith::AddIOp::create(b, loc, a, d); }
  Value sub(Value a, Value d) { return arith::SubIOp::create(b, loc, a, d); }
  Value resize(Value value, Type target) {
    auto from = cast<IntegerType>(value.getType()).getWidth();
    auto to = cast<IntegerType>(target).getWidth();
    if (from == to)
      return value;
    return from < to ? Value(LLVM::ZExtOp::create(b, loc, target, value))
                     : Value(LLVM::TruncOp::create(b, loc, target, value));
  }
  Value call(StringRef name, Type type, ValueRange args) {
    auto result =
        LLVM::CallOp::create(b, loc, type ? TypeRange{type} : TypeRange{},
                             SymbolRefAttr::get(b.getContext(), name), args);
    return type ? result.getResult() : Value{};
  }
  void increment(Value ptr, Value n) { store(add(load(ptr), n), ptr); }
  static uint64_t mask(unsigned width) {
    return width == 64 ? UINT64_MAX : (uint64_t{1} << width) - 1;
  }
  Value widthMask(Value width) {
    return arith::ShRUIOp::create(b, loc, c(UINT64_MAX),
                                  band(sub(c(64), width), c(63)));
  }
  // Avoid widening the canonical state or losing adjacent packed bits.
  struct Plane {
    Value address, packed, shift;
    IntegerType type;
  };
  Plane plane(Value base, Value offset, std::optional<uint64_t> fixed,
              unsigned width) {
    Value byte, shift;
    unsigned bits;
    if (fixed) {
      byte = c(*fixed / 8);
      shift = c(*fixed % 8);
      bits = ((*fixed % 8 + width + 7) / 8) * 8;
    } else {
      byte = arith::ShRUIOp::create(b, loc, offset, c(3));
      shift = band(offset, c(7));
      // Native state planes carry eight padding bytes for packed accesses.
      bits = 72;
    }
    auto type = b.getIntegerType(bits);
    Value addr =
        LLVM::GEPOp::create(b, loc, ptr, b.getI8Type(), base, ValueRange{byte});
    Value packed = LLVM::LoadOp::create(b, loc, type, addr, 1);
    return {addr, packed, resize(shift, type), type};
  }
  Value unpack(const Plane &p, Value writeMask) {
    return band(resize(arith::ShRUIOp::create(b, loc, p.packed, p.shift), i64),
                writeMask);
  }
  void pack(const Plane &p, Value value, Value writeMask) {
    Value positioned =
        arith::ShLIOp::create(b, loc, resize(writeMask, p.type), p.shift);
    Value inverse = arith::XOrIOp::create(
        b, loc, positioned,
        LLVM::ConstantOp::create(
            b, loc, p.type,
            IntegerAttr::get(p.type, APInt::getAllOnes(p.type.getWidth()))));
    Value next =
        bor(band(p.packed, inverse),
            arith::ShLIOp::create(
                b, loc, resize(band(value, writeMask), p.type), p.shift));
    LLVM::StoreOp::create(b, loc, next, p.address, 1);
  }
  Value commitChunk(Value offset, std::optional<uint64_t> fixed,
                    unsigned maxWidth, Value width, Value writeMask,
                    Value value, Value unknown, Value transient) {
    Plane vp = plane(valuePlane, offset, fixed, maxWidth);
    Plane up = plane(unknownPlane, offset, fixed, maxWidth);
    Value oldValue = unpack(vp, writeMask), oldUnknown = unpack(up, writeMask);
    Value newValue = band(value, writeMask),
          newUnknown = band(unknown, writeMask);
    Value unknownChange = bxor(oldUnknown, newUnknown);
    Value difference = bor(bxor(oldValue, newValue), unknownChange);
    Value observed = bor(difference, band(transient, writeMask));
    store(bor(load(changed), observed), changed);
    Block *write = block(), *after = block();
    cf::CondBranchOp::create(b, loc, ne(difference, c(0)), write, after);
    b.setInsertionPointToStart(write);
    pack(vp, newValue, writeMask);
    pack(up, newUnknown, writeMask);
    Block *notify = block();
    cf::CondBranchOp::create(b, loc, ne(unknownChange, c(0)), notify, after);
    b.setInsertionPointToStart(notify);
    call("talos_rt_v1_static_nba_batch_knownness", {},
         {op.getContext(), offset, width, oldUnknown, newUnknown});
    cf::BranchOp::create(b, loc, after);
    b.setInsertionPointToStart(after);
    return observed;
  }
  void activate(schedule::NBACommitLayoutAttr root, Value local, Value width,
                Value observed, std::optional<uint64_t> fixed = {}) {
    // Fold consumers sharing one output word and one watched range. LLVM can
    // then union constant destinations without repeated ReadySet maintenance.
    std::map<std::tuple<uint64_t, uint64_t, uint32_t>, uint64_t> groups;
    for (auto consumer :
         root.getConsumers().getAsRange<schedule::NBAConsumerAttr>())
      groups[{consumer.getLow(), consumer.getWidth(),
              consumer.getBit() / 64}] |= uint64_t{1}
                                          << (consumer.getBit() % 64);
    for (auto &[key, bits] : groups) {
      auto [low, count, word] = key;
      Value selected;
      if (fixed) {
        uint64_t first = std::max(low, *fixed),
                 last = std::min(low + count, *fixed + 64);
        if (first >= last)
          continue;
        selected = band(observed, c(mask(last - first) << (first - *fixed)));
      } else {
        Value first = arith::MaxUIOp::create(b, loc, local, c(low));
        Value last =
            arith::MinUIOp::create(b, loc, add(local, width), c(low + count));
        Value overlap = arith::CmpIOp::create(b, loc, arith::CmpIPredicate::ugt,
                                              last, first);
        Value shifted = arith::ShRUIOp::create(b, loc, observed,
                                               band(sub(first, local), c(63)));
        selected =
            select(overlap, band(shifted, widthMask(sub(last, first))), c(0));
      }
      Value scratch = ready.at(word);
      store(bor(load(scratch), select(ne(selected, c(0)), c(bits), c(0))),
            scratch);
    }
  }
  void emitRoot(schedule::NBACommitLayoutAttr root) {
    for (auto slot :
         root.getSlots().getAsRange<schedule::NBASlotLayoutAttr>()) {
      Value valid = address(slot.getValid());
      Block *pending = block(), *next = block();
      cf::CondBranchOp::create(b, loc,
                               ne(load32(valid), llvmConstant(b, loc, i32, 0)),
                               pending, next);
      b.setInsertionPointToStart(pending);
      LLVM::StoreOp::create(b, loc, llvmConstant(b, loc, i32, 0), valid, 4);
      if (auto fixed = slot.getConstantOffset()) {
        uint64_t local = fixed.getValue().getZExtValue();
        uint64_t offset = root.getOffset() + local;
        Value observed = commitChunk(
            c(offset), offset, slot.getWidth(), c(slot.getWidth()),
            c(mask(slot.getWidth())), load(address(slot.getValue())),
            load(address(slot.getUnknown())), c(0));
        activate(root, c(local), c(slot.getWidth()), observed, local);
        increment(stages, c(1));
        increment(commits, c(1));
        cf::BranchOp::create(b, loc, next);
        b.setInsertionPointToStart(next);
        continue;
      }
      Value requested = load(address(slot.getOffset()));
      Value negative = arith::CmpIOp::create(b, loc, arith::CmpIPredicate::slt,
                                             requested, c(0));
      Value source = select(negative, sub(c(0), requested), c(0));
      Value destination = select(negative, c(0), requested);
      Value inRange =
          band(arith::CmpIOp::create(b, loc, arith::CmpIPredicate::ult, source,
                                     c(slot.getWidth())),
               arith::CmpIOp::create(b, loc, arith::CmpIPredicate::ult,
                                     destination, c(root.getWidth())));
      Block *write = block();
      cf::CondBranchOp::create(b, loc, inRange, write, next);
      b.setInsertionPointToStart(write);
      Value width =
          arith::MinUIOp::create(b, loc, sub(c(slot.getWidth()), source),
                                 sub(c(root.getWidth()), destination));
      Value value = arith::ShRUIOp::create(
          b, loc, load(address(slot.getValue())), source);
      Value unknown = arith::ShRUIOp::create(
          b, loc, load(address(slot.getUnknown())), source);
      Value offset = add(c(root.getOffset()), destination);
      Value observed = commitChunk(offset, {}, slot.getWidth(), width,
                                   widthMask(width), value, unknown, c(0));
      activate(root, destination, width, observed);
      increment(stages, c(1));
      increment(commits, c(1));
      cf::BranchOp::create(b, loc, next);
      b.setInsertionPointToStart(next);
    }
    if (auto symbol = root.getAccumulator()) {
      Value acc = address(symbol);
      auto field = [&](uint64_t offset) {
        return nbaFieldGEP(b, loc, acc, offset);
      };
      Value valid = load32(
          field(offsetof(talos_rt_generated_nba_accumulator_256, valid)));
      Value region = load32(field(
          offsetof(talos_rt_generated_nba_accumulator_256, exec_region)));
      Value selected =
          band(ne(valid, llvmConstant(b, loc, i32, 0)),
               arith::CmpIOp::create(b, loc, arith::CmpIPredicate::eq, region,
                                     op.getBarrier()));
      Block *write = block(), *next = block();
      cf::CondBranchOp::create(b, loc, selected, write, next);
      b.setInsertionPointToStart(write);
      for (uint64_t local = 0; local < root.getWidth(); local += 64) {
        unsigned width = std::min<uint64_t>(64, root.getWidth() - local);
        uint64_t word = local / 64;
        Value maskAddr = field(
            offsetof(talos_rt_generated_nba_accumulator_256, write_mask) +
            word * 8);
        Value transAddr = field(
            offsetof(talos_rt_generated_nba_accumulator_256, transient) +
            word * 8);
        Value writeMask = band(load(maskAddr), c(mask(width)));
        Value value = load(
            field(offsetof(talos_rt_generated_nba_accumulator_256, value) +
                  word * 8));
        Value unknown = load(
            field(offsetof(talos_rt_generated_nba_accumulator_256, unknown) +
                  word * 8));
        Value observed = commitChunk(
            c(root.getOffset() + local), root.getOffset() + local, width,
            c(width), writeMask, value, unknown, load(transAddr));
        activate(root, c(local), c(width), observed, local);
        store(c(0), maskAddr);
        store(c(0), transAddr);
        if (root.getWidth() > 64) {
          increment(stages,
                    resize(ne(band(writeMask, c(UINT32_MAX)), c(0)), i64));
          increment(
              stages,
              resize(ne(arith::ShRUIOp::create(b, loc, writeMask, c(32)), c(0)),
                     i64));
        }
      }
      if (root.getWidth() <= 64)
        increment(stages, c(1));
      increment(commits, c(1));
      LLVM::StoreOp::create(
          b, loc, llvmConstant(b, loc, i32, 0),
          field(offsetof(talos_rt_generated_nba_accumulator_256, valid)), 4);
      cf::BranchOp::create(b, loc, next);
      b.setInsertionPointToStart(next);
    }
  }
  schedule::NativeNBABatchOp op;
  schedule::NBABatchLayoutAttr layout;
  OpBuilder b;
  Location loc;
  ModuleOp module;
  IntegerType i64, i32;
  LLVM::LLVMPointerType ptr;
  Block *done = nullptr;
  Value valuePlane, unknownPlane, dirty, summary, stages, commits, changed;
  std::map<uint32_t, Value> ready;
  SmallVector<PromotableAllocationOpInterface> allocations;
};
} // namespace
void lowerSpecializedNBABatch(schedule::NativeNBABatchOp op) {
  BatchLowering(op).run();
}
} // namespace talos::detail
