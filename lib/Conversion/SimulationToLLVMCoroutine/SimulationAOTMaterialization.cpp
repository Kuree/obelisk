//===- SimulationAOTMaterialization.cpp - Native AOT LLVM plan --------===//

#include "SimulationAOTPlanning.h"
#include "SimulationToLLVMCoroutinePrivate.h"

#include "obelisk/Runtime/Runtime.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"

#include "llvm/ADT/STLExtras.h"

using namespace mlir;

namespace obelisk::detail {

LogicalResult makeNativeAOTPlan(
    ModuleOp module, uint32_t actorCount,
    ArrayRef<obelisk_rt_native_schedule_node> executableNodes,
    const NativeStateLayout &stateLayout,
    const NativeStaticNBAPlan &staticNBAPlan,
    const NativeStaticFanoutPlan &staticFanoutPlan,
    ArrayRef<obelisk_rt_static_actor_root> actorRoots, bool enableDirectState,
    bool enableStaticNBA, bool enableStaticControl, bool enableStaticFanout,
    bool enableCleanSuperstep, bool fullyStatic, bool rootSlotZero,
    const analysis::SimulationVPIAnalysis &vpi) {
  if (actorCount == 0 || executableNodes.empty())
    return module.emitError("AOT schedule has no executable actor nodes");
  MLIRContext *context = module.getContext();
  OpBuilder builder(context);
  Location location = module.getLoc();
  Type pointer = LLVM::LLVMPointerType::get(context);
  Type i32 = builder.getI32Type();
  Type i64 = builder.getI64Type();
  ArrayRef<obelisk_rt_static_nba_root> nbaRoots = staticNBAPlan.roots;
  ArrayRef<obelisk_rt_static_nba_site> nbaSites = staticNBAPlan.sites;
  SmallVector<obelisk_rt_static_fanout_entry> indexedFanoutEntries;
  if (staticFanoutPlan.exact) {
    indexedFanoutEntries.assign(staticFanoutPlan.entries.begin(),
                                staticFanoutPlan.entries.end());
    for (obelisk_rt_static_fanout_entry &entry : indexedFanoutEntries) {
      auto node = llvm::find_if(executableNodes, [&](const auto &candidate) {
        return candidate.actor_slot == entry.actor_slot &&
               candidate.continuation == entry.continuation;
      });
      if (node == executableNodes.end())
        return module.emitError(
            "static fanout has no indexed compute fragment");
      entry.compute_node =
          static_cast<uint32_t>(node - executableNodes.begin());
      entry.reserved = 0;
    }
  }
  ArrayRef<obelisk_rt_static_fanout_entry> fanoutEntries =
      indexedFanoutEntries;
  // Static time/region control and exact actor fanout are independent of VPI
  // reads. Writable VPI hands dirty roots and the affected event slot to the
  // existing guarded state/control paths; the exact dependency table remains
  // valid and can continue to wake native actors without subscriptions.
  bool staticControlEnabled =
      enableStaticControl && fullyStatic && vpi.hasComputeGraph();
  bool staticFanoutEnabled = enableStaticFanout && staticFanoutPlan.exact &&
                             fullyStatic && vpi.hasComputeGraph();
  bool guardedFanoutEnabled = !staticFanoutEnabled && staticFanoutPlan.exact &&
                              fullyStatic && vpi.hasComputeGraph();
  bool guardedSpecializationEnabled =
      vpi.allowsWrite() && (enableDirectState || enableStaticNBA);
  bool cleanSuperstepEnabled = enableCleanSuperstep && staticControlEnabled &&
                               staticFanoutPlan.exact && fullyStatic;
  uint64_t graphLayoutChecksum = 0;
  if (auto image =
          module->getAttrOfType<DenseI8ArrayAttr>("obelisk.bytecode.image")) {
    ArrayRef<int8_t> bytes = image.asArrayRef();
    if (bytes.size() < 40)
      return module.emitError("embedded bytecode checksum is truncated");
    for (unsigned byte = 0; byte != 8; ++byte)
      graphLayoutChecksum |= uint64_t{static_cast<uint8_t>(bytes[32 + byte])}
                             << (byte * 8);
  }
  Type stateType = LLVM::LLVMArrayType::get(pointer, actorCount);
  constexpr StringLiteral stateName = "__obelisk_aot_schedule_state_v1";
  constexpr StringLiteral nodesName = "__obelisk_aot_schedule_nodes_v1";
  constexpr StringLiteral nbaRootsName = "__obelisk_aot_nba_roots_v1";
  constexpr StringLiteral nbaSitesName = "__obelisk_aot_nba_sites_v1";
  constexpr StringLiteral nbaDirtyRootsName =
      "__obelisk_aot_nba_dirty_roots_v1";
  constexpr StringLiteral nbaDirtySummaryName =
      "__obelisk_aot_nba_dirty_summary_v1";
  constexpr StringLiteral fanoutName = "__obelisk_aot_static_fanout_v1";
  constexpr StringLiteral actorRootsName =
      "__obelisk_aot_static_actor_roots_v1";
  constexpr StringLiteral bindName = "__obelisk_aot_schedule_bind_v1";
  constexpr StringLiteral runName = "__obelisk_aot_schedule_run_v1";
  constexpr StringLiteral snapshotName = "__obelisk_aot_schedule_snapshot_v1";
  constexpr StringLiteral nbaCommitName = "__obelisk_aot_static_nba_commit_v1";
  constexpr StringLiteral planName = "__obelisk_aot_schedule_plan_v1";

  builder.setInsertionPointToStart(module.getBody());
  auto state = LLVM::GlobalOp::create(builder, location, stateType, false,
                                      LLVM::Linkage::Internal, stateName,
                                      Attribute{}, 8);
  Block *initializer = new Block;
  state.getInitializerRegion().push_back(initializer);
  builder.setInsertionPointToStart(initializer);
  LLVM::ReturnOp::create(builder, location,
                         LLVM::ZeroOp::create(builder, location, stateType));

  Type nodeType = LLVM::LLVMStructType::getLiteral(context, {i32, i32, i32});
  Type nodesType = LLVM::LLVMArrayType::get(nodeType, executableNodes.size());
  makeConstantGlobal(
      module, location, nodesType, nodesName, LLVM::Linkage::Internal, 4,
      [&](OpBuilder &initializerBuilder) {
        Value nodes =
            LLVM::ZeroOp::create(initializerBuilder, location, nodesType);
        for (auto [index, node] : llvm::enumerate(executableNodes)) {
          Value value =
              LLVM::ZeroOp::create(initializerBuilder, location, nodeType);
          value = insertValue(
              initializerBuilder, location, value,
              llvmConstant(initializerBuilder, location, i32, node.actor_slot),
              0);
          value = insertValue(initializerBuilder, location, value,
                              llvmConstant(initializerBuilder, location, i32,
                                           node.continuation),
                              1);
          value = insertValue(initializerBuilder, location, value,
                              llvmConstant(initializerBuilder, location, i32,
                                           node.fusion_group),
                              2);
          nodes = LLVM::InsertValueOp::create(
              initializerBuilder, location, nodes, value,
              ArrayRef<int64_t>{static_cast<int64_t>(index)});
        }
        return nodes;
      });

  Type nbaRootType =
      LLVM::LLVMStructType::getLiteral(context, {i32, i32, i64, pointer});
  if (!nbaRoots.empty()) {
    Type rootsType = LLVM::LLVMArrayType::get(nbaRootType, nbaRoots.size());
    makeConstantGlobal(
        module, location, rootsType, nbaRootsName, LLVM::Linkage::Internal, 8,
        [&](OpBuilder &initializerBuilder) {
          Value roots =
              LLVM::ZeroOp::create(initializerBuilder, location, rootsType);
          for (auto [index, root] : llvm::enumerate(nbaRoots)) {
            Value value =
                LLVM::ZeroOp::create(initializerBuilder, location, nbaRootType);
            value = insertValue(initializerBuilder, location, value,
                                llvmConstant(initializerBuilder, location, i32,
                                             root.commit_node),
                                0);
            value = insertValue(initializerBuilder, location, value,
                                llvmConstant(initializerBuilder, location, i32,
                                             root.static_state),
                                1);
            value = insertValue(
                initializerBuilder, location, value,
                llvmConstant(initializerBuilder, location, i64, root.bit_width),
                2);
            Value accumulator =
                index < staticNBAPlan.generatedAccumulators.size() &&
                        !staticNBAPlan.generatedAccumulators[index].empty()
                    ? LLVM::AddressOfOp::create(
                          initializerBuilder, location, pointer,
                          staticNBAPlan.generatedAccumulators[index])
                          .getResult()
                    : LLVM::ZeroOp::create(initializerBuilder, location,
                                           pointer)
                          .getResult();
            value = insertValue(initializerBuilder, location, value,
                                accumulator, 3);
            roots = LLVM::InsertValueOp::create(
                initializerBuilder, location, roots, value,
                ArrayRef<int64_t>{static_cast<int64_t>(index)});
          }
          return roots;
        });
  }
  uint32_t nbaDirtyWordCount =
      static_cast<uint32_t>((nbaRoots.size() + 63) / 64);
  uint32_t nbaDirtySummaryWordCount = (nbaDirtyWordCount + 63) / 64;
  if (nbaDirtyWordCount != 0) {
    Type dirtyType = LLVM::LLVMArrayType::get(i64, nbaDirtyWordCount);
    builder.setInsertionPointToStart(module.getBody());
    auto dirty = LLVM::GlobalOp::create(
        builder, location, dirtyType, false, LLVM::Linkage::Internal,
        nbaDirtyRootsName, Attribute{}, 8);
    Block *dirtyInitializer = new Block;
    dirty.getInitializerRegion().push_back(dirtyInitializer);
    builder.setInsertionPointToStart(dirtyInitializer);
    LLVM::ReturnOp::create(
        builder, location,
        LLVM::ZeroOp::create(builder, location, dirtyType));
  }
  if (nbaDirtySummaryWordCount != 0) {
    Type summaryType =
        LLVM::LLVMArrayType::get(i64, nbaDirtySummaryWordCount);
    builder.setInsertionPointToStart(module.getBody());
    auto summary = LLVM::GlobalOp::create(
        builder, location, summaryType, false, LLVM::Linkage::Internal,
        nbaDirtySummaryName, Attribute{}, 8);
    Block *summaryInitializer = new Block;
    summary.getInitializerRegion().push_back(summaryInitializer);
    builder.setInsertionPointToStart(summaryInitializer);
    LLVM::ReturnOp::create(
        builder, location,
        LLVM::ZeroOp::create(builder, location, summaryType));
  }
  Type nbaSiteType = LLVM::LLVMStructType::getLiteral(context, {i64, i32, i32});
  if (!nbaSites.empty()) {
    Type sitesType = LLVM::LLVMArrayType::get(nbaSiteType, nbaSites.size());
    makeConstantGlobal(
        module, location, sitesType, nbaSitesName, LLVM::Linkage::Internal, 8,
        [&](OpBuilder &initializerBuilder) {
          Value sites =
              LLVM::ZeroOp::create(initializerBuilder, location, sitesType);
          for (auto [index, site] : llvm::enumerate(nbaSites)) {
            Value value =
                LLVM::ZeroOp::create(initializerBuilder, location, nbaSiteType);
            value = insertValue(
                initializerBuilder, location, value,
                llvmConstant(initializerBuilder, location, i64, site.site), 0);
            value = insertValue(
                initializerBuilder, location, value,
                llvmConstant(initializerBuilder, location, i32, site.root), 1);
            value = insertValue(
                initializerBuilder, location, value,
                llvmConstant(initializerBuilder, location, i32, site.storage),
                2);
            sites = LLVM::InsertValueOp::create(
                initializerBuilder, location, sites, value,
                ArrayRef<int64_t>{static_cast<int64_t>(index)});
          }
          return sites;
        });
  }
  Type fanoutType = LLVM::LLVMStructType::getLiteral(
      context, {i32, i32, i32, i32, i32, i32, i64, i64});
  if (!fanoutEntries.empty()) {
    Type entriesType =
        LLVM::LLVMArrayType::get(fanoutType, fanoutEntries.size());
    makeConstantGlobal(
        module, location, entriesType, fanoutName, LLVM::Linkage::Internal, 8,
        [&](OpBuilder &initializerBuilder) {
          Value entries =
              LLVM::ZeroOp::create(initializerBuilder, location, entriesType);
          for (auto [index, entry] : llvm::enumerate(fanoutEntries)) {
            Value value =
                LLVM::ZeroOp::create(initializerBuilder, location, fanoutType);
            value = insertValue(initializerBuilder, location, value,
                                llvmConstant(initializerBuilder, location, i32,
                                             entry.static_state),
                                0);
            value = insertValue(initializerBuilder, location, value,
                                llvmConstant(initializerBuilder, location, i32,
                                             entry.actor_slot),
                                1);
            value = insertValue(initializerBuilder, location, value,
                                llvmConstant(initializerBuilder, location, i32,
                                             entry.continuation),
                                2);
            value = insertValue(
                initializerBuilder, location, value,
                llvmConstant(initializerBuilder, location, i32, entry.edge), 3);
            value = insertValue(
                initializerBuilder, location, value,
                llvmConstant(initializerBuilder, location, i32,
                             entry.compute_node),
                4);
            value = insertValue(
                initializerBuilder, location, value,
                llvmConstant(initializerBuilder, location, i32,
                             entry.reserved),
                5);
            value = insertValue(
                initializerBuilder, location, value,
                llvmConstant(initializerBuilder, location, i64, entry.low_bit),
                6);
            value = insertValue(initializerBuilder, location, value,
                                llvmConstant(initializerBuilder, location, i64,
                                             entry.bit_width),
                                7);
            entries = LLVM::InsertValueOp::create(
                initializerBuilder, location, entries, value,
                ArrayRef<int64_t>{static_cast<int64_t>(index)});
          }
          return entries;
        });
  }
  Type actorRootType =
      LLVM::LLVMStructType::getLiteral(context, {i32, i32, i32, i32});
  if (!actorRoots.empty()) {
    Type entriesType =
        LLVM::LLVMArrayType::get(actorRootType, actorRoots.size());
    makeConstantGlobal(
        module, location, entriesType, actorRootsName, LLVM::Linkage::Internal,
        4, [&](OpBuilder &initializerBuilder) {
          Value entries =
              LLVM::ZeroOp::create(initializerBuilder, location, entriesType);
          for (auto [index, entry] : llvm::enumerate(actorRoots)) {
            Value value = LLVM::ZeroOp::create(initializerBuilder, location,
                                               actorRootType);
            value = insertValue(initializerBuilder, location, value,
                                llvmConstant(initializerBuilder, location, i32,
                                             entry.actor_slot),
                                0);
            value = insertValue(initializerBuilder, location, value,
                                llvmConstant(initializerBuilder, location, i32,
                                             entry.static_state),
                                1);
            value = insertValue(
                initializerBuilder, location, value,
                llvmConstant(initializerBuilder, location, i32, entry.flags),
                2);
            entries = LLVM::InsertValueOp::create(
                initializerBuilder, location, entries, value,
                ArrayRef<int64_t>{static_cast<int64_t>(index)});
          }
          return entries;
        });
  }

  builder.setInsertionPointToEnd(module.getBody());
  auto bind = LLVM::LLVMFuncOp::create(
      builder, location, bindName,
      LLVM::LLVMFunctionType::get(i32, {pointer, pointer, i32, pointer},
                                  false));
  Block *bindEntry = bind.addEntryBlock(builder);
  builder.setInsertionPointToStart(bindEntry);
  Value slot =
      LLVM::ZExtOp::create(builder, location, i64, bindEntry->getArgument(2));
  Value actorAddress =
      LLVM::GEPOp::create(builder, location, pointer, pointer,
                          bindEntry->getArgument(0), ValueRange{slot});
  LLVM::StoreOp::create(builder, location, bindEntry->getArgument(3),
                        actorAddress, 8);
  LLVM::ReturnOp::create(builder, location,
                         llvmConstant(builder, location, i32, OBELISK_RT_OK));

  builder.setInsertionPointToEnd(module.getBody());
  auto run = LLVM::LLVMFuncOp::create(
      builder, location, runName,
      LLVM::LLVMFunctionType::get(i32, {pointer, pointer}, false));
  Block *runEntry = run.addEntryBlock(builder);
  builder.setInsertionPointToStart(runEntry);
  Value nodes =
      LLVM::AddressOfOp::create(builder, location, pointer, nodesName);
  Value runStatus =
      LLVM::CallOp::create(
          builder, location, TypeRange{i32},
          SymbolRefAttr::get(
              context, fullyStatic ? "obelisk_rt_v1_scheduler_run_aot_nodes"
                                   : "obelisk_rt_v1_scheduler_run"),
          fullyStatic ? ValueRange{runEntry->getArgument(1), nodes,
                                   llvmConstant(builder, location, i32,
                                                executableNodes.size())}
                      : ValueRange{runEntry->getArgument(1)})
          .getResult();
  LLVM::ReturnOp::create(builder, location, runStatus);

  builder.setInsertionPointToEnd(module.getBody());
  auto snapshot = LLVM::LLVMFuncOp::create(
      builder, location, snapshotName,
      LLVM::LLVMFunctionType::get(i32, {pointer, pointer, pointer}, false));
  Block *snapshotEntry = snapshot.addEntryBlock(builder);
  builder.setInsertionPointToStart(snapshotEntry);
  Value snapshotStatus =
      LLVM::CallOp::create(
          builder, location, TypeRange{i32},
          SymbolRefAttr::get(context, "obelisk_rt_v1_scheduler_snapshot_aot"),
          ValueRange{snapshotEntry->getArgument(1),
                     snapshotEntry->getArgument(2)})
          .getResult();
  LLVM::ReturnOp::create(builder, location, snapshotStatus);

  builder.setInsertionPointToEnd(module.getBody());
  auto nbaCommit = LLVM::LLVMFuncOp::create(
      builder, location, nbaCommitName,
      LLVM::LLVMFunctionType::get(i32, {pointer, pointer, i32, pointer},
                                  false));
  Block *nbaCommitEntry = nbaCommit.addEntryBlock(builder);
  Block *genericNBACommit = new Block;
  nbaCommit.getBody().push_back(genericNBACommit);
  builder.setInsertionPointToStart(nbaCommitEntry);
  Value committedCount = entryAlloca(builder, location, i32, 1, 4);
  LLVM::StoreOp::create(builder, location,
                        llvmConstant(builder, location, i32, 0),
                        committedCount, 4);

  bool generateScalarCommits = cleanSuperstepEnabled && enableDirectState &&
                               !guardedSpecializationEnabled &&
                               staticNBAPlan.generatedOffsets.size() ==
                                   nbaRoots.size();
  SmallVector<SmallVector<uint32_t>> scalarRootsByWord(nbaDirtyWordCount);
  uint64_t planeBytes = (stateLayout.bitCount + 7) / 8;
  if (generateScalarCommits)
    for (auto [rootIndex, root, accumulator, offset] : llvm::enumerate(
             nbaRoots, staticNBAPlan.generatedAccumulators,
             staticNBAPlan.generatedOffsets)) {
      uint64_t firstByte = offset / 8;
      uint64_t shift = offset % 8;
      bool crossesWord = root.bit_width > 64 - shift;
      bool addressable = root.bit_width != 0 && root.bit_width <= 64 &&
                         !accumulator.empty() && firstByte + 8 <= planeBytes &&
                         (!crossesWord || firstByte + 9 <= planeBytes);
      if (addressable)
        scalarRootsByWord[rootIndex / 64].push_back(
            static_cast<uint32_t>(rootIndex));
    }

  // Record roots actually consumed by the direct path. The generic
  // continuation still owns the canonical dirty hierarchy, but need not
  // rediscover that every generated accumulator in a leaf was cleared.
  SmallVector<Value> directlyCommittedByWord(nbaDirtyWordCount);
  for (uint32_t word = 0; word != nbaDirtyWordCount; ++word)
    if (!scalarRootsByWord[word].empty()) {
      directlyCommittedByWord[word] =
          entryAlloca(builder, location, i64, 1, 8);
      LLVM::StoreOp::create(builder, location,
                            llvmConstant(builder, location, i64, 0),
                            directlyCommittedByWord[word], 8);
    }

  bool generateGroupedFanout = llvm::any_of(
      scalarRootsByWord, [&](ArrayRef<uint32_t> roots) {
        return llvm::any_of(roots, [&](uint32_t rootIndex) {
          return llvm::any_of(
              fanoutEntries,
              [&](const obelisk_rt_static_fanout_entry &entry) {
                return entry.static_state == nbaRoots[rootIndex].static_state;
              });
        });
      });
  uint32_t activationWordCount =
      generateGroupedFanout
          ? static_cast<uint32_t>((executableNodes.size() + 63) / 64)
          : 0;
  Value activatedNodes;
  if (generateGroupedFanout) {
    activatedNodes =
        entryAlloca(builder, location, i64, activationWordCount, 8);
    for (uint32_t word = 0; word != activationWordCount; ++word)
      LLVM::StoreOp::create(
          builder, location, llvmConstant(builder, location, i64, 0),
          byteGEP(builder, location, activatedNodes,
                  uint64_t{word} * sizeof(uint64_t)),
          8);
  }

  Value directGuard =
      LLVM::CallOp::create(
          builder, location, TypeRange{i32},
          SymbolRefAttr::get(context,
                             "obelisk_rt_v1_static_nba_direct_commit_guard"),
          ValueRange{nbaCommitEntry->getArgument(1)})
          .getResult();
  Value directEnabled = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::ne, directGuard,
      llvmConstant(builder, location, i32, 0));
  Value stateValue = LLVM::AddressOfOp::create(
      builder, location, pointer, "__obelisk_state_value");
  Value stateUnknown = LLVM::AddressOfOp::create(
      builder, location, pointer, "__obelisk_state_unknown");

