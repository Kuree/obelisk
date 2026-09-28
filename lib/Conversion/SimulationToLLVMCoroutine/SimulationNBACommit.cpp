//===- SimulationNBACommit.cpp - Table-driven scalar NBA commits ----------===//

#include "SimulationAOTPlanning.h"
#include "SimulationToLLVMCoroutinePrivate.h"
#include "obelisk/Dialect/Schedule/ScheduleFields.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"

using namespace mlir;

namespace obelisk::detail {

void emitScalarNBACommitLoop(OpBuilder &b, ModuleOp module,
                             LLVM::LLVMFuncOp function, Block *exit,
                             Value region, const NativeStaticNBAPlan &plan,
                             ArrayRef<SmallVector<uint32_t>> rootsByWord,
                             ArrayRef<obelisk_rt_static_fanout_entry> fanout,
                             Value activatedNodes, Value activatedDirect,
                             unsigned directWords) {
  auto loc = function.getLoc();
  auto *context = b.getContext();
  auto i32 = b.getI32Type(), i64 = b.getI64Type();
  auto ptr = LLVM::LLVMPointerType::get(context);
  auto c = [&](uint64_t n) -> Value { return llvmConstant(b, loc, i64, n); };
  auto block = [&]() {
    auto *result = new Block;
    function.getBody().getBlocks().insert(Region::iterator(exit), result);
    return result;
  };
  auto address = [&](StringRef name) -> Value {
    return LLVM::AddressOfOp::create(b, loc, ptr, name);
  };
  auto gep = [&](Value base, Type type, Value index) -> Value {
    return LLVM::GEPOp::create(b, loc, ptr, type, base, ValueRange{index});
  };
  auto table = [&](StringRef suffix, ArrayRef<uint64_t> data) {
    OpBuilder::InsertionGuard guard(b);
    b.setInsertionPointToStart(module.getBody());
    std::string name = ("__obelisk_eval_nba_commit_" + suffix).str();
    auto tensor = RankedTensorType::get({int64_t(data.size())}, i64);
    LLVM::GlobalOp::create(b, loc, LLVM::LLVMArrayType::get(i64, data.size()),
                           true, LLVM::Linkage::Internal, name,
                           DenseIntElementsAttr::get(tensor, data), 8);
    return name;
  };
  auto load = [&](StringRef name, Value index) -> Value {
    return LLVM::LoadOp::create(b, loc, i64, gep(address(name), i64, index), 8);
  };
  auto band = [&](Value a, Value d) -> Value {
    return arith::AndIOp::create(b, loc, a, d);
  };
  auto bor = [&](Value a, Value d) -> Value {
    return arith::OrIOp::create(b, loc, a, d);
  };
  auto inv = [&](Value a) -> Value {
    return arith::XOrIOp::create(b, loc, a, c(UINT64_MAX));
  };
  auto cmp = [&](arith::CmpIPredicate predicate, Value a, Value d) -> Value {
    return arith::CmpIOp::create(b, loc, predicate, a, d);
  };
  auto eq = [&](Value a, Value d) {
    return cmp(arith::CmpIPredicate::eq, a, d);
  };
  auto select = [&](Value condition, Value yes, Value no) -> Value {
    return arith::SelectOp::create(b, loc, condition, yes, no);
  };

  // IEEE 1800-2023 4.5, 4.6, 10.4.2: only the existing merge-safe scalar
  // roots use this path. Visit their dirty bits in the same root order and
  // publish activation after the barrier. Ordered records drain separately.
  size_t count = plan.roots.size();
  SmallVector<uint64_t> masks(rootsByWord.size()), offsets(count),
      widths(count), shifts(count), classes(count), regions(count),
      fixedMasks(count), clear(count), transients(count), starts(count),
      ends(count);
  SmallVector<uint64_t> watchMasks, edges, destinations, direct;
  for (auto [word, roots] : llvm::enumerate(rootsByWord))
    for (uint32_t index : roots) {
      const auto &root = plan.roots[index];
      masks[word] |= uint64_t{1} << (index % 64);
      offsets[index] = plan.generatedOffsets[index] / 8;
      shifts[index] = plan.generatedOffsets[index] % 8;
      widths[index] = root.bit_width == 64
                          ? UINT64_MAX
                          : (uint64_t{1} << root.bit_width) - 1;
      unsigned bits = shifts[index] + root.bit_width;
      classes[index] = bits <= 8    ? 8
                       : bits <= 16 ? 16
                       : bits <= 32 ? 32
                       : bits <= 64 ? 64
                                    : 72;
      regions[index] = plan.generatedCommitRegions[index];
      fixedMasks[index] = plan.generatedFixedWriteMasks[index];
      clear[index] = !fixedMasks[index] && !plan.generatedFullRootStages[index];
      transients[index] = plan.trackTransients[index] ? widths[index] : 0;
      starts[index] = watchMasks.size();
      for (const auto &entry : fanout) {
        if (entry.static_state != root.static_state ||
            entry.low_bit >= root.bit_width)
          continue;
        uint64_t high =
            std::min(root.bit_width, entry.low_bit + entry.bit_width);
        if (entry.low_bit >= high)
          continue;
        uint64_t width = high - entry.low_bit;
        watchMasks.push_back(
            (width == 64 ? UINT64_MAX : (uint64_t{1} << width) - 1)
            << entry.low_bit);
        edges.push_back(entry.edge);
        bool isDirect = (entry.reserved & OBELISK_RT_FANOUT_ROUTE_MASK) ==
                            OBELISK_RT_FANOUT_DIRECT &&
                        entry.merged_bit < uint64_t(directWords) * 64;
        direct.push_back(isDirect);
        destinations.push_back(isDirect ? entry.merged_bit
                                        : entry.compute_node);
      }
      ends[index] = watchMasks.size();
    }
  auto maskTable = table("masks_v1", masks);
  auto offsetTable = table("offsets_v1", offsets);
  auto widthTable = table("widths_v1", widths);
  auto shiftTable = table("shifts_v1", shifts);
  auto classTable = table("classes_v1", classes);
  auto regionTable = table("regions_v1", regions);
  auto fixedTable = table("fixed_masks_v1", fixedMasks);
  auto clearTable = table("clear_v1", clear);
  auto transientTable = table("transients_v1", transients);
  auto startTable = table("starts_v1", starts);
  auto endTable = table("ends_v1", ends);
  auto watchTable = table("watch_masks_v1", watchMasks);
  auto edgeTable = table("edges_v1", edges);
  auto destinationTable = table("destinations_v1", destinations);
  auto directTable = table("direct_v1", direct);
  // Accumulators are also consumed by runtime handoffs. Keep their ABI storage
  // and use a constant pointer column instead of introducing a shadow payload.
  std::string accumulatorTable = "__obelisk_eval_nba_commit_accumulators_v1";
  {
    OpBuilder::InsertionGuard guard(b);
    b.setInsertionPointToStart(module.getBody());
    auto type = LLVM::LLVMArrayType::get(ptr, count);
    auto global =
        LLVM::GlobalOp::create(b, loc, type, true, LLVM::Linkage::Internal,
                               accumulatorTable, Attribute{});
    auto *init = new Block;
    global.getInitializerRegion().push_back(init);
    b.setInsertionPointToStart(init);
    Value value = LLVM::ZeroOp::create(b, loc, type);
    for (auto [word, roots] : llvm::enumerate(rootsByWord))
      for (uint32_t index : roots)
        value = LLVM::InsertValueOp::create(
            b, loc, value, address(plan.generatedAccumulators[index]),
            ArrayRef<int64_t>{index});
    LLVM::ReturnOp::create(b, loc, value);
  }

  auto *wordHead = block(), *wordLoad = block(), *rootHead = block(),
       *rootLoad = block(), *advanceWord = block(), *commit = block(),
       *publish = block(), *reset = block(), *watchHead = block(),
       *watchBody = block(), *watchStore = block(), *watchNext = block();
  wordHead->addArgument(i64, loc);
  rootHead->addArgument(i64, loc);
  watchHead->addArgument(i64, loc);
  for (unsigned i = 0; i < 4; ++i)
    publish->addArgument(i64, loc);
  cf::BranchOp::create(b, loc, wordHead, ValueRange{c(0)});
  b.setInsertionPointToStart(wordHead);
  Value word = wordHead->getArgument(0);
  cf::CondBranchOp::create(
      b, loc, cmp(arith::CmpIPredicate::ult, word, c(masks.size())), wordLoad,
      ValueRange{}, exit, ValueRange{});
  b.setInsertionPointToStart(wordLoad);
  Value dirtyAddress =
      gep(address("__obelisk_aot_nba_dirty_roots_v1"), i64, word);
  Value dirty = LLVM::LoadOp::create(b, loc, i64, dirtyAddress, 8);
  Value selectedMask = load(maskTable, word);
  // Match the existing barrier's ownership of words containing scalar roots.
  LLVM::StoreOp::create(b, loc, select(eq(selectedMask, c(0)), dirty, c(0)),
                        dirtyAddress, 8);
  cf::BranchOp::create(b, loc, rootHead, ValueRange{band(dirty, selectedMask)});
  b.setInsertionPointToStart(rootHead);
  Value remaining = rootHead->getArgument(0);
  cf::CondBranchOp::create(b, loc, eq(remaining, c(0)), advanceWord,
                           ValueRange{}, rootLoad, ValueRange{});
  b.setInsertionPointToStart(advanceWord);
  cf::BranchOp::create(b, loc, wordHead,
                       ValueRange{arith::AddIOp::create(b, loc, word, c(1))});
  b.setInsertionPointToStart(rootLoad);
  Value bit = LLVM::CountTrailingZerosOp::create(b, loc, i64, remaining, true);
  Value index = arith::AddIOp::create(
      b, loc, arith::ShLIOp::create(b, loc, word, c(6)), bit);
  Value rest = band(remaining, arith::SubIOp::create(b, loc, remaining, c(1)));
  Value accumulator = LLVM::LoadOp::create(
      b, loc, ptr, gep(address(accumulatorTable), ptr, index));
  auto field = [&](uint64_t offset) -> Value {
    return byteGEP(b, loc, accumulator, offset);
  };
  auto field64 = [&](uint64_t offset) -> Value {
    return LLVM::LoadOp::create(b, loc, i64, field(offset), 8);
  };
  auto region64 = LLVM::ZExtOp::create(b, loc, i64, region);
  Value fixedRegion = load(regionTable, index);
  Value storedRegion = LLVM::ZExtOp::create(
      b, loc, i64,
      LLVM::LoadOp::create(
          b, loc, i32,
          field(
              offsetof(obelisk_rt_generated_nba_accumulator_256, exec_region)),
          4));
  Value valid = LLVM::LoadOp::create(
      b, loc, i32,
      field(offsetof(obelisk_rt_generated_nba_accumulator_256, valid)), 4);
  Value regionMatches = select(
      eq(fixedRegion, c(UINT32_MAX)),
      band(eq(storedRegion, region64),
           cmp(arith::CmpIPredicate::ne, valid, llvmConstant(b, loc, i32, 0))),
      eq(fixedRegion, region64));
  cf::CondBranchOp::create(b, loc, regionMatches, commit, ValueRange{},
                           rootHead, ValueRange{rest});
  b.setInsertionPointToStart(commit);
  Value offset = load(offsetTable, index), shift = load(shiftTable, index),
        widthMask = load(widthTable, index),
        fixedMask = load(fixedTable, index);
  Value writeMask =
      select(eq(fixedMask, c(0)),
             band(field64(offsetof(obelisk_rt_generated_nba_accumulator_256,
                                   write_mask)),
                  widthMask),
             fixedMask);
  Value stagedValue =
      field64(offsetof(obelisk_rt_generated_nba_accumulator_256, value));
  auto unknownLoad = LLVM::LoadOp::create(
      b, loc, i64,
      field(offsetof(obelisk_rt_generated_nba_accumulator_256, unknown)), 8);
  schedule::set<schedule::Field::EvalTwoStateZeroUnknown>(unknownLoad,
                                                          b.getUnitAttr());
  Value stagedUnknown = unknownLoad;
  Value valueAddress =
      gep(address("__obelisk_state_value"), b.getI8Type(), offset);
  Value unknownAddress =
      gep(address("__obelisk_state_unknown"), b.getI8Type(), offset);
  SmallVector<APInt> cases;
  SmallVector<Block *> bodies;
  SmallVector<ValueRange> operands;
  constexpr unsigned storageWidths[] = {8, 16, 32, 64, 72};
  for (unsigned bits : storageWidths) {
    cases.emplace_back(64, bits);
    bodies.push_back(block());
    operands.push_back({});
  }
  LLVM::SwitchOp::create(b, loc, load(classTable, index), bodies.back(),
                         ValueRange{}, cases, bodies, operands);
  for (auto [ordinal, bits] : llvm::enumerate(storageWidths)) {
    b.setInsertionPointToStart(bodies[ordinal]);
    auto type = b.getIntegerType(bits);
    auto resize = [&](Value value, Type target) -> Value {
      unsigned from = cast<IntegerType>(value.getType()).getWidth();
      unsigned to = cast<IntegerType>(target).getWidth();
      if (from == to)
        return value;
      if (from < to)
        return LLVM::ZExtOp::create(b, loc, target, value);
      return LLVM::TruncOp::create(b, loc, target, value);
    };
    Value oldRaw = LLVM::LoadOp::create(b, loc, type, valueAddress, 1);
    Value unknownRaw = LLVM::LoadOp::create(b, loc, type, unknownAddress, 1);
    Value typedShift = resize(shift, type);
    auto extract = [&](Value raw) {
      return band(resize(arith::ShRUIOp::create(b, loc, raw, typedShift), i64),
                  widthMask);
    };
    Value oldValue = extract(oldRaw), oldUnknown = extract(unknownRaw);
    Value newValue =
        bor(band(oldValue, inv(writeMask)), band(stagedValue, writeMask));
    Value newUnknown =
        bor(band(oldUnknown, inv(writeMask)), band(stagedUnknown, writeMask));
    Value mask =
        arith::ShLIOp::create(b, loc, resize(widthMask, type), typedShift);
    auto store = [&](Value raw, Value value, Value destination) {
      Value keep = arith::XOrIOp::create(
          b, loc, mask,
          LLVM::ConstantOp::create(
              b, loc, type, b.getIntegerAttr(type, APInt::getAllOnes(bits))));
      Value positioned =
          arith::ShLIOp::create(b, loc, resize(value, type), typedShift);
      LLVM::StoreOp::create(
          b, loc, bor(band(raw, keep), band(positioned, mask)), destination, 1);
    };
    store(oldRaw, newValue, valueAddress);
    store(unknownRaw, newUnknown, unknownAddress);
    cf::BranchOp::create(
        b, loc, publish,
        ValueRange{oldValue, oldUnknown, newValue, newUnknown});
  }
  b.setInsertionPointToStart(publish);
  Value oldValue = publish->getArgument(0),
        oldUnknown = publish->getArgument(1),
        newValue = publish->getArgument(2),
        newUnknown = publish->getArgument(3);
  Value transientAddress =
      field(offsetof(obelisk_rt_generated_nba_accumulator_256, transient));
  Value transient = LLVM::LoadOp::create(b, loc, i64, transientAddress, 8);
  Value changed =
      bor(bor(arith::XOrIOp::create(b, loc, oldValue, newValue),
              arith::XOrIOp::create(b, loc, oldUnknown, newUnknown)),
          band(transient, load(transientTable, index)));
  LLVM::StoreOp::create(b, loc, c(0), transientAddress, 8);
  Value oldZero = band(inv(oldUnknown), inv(oldValue)),
        oldOne = band(inv(oldUnknown), oldValue),
        newZero = band(inv(newUnknown), inv(newValue)),
        newOne = band(inv(newUnknown), newValue);
  Value posedge = bor(band(oldZero, inv(newZero)), band(oldUnknown, newOne));
  Value negedge = bor(band(oldOne, inv(newOne)), band(oldUnknown, newZero));
  Value start = load(startTable, index), end = load(endTable, index);
  cf::CondBranchOp::create(b, loc, eq(load(clearTable, index), c(0)), watchHead,
                           ValueRange{start}, reset, ValueRange{});
  b.setInsertionPointToStart(reset);
  LLVM::StoreOp::create(
      b, loc, c(0),
      field(offsetof(obelisk_rt_generated_nba_accumulator_256, write_mask)), 8);
  LLVM::StoreOp::create(
      b, loc, llvmConstant(b, loc, i32, 0),
      field(offsetof(obelisk_rt_generated_nba_accumulator_256, valid)), 4);
  cf::BranchOp::create(b, loc, watchHead, ValueRange{start});
  b.setInsertionPointToStart(watchHead);
  Value watcher = watchHead->getArgument(0);
  cf::CondBranchOp::create(b, loc, cmp(arith::CmpIPredicate::ult, watcher, end),
                           watchBody, ValueRange{}, rootHead, ValueRange{rest});
  b.setInsertionPointToStart(watchBody);
  Value edge = load(edgeTable, watcher);
  Value observed =
      select(eq(edge, c(OBELISK_RT_WAIT_EDGE_POSEDGE)), posedge,
             select(eq(edge, c(OBELISK_RT_WAIT_EDGE_NEGEDGE)), negedge,
                    select(eq(edge, c(OBELISK_RT_WAIT_EDGE_BOTH)),
                           bor(posedge, negedge), changed)));
  cf::CondBranchOp::create(b, loc,
                           eq(band(observed, load(watchTable, watcher)), c(0)),
                           watchNext, ValueRange{}, watchStore, ValueRange{});
  b.setInsertionPointToStart(watchStore);
  Value destination = load(destinationTable, watcher);
  Value target;
  if (activatedNodes && activatedDirect)
    target = select(eq(load(directTable, watcher), c(0)), activatedNodes,
                    activatedDirect);
  else
    target = activatedDirect ? activatedDirect : activatedNodes;
  if (target) {
    Value at =
        gep(target, i64, arith::ShRUIOp::create(b, loc, destination, c(6)));
    Value previous = LLVM::LoadOp::create(b, loc, i64, at, 8);
    Value bitMask =
        arith::ShLIOp::create(b, loc, c(1), band(destination, c(63)));
    LLVM::StoreOp::create(b, loc, bor(previous, bitMask), at, 8);
  }
  cf::BranchOp::create(b, loc, watchNext);
  b.setInsertionPointToStart(watchNext);
  cf::BranchOp::create(
      b, loc, watchHead,
      ValueRange{arith::AddIOp::create(b, loc, watcher, c(1))});
}
} // namespace obelisk::detail
