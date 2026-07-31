//===- SimulationAOTMaterialization.cpp - Native AOT LLVM plan --------===//

#include "SimulationAOTPlanning.h"
#include "SimulationToLLVMCoroutinePrivate.h"

#include "obelisk/Runtime/Runtime.h"

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
  builder.setInsertionPointToStart(nbaCommitEntry);
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
  return success();
}

} // namespace obelisk::detail