  SmallVector<Block *> wordBlocks(nbaDirtyWordCount);
  for (uint32_t word = 0; word != nbaDirtyWordCount; ++word)
    if (!scalarRootsByWord[word].empty()) {
      wordBlocks[word] = new Block;
      nbaCommit.getBody().getBlocks().insert(
          Region::iterator(genericNBACommit), wordBlocks[word]);
    }
  Block *firstWord = genericNBACommit;
  for (Block *block : wordBlocks)
    if (block) {
      firstWord = block;
      break;
    }
  cf::CondBranchOp::create(builder, location, directEnabled, firstWord,
                           ValueRange{}, genericNBACommit, ValueRange{});

  auto nextWordAfter = [&](uint32_t current) -> Block * {
    for (uint32_t word = current + 1; word != nbaDirtyWordCount; ++word)
      if (wordBlocks[word])
        return wordBlocks[word];
    return genericNBACommit;
  };
  auto scalarMask = [](uint64_t width) {
    return width == 64 ? UINT64_MAX : (uint64_t{1} << width) - 1;
  };
  auto loadRoot = [&](Value plane, uint64_t offset, uint64_t width) {
    uint64_t firstByte = offset / 8;
    uint64_t shift = offset % 8;
    Value low = LLVM::LoadOp::create(
        builder, location, i64,
        byteGEP(builder, location, plane, firstByte), 1);
    Value value = low;
    if (shift != 0)
      value = arith::ShRUIOp::create(
          builder, location, value,
          llvmConstant(builder, location, i64, shift));
    if (width > 64 - shift) {
      Value high = LLVM::LoadOp::create(
          builder, location, builder.getI8Type(),
          byteGEP(builder, location, plane, firstByte + 8), 1);
      high = LLVM::ZExtOp::create(builder, location, i64, high);
      high = arith::ShLIOp::create(
          builder, location, high,
          llvmConstant(builder, location, i64, 64 - shift));
      value = arith::OrIOp::create(builder, location, value, high);
    }
    return arith::AndIOp::create(
               builder, location, value,
               llvmConstant(builder, location, i64, scalarMask(width)))
        .getResult();
  };
  auto storeRoot = [&](Value plane, uint64_t offset, uint64_t width,
                       Value value) {
    uint64_t firstByte = offset / 8;
    uint64_t shift = offset % 8;
    Value address = byteGEP(builder, location, plane, firstByte);
    Value oldLow = LLVM::LoadOp::create(builder, location, i64, address, 1);
    uint64_t lowMask = scalarMask(width) << shift;
    Value cleared = arith::AndIOp::create(
        builder, location, oldLow,
        llvmConstant(builder, location, i64, ~lowMask));
    Value positioned = value;
    if (shift != 0)
      positioned = arith::ShLIOp::create(
          builder, location, positioned,
          llvmConstant(builder, location, i64, shift));
    positioned = arith::AndIOp::create(
        builder, location, positioned,
        llvmConstant(builder, location, i64, lowMask));
    LLVM::StoreOp::create(
        builder, location,
        arith::OrIOp::create(builder, location, cleared, positioned), address,
        1);
    if (width <= 64 - shift)
      return;
    uint64_t highWidth = width - (64 - shift);
    uint8_t highMask = static_cast<uint8_t>((uint16_t{1} << highWidth) - 1);
    Value highAddress = byteGEP(builder, location, plane, firstByte + 8);
    Value oldHigh = LLVM::LoadOp::create(builder, location,
                                         builder.getI8Type(), highAddress, 1);
    Value highValue = arith::ShRUIOp::create(
        builder, location, value,
        llvmConstant(builder, location, i64, 64 - shift));
    highValue = LLVM::TruncOp::create(builder, location, builder.getI8Type(),
                                      highValue);
    Value newHigh = arith::OrIOp::create(
        builder, location,
        arith::AndIOp::create(
            builder, location, oldHigh,
            llvmConstant(builder, location, builder.getI8Type(),
                         static_cast<uint8_t>(~highMask))),
        arith::AndIOp::create(
            builder, location, highValue,
            llvmConstant(builder, location, builder.getI8Type(), highMask)));
    LLVM::StoreOp::create(builder, location, newHigh, highAddress, 1);
  };

