//===- SimulationPromotionReadiness.cpp - Shared knownness scan ----------===//

#include "SimulationAOTPlanning.h"
#include "SimulationToLLVMCoroutinePrivate.h"
#include "talos/Conversion/Passes.h"
#include "talos/Dialect/Schedule/ScheduleFields.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Pass/Pass.h"
#include "llvm/ADT/STLExtras.h"

using namespace mlir;

namespace talos {
#define GEN_PASS_DEF_MATERIALIZENATIVEGROUPPROMOTIONREADINESSPASS
#include "talos/Conversion/Passes.h.inc"
} // namespace talos

namespace talos::detail {

LogicalResult materializeNativeRoutePromotionScan(
    ModuleOp module, LLVM::LLVMFuncOp scanner, uint64_t stateBits,
    ArrayRef<NativeRoutePromotion> routes, StringRef pendingName,
    StringRef dirtyName) {
  OpBuilder builder(module.getContext());
  Location loc = scanner.getLoc();
  Type i1 = builder.getI1Type(), i8 = builder.getI8Type(),
       i64 = builder.getI64Type();
  Type ptr = LLVM::LLVMPointerType::get(module.getContext());
  SmallVector<uint64_t> scans, starts, ends;
  for (const auto &route : routes) {
    SmallVector<std::pair<uint64_t, uint64_t>> intervals;
    for (auto range : route.ranges) {
      if (!range.bitWidth || range.bitOffset > stateBits ||
          range.bitWidth > stateBits - range.bitOffset)
        return module.emitError("route promotion range exceeds native state");
      intervals.emplace_back(range.bitOffset, range.bitOffset + range.bitWidth);
    }
    llvm::sort(intervals);
    SmallVector<std::pair<uint64_t, uint64_t>> merged;
    for (auto [begin, end] : intervals) {
      if (!merged.empty() && begin <= merged.back().second)
        merged.back().second = std::max(merged.back().second, end);
      else
        merged.emplace_back(begin, end);
    }
    starts.push_back(scans.size() / 4);
    for (auto [begin, end] : merged)
      llvm::append_range(
          scans,
          ArrayRef<uint64_t>{begin / 8, end / 8 + (end % 8 != 0),
                             (UINT64_C(255) << (begin % 8)) & 255,
                             end % 8 ? (uint64_t{1} << (end % 8)) - 1 : 255});
    ends.push_back(scans.size() / 4);
  }
  auto constant = [&](uint64_t n) {
    return llvmConstant(builder, loc, i64, n);
  };
  auto address = [&](StringRef name) -> Value {
    return LLVM::AddressOfOp::create(builder, loc, ptr, name);
  };
  auto gep = [&](Value base, Type type, Value index) -> Value {
    return LLVM::GEPOp::create(builder, loc, ptr, type, base,
                               ArrayRef<LLVM::GEPArg>{index});
  };
  constexpr StringLiteral ownerName =
      "__talos_eval_route_promotion_owners_v1";
  constexpr StringLiteral rangeName =
      "__talos_eval_route_promotion_ranges_v1";
  auto ownerType =
      LLVM::LLVMStructType::getLiteral(module.getContext(), {i64, i64, ptr});
  auto ownerArray = LLVM::LLVMArrayType::get(ownerType, routes.size());
  builder.setInsertionPointToStart(module.getBody());
  auto ownerTable = LLVM::GlobalOp::create(builder, loc, ownerArray, true,
                                           LLVM::Linkage::Internal, ownerName,
                                           Attribute{}, 8);
  ownerTable.getInitializerRegion().push_back(new Block);
  builder.setInsertionPointToStart(&ownerTable.getInitializerRegion().front());
  Value owners = LLVM::ZeroOp::create(builder, loc, ownerArray);
  for (auto [index, route] : llvm::enumerate(routes)) {
    Value row = LLVM::ZeroOp::create(builder, loc, ownerType);
    row = LLVM::InsertValueOp::create(
        builder, loc, row, constant(starts[index]), ArrayRef<int64_t>{0});
    row = LLVM::InsertValueOp::create(builder, loc, row, constant(ends[index]),
                                      ArrayRef<int64_t>{1});
    if (!route.selector.empty())
      row = LLVM::InsertValueOp::create(
          builder, loc, row, address(route.selector), ArrayRef<int64_t>{2});
    owners = LLVM::InsertValueOp::create(builder, loc, owners, row,
                                         ArrayRef<int64_t>{int64_t(index)});
  }
  LLVM::ReturnOp::create(builder, loc, owners);
  builder.setInsertionPointToStart(module.getBody());
  auto tensor = RankedTensorType::get({int64_t(scans.size())}, i64);
  LLVM::GlobalOp::create(builder, loc,
                         LLVM::LLVMArrayType::get(i64, scans.size()), true,
                         LLVM::Linkage::Internal, rangeName,
                         DenseIntElementsAttr::get(tensor, scans), 8);

  Block *entry = scanner.addEntryBlock(builder);
  auto block = [&](ArrayRef<Type> arguments = {}) {
    auto *result = new Block;
    for (Type type : arguments)
      result->addArgument(type, loc);
    scanner.getBody().push_back(result);
    return result;
  };
  Block *nextWord = block({i64}), *readWord = block();
  Block *nextBit = block({i64}), *inspect = block(), *owner = block();
  Block *nextRange = block({i64}), *readRange = block();
  Block *nextByte = block({i64}), *advanceByte = block();
  Block *advanceRange = block(), *known = block(), *unknown = block();
  Block *publish = block({i1}), *advanceWord = block(), *done = block();
  auto branch = [&](Block *dest, ValueRange values) {
    return LLVM::BrOp::create(builder, loc, values, dest);
  };
  auto condBranch = [&](Value condition, Block *yes, ValueRange yesValues,
                        Block *no, ValueRange noValues) {
    return LLVM::CondBrOp::create(builder, loc, condition, yes, yesValues, no,
                                  noValues);
  };
  builder.setInsertionPointToStart(entry);
  Value pendingBase = address(pendingName), ownerBase = address(ownerName),
        rangeBase = address(rangeName),
        state = address("__talos_state_unknown");
  if (!dirtyName.empty())
    LLVM::StoreOp::create(builder, loc, llvmConstant(builder, loc, i8, 0),
                          address(dirtyName), 1);
  branch(nextWord, ValueRange{constant(0)});
  builder.setInsertionPointToStart(nextWord);
  Value word = nextWord->getArgument(0);
  Value finished =
      LLVM::ICmpOp::create(builder, loc, LLVM::ICmpPredicate::eq, word,
                           constant((routes.size() + 63) / 64));
  condBranch(finished, done, ValueRange{}, readWord, ValueRange{});
  builder.setInsertionPointToStart(readWord);
  Value pendingAddress = gep(pendingBase, i64, word);
  Value pending = LLVM::LoadOp::create(builder, loc, i64, pendingAddress, 8);
  // This scan executes no callbacks or actors. Snapshot and consume each word
  // once; subsequent mutations enqueue their own exact proof dependencies.
  LLVM::StoreOp::create(builder, loc, constant(0), pendingAddress, 8);
  branch(nextBit, ValueRange{pending});
  builder.setInsertionPointToStart(nextBit);
  Value remaining = nextBit->getArgument(0);
  Value empty = LLVM::ICmpOp::create(builder, loc, LLVM::ICmpPredicate::eq,
                                     remaining, constant(0));
  condBranch(empty, advanceWord, ValueRange{}, inspect, ValueRange{});
  builder.setInsertionPointToStart(inspect);
  Value tail = LLVM::AndOp::create(
      builder, loc, remaining,
      LLVM::SubOp::create(builder, loc, remaining, constant(1)));
  Value bit =
      LLVM::CountTrailingZerosOp::create(builder, loc, i64, remaining, true);
  Value id = LLVM::AddOp::create(
      builder, loc, LLVM::MulOp::create(builder, loc, word, constant(64)), bit);
  Value valid = LLVM::ICmpOp::create(builder, loc, LLVM::ICmpPredicate::ult, id,
                                     constant(routes.size()));
  condBranch(valid, owner, ValueRange{}, nextBit, ValueRange{tail});
  builder.setInsertionPointToStart(owner);
  Value row = LLVM::LoadOp::create(builder, loc, ownerType,
                                   gep(ownerBase, ownerType, id), 8);
  Value first =
      LLVM::ExtractValueOp::create(builder, loc, row, ArrayRef<int64_t>{0});
  Value end =
      LLVM::ExtractValueOp::create(builder, loc, row, ArrayRef<int64_t>{1});
  Value selector =
      LLVM::ExtractValueOp::create(builder, loc, row, ArrayRef<int64_t>{2});
  Value present =
      LLVM::ICmpOp::create(builder, loc, LLVM::ICmpPredicate::ne, selector,
                           LLVM::ZeroOp::create(builder, loc, ptr));
  condBranch(present, nextRange, ValueRange{first}, nextBit, ValueRange{tail});
  builder.setInsertionPointToStart(nextRange);
  Value range = nextRange->getArgument(0);
  Value allKnown =
      LLVM::ICmpOp::create(builder, loc, LLVM::ICmpPredicate::eq, range, end);
  condBranch(allKnown, known, ValueRange{}, readRange, ValueRange{});
  builder.setInsertionPointToStart(readRange);
  auto field = [&](uint64_t n) -> Value {
    Value index = LLVM::AddOp::create(
        builder, loc, LLVM::MulOp::create(builder, loc, range, constant(4)),
        constant(n));
    return LLVM::LoadOp::create(builder, loc, i64, gep(rangeBase, i64, index),
                                8);
  };
  Value beginByte = field(0), endByte = field(1);
  Value firstMask = LLVM::TruncOp::create(builder, loc, i8, field(2));
  Value lastMask = LLVM::TruncOp::create(builder, loc, i8, field(3));
  branch(nextByte, ValueRange{beginByte});
  builder.setInsertionPointToStart(nextByte);
  Value byte = nextByte->getArgument(0);
  Value following = LLVM::AddOp::create(builder, loc, byte, constant(1));
  Value isFirst = LLVM::ICmpOp::create(builder, loc, LLVM::ICmpPredicate::eq,
                                       byte, beginByte);
  Value isLast = LLVM::ICmpOp::create(builder, loc, LLVM::ICmpPredicate::eq,
                                      following, endByte);
  Value full = llvmConstant(builder, loc, i8, 255);
  Value mask = LLVM::AndOp::create(
      builder, loc,
      LLVM::SelectOp::create(builder, loc, isFirst, firstMask, full),
      LLVM::SelectOp::create(builder, loc, isLast, lastMask, full));
  Value bits = LLVM::LoadOp::create(builder, loc, i8, gep(state, i8, byte), 1);
  Value hasUnknown =
      LLVM::ICmpOp::create(builder, loc, LLVM::ICmpPredicate::ne,
                           LLVM::AndOp::create(builder, loc, bits, mask),
                           llvmConstant(builder, loc, i8, 0));
  condBranch(hasUnknown, unknown, ValueRange{}, advanceByte, ValueRange{});
  builder.setInsertionPointToStart(advanceByte);
  condBranch(isLast, advanceRange, ValueRange{}, nextByte,
             ValueRange{following});
  builder.setInsertionPointToStart(advanceRange);
  branch(nextRange,
         ValueRange{LLVM::AddOp::create(builder, loc, range, constant(1))});
  builder.setInsertionPointToStart(known);
  branch(publish, ValueRange{llvmConstant(builder, loc, i1, 1)});
  builder.setInsertionPointToStart(unknown);
  branch(publish, ValueRange{llvmConstant(builder, loc, i1, 0)});
  builder.setInsertionPointToStart(publish);
  LLVM::StoreOp::create(
      builder, loc,
      LLVM::ZExtOp::create(builder, loc, i8, publish->getArgument(0)), selector,
      1);
  branch(nextBit, ValueRange{tail});
  builder.setInsertionPointToStart(advanceWord);
  branch(nextWord,
         ValueRange{LLVM::AddOp::create(builder, loc, word, constant(1))});
  builder.setInsertionPointToStart(done);
  LLVM::ReturnOp::create(builder, loc, ValueRange{});
  return success();
}

// Kernel and group domains share the same exact masked-word scanner and
// negative hint protocol. Only kernels own scheduler promotion-pending bits.
static LogicalResult materializePromotionReadiness(ModuleOp module,
                                                   ArrayRef<uint64_t> owners,
                                                   ArrayRef<uint64_t> scans,
                                                   uint64_t domainCount,
                                                   bool group,
                                                   uint64_t stateBits) {
  MLIRContext *context = module.getContext();
  OpBuilder builder(context);
  Location loc = module.getLoc();
  Type i1 = builder.getI1Type(), i8 = builder.getI8Type(),
       i64 = builder.getI64Type();
  Type pointer = LLVM::LLVMPointerType::get(context);
  auto table = [&](StringRef name, ArrayRef<uint64_t> words,
                   bool isConstant = true) {
    builder.setInsertionPointToStart(module.getBody());
    auto tensor =
        RankedTensorType::get({static_cast<int64_t>(words.size())}, i64);
    return LLVM::GlobalOp::create(builder, loc,
                                  LLVM::LLVMArrayType::get(i64, words.size()),
                                  isConstant, LLVM::Linkage::Internal, name,
                                  DenseIntElementsAttr::get(tensor, words), 8);
  };
  StringRef ownerName = group ? "__talos_eval_group_promotion_domains_v1"
                              : "__talos_eval_kernel_promotion_owners_v1";
  StringRef rangeName = group ? "__talos_eval_group_promotion_ranges_v1"
                              : "__talos_eval_kernel_promotion_ranges_v1";
  StringRef hintName = group ? "__talos_eval_group_promotion_hints_v1"
                             : "__talos_eval_kernel_promotion_hints_v1";
  auto ownerTable = table(ownerName, owners);
  auto rangeTable = table(rangeName, scans);
  // A negative hint is not a cached result: every invocation rereads this
  // exact masked word. When it clears, rescan the complete certificate. The
  // immutable range table makes hints valid across invalidation and context
  // reset; no additional invalidation protocol is needed. A zero mask is an
  // empty hint and safely reads word zero in the guarded state allocation.
  SmallVector<uint64_t> hints(domainCount * 2, 0);
  auto hintTable = table(hintName, hints, false);
  auto constant = [&](uint64_t value) {
    return llvmConstant(builder, loc, i64, value);
  };
  auto address = [&](StringRef name) -> Value {
    return LLVM::AddressOfOp::create(builder, loc, pointer, name);
  };
  auto gep = [&](Value base, Type type, Value index) -> Value {
    return LLVM::GEPOp::create(builder, loc, pointer, type, base,
                               ArrayRef<LLVM::GEPArg>{index});
  };
  auto field = [&](Value base, Value record, uint64_t index,
                   uint64_t width = 4) -> Value {
    Value offset = arith::AddIOp::create(
        builder, loc,
        arith::MulIOp::create(builder, loc, record, constant(width)),
        constant(index));
    return LLVM::LoadOp::create(builder, loc, i64, gep(base, i64, offset), 8);
  };
  StringRef latchName = group ? groupPromotionLatchName
                              : "__talos_eval_kernel_promotion_latched_v1";
  constexpr StringLiteral pendingName =
      "__talos_eval_promotion_pending_mask_v1";
  StringRef scanName = group ? "__talos_eval_group_promotion_scan_v1"
                             : "__talos_eval_kernel_promotion_scan_v1";
  builder.setInsertionPointToEnd(module.getBody());
  auto scan = LLVM::LLVMFuncOp::create(
      builder, loc, scanName, LLVM::LLVMFunctionType::get(i1, {i64}, false));
  scan->setAttr("passthrough",
                builder.getArrayAttr({builder.getStringAttr("noinline"),
                                      builder.getStringAttr("cold")}));
  Block *entry = scan.addEntryBlock(builder);
  auto block = [&](bool argument = false) {
    auto *result = new Block;
    if (argument)
      result->addArgument(i64, loc);
    scan.getBody().push_back(result);
    return result;
  };
  Block *fullScan = block(), *nextRange = block(true), *readRange = block();
  Block *readWord = block(true), *advanceWord = block(),
        *advanceRange = block();
  Block *rememberUnknown = block(), *known = block(), *unknown = block();
  builder.setInsertionPointToStart(entry);
  Value owner = entry->getArgument(0);
  Value ownerBase = address(ownerTable.getSymName());
  Value rangeBase = address(rangeTable.getSymName());
  Value state = address("__talos_state_unknown");
  Value hintBase = address(hintTable.getSymName());
  Value hintIndex = arith::MulIOp::create(builder, loc, owner, constant(2));
  Value hintWordAddress = gep(hintBase, i64, hintIndex);
  Value hintMaskAddress =
      gep(hintBase, i64,
          arith::AddIOp::create(builder, loc, hintIndex, constant(1)));
  Value hintWord = LLVM::LoadOp::create(builder, loc, i64, hintWordAddress, 8);
  Value hintMask = LLVM::LoadOp::create(builder, loc, i64, hintMaskAddress, 8);
  // Both scan loads stay inside the certified words of the unknown plane.
  // Declaring that interval keeps the value plane out of the query's state
  // footprint, so callers need not materialize value-plane state around it.
  auto unknownPlaneBytes = builder.getDenseI64ArrayAttr(
      {0, int64_t(llvm::divideCeil(stateBits, 64) * 8)});
  auto hintLoad =
      LLVM::LoadOp::create(builder, loc, i64, gep(state, i64, hintWord), 8);
  schedule::set<schedule::Field::EvalStateAccessBytes>(hintLoad,
                                                       unknownPlaneBytes);
  Value hintBits = hintLoad;
  Value stillUnknown = arith::CmpIOp::create(
      builder, loc, arith::CmpIPredicate::ne,
      arith::AndIOp::create(builder, loc, hintBits, hintMask), constant(0));
  cf::CondBranchOp::create(builder, loc, stillUnknown, unknown, ValueRange{},
                           fullScan, ValueRange{});
  builder.setInsertionPointToStart(fullScan);
  Value first = field(ownerBase, owner, 0, group ? 2 : 4),
        end = field(ownerBase, owner, 1, group ? 2 : 4);
  cf::BranchOp::create(builder, loc, nextRange, ValueRange{first});

  builder.setInsertionPointToStart(nextRange);
  Value range = nextRange->getArgument(0);
  Value done =
      arith::CmpIOp::create(builder, loc, arith::CmpIPredicate::eq, range, end);
  cf::CondBranchOp::create(builder, loc, done, known, ValueRange{}, readRange,
                           ValueRange{});
  builder.setInsertionPointToStart(readRange);
  Value beginWord = field(rangeBase, range, 0),
        endWord = field(rangeBase, range, 1);
  Value firstMask = field(rangeBase, range, 2);
  Value lastMask = field(rangeBase, range, 3);
  cf::BranchOp::create(builder, loc, readWord, ValueRange{beginWord});

  builder.setInsertionPointToStart(readWord);
  Value word = readWord->getArgument(0);
  Value nextWord = arith::AddIOp::create(builder, loc, word, constant(1));
  Value isFirst = arith::CmpIOp::create(builder, loc, arith::CmpIPredicate::eq,
                                        word, beginWord);
  Value isLast = arith::CmpIOp::create(builder, loc, arith::CmpIPredicate::eq,
                                       nextWord, endWord);
  Value fullMask = constant(UINT64_MAX);
  Value mask = arith::AndIOp::create(
      builder, loc,
      arith::SelectOp::create(builder, loc, isFirst, firstMask, fullMask),
      arith::SelectOp::create(builder, loc, isLast, lastMask, fullMask));
  auto wordLoad =
      LLVM::LoadOp::create(builder, loc, i64, gep(state, i64, word), 8);
  schedule::set<schedule::Field::EvalStateAccessBytes>(wordLoad,
                                                       unknownPlaneBytes);
  Value bits = wordLoad;
  Value hasUnknown = arith::CmpIOp::create(
      builder, loc, arith::CmpIPredicate::ne,
      arith::AndIOp::create(builder, loc, bits, mask), constant(0));
  cf::CondBranchOp::create(builder, loc, hasUnknown, rememberUnknown,
                           ValueRange{}, advanceWord, ValueRange{});
  builder.setInsertionPointToStart(advanceWord);
  cf::CondBranchOp::create(builder, loc, isLast, advanceRange, ValueRange{},
                           readWord, ValueRange{nextWord});
  builder.setInsertionPointToStart(advanceRange);
  cf::BranchOp::create(
      builder, loc, nextRange,
      ValueRange{arith::AddIOp::create(builder, loc, range, constant(1))});

  builder.setInsertionPointToStart(rememberUnknown);
  LLVM::StoreOp::create(builder, loc, word, hintWordAddress, 8);
  LLVM::StoreOp::create(builder, loc, mask, hintMaskAddress, 8);
  cf::BranchOp::create(builder, loc, unknown);

  builder.setInsertionPointToStart(known);
  LLVM::StoreOp::create(builder, loc, llvmConstant(builder, loc, i8, 1),
                        gep(address(latchName), i8, owner), 1);
  if (!group) {
    Value pendingAddress =
        gep(address(pendingName), i64, field(ownerBase, owner, 2));
    Value pending = LLVM::LoadOp::create(builder, loc, i64, pendingAddress, 8);
    Value keep = arith::XOrIOp::create(builder, loc, field(ownerBase, owner, 3),
                                       constant(UINT64_MAX));
    LLVM::StoreOp::create(builder, loc,
                          arith::AndIOp::create(builder, loc, pending, keep),
                          pendingAddress, 8);
  }
  LLVM::ReturnOp::create(builder, loc, llvmConstant(builder, loc, i1, 1));
  builder.setInsertionPointToStart(unknown);
  LLVM::ReturnOp::create(builder, loc, llvmConstant(builder, loc, i1, 0));

  // IEEE 1800-2023 4.5/4.6: keep each existing readiness boundary and its
  // invalidation ordering. A valid latch needs no scan or additional call.
  builder.setInsertionPointToEnd(module.getBody());
  auto ready = LLVM::LLVMFuncOp::create(
      builder, loc, group ? groupPromotionReadyName : kernelPromotionReadyName,
      LLVM::LLVMFunctionType::get(i1, {i64}, false));
  ready->setAttr("passthrough",
                 builder.getArrayAttr({builder.getStringAttr("alwaysinline")}));
  Block *readyEntry = ready.addEntryBlock(builder);
  Block *latched = new Block, *check = new Block;
  ready.getBody().push_back(latched);
  ready.getBody().push_back(check);
  builder.setInsertionPointToStart(readyEntry);
  Value latch = LLVM::LoadOp::create(
      builder, loc, i8, gep(address(latchName), i8, readyEntry->getArgument(0)),
      1);
  Value isLatched =
      arith::CmpIOp::create(builder, loc, arith::CmpIPredicate::ne, latch,
                            llvmConstant(builder, loc, i8, 0));
  cf::CondBranchOp::create(builder, loc, isLatched, latched, ValueRange{},
                           check, ValueRange{});
  builder.setInsertionPointToStart(latched);
  LLVM::ReturnOp::create(builder, loc, llvmConstant(builder, loc, i1, 1));
  builder.setInsertionPointToStart(check);
  Value result = LLVM::CallOp::create(builder, loc, scan,
                                      ValueRange{readyEntry->getArgument(0)})
                     .getResult();
  LLVM::ReturnOp::create(builder, loc, result);
  return success();
}

LogicalResult materializeNativeKernelPromotionReadiness(
    ModuleOp module, uint64_t stateBits,
    ArrayRef<SmallVector<NativePromotionRange>> ranges,
    ArrayRef<std::string> twoStateExecutors,
    ArrayRef<talos_rt_native_merged_fragment> fragments) {
  if (llvm::all_of(twoStateExecutors,
                   [](const std::string &name) { return name.empty(); }))
    return success();

  // IEEE 1800-2023 6.3.1, 6.11.2: every certified bit must be known before
  // using a two-state body. Merge overlapping intervals without filling gaps
  // or admitting neighboring bits in either partial boundary word. The state
  // plane has an aligned guard word, so a masked final i64 load remains within
  // its allocation even when the last certified bit is not word-aligned.
  SmallVector<uint64_t> owners, scans;
  for (auto [index, executor] : llvm::enumerate(twoStateExecutors)) {
    SmallVector<std::pair<uint64_t, uint64_t>> intervals;
    if (!executor.empty())
      for (const NativePromotionRange &range : ranges[index]) {
        if (!range.bitWidth || range.bitOffset > stateBits ||
            range.bitWidth > stateBits - range.bitOffset)
          return module.emitError(
              "kernel promotion range exceeds native state");
        intervals.emplace_back(range.bitOffset,
                               range.bitOffset + range.bitWidth);
      }
    llvm::sort(intervals);
    SmallVector<std::pair<uint64_t, uint64_t>> merged;
    for (auto [begin, end] : intervals) {
      if (!merged.empty() && begin <= merged.back().second)
        merged.back().second = std::max(merged.back().second, end);
      else
        merged.emplace_back(begin, end);
    }
    uint64_t first = scans.size() / 4;
    for (auto [begin, end] : merged) {
      uint64_t firstMask = UINT64_MAX << (begin % 64);
      uint64_t lastMask =
          end % 64 ? (uint64_t{1} << (end % 64)) - 1 : UINT64_MAX;
      llvm::append_range(scans, ArrayRef<uint64_t>{begin / 64,
                                                   end / 64 + (end % 64 != 0),
                                                   firstMask, lastMask});
    }
    uint64_t bit = fragments[index].bit;
    llvm::append_range(owners,
                       ArrayRef<uint64_t>{first, scans.size() / 4, bit / 64,
                                          uint64_t{1} << (bit % 64)});
  }

  return materializePromotionReadiness(module, owners, scans,
                                       twoStateExecutors.size(), false,
                                       stateBits);
}

LogicalResult verifyNativeGroupProofDomains(ModuleOp module) {
  auto latches = module.lookupSymbol<LLVM::GlobalOp>(groupPromotionLatchName);
  if (!latches)
    return success();
  auto domains =
      schedule::get<schedule::Field::EvalGroupProofDependencies>(latches);
  auto type = dyn_cast<LLVM::LLVMArrayType>(latches.getGlobalType());
  auto stateBits = runtime::get<runtime::StateBits>(module);
  auto kernels = module.lookupSymbol<LLVM::GlobalOp>(
      "__talos_eval_kernel_promotion_latched_v1");
  auto kernelType = kernels
                        ? dyn_cast<LLVM::LLVMArrayType>(kernels.getGlobalType())
                        : LLVM::LLVMArrayType{};
  auto dependencies =
      kernels
          ? schedule::get<schedule::Field::EvalKernelProofDependencies>(kernels)
          : ArrayAttr{};
  if (!domains || !type || !type.getElementType().isInteger(8) ||
      domains.empty() || type.getNumElements() != domains.size() ||
      latches.getConstant() || latches.getAddrSpace() != 0 ||
      latches.getExternallyInitialized() ||
      latches.getLinkage() != LLVM::Linkage::Internal || !stateBits ||
      !kernelType || !kernelType.getElementType().isInteger(8) || !dependencies)
    return latches.emitError("group proof inventory has invalid latch storage");
  bool startsUnknown = false;
  if (auto initial =
          dyn_cast_or_null<DenseIntElementsAttr>(latches.getValueOrNull()))
    startsUnknown =
        initial.isSplat() && initial.getSplatValue<APInt>().isZero();
  Region &initializer = latches.getInitializerRegion();
  if (initializer.hasOneBlock() &&
      initializer.front().getOperations().size() == 2) {
    auto zero = dyn_cast<LLVM::ZeroOp>(initializer.front().front());
    auto result = dyn_cast<LLVM::ReturnOp>(initializer.front().back());
    startsUnknown = zero && result && result.getNumOperands() == 1 &&
                    result.getOperand(0) == zero.getResult();
  }
  if (!startsUnknown)
    return latches.emitError("group proof latches must initialize to zero");

  using Intervals = SmallVector<std::pair<uint64_t, uint64_t>>;
  auto append = [&](DenseI64ArrayAttr ranges, Intervals &result) {
    if (!ranges || ranges.size() % 2)
      return false;
    auto values = ranges.asArrayRef();
    for (size_t i = 0; i != values.size(); i += 2) {
      if (values[i] < 0 || values[i + 1] <= 0)
        return false;
      uint64_t begin = values[i], width = values[i + 1];
      if (begin > stateBits.getUInt() || width > stateBits.getUInt() - begin)
        return false;
      result.emplace_back(begin, begin + width);
    }
    return true;
  };
  DenseMap<uint64_t, Intervals> sourceRanges;
  for (Attribute attribute : dependencies) {
    auto kernel = dyn_cast<schedule::KernelProofDependencyAttr>(attribute);
    if (!kernel || !kernel.getPendingBit() ||
        !append(kernel.getRanges(),
                sourceRanges[kernel.getPendingBit().getUInt()]))
      return kernels.emitError("group proof has invalid member dependencies");
  }
  for (auto [index, attribute] : llvm::enumerate(domains)) {
    auto domain = dyn_cast<schedule::GroupProofDependencyAttr>(attribute);
    if (!domain || !domain.getDomain() ||
        domain.getDomain().getUInt() != index || !domain.getMembers() ||
        domain.getMembers().empty())
      return latches.emitError("group proof has invalid dense domain identity");
    Intervals expected, actual, merged;
    llvm::SmallDenseSet<int32_t> seen;
    for (int32_t member : domain.getMembers().asArrayRef()) {
      if (member < 0 || uint64_t(member) >= kernelType.getNumElements() ||
          !seen.insert(member).second)
        return latches.emitError("group proof has invalid member identity");
      auto source = sourceRanges.find(member);
      // Missing reverse dependencies do not distinguish an empty, always-known
      // certificate from an ineligible owner. This initial policy requires
      // explicit nonempty proof evidence for every member.
      if (source == sourceRanges.end() || source->second.empty())
        return latches.emitError(
            "group proof member has no explicit certificate");
      llvm::append_range(expected, source->second);
    }
    llvm::sort(expected);
    for (auto [begin, end] : expected) {
      if (!merged.empty() && begin <= merged.back().second)
        merged.back().second = std::max(merged.back().second, end);
      else
        merged.emplace_back(begin, end);
    }
    if (!append(domain.getRanges(), actual) || actual != merged ||
        actual.empty())
      return latches.emitError(
          "group proof ranges must equal the exact member certificate union");
  }
  // Readiness is built before group emission. The reverse-index phase calls
  // this verifier again once group/candidate functions exist, before consuming
  // the inventory. References must name exactly that immutable planned domain.
  for (auto function : module.getOps<LLVM::LLVMFuncOp>())
    if (auto domain =
            schedule::get<schedule::Field::EvalGroupProofDomain>(function)) {
      auto id = domain.getDomain();
      if (!id || id.getUInt() >= domains.size() ||
          domains[id.getUInt()] != domain ||
          schedule::get<schedule::Field::EvalRankedMembers>(function) !=
              domain.getMembers())
        return function.emitError(
            "group function has an invalid planned domain reference");
    }
  return success();
}

LogicalResult materializeNativeGroupPromotionReadiness(ModuleOp module) {
  auto latches = module.lookupSymbol<LLVM::GlobalOp>(groupPromotionLatchName);
  if (!latches)
    return success();
  if (failed(verifyNativeGroupProofDomains(module)))
    return failure();
  if (module.lookupSymbol(groupPromotionReadyName) ||
      module.lookupSymbol("__talos_eval_group_promotion_scan_v1"))
    return latches.emitError("group readiness was materialized twice");
  auto domains =
      schedule::get<schedule::Field::EvalGroupProofDependencies>(latches);
  SmallVector<uint64_t> records, scans;
  for (Attribute attribute : domains) {
    auto domain = cast<schedule::GroupProofDependencyAttr>(attribute);
    uint64_t first = scans.size() / 4;
    auto ranges = domain.getRanges().asArrayRef();
    for (size_t i = 0; i != ranges.size(); i += 2) {
      uint64_t begin = ranges[i], end = begin + ranges[i + 1];
      llvm::append_range(
          scans, ArrayRef<uint64_t>{begin / 64, end / 64 + (end % 64 != 0),
                                    UINT64_MAX << (begin % 64),
                                    end % 64 ? (uint64_t{1} << (end % 64)) - 1
                                             : UINT64_MAX});
    }
    llvm::append_range(records, ArrayRef<uint64_t>{first, scans.size() / 4});
  }
  // IEEE 1800-2023 4.5/4.6 and 6.3.1: this query only inspects the existing
  // canonical X/Z at the group entry boundary. It neither runs inactive
  // members nor changes their pending bits/latches. Exact unknown-write
  // publication and the full context/runtime reset revoke positive results;
  // a negative hint rereads its immutable exact mask on every query.
  return materializePromotionReadiness(
      module, records, scans, domains.size(), true,
      runtime::get<runtime::StateBits>(module).getUInt());
}

} // namespace talos::detail

namespace talos {
namespace {
class MaterializeNativeGroupPromotionReadinessPass final
    : public impl::MaterializeNativeGroupPromotionReadinessPassBase<
          MaterializeNativeGroupPromotionReadinessPass> {
  void runOnOperation() override {
    if (failed(
            detail::materializeNativeGroupPromotionReadiness(getOperation())))
      signalPassFailure();
  }
};
} // namespace
} // namespace talos
