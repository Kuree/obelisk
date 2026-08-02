//===- SimulationThreeTierMaterialization.cpp - Kernel variant state ---===//

#include "SimulationAOTPlanning.h"
#include "SimulationToLLVMCoroutinePrivate.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"

#include "llvm/ADT/STLExtras.h"

using namespace mlir;

namespace obelisk::detail {
namespace {

struct PromotionBytes {
  uint64_t firstByte;
  uint64_t byteCount;
  uint8_t firstMask;
  uint8_t lastMask;
};

PromotionBytes toPromotionBytes(NativePromotionRange range) {
  uint64_t firstBit = range.bitOffset % 8;
  uint64_t exclusive = firstBit + range.bitWidth;
  uint64_t byteCount = (exclusive + 7) / 8;
  uint8_t firstMask = static_cast<uint8_t>(UINT8_MAX << firstBit);
  uint8_t lastMask = exclusive % 8 == 0
                         ? UINT8_MAX
                         : static_cast<uint8_t>((1u << (exclusive % 8)) - 1);
  return {range.bitOffset / 8, byteCount, firstMask, lastMask};
}

} // namespace

LogicalResult materializeNativeThreeTierPlan(ModuleOp module,
                                             const NativeThreeTierPlan &plan) {
  MLIRContext *context = module.getContext();
  Location location = module.getLoc();
  OpBuilder builder(context);
  Type pointer = LLVM::LLVMPointerType::get(context);
  Type i1 = builder.getI1Type();
  Type i8 = builder.getI8Type();
  Type i32 = builder.getI32Type();
  Type i64 = builder.getI64Type();

  // Tier 2 is an allocation-free local dirty-mask fixed point.  The callback
  // evaluates exactly one SCC member and returns {members dirtied,
  // downstream publications}.  There is no semantic iteration limit: a
  // genuinely oscillating SCC remains interruptible through the callback's
  // generated diagnostic path, while a convergent SCC runs until its dirty
  // set is empty.
  Type callbackResult = LLVM::LLVMStructType::getLiteral(context, {i64, i64});
  auto callbackType =
      LLVM::LLVMFunctionType::get(callbackResult, {pointer, i32}, false);
  for (const NativeThreeTierKernelPlan &kernel : plan.kernels) {
    if (kernel.tier != sim::SchedulerTierKind::Tier2 ||
        kernel.schedule != sim::ComputeScheduleKind::Convergence)
      continue;
    // A ready bit represents the complete merged SCC.  The compute graph
    // currently bounds a directly generated SCC to one mask word; larger SCCs
    // retain their fine native runtime entry until the multiword ABI lands.
    // Do not truncate the mask or silently change semantics.
    if (kernel.memberCount > 64) {
      // Preserve the fine native/runtime owner until the direct ABI grows a
      // multiword member mask. Record the decision in IR so tests and later
      // diagnostics distinguish fallback from accidental omission.
      module->setAttr("obelisk.tier2.multiword_fallback",
                      UnitAttr::get(context));
      continue;
    }
    auto schedule = module.lookupSymbol<LLVM::LLVMFuncOp>(
        (Twine("__obelisk_tier2_converge_v1_") + Twine(kernel.id)).str());
    if (schedule)
      return module.emitError("duplicate Tier-2 convergence subkernel");

    SmallString<64> subkernelName;
    (Twine("__obelisk_tier2_converge_v1_") + Twine(kernel.id))
        .toVector(subkernelName);
    SmallString<64> membersName;
    (Twine("__obelisk_tier2_members_v1_") + Twine(kernel.id))
        .toVector(membersName);
    Type membersType = LLVM::LLVMArrayType::get(i32, kernel.memberIDs.size());
    makeConstantGlobal(
        module, location, membersType, membersName, LLVM::Linkage::Internal, 4,
        [&](OpBuilder &initializer) {
          Value members =
              LLVM::ZeroOp::create(initializer, location, membersType);
          for (auto [index, fragment] : llvm::enumerate(kernel.memberIDs))
            members = LLVM::InsertValueOp::create(
                initializer, location, members,
                llvmConstant(initializer, location, i32, fragment),
                ArrayRef<int64_t>{static_cast<int64_t>(index)});
          return members;
        });
    builder.setInsertionPointToEnd(module.getBody());
    auto subkernel = LLVM::LLVMFuncOp::create(
        builder, location, subkernelName,
        LLVM::LLVMFunctionType::get(i64, {pointer, i64, pointer}, false));
    Block *entry = subkernel.addEntryBlock(builder);
    Block *outer = new Block;
    outer->addArgument(i64, location); // active members
    outer->addArgument(i64, location); // all downstream publications
    Block *member = new Block;
    member->addArgument(i64, location); // active members
    member->addArgument(i64, location); // member index
    member->addArgument(i64, location); // next dirty members
    member->addArgument(i64, location); // all downstream publications
    Block *evaluate = new Block;
    for (Type type : member->getArgumentTypes())
      evaluate->addArgument(type, location);
    Block *advance = new Block;
    for (Type type : member->getArgumentTypes())
      advance->addArgument(type, location);
    Block *afterSweep = new Block;
    afterSweep->addArgument(i64, location); // next dirty members
    afterSweep->addArgument(i64, location); // all publications
    Block *done = new Block;
    done->addArgument(i64, location);
    subkernel.getBody().push_back(outer);
    subkernel.getBody().push_back(member);
    subkernel.getBody().push_back(evaluate);
    subkernel.getBody().push_back(advance);
    subkernel.getBody().push_back(afterSweep);
    subkernel.getBody().push_back(done);

    builder.setInsertionPointToStart(entry);
    Value inactive = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, entry->getArgument(1),
        llvmConstant(builder, location, i64, 0));
    cf::CondBranchOp::create(
        builder, location, inactive, done,
        ValueRange{llvmConstant(builder, location, i64, 0)}, outer,
        ValueRange{entry->getArgument(1),
                   llvmConstant(builder, location, i64, 0)});

    builder.setInsertionPointToStart(outer);
    cf::BranchOp::create(builder, location, member,
                         ValueRange{outer->getArgument(0),
                                    llvmConstant(builder, location, i64, 0),
                                    llvmConstant(builder, location, i64, 0),
                                    outer->getArgument(1)});

    builder.setInsertionPointToStart(member);
    // The merged kernel inventory is the member count.  A zero inventory is
    // rejected by the schedule verifier, so the shift below is always valid.
    uint64_t memberCount = kernel.memberCount;
    Value membersDone = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, member->getArgument(1),
        llvmConstant(builder, location, i64, memberCount));
    Block *selectMember = new Block;
    for (Type type : member->getArgumentTypes())
      selectMember->addArgument(type, location);
    subkernel.getBody().getBlocks().insert(Region::iterator(evaluate),
                                           selectMember);
    cf::CondBranchOp::create(
        builder, location, membersDone, afterSweep,
        ValueRange{member->getArgument(2), member->getArgument(3)},
        selectMember, member->getArguments());