  for (uint32_t word = 0; word != nbaDirtyWordCount; ++word) {
    if (!wordBlocks[word])
      continue;
    builder.setInsertionPointToStart(wordBlocks[word]);
    Value dirtyBase = LLVM::AddressOfOp::create(
        builder, location, pointer, nbaDirtyRootsName);
    Value dirty = LLVM::LoadOp::create(
        builder, location, i64,
        byteGEP(builder, location, dirtyBase,
                uint64_t{word} * sizeof(uint64_t)),
        8);
    Value wordEmpty = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, dirty,
        llvmConstant(builder, location, i64, 0));
    Block *next = nextWordAfter(word);
    Block *firstRoot = new Block;
    nbaCommit.getBody().getBlocks().insert(Region::iterator(next), firstRoot);
    cf::CondBranchOp::create(builder, location, wordEmpty, next, ValueRange{},
                             firstRoot, ValueRange{});

    Block *rootBlock = firstRoot;
    for (auto [position, rootIndex] :
         llvm::enumerate(scalarRootsByWord[word])) {
      const obelisk_rt_static_nba_root &root = nbaRoots[rootIndex];
      uint64_t offset = staticNBAPlan.generatedOffsets[rootIndex];
      StringRef accumulator =
          staticNBAPlan.generatedAccumulators[rootIndex];
      Block *afterRoot = position + 1 == scalarRootsByWord[word].size()
                             ? next
                             : new Block;
      if (afterRoot != next)
        nbaCommit.getBody().getBlocks().insert(Region::iterator(next),
                                               afterRoot);
      Block *commitRoot = new Block;
      nbaCommit.getBody().getBlocks().insert(Region::iterator(afterRoot),
                                             commitRoot);
      builder.setInsertionPointToStart(rootBlock);
      Value selected = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ne,
          arith::AndIOp::create(
              builder, location, dirty,
              llvmConstant(builder, location, i64,
                           uint64_t{1} << (rootIndex % 64))),
          llvmConstant(builder, location, i64, 0));
      Value accumulatorBase = LLVM::AddressOfOp::create(
          builder, location, pointer, accumulator);
      Value valid = LLVM::LoadOp::create(
          builder, location, i32,
          byteGEP(builder, location, accumulatorBase,
                  offsetof(obelisk_rt_generated_nba_accumulator_256, valid)),
          4);
      Value region = LLVM::LoadOp::create(
          builder, location, i32,
          byteGEP(
              builder, location, accumulatorBase,
              offsetof(obelisk_rt_generated_nba_accumulator_256, exec_region)),
          4);
      Value validRoot = arith::AndIOp::create(
          builder, location, selected,
          arith::AndIOp::create(
              builder, location,
              arith::CmpIOp::create(
                  builder, location, arith::CmpIPredicate::ne, valid,
                  llvmConstant(builder, location, i32, 0)),
              arith::CmpIOp::create(
                  builder, location, arith::CmpIPredicate::eq, region,
                  nbaCommitEntry->getArgument(2))));
      cf::CondBranchOp::create(builder, location, validRoot, commitRoot,
                               ValueRange{}, afterRoot, ValueRange{});

      builder.setInsertionPointToStart(commitRoot);
      Value stagedValue = LLVM::LoadOp::create(
          builder, location, i64,
          byteGEP(builder, location, accumulatorBase,
                  offsetof(obelisk_rt_generated_nba_accumulator_256, value)),
          8);
      Value stagedUnknown = LLVM::LoadOp::create(
          builder, location, i64,
          byteGEP(builder, location, accumulatorBase,
                  offsetof(obelisk_rt_generated_nba_accumulator_256, unknown)),
          8);
      Value writeMask = LLVM::LoadOp::create(
          builder, location, i64,
          byteGEP(
              builder, location, accumulatorBase,
              offsetof(obelisk_rt_generated_nba_accumulator_256, write_mask)),
          8);
      writeMask = arith::AndIOp::create(
          builder, location, writeMask,
          llvmConstant(builder, location, i64, scalarMask(root.bit_width)));
      Value oldValue = loadRoot(stateValue, offset, root.bit_width);
      Value oldUnknown = loadRoot(stateUnknown, offset, root.bit_width);
      Value inverseMask = arith::XOrIOp::create(
          builder, location, writeMask,
          llvmConstant(builder, location, i64, UINT64_MAX));
      Value newValue = arith::OrIOp::create(
          builder, location,
          arith::AndIOp::create(builder, location, oldValue, inverseMask),
          arith::AndIOp::create(builder, location, stagedValue, writeMask));
      Value newUnknown = arith::OrIOp::create(
          builder, location,
          arith::AndIOp::create(builder, location, oldUnknown, inverseMask),
          arith::AndIOp::create(builder, location, stagedUnknown, writeMask));
      storeRoot(stateValue, offset, root.bit_width, newValue);
      storeRoot(stateUnknown, offset, root.bit_width, newUnknown);
      LLVM::StoreOp::create(builder, location,
                            llvmConstant(builder, location, i64, 0),
                            byteGEP(builder, location, accumulatorBase,
                                    offsetof(
                                        obelisk_rt_generated_nba_accumulator_256,
                                        write_mask)),
                            8);
      LLVM::StoreOp::create(
          builder, location, llvmConstant(builder, location, i32, 0),
          byteGEP(builder, location, accumulatorBase,
                  offsetof(obelisk_rt_generated_nba_accumulator_256, valid)),
          4);
      Value directlyCommitted = LLVM::LoadOp::create(
          builder, location, i64, directlyCommittedByWord[word], 8);
      LLVM::StoreOp::create(
          builder, location,
          arith::OrIOp::create(
              builder, location, directlyCommitted,
              llvmConstant(builder, location, i64,
                           uint64_t{1} << (rootIndex % 64))),
          directlyCommittedByWord[word], 8);
      Value changed = arith::OrIOp::create(
          builder, location,
          arith::XOrIOp::create(builder, location, oldValue, newValue),
          arith::XOrIOp::create(builder, location, oldUnknown, newUnknown));
      Value rootChanged = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ne, changed,
          llvmConstant(builder, location, i64, 0));
      Value priorChanged = LLVM::LoadOp::create(
          builder, location, i32, nbaCommitEntry->getArgument(3), 4);
      Value changedI32 = LLVM::ZExtOp::create(builder, location, i32,
                                              rootChanged);
      LLVM::StoreOp::create(
          builder, location,
          arith::OrIOp::create(builder, location, priorChanged, changedI32),
          nbaCommitEntry->getArgument(3), 4);
      Value count = LLVM::LoadOp::create(builder, location, i32,
                                         committedCount, 4);
      LLVM::StoreOp::create(
          builder, location,
          arith::AddIOp::create(
              builder, location, count,
              llvmConstant(builder, location, i32, uint32_t{1})),
          committedCount, 4);
      struct TriggerGroup {
        uint32_t edge;
        uint64_t mask;
        SmallVector<uint64_t> nodes;
      };
      SmallVector<TriggerGroup> groups;
      for (const obelisk_rt_static_fanout_entry &entry : fanoutEntries) {
        if (entry.static_state != root.static_state ||
            entry.low_bit >= root.bit_width)
          continue;
        uint64_t high = std::min<uint64_t>(
            root.bit_width, entry.low_bit + entry.bit_width);
        if (entry.low_bit >= high)
          continue;
        uint64_t mask = scalarMask(high - entry.low_bit) << entry.low_bit;
        auto group = llvm::find_if(groups, [&](const TriggerGroup &candidate) {
          return candidate.edge == entry.edge && candidate.mask == mask;
        });
        if (group == groups.end()) {
          groups.push_back({entry.edge, mask,
                            SmallVector<uint64_t>(activationWordCount, 0)});
          group = std::prev(groups.end());
        }
        group->nodes[entry.compute_node / 64] |=
            uint64_t{1} << (entry.compute_node % 64);
      }
      if (!groups.empty()) {
        Value widthMask = llvmConstant(builder, location, i64,
                                       scalarMask(root.bit_width));
        auto invert = [&](Value value) {
          return arith::XOrIOp::create(
                     builder, location, value,
                     llvmConstant(builder, location, i64, UINT64_MAX))
              .getResult();
        };
        Value oldKnown = arith::AndIOp::create(
            builder, location, invert(oldUnknown), widthMask);
        Value newKnown = arith::AndIOp::create(
            builder, location, invert(newUnknown), widthMask);
        Value oldZero = arith::AndIOp::create(builder, location, oldKnown,
                                              invert(oldValue));
        Value oldOne = arith::AndIOp::create(builder, location, oldKnown,
                                             oldValue);
        Value newZero = arith::AndIOp::create(builder, location, newKnown,
                                              invert(newValue));
        Value newOne = arith::AndIOp::create(builder, location, newKnown,
                                             newValue);
        Value posedge = arith::AndIOp::create(
            builder, location,
            arith::OrIOp::create(
                builder, location,
                arith::AndIOp::create(builder, location, oldZero,
                                       invert(newZero)),
                arith::AndIOp::create(builder, location, oldUnknown, newOne)),
            widthMask);
        Value negedge = arith::AndIOp::create(
            builder, location,
            arith::OrIOp::create(
                builder, location,
                arith::AndIOp::create(builder, location, oldOne,
                                       invert(newOne)),
                arith::AndIOp::create(builder, location, oldUnknown, newZero)),
            widthMask);
        for (const TriggerGroup &group : groups) {
          Value observed = changed;
          switch (group.edge) {
          case OBELISK_RT_WAIT_EDGE_POSEDGE:
            observed = posedge;
            break;
          case OBELISK_RT_WAIT_EDGE_NEGEDGE:
            observed = negedge;
            break;
          case OBELISK_RT_WAIT_EDGE_BOTH:
            observed = arith::OrIOp::create(builder, location, posedge,
                                             negedge);
            break;
          default:
            break;
          }
          Value triggered = arith::CmpIOp::create(
              builder, location, arith::CmpIPredicate::ne,
              arith::AndIOp::create(
                  builder, location, observed,
                  llvmConstant(builder, location, i64, group.mask)),
              llvmConstant(builder, location, i64, 0));
          for (auto [activationWord, nodeMask] :
               llvm::enumerate(group.nodes)) {
            if (nodeMask == 0)
              continue;
            Value address = byteGEP(
                builder, location, activatedNodes,
                uint64_t{activationWord} * sizeof(uint64_t));
            Value active = LLVM::LoadOp::create(builder, location, i64,
                                                address, 8);
            Value selected = arith::SelectOp::create(
                builder, location, triggered,
                llvmConstant(builder, location, i64, nodeMask),
                llvmConstant(builder, location, i64, 0));
            LLVM::StoreOp::create(
                builder, location,
                arith::OrIOp::create(builder, location, active, selected),
                address, 8);
          }
        }
      }
      cf::BranchOp::create(builder, location, afterRoot);
      rootBlock = afterRoot;
    }
  }

  builder.setInsertionPointToStart(genericNBACommit);
  if (generateGroupedFanout)
    LLVM::CallOp::create(
        builder, location, TypeRange{},
        SymbolRefAttr::get(
            context, "obelisk_rt_v1_scheduler_activate_static_nodes"),
        ValueRange{nbaCommitEntry->getArgument(1), activatedNodes,
                   llvmConstant(builder, location, i32,
                                activationWordCount)});
  Value dirtyBase;
  if (nbaDirtyWordCount != 0)
    dirtyBase = LLVM::AddressOfOp::create(builder, location, pointer,
                                          nbaDirtyRootsName);
  for (uint32_t word = 0; word != nbaDirtyWordCount; ++word) {
    if (!directlyCommittedByWord[word])
      continue;
    Value dirtyAddress = byteGEP(builder, location, dirtyBase,
                                 uint64_t{word} * sizeof(uint64_t));
    Value dirty = LLVM::LoadOp::create(builder, location, i64, dirtyAddress, 8);
    Value committed = LLVM::LoadOp::create(
        builder, location, i64, directlyCommittedByWord[word], 8);
    LLVM::StoreOp::create(
        builder, location,
        arith::AndIOp::create(
            builder, location, dirty,
            arith::XOrIOp::create(
                builder, location, committed,
                llvmConstant(builder, location, i64, UINT64_MAX))),
        dirtyAddress, 8);
  }
  Value directCount = LLVM::LoadOp::create(builder, location, i32,
                                           committedCount, 4);
  LLVM::CallOp::create(
      builder, location, TypeRange{},
      SymbolRefAttr::get(
          context, "obelisk_rt_v1_static_nba_account_generated_commits"),
      ValueRange{nbaCommitEntry->getArgument(1), directCount});
  Value nbaCommitStatus =
      LLVM::CallOp::create(
          builder, location, TypeRange{i32},
          SymbolRefAttr::get(context, "obelisk_rt_v1_static_nba_commit_roots"),
          ValueRange{nbaCommitEntry->getArgument(1),
                     llvmConstant(builder, location, i32, nbaRoots.size()),
                     nbaCommitEntry->getArgument(2),
                     nbaCommitEntry->getArgument(3)})
          .getResult();
  LLVM::ReturnOp::create(builder, location, nbaCommitStatus);

  auto planType = LLVM::LLVMStructType::getLiteral(
      context,
      {i32, i64,     pointer, i64,     i32,     i32,     pointer, pointer,
       i64, pointer, pointer, pointer, pointer, i32,     i32,     pointer,
       i64, pointer, i64,     pointer, i64,     pointer, pointer, pointer,
       i32, i32,     pointer, i32,     i32});
  makeConstantGlobal(
      module, location, planType, planName, LLVM::Linkage::Internal, 8,
      [&](OpBuilder &initializerBuilder) {
        Value value =
            LLVM::ZeroOp::create(initializerBuilder, location, planType);
        value =
            insertValue(initializerBuilder, location, value,
                        llvmConstant(initializerBuilder, location, i32,
                                     sizeof(obelisk_rt_native_schedule_plan)),
                        0);
        value = insertValue(initializerBuilder, location, value,
                            llvmConstant(initializerBuilder, location, i64,
                                         graphLayoutChecksum),
                            1);
        value =
            insertValue(initializerBuilder, location, value,
                        LLVM::AddressOfOp::create(initializerBuilder, location,
                                                  pointer, stateName),
                        2);
        value = insertValue(initializerBuilder, location, value,
                            llvmConstant(initializerBuilder, location, i64,
                                         uint64_t{actorCount} * sizeof(void *)),
                            3);
        value = insertValue(
            initializerBuilder, location, value,
            llvmConstant(initializerBuilder, location, i32, actorCount), 4);
        value = insertValue(
            initializerBuilder, location, value,
            llvmConstant(
                initializerBuilder, location, i32,
                (fullyStatic ? OBELISK_RT_NATIVE_SCHEDULE_FULLY_STATIC : 0) |
                    (rootSlotZero ? OBELISK_RT_NATIVE_SCHEDULE_ROOT_SLOT_ZERO
                                  : 0) |
                    (staticControlEnabled
                         ? OBELISK_RT_NATIVE_SCHEDULE_STATIC_CONTROL
                         : 0) |
                    (staticFanoutEnabled
                         ? OBELISK_RT_NATIVE_SCHEDULE_STATIC_FANOUT
                         : 0) |
                    (enableDirectState ? OBELISK_RT_NATIVE_SCHEDULE_DIRECT_STATE
                                       : 0) |
                    (enableStaticNBA ? OBELISK_RT_NATIVE_SCHEDULE_STATIC_NBA
                                     : 0) |
                    (fullyStatic ? OBELISK_RT_NATIVE_SCHEDULE_GENERATED_ACTIONS
                                 : 0) |
                    (guardedFanoutEnabled
                         ? OBELISK_RT_NATIVE_SCHEDULE_GUARDED_FANOUT
                         : 0) |
                    (guardedSpecializationEnabled
                         ? OBELISK_RT_NATIVE_SCHEDULE_GUARDED_SPECIALIZATION
                         : 0) |
                    (cleanSuperstepEnabled
                         ? OBELISK_RT_NATIVE_SCHEDULE_CLEAN_SUPERSTEP
                         : 0)),
            5);
        value = insertValue(initializerBuilder, location, value,
                            LLVM::AddressOfOp::create(initializerBuilder,
                                                      location, pointer,
                                                      "__obelisk_state_value"),
                            6);
        value = insertValue(
            initializerBuilder, location, value,
            LLVM::AddressOfOp::create(initializerBuilder, location, pointer,
                                      "__obelisk_state_unknown"),
            7);
        value = insertValue(initializerBuilder, location, value,
                            llvmConstant(initializerBuilder, location, i64,
                                         stateLayout.bitCount),
                            8);
        value =
            insertValue(initializerBuilder, location, value,
                        LLVM::AddressOfOp::create(initializerBuilder, location,
                                                  pointer, bindName),
                        9);
        value = insertValue(initializerBuilder, location, value,
                            LLVM::AddressOfOp::create(
                                initializerBuilder, location, pointer, runName),
                            10);
        value =
            insertValue(initializerBuilder, location, value,
                        LLVM::AddressOfOp::create(initializerBuilder, location,
                                                  pointer, snapshotName),
                        11);
        Value rootsAddress =
            nbaRoots.empty()
                ? LLVM::ZeroOp::create(initializerBuilder, location, pointer)
                      .getResult()
                : LLVM::AddressOfOp::create(initializerBuilder, location,
                                            pointer, nbaRootsName)
                      .getResult();
        value =
            insertValue(initializerBuilder, location, value, rootsAddress, 12);
        value = insertValue(
            initializerBuilder, location, value,
            llvmConstant(initializerBuilder, location, i32, nbaRoots.size()),
            13);
        value =
            insertValue(initializerBuilder, location, value,
                        llvmConstant(initializerBuilder, location, i32, 0), 14);
        Value sitesAddress =
            nbaSites.empty()
                ? LLVM::ZeroOp::create(initializerBuilder, location, pointer)
                      .getResult()
                : LLVM::AddressOfOp::create(initializerBuilder, location,
                                            pointer, nbaSitesName)
                      .getResult();
        value =
            insertValue(initializerBuilder, location, value, sitesAddress, 15);
        value = insertValue(
            initializerBuilder, location, value,
            llvmConstant(initializerBuilder, location, i64, nbaSites.size()),
            16);
        Value fanoutAddress =
            fanoutEntries.empty()
                ? LLVM::ZeroOp::create(initializerBuilder, location, pointer)
                      .getResult()
                : LLVM::AddressOfOp::create(initializerBuilder, location,
                                            pointer, fanoutName)
                      .getResult();
        value =
            insertValue(initializerBuilder, location, value, fanoutAddress, 17);
        value = insertValue(initializerBuilder, location, value,
                            llvmConstant(initializerBuilder, location, i64,
                                         fanoutEntries.size()),
                            18);
        Value actorRootsAddress =
            actorRoots.empty()
                ? LLVM::ZeroOp::create(initializerBuilder, location, pointer)
                      .getResult()
                : LLVM::AddressOfOp::create(initializerBuilder, location,
                                            pointer, actorRootsName)
                      .getResult();
        value = insertValue(initializerBuilder, location, value,
                            actorRootsAddress, 19);
        value = insertValue(
            initializerBuilder, location, value,
            llvmConstant(initializerBuilder, location, i64, actorRoots.size()),
            20);
        Value commitAddress =
            enableStaticNBA
                ? LLVM::AddressOfOp::create(initializerBuilder, location,
                                            pointer, nbaCommitName)
                      .getResult()
                : LLVM::ZeroOp::create(initializerBuilder, location, pointer)
                      .getResult();
        value =
            insertValue(initializerBuilder, location, value, commitAddress, 21);
        Value specializationFast =
            guardedSpecializationEnabled
                ? LLVM::AddressOfOp::create(
                      initializerBuilder, location, pointer,
                      "__obelisk_static_specialization_fast_v1")
                      .getResult()
                : LLVM::ZeroOp::create(initializerBuilder, location, pointer)
                      .getResult();
        value = insertValue(initializerBuilder, location, value,
                            specializationFast, 22);
        Value dirtyRoots =
            nbaDirtyWordCount == 0
                ? LLVM::ZeroOp::create(initializerBuilder, location, pointer)
                      .getResult()
                : LLVM::AddressOfOp::create(initializerBuilder, location,
                                            pointer, nbaDirtyRootsName)
                      .getResult();
        value = insertValue(initializerBuilder, location, value, dirtyRoots,
                            23);
        value = insertValue(
            initializerBuilder, location, value,
            llvmConstant(initializerBuilder, location, i32,
                         nbaDirtyWordCount),
            24);
        value = insertValue(initializerBuilder, location, value,
                            llvmConstant(initializerBuilder, location, i32, 0),
                            25);
        Value dirtySummary =
            nbaDirtySummaryWordCount == 0
                ? LLVM::ZeroOp::create(initializerBuilder, location, pointer)
                      .getResult()
                : LLVM::AddressOfOp::create(initializerBuilder, location,
                                            pointer, nbaDirtySummaryName)
                      .getResult();
        value = insertValue(initializerBuilder, location, value, dirtySummary,
                            26);
        value = insertValue(
            initializerBuilder, location, value,
            llvmConstant(initializerBuilder, location, i32,
                         nbaDirtySummaryWordCount),
            27);
        return insertValue(initializerBuilder, location, value,
                           llvmConstant(initializerBuilder, location, i32, 0),
                           28);
      });
  if (fullyStatic)
    getOrDeclareLLVMFunction(module, "obelisk_rt_v1_scheduler_run_aot_nodes",
                             i32, {pointer, pointer, i32});
  else
    getOrDeclareLLVMFunction(module, "obelisk_rt_v1_scheduler_run", i32,
                             {pointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_scheduler_snapshot_aot", i32,
                           {pointer, pointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_static_nba_commit_root", i32,
                           {pointer, i32, i32, pointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_static_nba_commit_roots", i32,
                           {pointer, i32, i32, pointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_static_nba_direct_commit_guard", i32, {pointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_static_nba_account_generated_commits",
      LLVM::LLVMVoidType::get(context), {pointer, i32});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_scheduler_activate_static_nodes",
      LLVM::LLVMVoidType::get(context), {pointer, pointer, i32});
  return success();
}

} // namespace obelisk::detail