    builder.setInsertionPointToStart(selectMember);
    // Keep the shift strictly below memberCount.  Computing it in the test
    // block made the terminal iteration of a 64-member SCC evaluate `1 <<
    // 64`, which is poison in LLVM IR even though control then exited.
    Value bit = arith::ShLIOp::create(builder, location,
                                      llvmConstant(builder, location, i64, 1),
                                      selectMember->getArgument(1));
    Value selected = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ne,
        arith::AndIOp::create(builder, location, selectMember->getArgument(0),
                              bit),
        llvmConstant(builder, location, i64, 0));
    cf::CondBranchOp::create(builder, location, selected, evaluate,
                             selectMember->getArguments(), advance,
                             selectMember->getArguments());

    builder.setInsertionPointToStart(evaluate);
    Value membersBase =
        LLVM::AddressOfOp::create(builder, location, pointer, membersName);
    Value memberAddress =
        LLVM::GEPOp::create(builder, location, pointer, i32, membersBase,
                            ValueRange{evaluate->getArgument(1)});
    Value fragmentID =
        LLVM::LoadOp::create(builder, location, i32, memberAddress, 4);
    auto result = LLVM::CallOp::create(
        builder, location, callbackType,
        ValueRange{entry->getArgument(2), entry->getArgument(0), fragmentID});
    Value redirty = LLVM::ExtractValueOp::create(builder, location,
                                                 result.getResult(), {0});
    uint64_t memberMask =
        memberCount == 64 ? UINT64_MAX : (uint64_t{1} << memberCount) - 1;
    redirty =
        arith::AndIOp::create(builder, location, redirty,
                              llvmConstant(builder, location, i64, memberMask));
    Value publications = LLVM::ExtractValueOp::create(builder, location,
                                                      result.getResult(), {1});
    SmallVector<Value> evaluatedArgs(evaluate->getArguments().begin(),
                                     evaluate->getArguments().end());
    evaluatedArgs[2] = arith::OrIOp::create(builder, location,
                                            evaluate->getArgument(2), redirty);
    evaluatedArgs[3] = arith::OrIOp::create(
        builder, location, evaluate->getArgument(3), publications);
    cf::BranchOp::create(builder, location, advance, evaluatedArgs);

    builder.setInsertionPointToStart(advance);
    SmallVector<Value> advancedArgs(advance->getArguments().begin(),
                                    advance->getArguments().end());
    advancedArgs[1] =
        arith::AddIOp::create(builder, location, advance->getArgument(1),
                              llvmConstant(builder, location, i64, 1));
    cf::BranchOp::create(builder, location, member, advancedArgs);

    builder.setInsertionPointToStart(afterSweep);
    Value converged = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, afterSweep->getArgument(0),
        llvmConstant(builder, location, i64, 0));
    cf::CondBranchOp::create(builder, location, converged, done,
                             ValueRange{afterSweep->getArgument(1)}, outer,
                             afterSweep->getArguments());

    builder.setInsertionPointToStart(done);
    LLVM::ReturnOp::create(builder, location, done->getArgument(0));

    // This is the direct Tier-1 -> Tier-2 call edge.  Runtime entry points use
    // the same subkernel only for asynchronous transactions.
    SmallString<64> invokeName;
    (Twine("__obelisk_tier1_invoke_tier2_v1_") + Twine(kernel.id))
        .toVector(invokeName);
    builder.setInsertionPointToEnd(module.getBody());
    auto invoke = LLVM::LLVMFuncOp::create(
        builder, location, invokeName,
        LLVM::LLVMFunctionType::get(i64, {pointer, i64, pointer}, false));
    Block *invokeEntry = invoke.addEntryBlock(builder);
    builder.setInsertionPointToStart(invokeEntry);
    Value downstream = LLVM::CallOp::create(builder, location, TypeRange{i64},
                                            SymbolRefAttr::get(subkernel),
                                            invokeEntry->getArguments())
                           .getResult();
    LLVM::ReturnOp::create(builder, location, downstream);
  }

  // Tier-1 model bodies stay out of line and are selected before entering the
  // hot body.  Both variants use the same small callback ABI here; native
  // model lowering binds the four-state and two-state callback pointers to
  // different generated fragment bodies.  The wrappers themselves contain no
  // promotion test and no runtime call.
  struct Tier1Variants {
    uint32_t kernel;
    LLVM::LLVMFuncOp fourState;
    LLVM::LLVMFuncOp twoState;
  };
  SmallVector<Tier1Variants> tier1Variants;
  auto wrapperType =
      LLVM::LLVMFunctionType::get(i64, {pointer, pointer}, false);
  for (const NativeThreeTierKernelPlan &kernel : plan.kernels) {
    if (kernel.tier != sim::SchedulerTierKind::Tier1)
      continue;
    auto makeVariant = [&](StringRef plane) {
      SmallString<64> name;
      (Twine("__obelisk_tier1_eval_") + plane + "_v1_" + Twine(kernel.id))
          .toVector(name);
      builder.setInsertionPointToEnd(module.getBody());
      auto function =
          LLVM::LLVMFuncOp::create(builder, location, name, wrapperType);
      Block *body = function.addEntryBlock(builder);
      builder.setInsertionPointToStart(body);
      Value publications = llvmConstant(builder, location, i64, 0);
      for (uint32_t fragment : kernel.memberIDs) {
        auto result = LLVM::CallOp::create(
            builder, location, callbackType,
            ValueRange{body->getArgument(1), body->getArgument(0),
                       llvmConstant(builder, location, i32, fragment)});
        publications = arith::OrIOp::create(
            builder, location, publications,
            LLVM::ExtractValueOp::create(builder, location, result.getResult(),
                                         {1}));
      }
      LLVM::ReturnOp::create(builder, location, publications);
      return function;
    };
    LLVM::LLVMFuncOp fourState = makeVariant("four_state");
    LLVM::LLVMFuncOp twoState;
    if (kernel.twoStateEligible)
      twoState = makeVariant("two_state");
    tier1Variants.push_back({kernel.id, fourState, twoState});
  }

  SmallVector<const NativeThreeTierKernelPlan *> candidates;
  for (const NativeThreeTierKernelPlan &kernel : plan.kernels)
    if (kernel.tier == sim::SchedulerTierKind::Tier1 && kernel.twoStateEligible)
      candidates.push_back(&kernel);
  constexpr StringLiteral routeName = "__obelisk_tier1_selected_variant_v1";
  if (!candidates.empty()) {
    Type rangeType =
        LLVM::LLVMStructType::getLiteral(context, {i64, i64, i8, i8});
    constexpr StringLiteral dirtyName = "__obelisk_tier1_promotion_dirty_v1";
    constexpr StringLiteral scanName = "__obelisk_tier1_scan_unknown_v1";
    constexpr StringLiteral tableName = "__obelisk_tier1_promotion_hooks_v1";

    auto makeByteState = [&](StringRef name, bool dirty) {
      Type arrayType = LLVM::LLVMArrayType::get(i8, candidates.size());
      OpBuilder globalBuilder(context);
      globalBuilder.setInsertionPointToStart(module.getBody());
      auto global = LLVM::GlobalOp::create(
          globalBuilder, location, arrayType, /*isConstant=*/false,
          LLVM::Linkage::Internal, name, Attribute{}, 1);
      Block *initializer = new Block;
      global.getInitializerRegion().push_back(initializer);
      globalBuilder.setInsertionPointToStart(initializer);
      Value value = LLVM::ZeroOp::create(globalBuilder, location, arrayType);
      for (auto [index, kernel] : llvm::enumerate(candidates)) {
        bool initiallyKnown = kernel->promotionRanges.empty();
        uint8_t byte = dirty ? !initiallyKnown : initiallyKnown;
        if (byte == 0)
          continue;
        value = LLVM::InsertValueOp::create(
            globalBuilder, location, value,
            llvmConstant(globalBuilder, location, i8, byte),
            ArrayRef<int64_t>{static_cast<int64_t>(index)});
      }
      LLVM::ReturnOp::create(globalBuilder, location, value);
      return global;
    };
    makeByteState(dirtyName, true);
    makeByteState(routeName, false);

    // One allocation-free scanner serves every candidate.  It masks the first
    // and last byte of each packed root, so unrelated adjacent X/Z bits cannot
    // delay a kernel's promotion.  There is deliberately no scan in either hot
    // variant; callers invoke this only while the candidate's dirty latch is
    // set.
    builder.setInsertionPointToEnd(module.getBody());
    auto scanner = LLVM::LLVMFuncOp::create(
        builder, location, scanName,
        LLVM::LLVMFunctionType::get(i1, {pointer, i32}, false));
    Block *entry = scanner.addEntryBlock(builder);
    Block *rootHead = new Block;
    rootHead->addArgument(i64, location);
    Block *loadRoot = new Block;
    loadRoot->addArgument(i64, location);
    Block *byteHead = new Block;
    byteHead->addArgument(i64, location); // root index
    byteHead->addArgument(i64, location); // byte index
    byteHead->addArgument(i64, location); // first byte
    byteHead->addArgument(i64, location); // byte count
    byteHead->addArgument(i8, location);  // first mask
    byteHead->addArgument(i8, location);  // last mask
    Block *loadByte = new Block;
    for (Type type : byteHead->getArgumentTypes())
      loadByte->addArgument(type, location);
    Block *known = new Block;
    Block *unknown = new Block;
    scanner.getBody().push_back(rootHead);
    scanner.getBody().push_back(loadRoot);
    scanner.getBody().push_back(byteHead);
    scanner.getBody().push_back(loadByte);
    scanner.getBody().push_back(known);
    scanner.getBody().push_back(unknown);

    builder.setInsertionPointToStart(entry);
    cf::BranchOp::create(builder, location, rootHead,
                         ValueRange{llvmConstant(builder, location, i64, 0)});

    builder.setInsertionPointToStart(rootHead);
    Value rootIndex = rootHead->getArgument(0);
    Value rootCount =
        LLVM::ZExtOp::create(builder, location, i64, entry->getArgument(1));
    Value rootsDone = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, rootIndex, rootCount);
    cf::CondBranchOp::create(builder, location, rootsDone, known, ValueRange{},
                             loadRoot, ValueRange{rootIndex});

    builder.setInsertionPointToStart(loadRoot);
    Value selectedRoot = loadRoot->getArgument(0);
    Value rangeAddress =
        LLVM::GEPOp::create(builder, location, pointer, rangeType,
                            entry->getArgument(0), ValueRange{selectedRoot});
    Value range =
        LLVM::LoadOp::create(builder, location, rangeType, rangeAddress, 1);
    SmallVector<Value> byteArguments{
        selectedRoot,
        llvmConstant(builder, location, i64, 0),
        LLVM::ExtractValueOp::create(builder, location, range, {0}),
        LLVM::ExtractValueOp::create(builder, location, range, {1}),
        LLVM::ExtractValueOp::create(builder, location, range, {2}),
        LLVM::ExtractValueOp::create(builder, location, range, {3})};
    cf::BranchOp::create(builder, location, byteHead, byteArguments);

    builder.setInsertionPointToStart(byteHead);
    Value byteIndex = byteHead->getArgument(1);
    Value byteCount = byteHead->getArgument(3);
    Value bytesDone = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, byteIndex, byteCount);
    Value nextRoot =
        arith::AddIOp::create(builder, location, byteHead->getArgument(0),
                              llvmConstant(builder, location, i64, 1));
    cf::CondBranchOp::create(builder, location, bytesDone, rootHead,
                             ValueRange{nextRoot}, loadByte,
                             byteHead->getArguments());

    builder.setInsertionPointToStart(loadByte);
    Value currentByte = arith::AddIOp::create(
        builder, location, loadByte->getArgument(2), loadByte->getArgument(1));
    Value unknownPlane = LLVM::AddressOfOp::create(builder, location, pointer,
                                                   "__obelisk_state_unknown");
    Value byteAddress = LLVM::GEPOp::create(
        builder, location, pointer, i8, unknownPlane, ValueRange{currentByte});
    Value unknownBits =
        LLVM::LoadOp::create(builder, location, i8, byteAddress, 1);
    Value isFirst = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, loadByte->getArgument(1),
        llvmConstant(builder, location, i64, 0));
    Value one = llvmConstant(builder, location, i64, 1);
    Value nextByte =
        arith::AddIOp::create(builder, location, loadByte->getArgument(1), one);
    Value isLast =
        arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                              nextByte, loadByte->getArgument(3));
    Value fullMask = llvmConstant(builder, location, i8, UINT8_MAX);
    Value firstMask = arith::SelectOp::create(
        builder, location, isFirst, loadByte->getArgument(4), fullMask);
    Value lastMask = arith::SelectOp::create(
        builder, location, isLast, loadByte->getArgument(5), fullMask);
    unknownBits = arith::AndIOp::create(
        builder, location, unknownBits,
        arith::AndIOp::create(builder, location, firstMask, lastMask));
    Value hasUnknown = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ne, unknownBits,
        llvmConstant(builder, location, i8, 0));
    SmallVector<Value> nextByteArguments(loadByte->getArguments().begin(),
                                         loadByte->getArguments().end());
    nextByteArguments[1] = nextByte;
    cf::CondBranchOp::create(builder, location, hasUnknown, unknown,
                             ValueRange{}, byteHead, nextByteArguments);

    builder.setInsertionPointToStart(known);
    LLVM::ReturnOp::create(builder, location,
                           llvmConstant(builder, location, i1, 1));
    builder.setInsertionPointToStart(unknown);
    LLVM::ReturnOp::create(builder, location,
                           llvmConstant(builder, location, i1, 0));

    SmallVector<LLVM::LLVMFuncOp> promoteFunctions;
    SmallVector<LLVM::LLVMFuncOp> invalidateFunctions;
    for (auto [candidateIndex, kernel] : llvm::enumerate(candidates)) {
      SmallString<64> rangesName;
      (Twine("__obelisk_tier1_promotion_ranges_v1_") + Twine(kernel->id))
          .toVector(rangesName);
      Type rangesType =
          LLVM::LLVMArrayType::get(rangeType, kernel->promotionRanges.size());
      makeConstantGlobal(
          module, location, rangesType, rangesName, LLVM::Linkage::Internal, 8,
          [&](OpBuilder &initializer) {
            Value ranges =
                LLVM::ZeroOp::create(initializer, location, rangesType);
            for (auto [index, bitRange] :
                 llvm::enumerate(kernel->promotionRanges)) {
              PromotionBytes bytes = toPromotionBytes(bitRange);
              Value record =
                  LLVM::ZeroOp::create(initializer, location, rangeType);
              record = insertValue(
                  initializer, location, record,
                  llvmConstant(initializer, location, i64, bytes.firstByte), 0);
              record = insertValue(
                  initializer, location, record,
                  llvmConstant(initializer, location, i64, bytes.byteCount), 1);
              record = insertValue(
                  initializer, location, record,
                  llvmConstant(initializer, location, i8, bytes.firstMask), 2);
              record = insertValue(
                  initializer, location, record,
                  llvmConstant(initializer, location, i8, bytes.lastMask), 3);
              ranges = LLVM::InsertValueOp::create(
                  initializer, location, ranges, record,
                  ArrayRef<int64_t>{static_cast<int64_t>(index)});
            }
            return ranges;
          });

      SmallString<64> promoteName;
      (Twine("__obelisk_tier1_try_promote_v1_") + Twine(kernel->id))
          .toVector(promoteName);
      builder.setInsertionPointToEnd(module.getBody());
      auto promote =
          LLVM::LLVMFuncOp::create(builder, location, promoteName,
                                   LLVM::LLVMFunctionType::get(i1, {}, false));
      promoteFunctions.push_back(promote);
      Block *promoteEntry = promote.addEntryBlock(builder);
      Block *scan = new Block;
      Block *cached = new Block;
      promote.getBody().push_back(scan);
      promote.getBody().push_back(cached);
      builder.setInsertionPointToStart(promoteEntry);
      Value dirtyBase =
          LLVM::AddressOfOp::create(builder, location, pointer, dirtyName);
      Value routeBase =
          LLVM::AddressOfOp::create(builder, location, pointer, routeName);
      Value candidateOffset =
          llvmConstant(builder, location, i64, candidateIndex);
      Value dirtyAddress =
          LLVM::GEPOp::create(builder, location, pointer, i8, dirtyBase,
                              ValueRange{candidateOffset});
      Value routeAddress =
          LLVM::GEPOp::create(builder, location, pointer, i8, routeBase,
                              ValueRange{candidateOffset});
      Value dirty =
          LLVM::LoadOp::create(builder, location, i8, dirtyAddress, 1);
      Value needsScan =
          arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ne,
                                dirty, llvmConstant(builder, location, i8, 0));
      cf::CondBranchOp::create(builder, location, needsScan, scan, ValueRange{},
                               cached, ValueRange{});

      builder.setInsertionPointToStart(cached);
      Value route =
          LLVM::LoadOp::create(builder, location, i8, routeAddress, 1);
      Value selected =
          arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ne,
                                route, llvmConstant(builder, location, i8, 0));
      LLVM::ReturnOp::create(builder, location, selected);

      builder.setInsertionPointToStart(scan);
      Value ranges =
          LLVM::AddressOfOp::create(builder, location, pointer, rangesName);
      Value allKnown =
          LLVM::CallOp::create(
              builder, location, TypeRange{i1}, SymbolRefAttr::get(scanner),
              ValueRange{ranges, llvmConstant(builder, location, i32,
                                              kernel->promotionRanges.size())})
              .getResult();
      Value selectedByte =
          LLVM::ZExtOp::create(builder, location, i8, allKnown);
      LLVM::StoreOp::create(builder, location, selectedByte, routeAddress, 1);
      LLVM::StoreOp::create(builder, location,
                            llvmConstant(builder, location, i8, 0),
                            dirtyAddress, 1);
      LLVM::ReturnOp::create(builder, location, allKnown);

      SmallString<64> invalidateName;
      (Twine("__obelisk_tier1_invalidate_v1_") + Twine(kernel->id))
          .toVector(invalidateName);
      builder.setInsertionPointToEnd(module.getBody());
      auto invalidate = LLVM::LLVMFuncOp::create(
          builder, location, invalidateName,
          LLVM::LLVMFunctionType::get(LLVM::LLVMVoidType::get(context), {},
                                      false));
      invalidateFunctions.push_back(invalidate);
      Block *invalidateEntry = invalidate.addEntryBlock(builder);
      builder.setInsertionPointToStart(invalidateEntry);
      Value invalidateDirtyBase =
          LLVM::AddressOfOp::create(builder, location, pointer, dirtyName);
      Value invalidateRouteBase =
          LLVM::AddressOfOp::create(builder, location, pointer, routeName);
      Value invalidateOffset =
          llvmConstant(builder, location, i64, candidateIndex);
      Value invalidateDirty = LLVM::GEPOp::create(builder, location, pointer,
                                                  i8, invalidateDirtyBase,
                                                  ValueRange{invalidateOffset});
      Value invalidateRoute = LLVM::GEPOp::create(builder, location, pointer,
                                                  i8, invalidateRouteBase,
                                                  ValueRange{invalidateOffset});
      LLVM::StoreOp::create(builder, location,
                            llvmConstant(builder, location, i8, 1),
                            invalidateDirty, 1);
      LLVM::StoreOp::create(builder, location,
                            llvmConstant(builder, location, i8, 0),
                            invalidateRoute, 1);
      LLVM::ReturnOp::create(builder, location, ValueRange{});
    }

    Type hookType =
        LLVM::LLVMStructType::getLiteral(context, {i32, pointer, pointer});
    Type hooksType = LLVM::LLVMArrayType::get(hookType, candidates.size());
    makeConstantGlobal(
        module, location, hooksType, tableName, LLVM::Linkage::Internal, 8,
        [&](OpBuilder &initializer) {
          Value hooks = LLVM::ZeroOp::create(initializer, location, hooksType);
          for (auto [index, kernel, promote, invalidate] : llvm::enumerate(
                   candidates, promoteFunctions, invalidateFunctions)) {
            Value hook = LLVM::ZeroOp::create(initializer, location, hookType);
            hook = insertValue(
                initializer, location, hook,
                llvmConstant(initializer, location, i32, kernel->id), 0);
            hook = insertValue(initializer, location, hook,
                               LLVM::AddressOfOp::create(initializer, location,
                                                         pointer,
                                                         promote.getSymName()),
                               1);
            hook = insertValue(
                initializer, location, hook,
                LLVM::AddressOfOp::create(initializer, location, pointer,
                                          invalidate.getSymName()),
                2);
            hooks = LLVM::InsertValueOp::create(
                initializer, location, hooks, hook,
                ArrayRef<int64_t>{static_cast<int64_t>(index)});
          }
          return hooks;
        });
  }

  // The generated slot-local coordinator consumes owner ready bits and calls
  // only generated Tier-1 wrappers or direct Tier-2 subkernels.  Unsupported
  // Tier-3 bits are deliberately left set for the outer checkpoint handoff.
  // Publications are returned as a compact mask for the caller to route into
  // downstream owner bits before the combined NBA barrier.
  SmallVector<const NativeThreeTierKernelPlan *> executable;
  for (const NativeThreeTierKernelPlan &kernel : plan.kernels) {
    bool tier1 = kernel.tier == sim::SchedulerTierKind::Tier1;
    bool tier2 = kernel.tier == sim::SchedulerTierKind::Tier2 &&
                 kernel.schedule == sim::ComputeScheduleKind::Convergence &&
                 kernel.memberCount <= 64;
    if ((tier1 || tier2) && kernel.readyBit >= 64) {
      module->setAttr("obelisk.tier1.multiword_ready_fallback",
                      UnitAttr::get(context));
      continue;
    }
    if ((tier1 || tier2) && kernel.owner < plan.ownerCount)
      executable.push_back(&kernel);
  }
  if (!plan.kernels.empty()) {
    builder.setInsertionPointToEnd(module.getBody());
    auto evalStep = LLVM::LLVMFuncOp::create(
        builder, location, "__obelisk_tier1_eval_step_v1",
        LLVM::LLVMFunctionType::get(
            i64, {pointer, pointer, pointer, pointer, pointer}, false));
    Block *entry = evalStep.addEntryBlock(builder);
    Block *done = new Block;
    done->addArgument(i64, location);
    evalStep.getBody().push_back(done);
    SmallVector<Block *> tests;
    SmallVector<Block *> bodies;
    for ([[maybe_unused]] const NativeThreeTierKernelPlan *kernel :
         executable) {
      Block *test = new Block;
      test->addArgument(i64, location);
      Block *body = new Block;
      body->addArgument(i64, location);
      evalStep.getBody().getBlocks().insert(Region::iterator(done), test);
      evalStep.getBody().getBlocks().insert(Region::iterator(done), body);
      tests.push_back(test);
      bodies.push_back(body);
    }

    builder.setInsertionPointToStart(entry);
    Block *first = tests.empty() ? done : tests.front();
    cf::BranchOp::create(builder, location, first,
                         ValueRange{llvmConstant(builder, location, i64, 0)});

    auto candidateIndex = [&](uint32_t id) -> std::optional<uint64_t> {
      auto found = llvm::find_if(candidates, [&](const auto *candidate) {
        return candidate->id == id;
      });
      if (found == candidates.end())
        return std::nullopt;
      return static_cast<uint64_t>(found - candidates.begin());
    };
    auto variantsFor = [&](uint32_t id) -> Tier1Variants * {
      auto found = llvm::find_if(tier1Variants, [&](const auto &variants) {
        return variants.kernel == id;
      });
      return found == tier1Variants.end() ? nullptr : &*found;
    };
    for (auto [index, kernel] : llvm::enumerate(executable)) {
      Block *test = tests[index];
      Block *body = bodies[index];
      Block *next = index + 1 == tests.size() ? done : tests[index + 1];
      builder.setInsertionPointToStart(test);
      Value ownerAddress = LLVM::GEPOp::create(
          builder, location, pointer, i64, entry->getArgument(1),
          ValueRange{llvmConstant(builder, location, i64, kernel->owner)});
      Value ready =
          LLVM::LoadOp::create(builder, location, i64, ownerAddress, 8);
      uint64_t readyMask = uint64_t{1} << kernel->readyBit;
      Value selected = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ne,
          arith::AndIOp::create(
              builder, location, ready,
              llvmConstant(builder, location, i64, readyMask)),
          llvmConstant(builder, location, i64, 0));
      cf::CondBranchOp::create(builder, location, selected, body,
                               test->getArguments(), next,
                               test->getArguments());

      builder.setInsertionPointToStart(body);
      LLVM::StoreOp::create(
          builder, location,
          arith::AndIOp::create(
              builder, location, ready,
              llvmConstant(builder, location, i64, ~readyMask)),
          ownerAddress, 8);
      Value publications;
      if (kernel->tier == sim::SchedulerTierKind::Tier2) {
        SmallString<64> subkernelName;
        (Twine("__obelisk_tier2_converge_v1_") + Twine(kernel->id))
            .toVector(subkernelName);
        uint64_t ingress = kernel->memberCount == 64
                               ? UINT64_MAX
                               : (uint64_t{1} << kernel->memberCount) - 1;
        publications =
            LLVM::CallOp::create(
                builder, location, TypeRange{i64},
                SymbolRefAttr::get(context, subkernelName),
                ValueRange{entry->getArgument(0),
                           llvmConstant(builder, location, i64, ingress),
                           entry->getArgument(4)})
                .getResult();
      } else {
        Tier1Variants *variants = variantsFor(kernel->id);
        if (!variants)
          return module.emitError("Tier-1 kernel has no generated variant"),
                 failure();
        Value selectedFunction = LLVM::AddressOfOp::create(
            builder, location, pointer, variants->fourState.getSymName());
        Value selectedCallback = entry->getArgument(2);
        if (std::optional<uint64_t> candidate = candidateIndex(kernel->id)) {
          Value routeBase =
              LLVM::AddressOfOp::create(builder, location, pointer, routeName);
          Value routeAddress = LLVM::GEPOp::create(
              builder, location, pointer, i8, routeBase,
              ValueRange{llvmConstant(builder, location, i64, *candidate)});
          Value route =
              LLVM::LoadOp::create(builder, location, i8, routeAddress, 1);
          Value useTwoState = arith::CmpIOp::create(
              builder, location, arith::CmpIPredicate::ne, route,
              llvmConstant(builder, location, i8, 0));
          Value twoStateFunction = LLVM::AddressOfOp::create(
              builder, location, pointer, variants->twoState.getSymName());
          selectedFunction =
              arith::SelectOp::create(builder, location, useTwoState,
                                      twoStateFunction, selectedFunction);
          selectedCallback =
              arith::SelectOp::create(builder, location, useTwoState,
                                      entry->getArgument(3), selectedCallback);
        }
        publications = LLVM::CallOp::create(builder, location, wrapperType,
                                            ValueRange{selectedFunction,
                                                       entry->getArgument(0),
                                                       selectedCallback})
                           .getResult();
      }
      Value combined = arith::OrIOp::create(builder, location,
                                            body->getArgument(0), publications);
      cf::BranchOp::create(builder, location, next, ValueRange{combined});
    }
    builder.setInsertionPointToStart(done);
    LLVM::ReturnOp::create(builder, location, done->getArgument(0));
  }

  return success();
}

} // namespace obelisk::detail
