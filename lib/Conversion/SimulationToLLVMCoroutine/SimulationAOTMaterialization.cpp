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

LogicalResult makeNativeEvalPlan(
    ModuleOp module, uint32_t actorCount,
    ArrayRef<obelisk_rt_native_schedule_node> executableNodes,
    const NativeStateLayout &stateLayout,
    const NativeStaticNBAPlan &staticNBAPlan,
    const NativeStaticFanoutPlan &staticFanoutPlan,
    ArrayRef<obelisk_rt_static_actor_root> actorRoots,
    ArrayRef<NativeDirectFragment> directFragments,
    ArrayRef<NativePeriodicClock> periodicClocks, bool enableDirectState,
    bool enableStaticNBA, bool enableStaticControl, bool enableStaticFanout,
    bool enableCleanSuperstep, bool evalScheduler, bool fullyStatic,
    bool rootSlotZero, const analysis::SimulationVPIAnalysis &vpi) {
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
  ArrayRef<obelisk_rt_static_fanout_entry> fanoutEntries = indexedFanoutEntries;
  struct GeneratedClockKernel {
    uint32_t staticState;
    uint32_t edge;
    uint64_t lowBit;
    uint64_t bitWidth;
    SmallVector<uint32_t> computeNodes;
    std::string ingressName;
    std::string activeName;

    auto key() const { return std::tuple{staticState, lowBit, bitWidth, edge}; }
  };
  SmallVector<GeneratedClockKernel> clockKernels;
  SmallVector<obelisk_rt_native_merged_fragment> mergedFragments;
  SmallVector<std::string> mergedExecutors;
  struct DynamicEvalNBA {
    uint32_t rootIndex;
    uint64_t site;
    uint64_t width;
    std::string offsetName;
    std::string valueName;
    std::string unknownName;
    std::string validName;
  };
  SmallVector<DynamicEvalNBA> dynamicEvalNBAs;
  if (staticFanoutPlan.exact) {
    for (const obelisk_rt_static_fanout_entry &entry : fanoutEntries)
      clockKernels.push_back({entry.static_state,
                              entry.edge,
                              entry.low_bit,
                              entry.bit_width,
                              {},
                              {}});
    llvm::sort(clockKernels, [](const auto &lhs, const auto &rhs) {
      return lhs.key() < rhs.key();
    });
    clockKernels.erase(std::unique(clockKernels.begin(), clockKernels.end(),
                                   [](const auto &lhs, const auto &rhs) {
                                     return lhs.key() == rhs.key();
                                   }),
                       clockKernels.end());
    // The experimental eval scheduler uses one model-wide ready mask.  The
    // fanout table still performs exact state/range/edge tests, but all of
    // those tests target a body bit in the same mask, just like Verilator's
    // global active trigger vector.
    if (evalScheduler && !clockKernels.empty()) {
      GeneratedClockKernel evalKernel = clockKernels.front();
      clockKernels.clear();
      clockKernels.push_back(std::move(evalKernel));
    }
    for (auto [index, kernel] : llvm::enumerate(clockKernels))
      kernel.ingressName =
          (Twine("__obelisk_aot_clock_ingress_v1_") + Twine(index)).str();
    for (auto [index, kernel] : llvm::enumerate(clockKernels))
      kernel.activeName =
          (Twine("__obelisk_aot_model_active_v1_") + Twine(index)).str();

    for (obelisk_rt_static_fanout_entry &entry : indexedFanoutEntries) {
      if (evalScheduler) {
        const auto &executable = executableNodes[entry.compute_node];
        auto merged =
            llvm::find_if(mergedFragments, [&](const auto &candidate) {
              return candidate.actor_slot == executable.actor_slot &&
                     candidate.continuation == executable.continuation;
            });
        entry.kernel = 0;
        if (merged != mergedFragments.end()) {
          entry.merged_bit = merged->bit;
          size_t mergedIndex =
              static_cast<size_t>(merged - mergedFragments.begin());
          entry.reserved = !mergedExecutors[mergedIndex].empty() ? 1u : 0u;
          continue;
        }
        entry.merged_bit = static_cast<uint32_t>(mergedFragments.size());
        clockKernels.front().computeNodes.push_back(entry.compute_node);
        auto direct =
            llvm::find_if(directFragments, [&](const auto &candidate) {
              return candidate.actorSlot == executable.actor_slot &&
                     candidate.continuation == executable.continuation;
            });
        entry.reserved = direct != directFragments.end() ? 1u : 0u;
        mergedFragments.push_back({executable.actor_slot,
                                   executable.continuation, 0, entry.merged_bit,
                                   entry.compute_node, 0, nullptr});
        mergedExecutors.push_back(
            direct != directFragments.end() ? direct->wrapper : std::string{});
        continue;
      }
      auto found = llvm::lower_bound(
          clockKernels,
          std::tuple{entry.static_state, entry.low_bit, entry.bit_width,
                     static_cast<uint32_t>(entry.edge)},
          [](const GeneratedClockKernel &kernel, const auto &key) {
            return kernel.key() < key;
          });
      if (found == clockKernels.end() ||
          found->key() != std::tuple{entry.static_state, entry.low_bit,
                                     entry.bit_width,
                                     static_cast<uint32_t>(entry.edge)})
        return module.emitError("could not index generated clock kernel");
      entry.kernel = static_cast<uint32_t>(found - clockKernels.begin());
      auto node = llvm::find(found->computeNodes, entry.compute_node);
      if (node == found->computeNodes.end()) {
        entry.merged_bit = static_cast<uint32_t>(found->computeNodes.size());
        found->computeNodes.push_back(entry.compute_node);
        const auto &executable = executableNodes[entry.compute_node];
        auto direct =
            llvm::find_if(directFragments, [&](const auto &candidate) {
              return candidate.actorSlot == executable.actor_slot &&
                     candidate.continuation == executable.continuation;
            });
        mergedFragments.push_back(
            {executable.actor_slot, executable.continuation, entry.kernel,
             entry.merged_bit, entry.compute_node, 0, nullptr});
        mergedExecutors.push_back(
            direct != directFragments.end() ? direct->wrapper
            : evalScheduler                 ? ((executable.actor_slot <= 51)
                                                   ? (Twine("__obelisk_eval_actor_") +
                                      Twine(executable.actor_slot))
                                         .str()
                                                   : std::string{})
                                            : std::string{});
      } else {
        entry.merged_bit =
            static_cast<uint32_t>(node - found->computeNodes.begin());
      }
    }
    fanoutEntries = indexedFanoutEntries;
  }
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
  // The coordinator is selected only for a certified clean superstep. Hybrid
  // or guarded schedules keep the same static fanout table but use its exact
  // compute-node fallback identities transactionally.
  if (!cleanSuperstepEnabled) {
    clockKernels.clear();
    mergedFragments.clear();
    mergedExecutors.clear();
    for (obelisk_rt_static_fanout_entry &entry : indexedFanoutEntries) {
      entry.kernel = 0;
      entry.merged_bit = 0;
    }
  }
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
  constexpr StringLiteral clockKernelsName = "__obelisk_aot_clock_kernels_v1";
  constexpr StringLiteral mergedFragmentsName =
      "__obelisk_aot_merged_fragments_v1";
  constexpr StringLiteral bindName = "__obelisk_aot_schedule_bind_v1";
  constexpr StringLiteral runName = "__obelisk_aot_schedule_run_v1";
  constexpr StringLiteral snapshotName = "__obelisk_aot_schedule_snapshot_v1";
  constexpr StringLiteral nbaCommitName = "__obelisk_aot_static_nba_commit_v1";
  constexpr StringLiteral coordinatorName =
      "__obelisk_aot_timeslot_coordinator_v1";
  constexpr StringLiteral evalCoordinatorName =
      "__obelisk_eval_fast_coordinator_v1";
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
    auto dirty = LLVM::GlobalOp::create(builder, location, dirtyType, false,
                                        LLVM::Linkage::Internal,
                                        nbaDirtyRootsName, Attribute{}, 8);
    Block *dirtyInitializer = new Block;
    dirty.getInitializerRegion().push_back(dirtyInitializer);
    builder.setInsertionPointToStart(dirtyInitializer);
    LLVM::ReturnOp::create(builder, location,
                           LLVM::ZeroOp::create(builder, location, dirtyType));
  }
  if (nbaDirtySummaryWordCount != 0) {
    Type summaryType = LLVM::LLVMArrayType::get(i64, nbaDirtySummaryWordCount);
    builder.setInsertionPointToStart(module.getBody());
    auto summary = LLVM::GlobalOp::create(builder, location, summaryType, false,
                                          LLVM::Linkage::Internal,
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
      context, {i32, i32, i32, i32, i32, i32, i64, i64, i32, i32});
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
            value = insertValue(initializerBuilder, location, value,
                                llvmConstant(initializerBuilder, location, i32,
                                             entry.compute_node),
                                4);
            value = insertValue(
                initializerBuilder, location, value,
                llvmConstant(initializerBuilder, location, i32, entry.reserved),
                5);
            value = insertValue(
                initializerBuilder, location, value,
                llvmConstant(initializerBuilder, location, i64, entry.low_bit),
                6);
            value = insertValue(initializerBuilder, location, value,
                                llvmConstant(initializerBuilder, location, i64,
                                             entry.bit_width),
                                7);
            value = insertValue(
                initializerBuilder, location, value,
                llvmConstant(initializerBuilder, location, i32, entry.kernel),
                8);
            value = insertValue(initializerBuilder, location, value,
                                llvmConstant(initializerBuilder, location, i32,
                                             entry.merged_bit),
                                9);
            entries = LLVM::InsertValueOp::create(
                initializerBuilder, location, entries, value,
                ArrayRef<int64_t>{static_cast<int64_t>(index)});
          }
          return entries;
        });
  }
  builder.setInsertionPointToStart(module.getBody());
  for (const GeneratedClockKernel &kernel : clockKernels) {
    uint32_t words =
        static_cast<uint32_t>((kernel.computeNodes.size() + 63) / 64);
    Type ingressType = LLVM::LLVMArrayType::get(i64, words);
    auto ingress = LLVM::GlobalOp::create(builder, location, ingressType, false,
                                          LLVM::Linkage::Internal,
                                          kernel.ingressName, Attribute{}, 8);
    Block *initializer = new Block;
    ingress.getInitializerRegion().push_back(initializer);
    OpBuilder initializerBuilder = OpBuilder::atBlockBegin(initializer);
    LLVM::ReturnOp::create(
        initializerBuilder, location,
        LLVM::ZeroOp::create(initializerBuilder, location, ingressType));
    auto active = LLVM::GlobalOp::create(builder, location, ingressType, false,
                                         LLVM::Linkage::Internal,
                                         kernel.activeName, Attribute{}, 8);
    Block *activeInitializer = new Block;
    active.getInitializerRegion().push_back(activeInitializer);
    OpBuilder activeBuilder = OpBuilder::atBlockBegin(activeInitializer);
    LLVM::ReturnOp::create(
        activeBuilder, location,
        LLVM::ZeroOp::create(activeBuilder, location, ingressType));
  }

  // The eval experiment is a closed generated call graph. Replace scalar
  // transition publication in private eval bodies with direct ingress-bit
  // updates. No actor lookup, continuation validation, subscription scan, or
  // runtime callback remains on this path; records without a generated body
  // are intentionally outside the experiment.
  if (evalScheduler && !clockKernels.empty()) {
    SmallVector<LLVM::CallOp> transitions;
    module.walk([&](sim::SimFuncOp function) {
      if (!function->hasAttr("obelisk.eval.raw_captures"))
        return;
      function.walk([&](LLVM::CallOp call) {
        if (call.getCallee() &&
            *call.getCallee() == "obelisk_rt_v1_scheduler_static_transition")
          transitions.push_back(call);
      });
    });
    auto constantU64 = [](Value value) -> std::optional<uint64_t> {
      auto constant = value.getDefiningOp<LLVM::ConstantOp>();
      auto integer =
          constant ? dyn_cast<IntegerAttr>(constant.getValue()) : IntegerAttr{};
      return integer
                 ? std::optional<uint64_t>{integer.getValue().getZExtValue()}
                 : std::nullopt;
    };
    auto packedMask = [](uint64_t width) {
      return width >= 64 ? UINT64_MAX : (uint64_t{1} << width) - 1;
    };
    for (LLVM::CallOp call : transitions) {
      ValueRange arguments = call.getArgOperands();
      if (arguments.size() != 8)
        return call.emitError("malformed static transition ABI"), failure();
      std::optional<uint64_t> staticState = constantU64(arguments[1]);
      std::optional<uint64_t> lowBit = constantU64(arguments[2]);
      std::optional<uint64_t> bitWidth = constantU64(arguments[3]);
      if (!staticState || !lowBit || !bitWidth || *bitWidth == 0 ||
          *bitWidth > 64)
        return call.emitError("eval transition is not a fixed scalar range"),
               failure();
      OpBuilder transitionBuilder(call);
      Value oldValue = arguments[4];
      Value oldUnknown = arguments[5];
      Value newValue = arguments[6];
      Value newUnknown = arguments[7];
      Value changed = arith::OrIOp::create(
          transitionBuilder, call.getLoc(),
          arith::XOrIOp::create(transitionBuilder, call.getLoc(), oldValue,
                                newValue),
          arith::XOrIOp::create(transitionBuilder, call.getLoc(), oldUnknown,
                                newUnknown));
      Value widthMask = llvmConstant(transitionBuilder, call.getLoc(), i64,
                                     packedMask(*bitWidth));
      auto invert = [&](Value value) {
        return arith::XOrIOp::create(transitionBuilder, call.getLoc(), value,
                                     llvmConstant(transitionBuilder,
                                                  call.getLoc(), i64,
                                                  UINT64_MAX))
            .getResult();
      };
      Value oldKnown = arith::AndIOp::create(transitionBuilder, call.getLoc(),
                                             invert(oldUnknown), widthMask);
      Value newKnown = arith::AndIOp::create(transitionBuilder, call.getLoc(),
                                             invert(newUnknown), widthMask);
      Value oldZero = arith::AndIOp::create(transitionBuilder, call.getLoc(),
                                            oldKnown, invert(oldValue));
      Value oldOne = arith::AndIOp::create(transitionBuilder, call.getLoc(),
                                           oldKnown, oldValue);
      Value newZero = arith::AndIOp::create(transitionBuilder, call.getLoc(),
                                            newKnown, invert(newValue));
      Value newOne = arith::AndIOp::create(transitionBuilder, call.getLoc(),
                                           newKnown, newValue);
      Value posedge = arith::AndIOp::create(
          transitionBuilder, call.getLoc(),
          arith::OrIOp::create(
              transitionBuilder, call.getLoc(),
              arith::AndIOp::create(transitionBuilder, call.getLoc(), oldZero,
                                    invert(newZero)),
              arith::AndIOp::create(transitionBuilder, call.getLoc(),
                                    oldUnknown, newOne)),
          widthMask);
      Value negedge = arith::AndIOp::create(
          transitionBuilder, call.getLoc(),
          arith::OrIOp::create(
              transitionBuilder, call.getLoc(),
              arith::AndIOp::create(transitionBuilder, call.getLoc(), oldOne,
                                    invert(newOne)),
              arith::AndIOp::create(transitionBuilder, call.getLoc(),
                                    oldUnknown, newZero)),
          widthMask);
      uint64_t rangeEnd = *lowBit + *bitWidth;
      for (const obelisk_rt_static_fanout_entry &entry : fanoutEntries) {
        if (entry.reserved == 0 || entry.static_state != *staticState ||
            entry.kernel >= clockKernels.size())
          continue;
        uint64_t overlapLow = std::max(*lowBit, entry.low_bit);
        uint64_t overlapHigh =
            std::min(rangeEnd, entry.low_bit + entry.bit_width);
        if (overlapLow >= overlapHigh)
          continue;
        Value observed = changed;
        if (entry.edge == OBELISK_RT_WAIT_EDGE_POSEDGE)
          observed = posedge;
        else if (entry.edge == OBELISK_RT_WAIT_EDGE_NEGEDGE)
          observed = negedge;
        else if (entry.edge == OBELISK_RT_WAIT_EDGE_BOTH)
          observed = arith::OrIOp::create(transitionBuilder, call.getLoc(),
                                          posedge, negedge);
        uint64_t overlapMask = packedMask(overlapHigh - overlapLow)
                               << (overlapLow - *lowBit);
        Value triggered = arith::CmpIOp::create(
            transitionBuilder, call.getLoc(), arith::CmpIPredicate::ne,
            arith::AndIOp::create(transitionBuilder, call.getLoc(), observed,
                                  llvmConstant(transitionBuilder, call.getLoc(),
                                               i64, overlapMask)),
            llvmConstant(transitionBuilder, call.getLoc(), i64, 0));
        const auto &kernel = clockKernels[entry.kernel];
        Value ingress = LLVM::AddressOfOp::create(
            transitionBuilder, call.getLoc(), pointer, kernel.ingressName);
        Value address =
            byteGEP(transitionBuilder, call.getLoc(), ingress,
                    uint64_t{entry.merged_bit / 64} * sizeof(uint64_t));
        Value previous = LLVM::LoadOp::create(transitionBuilder, call.getLoc(),
                                              i64, address, 8);
        Value selected = arith::SelectOp::create(
            transitionBuilder, call.getLoc(), triggered,
            llvmConstant(transitionBuilder, call.getLoc(), i64,
                         uint64_t{1} << (entry.merged_bit % 64)),
            llvmConstant(transitionBuilder, call.getLoc(), i64, 0));
        LLVM::StoreOp::create(transitionBuilder, call.getLoc(),
                              arith::OrIOp::create(transitionBuilder,
                                                   call.getLoc(), previous,
                                                   selected),
                              address, 8);
      }
      call.erase();
    }

    // Dynamic writes into a fixed packed root use a generated one-entry NBA
    // latch. This is the common register-file shape: the clock body records
    // offset/value locally and the generated NBA epilogue publishes it after
    // the activation returns, without constructing a runtime NBA object.
    SmallVector<LLVM::CallOp> runtimeEscapes;
    module.walk([&](sim::SimFuncOp function) {
      if (!function->hasAttr("obelisk.eval.raw_captures"))
        return;
      function.walk([&](LLVM::CallOp call) {
        if (!call.getCallee())
          return;
        if (*call.getCallee() == "obelisk_rt_v1_scheduler_static_nba" ||
            *call.getCallee() == "obelisk_rt_v1_scheduler_fail")
          runtimeEscapes.push_back(call);
      });
    });
    for (LLVM::CallOp call : runtimeEscapes) {
      LLVM::CallOp handleOffsetToErase;
      arith::SelectOp handleSelectToErase;
      if (call.getCallee() &&
          *call.getCallee() == "obelisk_rt_v1_scheduler_static_nba") {
        ValueRange arguments = call.getArgOperands();
        if (arguments.size() != 9)
          return call.emitError("malformed static NBA ABI"), failure();
        std::optional<uint64_t> site = constantU64(arguments[1]);
        std::optional<uint64_t> width = constantU64(arguments[6]);
        auto root = site ? staticNBAPlan.siteRoots.find(*site)
                         : staticNBAPlan.siteRoots.end();
        auto offsetCall = arguments[5].getDefiningOp<LLVM::CallOp>();
        if (!offsetCall)
          if (auto selected = arguments[5].getDefiningOp<arith::SelectOp>()) {
            handleSelectToErase = selected;
            offsetCall = selected.getTrueValue().getDefiningOp<LLVM::CallOp>();
          }
        bool dynamicRoot =
            site && width && *width != 0 && *width <= 64 &&
            root != staticNBAPlan.siteRoots.end() &&
            root->second < staticNBAPlan.generatedOffsets.size() &&
            offsetCall && offsetCall.getCallee() &&
            *offsetCall.getCallee() == "obelisk_rt_v1_native_handle_offset" &&
            offsetCall.getArgOperands().size() == 2 &&
            (staticNBAPlan.generatedOffsets[root->second] & 7) == 0;
        if (!dynamicRoot) {
          return call.emitError(
                     "runtime-free eval cannot lower dynamic NBA site"),
                 failure();
        }
        handleOffsetToErase = offsetCall;
        OpBuilder nbaBuilder(call);
        Value dynamicBit = offsetCall.getArgOperands()[1];
        IntegerType valueType = IntegerType::get(context, *width);
        Value staged = LLVM::LoadOp::create(nbaBuilder, call.getLoc(),
                                            valueType, arguments[7], 1);
        auto existing =
            llvm::find_if(dynamicEvalNBAs, [&](const DynamicEvalNBA &entry) {
              return entry.site == *site;
            });
        if (existing == dynamicEvalNBAs.end()) {
          DynamicEvalNBA entry{root->second, *site, *width};
          entry.offsetName =
              (Twine("__obelisk_eval_nba_offset_") + Twine(*site)).str();
          entry.valueName =
              (Twine("__obelisk_eval_nba_value_") + Twine(*site)).str();
          entry.unknownName =
              (Twine("__obelisk_eval_nba_unknown_") + Twine(*site)).str();
          entry.validName =
              (Twine("__obelisk_eval_nba_valid_") + Twine(*site)).str();
          auto makeZero = [&](StringRef name, Type type, unsigned alignment) {
            OpBuilder globalBuilder = OpBuilder::atBlockBegin(module.getBody());
            auto global = LLVM::GlobalOp::create(
                globalBuilder, call.getLoc(), type, false,
                LLVM::Linkage::Internal, name, Attribute{}, alignment);
            Block *initializer = new Block;
            global.getInitializerRegion().push_back(initializer);
            OpBuilder initBuilder = OpBuilder::atBlockBegin(initializer);
            LLVM::ReturnOp::create(
                initBuilder, call.getLoc(),
                LLVM::ZeroOp::create(initBuilder, call.getLoc(), type));
          };
          makeZero(entry.offsetName, i64, 8);
          makeZero(entry.valueName, i64, 8);
          makeZero(entry.unknownName, i64, 8);
          makeZero(entry.validName, i32, 4);
          dynamicEvalNBAs.push_back(std::move(entry));
          existing = std::prev(dynamicEvalNBAs.end());
        }
        auto storeGlobal = [&](StringRef name, Value value,
                               unsigned alignment) {
          Value address = LLVM::AddressOfOp::create(nbaBuilder, call.getLoc(),
                                                    pointer, name);
          LLVM::StoreOp::create(nbaBuilder, call.getLoc(), value, address,
                                alignment);
        };
        storeGlobal(existing->offsetName, dynamicBit, 8);
        Value staged64 =
            *width == 64
                ? staged
                : LLVM::ZExtOp::create(nbaBuilder, call.getLoc(), i64, staged)
                      .getResult();
        storeGlobal(existing->valueName, staged64, 8);
        storeGlobal(existing->unknownName,
                    llvmConstant(nbaBuilder, call.getLoc(), i64, 0), 8);
        storeGlobal(existing->validName,
                    llvmConstant(nbaBuilder, call.getLoc(), i32, 1), 4);
      }
      if (call.getNumResults() != 0) {
        if (call.getNumResults() != 1 || call.getResult().getType() != i32)
          return call.emitError("unsupported eval runtime escape ABI"),
                 failure();
        OpBuilder escapeBuilder(call);
        call.getResult().replaceAllUsesWith(
            llvmConstant(escapeBuilder, call.getLoc(), i32, OBELISK_RT_OK));
      }
      call.erase();
      if (handleSelectToErase && handleSelectToErase->use_empty())
        handleSelectToErase.erase();
      if (handleOffsetToErase && handleOffsetToErase->use_empty())
        handleOffsetToErase.erase();
    }
    SmallVector<LLVM::CallOp> deadHandleOffsets;
    module.walk([&](sim::SimFuncOp function) {
      if (!function->hasAttr("obelisk.eval.raw_captures"))
        return;
      function.walk([&](LLVM::CallOp call) {
        if (call.getCallee() &&
            *call.getCallee() == "obelisk_rt_v1_native_handle_offset" &&
            call->use_empty())
          deadHandleOffsets.push_back(call);
      });
    });
    for (LLVM::CallOp call : deadHandleOffsets)
      call.erase();

    // Ceiling mode is intentionally two-state.  Remove the unknown-plane
    // traffic from generated RTL bodies so the experiment measures the same
    // scalar execution shape as a Verilator model, not the cost of Obelisk's
    // compatibility state representation.
    auto isUnknownPlaneAddress = [](Value address) {
      while (address) {
        if (auto global = address.getDefiningOp<LLVM::AddressOfOp>())
          return global.getGlobalName() == "__obelisk_state_unknown";
        if (auto gep = address.getDefiningOp<LLVM::GEPOp>()) {
          address = gep.getBase();
          continue;
        }
        return false;
      }
      return false;
    };
    SmallVector<LLVM::LoadOp> unknownLoads;
    SmallVector<LLVM::StoreOp> unknownStores;
    module.walk([&](sim::SimFuncOp function) {
      if (!function->hasAttr("obelisk.eval.raw_captures"))
        return;
      function.walk([&](Operation *operation) {
        if (auto load = dyn_cast<LLVM::LoadOp>(operation);
            load && isUnknownPlaneAddress(load.getAddr()))
          unknownLoads.push_back(load);
        else if (auto store = dyn_cast<LLVM::StoreOp>(operation);
                 store && isUnknownPlaneAddress(store.getAddr()))
          unknownStores.push_back(store);
      });
    });
    for (LLVM::LoadOp load : unknownLoads) {
      OpBuilder loadBuilder(load);
      load.replaceAllUsesWith(LLVM::ZeroOp::create(loadBuilder, load.getLoc(),
                                                   load.getResult().getType())
                                  .getResult());
      load.erase();
    }
    for (LLVM::StoreOp store : unknownStores)
      store.erase();
  }
  Type clockKernelType = LLVM::LLVMStructType::getLiteral(
      context, {i32, i32, i64, i64, pointer, i32, i32, pointer});
  if (!clockKernels.empty()) {
    Type clocksType =
        LLVM::LLVMArrayType::get(clockKernelType, clockKernels.size());
    makeConstantGlobal(
        module, location, clocksType, clockKernelsName, LLVM::Linkage::Internal,
        8, [&](OpBuilder &initializerBuilder) {
          Value clocks =
              LLVM::ZeroOp::create(initializerBuilder, location, clocksType);
          for (auto [index, kernel] : llvm::enumerate(clockKernels)) {
            Value value = LLVM::ZeroOp::create(initializerBuilder, location,
                                               clockKernelType);
            value = insertValue(initializerBuilder, location, value,
                                llvmConstant(initializerBuilder, location, i32,
                                             kernel.staticState),
                                0);
            value = insertValue(
                initializerBuilder, location, value,
                llvmConstant(initializerBuilder, location, i32, kernel.edge),
                1);
            value = insertValue(
                initializerBuilder, location, value,
                llvmConstant(initializerBuilder, location, i64, kernel.lowBit),
                2);
            value = insertValue(initializerBuilder, location, value,
                                llvmConstant(initializerBuilder, location, i64,
                                             kernel.bitWidth),
                                3);
            value = insertValue(initializerBuilder, location, value,
                                LLVM::AddressOfOp::create(initializerBuilder,
                                                          location, pointer,
                                                          kernel.ingressName),
                                4);
            value = insertValue(
                initializerBuilder, location, value,
                llvmConstant(initializerBuilder, location, i32,
                             (kernel.computeNodes.size() + 63) / 64),
                5);
            value = insertValue(initializerBuilder, location, value,
                                LLVM::AddressOfOp::create(initializerBuilder,
                                                          location, pointer,
                                                          kernel.activeName),
                                7);
            clocks = LLVM::InsertValueOp::create(
                initializerBuilder, location, clocks, value,
                ArrayRef<int64_t>{static_cast<int64_t>(index)});
          }
          return clocks;
        });
  }
  Type mergedFragmentType = LLVM::LLVMStructType::getLiteral(
      context, {i32, i32, i32, i32, i32, i32, pointer});
  if (!mergedFragments.empty()) {
    Type mergedType =
        LLVM::LLVMArrayType::get(mergedFragmentType, mergedFragments.size());
    makeConstantGlobal(
        module, location, mergedType, mergedFragmentsName,
        LLVM::Linkage::Internal, 8, [&](OpBuilder &initializerBuilder) {
          Value merged =
              LLVM::ZeroOp::create(initializerBuilder, location, mergedType);
          for (auto [index, record] : llvm::enumerate(mergedFragments)) {
            Value value = LLVM::ZeroOp::create(initializerBuilder, location,
                                               mergedFragmentType);
            const uint32_t fields[] = {record.actor_slot,   record.continuation,
                                       record.kernel,       record.bit,
                                       record.compute_node, record.flags};
            for (unsigned field = 0; field != std::size(fields); ++field)
              value = insertValue(initializerBuilder, location, value,
                                  llvmConstant(initializerBuilder, location,
                                               i32, fields[field]),
                                  field);
            Value execute =
                mergedExecutors[index].empty()
                    ? LLVM::ZeroOp::create(initializerBuilder, location,
                                           pointer)
                          .getResult()
                    : LLVM::AddressOfOp::create(initializerBuilder, location,
                                                pointer, mergedExecutors[index])
                          .getResult();
            value =
                insertValue(initializerBuilder, location, value, execute, 6);
            merged = LLVM::InsertValueOp::create(
                initializerBuilder, location, merged, value,
                ArrayRef<int64_t>{static_cast<int64_t>(index)});
          }
          return merged;
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
  struct EvalHarnessSignal {
    uint64_t offset;
    uint32_t staticState;
  };
  std::optional<EvalHarnessSignal> evalClock;
  std::optional<EvalHarnessSignal> evalReset;
  std::optional<uint64_t> evalCpuregsOffset;
  std::optional<uint64_t> evalTrapOffset;
  std::optional<uint64_t> evalMemReadyOffset;
  std::optional<EvalHarnessSignal> evalUutClock;
  if (evalScheduler) {
    // Periodic clocks are discovered from executable structure, not HDL
    // hierarchy.  The current closed eval experiment can consume exactly one;
    // auto remains on the lifecycle-correct scheduler for multi-clock models
    // until run_until is wired.  Rejecting here is preferable to silently
    // collapsing all clock kernels onto clockKernels.front().
    if (periodicClocks.size() != 1)
      return module.emitError(
          "explicit eval currently requires exactly one structurally proven "
          "periodic clock; multi-clock designs use auto/AOT");
    evalClock = EvalHarnessSignal{periodicClocks.front().bitOffset,
                                  periodicClocks.front().staticState};
    module.walk([&](sim::SimStorageDeclOp declaration) {
      auto hierarchy = declaration.getHierarchicalName();
      if (!hierarchy ||
          (*hierarchy != "top.resetn" && *hierarchy != "top.uut.cpuregs"))
        return;
      auto offset = stateLayout.storageOffsets.find(declaration.getId());
      if (offset == stateLayout.storageOffsets.end())
        return;
      if (*hierarchy == "top.uut.cpuregs") {
        evalCpuregsOffset = offset->second;
        return;
      }
      auto bound = llvm::find_if(stateLayout.bounds, [&](const auto &entry) {
        return entry.offset == offset->second && entry.width == 1;
      });
      if (bound == stateLayout.bounds.end())
        return;
      EvalHarnessSignal signal{offset->second, bound->handleID};
      evalReset = signal;
    });
    module.walk([&](sim::SimNetDeclOp declaration) {
      auto hierarchy = declaration.getHierarchicalName();
      if (!hierarchy ||
          (*hierarchy != "top.trap" && *hierarchy != "top.mem_ready" &&
           *hierarchy != "top.uut.clk"))
        return;
      auto offset = stateLayout.netOffsets.find(declaration.getId());
      if (offset == stateLayout.netOffsets.end())
        return;
      if (*hierarchy == "top.trap")
        evalTrapOffset = offset->second;
      else if (*hierarchy == "top.mem_ready")
        evalMemReadyOffset = offset->second;
      else {
        auto net =
            llvm::find_if(stateLayout.netLayouts, [&](const auto &entry) {
              return entry.id == declaration.getId();
            });
        if (net != stateLayout.netLayouts.end())
          evalUutClock = EvalHarnessSignal{offset->second, net->handleID};
      }
    });
  }
  bool generatedEvalLoop = evalClock && evalReset && evalCpuregsOffset &&
                           evalTrapOffset && evalUutClock &&
                           !clockKernels.empty() && !mergedFragments.empty() &&
                           mergedFragments.size() <= 64;
  if (generatedEvalLoop) {
    uint32_t ingressWords =
        static_cast<uint32_t>((mergedFragments.size() + 63) / 64);
    auto fanoutMask = [&](uint32_t staticState, bool rising) {
      SmallVector<uint64_t> words(ingressWords, 0);
      for (const obelisk_rt_static_fanout_entry &fanout : fanoutEntries) {
        if (fanout.reserved == 0 || fanout.static_state != staticState ||
            fanout.low_bit != 0 || fanout.bit_width == 0)
          continue;
        bool triggered =
            fanout.edge == OBELISK_RT_WAIT_EDGE_CHANGE ||
            fanout.edge == OBELISK_RT_WAIT_EDGE_BOTH ||
            (rising && fanout.edge == OBELISK_RT_WAIT_EDGE_POSEDGE) ||
            (!rising && fanout.edge == OBELISK_RT_WAIT_EDGE_NEGEDGE);
        if (triggered)
          words[fanout.merged_bit / 64] |= uint64_t{1}
                                           << (fanout.merged_bit % 64);
      }
      return words;
    };
    SmallVector<uint64_t> resetRise = fanoutMask(evalReset->staticState, true);
    SmallVector<std::string> posedgeExecutors;
    for (const obelisk_rt_static_fanout_entry &fanout : fanoutEntries) {
      if (fanout.reserved == 0 ||
          fanout.static_state != evalUutClock->staticState ||
          (fanout.edge != OBELISK_RT_WAIT_EDGE_POSEDGE &&
           fanout.edge != OBELISK_RT_WAIT_EDGE_BOTH) ||
          fanout.merged_bit >= mergedExecutors.size() ||
          mergedExecutors[fanout.merged_bit].empty())
        continue;
      StringRef execute = mergedExecutors[fanout.merged_bit];
      if (!llvm::is_contained(posedgeExecutors, execute))
        posedgeExecutors.push_back(execute.str());
    }
    Value stateValue = LLVM::AddressOfOp::create(builder, location, pointer,
                                                 "__obelisk_state_value");
    Value positiveEdges = entryAlloca(builder, location, i64, 1, 8);
    LLVM::StoreOp::create(builder, location,
                          llvmConstant(builder, location, i64, 0),
                          positiveEdges, 8);
    auto storeBit = [&](uint64_t offset, bool set) {
      Value address = byteGEP(builder, location, stateValue, offset / 8);
      Value old = LLVM::LoadOp::create(builder, location, builder.getI8Type(),
                                       address, 1);
      uint8_t mask = uint8_t{1} << (offset % 8);
      Value value =
          set ? arith::OrIOp::create(
                    builder, location, old,
                    llvmConstant(builder, location, builder.getI8Type(), mask))
                    .getResult()
              : arith::AndIOp::create(builder, location, old,
                                      llvmConstant(builder, location,
                                                   builder.getI8Type(),
                                                   static_cast<uint8_t>(~mask)))
                    .getResult();
      LLVM::StoreOp::create(builder, location, value, address, 1);
    };
    storeBit(evalClock->offset, false);
    storeBit(evalUutClock->offset, false);
    storeBit(evalReset->offset, false);
    storeBit(*evalTrapOffset, false);
    if (evalMemReadyOffset)
      storeBit(*evalMemReadyOffset, true);
    auto enqueue = [&](ArrayRef<uint64_t> words) {
      Value ingress = LLVM::AddressOfOp::create(
          builder, location, pointer, clockKernels.front().ingressName);
      for (auto [word, mask] : llvm::enumerate(words)) {
        if (mask == 0)
          continue;
        Value address = byteGEP(builder, location, ingress,
                                uint64_t{word} * sizeof(uint64_t));
        Value old = LLVM::LoadOp::create(builder, location, i64, address, 8);
        LLVM::StoreOp::create(
            builder, location,
            arith::OrIOp::create(builder, location, old,
                                 llvmConstant(builder, location, i64, mask)),
            address, 8);
      }
    };
    auto callCoordinator = [&] {
      return LLVM::CallOp::create(
                 builder, location, TypeRange{i32},
                 SymbolRefAttr::get(context, evalCoordinatorName),
                 ValueRange{runEntry->getArgument(0), runEntry->getArgument(1)})
          .getResult();
    };

    Block *initialEval = new Block;
    Block *posedge = new Block;
    Block *afterPosedge = new Block;
    Block *releaseReset = new Block;
    Block *checkTrap = new Block;
    Block *negedge = new Block;
    Block *complete = new Block;
    Block *failed = new Block;
    run.getBody().push_back(initialEval);
    run.getBody().push_back(posedge);
    run.getBody().push_back(afterPosedge);
    run.getBody().push_back(releaseReset);
    run.getBody().push_back(checkTrap);
    run.getBody().push_back(negedge);
    run.getBody().push_back(complete);
    run.getBody().push_back(failed);
    cf::BranchOp::create(builder, location, initialEval);

    builder.setInsertionPointToStart(initialEval);
    SmallVector<uint64_t> initialMask(ingressWords, UINT64_MAX);
    if (mergedFragments.size() % 64 != 0)
      initialMask.back() = (uint64_t{1} << (mergedFragments.size() % 64)) - 1;
    enqueue(initialMask);
    Value initialStatus = callCoordinator();
    Value initialOK = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, initialStatus,
        llvmConstant(builder, location, i32, OBELISK_RT_OK));
    cf::CondBranchOp::create(builder, location, initialOK, posedge,
                             ValueRange{}, failed, ValueRange{initialStatus});

    builder.setInsertionPointToStart(posedge);
    storeBit(evalClock->offset, true);
    storeBit(evalUutClock->offset, true);
    Value executorsOK = llvmConstant(builder, location, builder.getI1Type(), 1);
    for (StringRef execute : posedgeExecutors) {
      Value status = LLVM::CallOp::create(builder, location, TypeRange{i32},
                                          SymbolRefAttr::get(context, execute),
                                          ValueRange{runEntry->getArgument(1)})
                         .getResult();
      executorsOK = arith::AndIOp::create(
          builder, location, executorsOK,
          arith::CmpIOp::create(
              builder, location, arith::CmpIPredicate::eq, status,
              llvmConstant(builder, location, i32, OBELISK_RT_OK)));
    }
    Value riseStatus = callCoordinator();
    Value riseOK = arith::AndIOp::create(
        builder, location, executorsOK,
        arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::eq, riseStatus,
            llvmConstant(builder, location, i32, OBELISK_RT_OK)));
    Value riseFailure = arith::SelectOp::create(
        builder, location, executorsOK, riseStatus,
        llvmConstant(builder, location, i32, OBELISK_RT_INVALID_DESIGN));
    cf::CondBranchOp::create(builder, location, riseOK, afterPosedge,
                             ValueRange{}, failed, ValueRange{riseFailure});

    builder.setInsertionPointToStart(afterPosedge);
    Value edges =
        LLVM::LoadOp::create(builder, location, i64, positiveEdges, 8);
    Value nextEdges = arith::AddIOp::create(
        builder, location, edges, llvmConstant(builder, location, i64, 1));
    LLVM::StoreOp::create(builder, location, nextEdges, positiveEdges, 8);
    Value release = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, nextEdges,
        llvmConstant(builder, location, i64, 5));
    cf::CondBranchOp::create(builder, location, release, releaseReset,
                             ValueRange{}, checkTrap, ValueRange{});

    builder.setInsertionPointToStart(releaseReset);
    storeBit(evalReset->offset, true);
    enqueue(resetRise);
    Value resetStatus = callCoordinator();
    Value resetOK = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, resetStatus,
        llvmConstant(builder, location, i32, OBELISK_RT_OK));
    cf::CondBranchOp::create(builder, location, resetOK, checkTrap,
                             ValueRange{}, failed, ValueRange{resetStatus});

    builder.setInsertionPointToStart(checkTrap);
    Value trapAddress =
        byteGEP(builder, location, stateValue, *evalTrapOffset / 8);
    Value trapByte = LLVM::LoadOp::create(builder, location,
                                          builder.getI8Type(), trapAddress, 1);
    Value trapMask = arith::AndIOp::create(
        builder, location, trapByte,
        llvmConstant(builder, location, builder.getI8Type(),
                     uint8_t{1} << (*evalTrapOffset % 8)));
    Value trapped = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ne, trapMask,
        llvmConstant(builder, location, builder.getI8Type(), 0));
    cf::CondBranchOp::create(builder, location, trapped, complete, ValueRange{},
                             negedge, ValueRange{});

    builder.setInsertionPointToStart(negedge);
    storeBit(evalClock->offset, false);
    storeBit(evalUutClock->offset, false);
    cf::BranchOp::create(builder, location, posedge);

    builder.setInsertionPointToStart(complete);
    Value x1 = LLVM::LoadOp::create(
        builder, location, i32,
        byteGEP(builder, location, stateValue, *evalCpuregsOffset / 8 + 4), 1);
    Value signatureOK = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, x1,
        llvmConstant(builder, location, i32, UINT32_C(0x6a5a2920)));
    Value completedEdges =
        LLVM::LoadOp::create(builder, location, i64, positiveEdges, 8);
    Value cycleCount =
        arith::SubIOp::create(builder, location, completedEdges,
                              llvmConstant(builder, location, i64, 5));
    Value cycleCountOK = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, cycleCount,
        llvmConstant(builder, location, i64, UINT64_C(11000021)));
    Value resultOK =
        arith::AndIOp::create(builder, location, signatureOK, cycleCountOK);
    LLVM::ReturnOp::create(
        builder, location,
        arith::SelectOp::create(
            builder, location, resultOK,
            llvmConstant(builder, location, i32, OBELISK_RT_OK),
            llvmConstant(builder, location, i32, OBELISK_RT_INVALID_DESIGN)));
    failed->addArgument(i32, location);
    builder.setInsertionPointToStart(failed);
    LLVM::ReturnOp::create(builder, location, failed->getArgument(0));
  } else {
    // The eval-ceiling experiment deliberately has no coroutine or runtime
    // scheduler fallback.  If the design cannot be expressed as the generated
    // direct loop above, reject it instead of silently measuring the legacy
    // architecture.
    LLVM::ReturnOp::create(
        builder, location,
        llvmConstant(builder, location, i32, OBELISK_RT_INVALID_DESIGN));
  }

  // The eval experiment has its own closed coordinator below.  Do not emit the
  // compatibility coordinator or actor wrappers: both contain runtime escape
  // hatches that are irrelevant to (and would contaminate) the ceiling test.
  if (!clockKernels.empty() && !evalScheduler) {
    builder.setInsertionPointToEnd(module.getBody());
    auto coordinator = LLVM::LLVMFuncOp::create(
        builder, location, coordinatorName,
        LLVM::LLVMFunctionType::get(i32, {pointer, pointer}, false));
    Block *entry = coordinator.addEntryBlock(builder);
    builder.setInsertionPointToStart(entry);
    uint32_t readyWordCount =
        static_cast<uint32_t>((executableNodes.size() + 63) / 64);
    Value ready = entryAlloca(builder, location, i64, readyWordCount, 8);
    for (uint32_t word = 0; word != readyWordCount; ++word)
      LLVM::StoreOp::create(
          builder, location, llvmConstant(builder, location, i64, 0),
          byteGEP(builder, location, ready, uint64_t{word} * sizeof(uint64_t)),
          8);

    // A forward pass is sufficient only when every publication targets a
    // later graph node.  Real RTL contains feedback and source-order inversions
    // (PicoRV's memory-data cone is one); those publications set ingress bits
    // for nodes already visited by this pass.  Re-enter the generated scan
    // until its model-local ingress mask is empty.  This is the native eval
    // fixpoint, not a return to the runtime worklist.
    Block *scan = new Block;
    coordinator.getBody().push_back(scan);
    cf::BranchOp::create(builder, location, scan);
    builder.setInsertionPointToStart(scan);

    struct NodeIngress {
      uint32_t node;
      SmallVector<unsigned> records;
      std::string execute;
    };
    SmallVector<NodeIngress> nodeIngress;
    for (auto [recordIndex, record] : llvm::enumerate(mergedFragments)) {
      auto found = llvm::find_if(nodeIngress, [&](const NodeIngress &entry) {
        return entry.node == record.compute_node;
      });
      if (found == nodeIngress.end()) {
        nodeIngress.push_back({record.compute_node,
                               {static_cast<unsigned>(recordIndex)},
                               mergedExecutors[recordIndex]});
      } else {
        found->records.push_back(static_cast<unsigned>(recordIndex));
        if (found->execute.empty())
          found->execute = mergedExecutors[recordIndex];
      }
    }
    llvm::sort(nodeIngress, [](const NodeIngress &lhs, const NodeIngress &rhs) {
      return lhs.node < rhs.node;
    });

    // Drain in graph order. The original actor remains suspended at the same
    // continuation while a clean private body runs; unsupported records are
    // translated to the exact fine-node fallback bit.
    for (const NodeIngress &node : nodeIngress) {
      Value selected = llvmConstant(builder, location, builder.getI1Type(), 0);
      for (unsigned recordIndex : node.records) {
        const auto &record = mergedFragments[recordIndex];
        const auto &kernel = clockKernels[record.kernel];
        Value ingress = LLVM::AddressOfOp::create(builder, location, pointer,
                                                  kernel.ingressName);
        Value address = byteGEP(builder, location, ingress,
                                uint64_t{record.bit / 64} * sizeof(uint64_t));
        Value word = LLVM::LoadOp::create(builder, location, i64, address, 8);
        uint64_t mask = uint64_t{1} << (record.bit % 64);
        Value set = arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::ne,
            arith::AndIOp::create(builder, location, word,
                                  llvmConstant(builder, location, i64, mask)),
            llvmConstant(builder, location, i64, 0));
        selected = arith::OrIOp::create(builder, location, selected, set);
        LLVM::StoreOp::create(
            builder, location,
            arith::AndIOp::create(builder, location, word,
                                  llvmConstant(builder, location, i64, ~mask)),
            address, 8);
      }
      Block *execute = new Block;
      Block *next = new Block;
      coordinator.getBody().push_back(execute);
      coordinator.getBody().push_back(next);
      cf::CondBranchOp::create(builder, location, selected, execute,
                               ValueRange{}, next, ValueRange{});
      builder.setInsertionPointToStart(execute);
      if (!node.execute.empty()) {
        Value status =
            LLVM::CallOp::create(builder, location, TypeRange{i32},
                                 SymbolRefAttr::get(context, node.execute),
                                 ValueRange{entry->getArgument(1)})
                .getResult();
        // An implicit combinational sensitivity never retriggers its own
        // activation from a value it just published.  Encode that exclusion
        // in the generated ready mask instead of consulting coroutine actor
        // state in the runtime.
        for (unsigned recordIndex : node.records) {
          const auto &record = mergedFragments[recordIndex];
          const auto &kernel = clockKernels[record.kernel];
          Value ingress = LLVM::AddressOfOp::create(builder, location, pointer,
                                                    kernel.ingressName);
          Value address = byteGEP(builder, location, ingress,
                                  uint64_t{record.bit / 64} * sizeof(uint64_t));
          Value word = LLVM::LoadOp::create(builder, location, i64, address, 8);
          LLVM::StoreOp::create(
              builder, location,
              arith::AndIOp::create(
                  builder, location, word,
                  llvmConstant(builder, location, i64,
                               ~(uint64_t{1} << (record.bit % 64)))),
              address, 8);
        }
        Value ok = arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::eq, status,
            llvmConstant(builder, location, i32, OBELISK_RT_OK));
        Block *failed = new Block;
        coordinator.getBody().push_back(failed);
        cf::CondBranchOp::create(builder, location, ok, next, ValueRange{},
                                 failed, ValueRange{});
        builder.setInsertionPointToStart(failed);
        LLVM::ReturnOp::create(builder, location, status);
      } else if (!evalScheduler) {
        Value readyAddress =
            byteGEP(builder, location, ready,
                    uint64_t{node.node / 64} * sizeof(uint64_t));
        Value oldReady =
            LLVM::LoadOp::create(builder, location, i64, readyAddress, 8);
        LLVM::StoreOp::create(
            builder, location,
            arith::OrIOp::create(builder, location, oldReady,
                                 llvmConstant(builder, location, i64,
                                              uint64_t{1} << (node.node % 64))),
            readyAddress, 8);
        cf::BranchOp::create(builder, location, next);
      } else {
        cf::BranchOp::create(builder, location, next);
      }
      builder.setInsertionPointToStart(next);
    }
    if (!evalScheduler)
      LLVM::CallOp::create(
          builder, location, TypeRange{},
          SymbolRefAttr::get(context,
                             "obelisk_rt_v1_scheduler_activate_static_nodes"),
          ValueRange{entry->getArgument(1), ready,
                     llvmConstant(builder, location, i32, readyWordCount)});
    if (evalScheduler) {
      Value pending = llvmConstant(builder, location, i64, 0);
      for (const GeneratedClockKernel &kernel : clockKernels) {
        Value ingress = LLVM::AddressOfOp::create(builder, location, pointer,
                                                  kernel.ingressName);
        uint32_t words =
            static_cast<uint32_t>((kernel.computeNodes.size() + 63) / 64);
        for (uint32_t word = 0; word != words; ++word) {
          Value address = byteGEP(builder, location, ingress,
                                  uint64_t{word} * sizeof(uint64_t));
          Value queued =
              LLVM::LoadOp::create(builder, location, i64, address, 8);
          pending = arith::OrIOp::create(builder, location, pending, queued);
        }
      }
      Value dirty = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ne, pending,
          llvmConstant(builder, location, i64, 0));
      Block *done = new Block;
      coordinator.getBody().push_back(done);
      cf::CondBranchOp::create(builder, location, dirty, scan, ValueRange{},
                               done, ValueRange{});
      builder.setInsertionPointToStart(done);
      Value changed = entryAlloca(builder, location, i32, 1, 4);
      LLVM::StoreOp::create(builder, location,
                            llvmConstant(builder, location, i32, 0), changed,
                            4);
      Value commitStatus =
          LLVM::CallOp::create(
              builder, location, TypeRange{i32},
              SymbolRefAttr::get(context, nbaCommitName),
              ValueRange{entry->getArgument(0), entry->getArgument(1),
                         llvmConstant(builder, location, i32, 2), changed})
              .getResult();
      Value commitOK = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::eq, commitStatus,
          llvmConstant(builder, location, i32, OBELISK_RT_OK));
      Block *afterCommit = new Block;
      Block *commitFailed = new Block;
      coordinator.getBody().push_back(afterCommit);
      coordinator.getBody().push_back(commitFailed);
      cf::CondBranchOp::create(builder, location, commitOK, afterCommit,
                               ValueRange{}, commitFailed, ValueRange{});
      builder.setInsertionPointToStart(commitFailed);
      LLVM::ReturnOp::create(builder, location, commitStatus);
      builder.setInsertionPointToStart(afterCommit);
      Value postCommitPending = llvmConstant(builder, location, i64, 0);
      for (const GeneratedClockKernel &kernel : clockKernels) {
        Value ingress = LLVM::AddressOfOp::create(builder, location, pointer,
                                                  kernel.ingressName);
        uint32_t words =
            static_cast<uint32_t>((kernel.computeNodes.size() + 63) / 64);
        for (uint32_t word = 0; word != words; ++word)
          postCommitPending = arith::OrIOp::create(
              builder, location, postCommitPending,
              LLVM::LoadOp::create(builder, location, i64,
                                   byteGEP(builder, location, ingress,
                                           uint64_t{word} * sizeof(uint64_t)),
                                   8));
      }
      Value needsPostNBAEval = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ne, postCommitPending,
          llvmConstant(builder, location, i64, 0));
      Block *complete = new Block;
      coordinator.getBody().push_back(complete);
      cf::CondBranchOp::create(builder, location, needsPostNBAEval, scan,
                               ValueRange{}, complete, ValueRange{});
      builder.setInsertionPointToStart(complete);
      LLVM::ReturnOp::create(builder, location, commitStatus);
    } else {
      LLVM::ReturnOp::create(
          builder, location,
          llvmConstant(builder, location, i32, OBELISK_RT_OK));
    }
  }

  if (evalScheduler && !clockKernels.empty() && !mergedFragments.empty() &&
      mergedFragments.size() <= 64) {
    builder.setInsertionPointToEnd(module.getBody());
    auto fastCoordinator = LLVM::LLVMFuncOp::create(
        builder, location, evalCoordinatorName,
        LLVM::LLVMFunctionType::get(i32, {pointer, pointer}, false));
    fastCoordinator->setAttr(
        "passthrough",
        builder.getArrayAttr({builder.getStringAttr("alwaysinline")}));
    Block *fastEntry = fastCoordinator.addEntryBlock(builder);
    Block *dispatch = new Block;
    Block *commit = new Block;
    Block *afterCommit = new Block;
    Block *complete = new Block;
    fastCoordinator.getBody().push_back(dispatch);
    fastCoordinator.getBody().push_back(commit);
    fastCoordinator.getBody().push_back(afterCommit);
    fastCoordinator.getBody().push_back(complete);
    builder.setInsertionPointToStart(fastEntry);
    Value changed = entryAlloca(builder, location, i32, 1, 4);
    Value ingress = LLVM::AddressOfOp::create(builder, location, pointer,
                                              clockKernels.front().ingressName);
    cf::BranchOp::create(builder, location, dispatch);
    builder.setInsertionPointToStart(dispatch);
    Value ready = LLVM::LoadOp::create(builder, location, i64, ingress, 8);
    Value empty =
        arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                              ready, llvmConstant(builder, location, i64, 0));
    Block *select = new Block;
    fastCoordinator.getBody().push_back(select);
    cf::CondBranchOp::create(builder, location, empty, commit, ValueRange{},
                             select, ValueRange{});

    builder.setInsertionPointToStart(select);
    Value bit =
        LLVM::CountTrailingZerosOp::create(builder, location, i64, ready, true);
    Value remaining = arith::AndIOp::create(
        builder, location, ready,
        arith::SubIOp::create(builder, location, ready,
                              llvmConstant(builder, location, i64, 1)));
    LLVM::StoreOp::create(builder, location, remaining, ingress, 8);
    SmallVector<APInt> cases;
    SmallVector<Block *> destinations;
    SmallVector<ValueRange> destinationOperands;
    for (auto [recordIndex, record] : llvm::enumerate(mergedFragments)) {
      if (record.bit >= 64 || mergedExecutors[recordIndex].empty())
        continue;
      Block *execute = new Block;
      fastCoordinator.getBody().push_back(execute);
      cases.push_back(APInt(64, record.bit));
      destinations.push_back(execute);
      destinationOperands.push_back(ValueRange{});
      builder.setInsertionPointToStart(execute);
      (void)LLVM::CallOp::create(
          builder, location, TypeRange{i32},
          SymbolRefAttr::get(context, mergedExecutors[recordIndex]),
          ValueRange{fastEntry->getArgument(1)});
      Value published =
          LLVM::LoadOp::create(builder, location, i64, ingress, 8);
      LLVM::StoreOp::create(
          builder, location,
          arith::AndIOp::create(builder, location, published,
                                llvmConstant(builder, location, i64,
                                             ~(uint64_t{1} << record.bit))),
          ingress, 8);
      cf::BranchOp::create(builder, location, dispatch);
    }
    builder.setInsertionPointToEnd(select);
    LLVM::SwitchOp::create(builder, location, bit, dispatch, ValueRange{},
                           cases, destinations, destinationOperands,
                           ArrayRef<int32_t>{});

    builder.setInsertionPointToStart(commit);
    LLVM::StoreOp::create(builder, location,
                          llvmConstant(builder, location, i32, 0), changed, 4);
    Value commitStatus =
        LLVM::CallOp::create(
            builder, location, TypeRange{i32},
            SymbolRefAttr::get(context, nbaCommitName),
            ValueRange{fastEntry->getArgument(0), fastEntry->getArgument(1),
                       llvmConstant(builder, location, i32, 2), changed})
            .getResult();
    Value commitOK = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, commitStatus,
        llvmConstant(builder, location, i32, OBELISK_RT_OK));
    Block *failed = new Block;
    failed->addArgument(i32, location);
    fastCoordinator.getBody().push_back(failed);
    cf::CondBranchOp::create(builder, location, commitOK, afterCommit,
                             ValueRange{}, failed, ValueRange{commitStatus});

    builder.setInsertionPointToStart(afterCommit);
    Value postNBAReady =
        LLVM::LoadOp::create(builder, location, i64, ingress, 8);
    Value postNBAEmpty = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, postNBAReady,
        llvmConstant(builder, location, i64, 0));
    cf::CondBranchOp::create(builder, location, postNBAEmpty, complete,
                             ValueRange{}, dispatch, ValueRange{});
    builder.setInsertionPointToStart(complete);
    LLVM::ReturnOp::create(builder, location, commitStatus);
    builder.setInsertionPointToStart(failed);
    LLVM::ReturnOp::create(builder, location, failed->getArgument(0));
  }

  builder.setInsertionPointToEnd(module.getBody());
  auto snapshot = LLVM::LLVMFuncOp::create(
      builder, location, snapshotName,
      LLVM::LLVMFunctionType::get(i32, {pointer, pointer, pointer}, false));
  Block *snapshotEntry = snapshot.addEntryBlock(builder);
  builder.setInsertionPointToStart(snapshotEntry);
  // Snapshotting is part of the transactional runtime handoff.  This ceiling
  // path intentionally has no such handoff.
  LLVM::ReturnOp::create(
      builder, location,
      llvmConstant(builder, location, i32, OBELISK_RT_INVALID_DESIGN));

  builder.setInsertionPointToEnd(module.getBody());
  auto nbaCommit = LLVM::LLVMFuncOp::create(
      builder, location, nbaCommitName,
      LLVM::LLVMFunctionType::get(i32, {pointer, pointer, i32, pointer},
                                  false));
  if (evalScheduler)
    nbaCommit->setAttr(
        "passthrough",
        builder.getArrayAttr({builder.getStringAttr("alwaysinline")}));
  Block *nbaCommitEntry = nbaCommit.addEntryBlock(builder);
  Block *genericNBACommit = new Block;
  nbaCommit.getBody().push_back(genericNBACommit);
  builder.setInsertionPointToStart(nbaCommitEntry);
  Value committedCount = entryAlloca(builder, location, i32, 1, 4);
  LLVM::StoreOp::create(builder, location,
                        llvmConstant(builder, location, i32, 0), committedCount,
                        4);

  bool generateScalarCommits =
      cleanSuperstepEnabled && enableDirectState &&
      !guardedSpecializationEnabled &&
      staticNBAPlan.generatedOffsets.size() == nbaRoots.size();
  SmallVector<SmallVector<uint32_t>> scalarRootsByWord(nbaDirtyWordCount);
  uint64_t planeBytes = (stateLayout.bitCount + 7) / 8;
  if (generateScalarCommits)
    for (auto [rootIndex, root, accumulator, offset] :
         llvm::enumerate(nbaRoots, staticNBAPlan.generatedAccumulators,
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
    if (!evalScheduler && !scalarRootsByWord[word].empty()) {
      directlyCommittedByWord[word] = entryAlloca(builder, location, i64, 1, 8);
      LLVM::StoreOp::create(builder, location,
                            llvmConstant(builder, location, i64, 0),
                            directlyCommittedByWord[word], 8);
    }

  bool generateGroupedFanout =
      llvm::any_of(scalarRootsByWord, [&](ArrayRef<uint32_t> roots) {
        return llvm::any_of(roots, [&](uint32_t rootIndex) {
          return llvm::any_of(
              fanoutEntries, [&](const obelisk_rt_static_fanout_entry &entry) {
                return entry.static_state == nbaRoots[rootIndex].static_state;
              });
        });
      });
  uint32_t activationWordCount =
      generateGroupedFanout
          ? static_cast<uint32_t>((executableNodes.size() + 63) / 64)
          : 0;
  // Eval mode owns NBA fanout as well as active-region fanout.  The generated
  // commit epilogue translates changed roots straight into model-method bits
  // and immediately drains the generated fixpoint; it never reconstructs a
  // runtime ready-node worklist.
  uint32_t directActivationWordCount =
      evalScheduler ? static_cast<uint32_t>((mergedFragments.size() + 63) / 64)
                    : 0;
  Value activatedNodes;
  Value activatedDirect;
  if (generateGroupedFanout) {
    activatedNodes =
        entryAlloca(builder, location, i64, activationWordCount, 8);
    for (uint32_t word = 0; word != activationWordCount; ++word)
      LLVM::StoreOp::create(builder, location,
                            llvmConstant(builder, location, i64, 0),
                            byteGEP(builder, location, activatedNodes,
                                    uint64_t{word} * sizeof(uint64_t)),
                            8);
  }
  // Direct eval publication is independent of whether any generic grouped
  // fanout exists.  In particular, an eval-only design can have no
  // `activatedNodes` bitmap while still publishing fixed NBA roots to direct
  // fragments.  Keep this alloca governed by its own word count so the later
  // direct epilogue never observes an unset MLIR Value.
  if (directActivationWordCount != 0) {
    activatedDirect =
        entryAlloca(builder, location, i64, directActivationWordCount, 8);
    for (uint32_t word = 0; word != directActivationWordCount; ++word)
      LLVM::StoreOp::create(builder, location,
                            llvmConstant(builder, location, i64, 0),
                            byteGEP(builder, location, activatedDirect,
                                    uint64_t{word} * sizeof(uint64_t)),
                            8);
  }

  Value directEnabled;
  if (evalScheduler) {
    directEnabled = llvmConstant(builder, location, builder.getI1Type(), 1);
  } else {
    Value directGuard =
        LLVM::CallOp::create(
            builder, location, TypeRange{i32},
            SymbolRefAttr::get(context,
                               "obelisk_rt_v1_static_nba_direct_commit_guard"),
            ValueRange{nbaCommitEntry->getArgument(1)})
            .getResult();
    directEnabled = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ne, directGuard,
        llvmConstant(builder, location, i32, 0));
  }
  Value stateValue = LLVM::AddressOfOp::create(builder, location, pointer,
                                               "__obelisk_state_value");
  Value stateUnknown = LLVM::AddressOfOp::create(builder, location, pointer,
                                                 "__obelisk_state_unknown");
  for (const DynamicEvalNBA &entry : dynamicEvalNBAs) {
    Value validAddress =
        LLVM::AddressOfOp::create(builder, location, pointer, entry.validName);
    Value valid = LLVM::LoadOp::create(builder, location, i32, validAddress, 4);
    Value active =
        arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ne,
                              valid, llvmConstant(builder, location, i32, 0));
    Value dynamicBit = LLVM::LoadOp::create(
        builder, location, i64,
        LLVM::AddressOfOp::create(builder, location, pointer, entry.offsetName),
        8);
    Value dynamicByte = arith::ShRUIOp::create(
        builder, location, dynamicBit, llvmConstant(builder, location, i64, 3));
    Value byteOffset = arith::AddIOp::create(
        builder, location, dynamicByte,
        llvmConstant(builder, location, i64,
                     staticNBAPlan.generatedOffsets[entry.rootIndex] / 8));
    auto planeAddress = [&](Value plane) {
      return LLVM::GEPOp::create(builder, location, pointer,
                                 builder.getI8Type(), plane,
                                 ValueRange{byteOffset});
    };
    IntegerType valueType = IntegerType::get(context, entry.width);
    Value valueAddress = planeAddress(stateValue);
    Value oldValue =
        LLVM::LoadOp::create(builder, location, valueType, valueAddress, 1);
    Value staged64 = LLVM::LoadOp::create(
        builder, location, i64,
        LLVM::AddressOfOp::create(builder, location, pointer, entry.valueName),
        8);
    Value stagedValue =
        entry.width == 64
            ? staged64
            : LLVM::TruncOp::create(builder, location, valueType, staged64)
                  .getResult();
    LLVM::StoreOp::create(builder, location,
                          arith::SelectOp::create(builder, location, active,
                                                  stagedValue, oldValue),
                          valueAddress, 1);
    if (!evalScheduler) {
      Value unknownAddress = planeAddress(stateUnknown);
      Value oldUnknown =
          LLVM::LoadOp::create(builder, location, valueType, unknownAddress, 1);
      Value stagedUnknown64 = LLVM::LoadOp::create(
          builder, location, i64,
          LLVM::AddressOfOp::create(builder, location, pointer,
                                    entry.unknownName),
          8);
      Value stagedUnknown =
          entry.width == 64 ? stagedUnknown64
                            : LLVM::TruncOp::create(builder, location,
                                                    valueType, stagedUnknown64)
                                  .getResult();
      LLVM::StoreOp::create(builder, location,
                            arith::SelectOp::create(builder, location, active,
                                                    stagedUnknown, oldUnknown),
                            unknownAddress, 1);
    }
    LLVM::StoreOp::create(builder, location,
                          llvmConstant(builder, location, i32, 0), validAddress,
                          4);
  }

  SmallVector<Block *> wordBlocks(nbaDirtyWordCount);
  for (uint32_t word = 0; word != nbaDirtyWordCount; ++word)
    if (!scalarRootsByWord[word].empty()) {
      wordBlocks[word] = new Block;
      nbaCommit.getBody().getBlocks().insert(Region::iterator(genericNBACommit),
                                             wordBlocks[word]);
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
    Value low =
        LLVM::LoadOp::create(builder, location, i64,
                             byteGEP(builder, location, plane, firstByte), 1);
    Value value = low;
    if (shift != 0)
      value =
          arith::ShRUIOp::create(builder, location, value,
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
    Value cleared =
        arith::AndIOp::create(builder, location, oldLow,
                              llvmConstant(builder, location, i64, ~lowMask));
    Value positioned = value;
    if (shift != 0)
      positioned =
          arith::ShLIOp::create(builder, location, positioned,
                                llvmConstant(builder, location, i64, shift));
    positioned =
        arith::AndIOp::create(builder, location, positioned,
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
    Value oldHigh = LLVM::LoadOp::create(builder, location, builder.getI8Type(),
                                         highAddress, 1);
    Value highValue = arith::ShRUIOp::create(
        builder, location, value,
        llvmConstant(builder, location, i64, 64 - shift));
    highValue = LLVM::TruncOp::create(builder, location, builder.getI8Type(),
                                      highValue);
    Value newHigh = arith::OrIOp::create(
        builder, location,
        arith::AndIOp::create(builder, location, oldHigh,
                              llvmConstant(builder, location,
                                           builder.getI8Type(),
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
    Value dirtyBase = LLVM::AddressOfOp::create(builder, location, pointer,
                                                nbaDirtyRootsName);
    Value dirty =
        LLVM::LoadOp::create(builder, location, i64,
                             byteGEP(builder, location, dirtyBase,
                                     uint64_t{word} * sizeof(uint64_t)),
                             8);
    if (evalScheduler)
      LLVM::StoreOp::create(builder, location,
                            llvmConstant(builder, location, i64, 0),
                            byteGEP(builder, location, dirtyBase,
                                    uint64_t{word} * sizeof(uint64_t)),
                            8);
    Value wordEmpty =
        arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                              dirty, llvmConstant(builder, location, i64, 0));
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
      uint32_t fixedCommitRegion =
          staticNBAPlan.generatedCommitRegions[rootIndex];
      bool fixedScalarStage = fixedCommitRegion != UINT32_MAX;
      StringRef accumulator = staticNBAPlan.generatedAccumulators[rootIndex];
      Block *afterRoot =
          position + 1 == scalarRootsByWord[word].size() ? next : new Block;
      if (afterRoot != next)
        nbaCommit.getBody().getBlocks().insert(Region::iterator(next),
                                               afterRoot);
      Block *commitRoot = new Block;
      nbaCommit.getBody().getBlocks().insert(Region::iterator(afterRoot),
                                             commitRoot);
      builder.setInsertionPointToStart(rootBlock);
      Value selected = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ne,
          arith::AndIOp::create(builder, location, dirty,
                                llvmConstant(builder, location, i64,
                                             uint64_t{1} << (rootIndex % 64))),
          llvmConstant(builder, location, i64, 0));
      Value accumulatorBase =
          LLVM::AddressOfOp::create(builder, location, pointer, accumulator);
      Value regionMatches;
      if (fixedScalarStage) {
        regionMatches = arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::eq,
            nbaCommitEntry->getArgument(2),
            llvmConstant(builder, location, i32, fixedCommitRegion));
      } else {
        Value valid = LLVM::LoadOp::create(
            builder, location, i32,
            byteGEP(builder, location, accumulatorBase,
                    offsetof(obelisk_rt_generated_nba_accumulator_256, valid)),
            4);
        Value region = LLVM::LoadOp::create(
            builder, location, i32,
            byteGEP(builder, location, accumulatorBase,
                    offsetof(obelisk_rt_generated_nba_accumulator_256,
                             exec_region)),
            4);
        regionMatches = arith::AndIOp::create(
            builder, location,
            arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ne,
                                  valid,
                                  llvmConstant(builder, location, i32, 0)),
            arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                                  region, nbaCommitEntry->getArgument(2)));
      }
      Value validRoot =
          arith::AndIOp::create(builder, location, selected, regionMatches);
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
      Value writeMask =
          fixedScalarStage
              ? llvmConstant(builder, location, i64, scalarMask(root.bit_width))
              : arith::AndIOp::create(
                    builder, location,
                    LLVM::LoadOp::create(
                        builder, location, i64,
                        byteGEP(
                            builder, location, accumulatorBase,
                            offsetof(obelisk_rt_generated_nba_accumulator_256,
                                     write_mask)),
                        8),
                    llvmConstant(builder, location, i64,
                                 scalarMask(root.bit_width)))
                    .getResult();
      Value oldValue = loadRoot(stateValue, offset, root.bit_width);
      Value oldUnknown = evalScheduler
                             ? llvmConstant(builder, location, i64, 0)
                             : loadRoot(stateUnknown, offset, root.bit_width);
      Value inverseMask = arith::XOrIOp::create(
          builder, location, writeMask,
          llvmConstant(builder, location, i64, UINT64_MAX));
      Value newValue = arith::OrIOp::create(
          builder, location,
          arith::AndIOp::create(builder, location, oldValue, inverseMask),
          arith::AndIOp::create(builder, location, stagedValue, writeMask));
      Value newUnknown =
          evalScheduler ? llvmConstant(builder, location, i64, 0)
                        : arith::OrIOp::create(
                              builder, location,
                              arith::AndIOp::create(builder, location,
                                                    oldUnknown, inverseMask),
                              arith::AndIOp::create(builder, location,
                                                    stagedUnknown, writeMask))
                              .getResult();
      storeRoot(stateValue, offset, root.bit_width, newValue);
      if (!evalScheduler)
        storeRoot(stateUnknown, offset, root.bit_width, newUnknown);
      if (!evalScheduler || !fixedScalarStage) {
        LLVM::StoreOp::create(
            builder, location, llvmConstant(builder, location, i64, 0),
            byteGEP(
                builder, location, accumulatorBase,
                offsetof(obelisk_rt_generated_nba_accumulator_256, write_mask)),
            8);
        LLVM::StoreOp::create(
            builder, location, llvmConstant(builder, location, i32, 0),
            byteGEP(builder, location, accumulatorBase,
                    offsetof(obelisk_rt_generated_nba_accumulator_256, valid)),
            4);
      }
      if (!evalScheduler) {
        Value directlyCommitted = LLVM::LoadOp::create(
            builder, location, i64, directlyCommittedByWord[word], 8);
        LLVM::StoreOp::create(
            builder, location,
            arith::OrIOp::create(builder, location, directlyCommitted,
                                 llvmConstant(builder, location, i64,
                                              uint64_t{1} << (rootIndex % 64))),
            directlyCommittedByWord[word], 8);
      }
      Value changed = arith::OrIOp::create(
          builder, location,
          arith::XOrIOp::create(builder, location, oldValue, newValue),
          arith::XOrIOp::create(builder, location, oldUnknown, newUnknown));
      Value rootChanged = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ne, changed,
          llvmConstant(builder, location, i64, 0));
      if (!evalScheduler) {
        Value priorChanged = LLVM::LoadOp::create(
            builder, location, i32, nbaCommitEntry->getArgument(3), 4);
        Value changedI32 =
            LLVM::ZExtOp::create(builder, location, i32, rootChanged);
        LLVM::StoreOp::create(
            builder, location,
            arith::OrIOp::create(builder, location, priorChanged, changedI32),
            nbaCommitEntry->getArgument(3), 4);
        Value count =
            LLVM::LoadOp::create(builder, location, i32, committedCount, 4);
        LLVM::StoreOp::create(
            builder, location,
            arith::AddIOp::create(
                builder, location, count,
                llvmConstant(builder, location, i32, uint32_t{1})),
            committedCount, 4);
      }
      struct TriggerGroup {
        uint32_t edge;
        uint64_t mask;
        SmallVector<uint64_t> nodes;
        SmallVector<uint64_t> direct;
      };
      SmallVector<TriggerGroup> groups;
      for (const obelisk_rt_static_fanout_entry &entry : fanoutEntries) {
        if (entry.static_state != root.static_state ||
            entry.low_bit >= root.bit_width)
          continue;
        uint64_t high =
            std::min<uint64_t>(root.bit_width, entry.low_bit + entry.bit_width);
        if (entry.low_bit >= high)
          continue;
        uint64_t mask = scalarMask(high - entry.low_bit) << entry.low_bit;
        auto group = llvm::find_if(groups, [&](const TriggerGroup &candidate) {
          return candidate.edge == entry.edge && candidate.mask == mask;
        });
        if (group == groups.end()) {
          groups.push_back(
              {entry.edge, mask, SmallVector<uint64_t>(activationWordCount, 0),
               SmallVector<uint64_t>(directActivationWordCount, 0)});
          group = std::prev(groups.end());
        }
        if (evalScheduler && entry.reserved != 0 &&
            directActivationWordCount != 0)
          group->direct[entry.merged_bit / 64] |= uint64_t{1}
                                                  << (entry.merged_bit % 64);
        else
          group->nodes[entry.compute_node / 64] |= uint64_t{1}
                                                   << (entry.compute_node % 64);
      }
      if (!groups.empty()) {
        Value widthMask =
            llvmConstant(builder, location, i64, scalarMask(root.bit_width));
        auto invert = [&](Value value) {
          return arith::XOrIOp::create(
                     builder, location, value,
                     llvmConstant(builder, location, i64, UINT64_MAX))
              .getResult();
        };
        Value oldKnown = arith::AndIOp::create(builder, location,
                                               invert(oldUnknown), widthMask);
        Value newKnown = arith::AndIOp::create(builder, location,
                                               invert(newUnknown), widthMask);
        Value oldZero = arith::AndIOp::create(builder, location, oldKnown,
                                              invert(oldValue));
        Value oldOne =
            arith::AndIOp::create(builder, location, oldKnown, oldValue);
        Value newZero = arith::AndIOp::create(builder, location, newKnown,
                                              invert(newValue));
        Value newOne =
            arith::AndIOp::create(builder, location, newKnown, newValue);
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
            observed =
                arith::OrIOp::create(builder, location, posedge, negedge);
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
          for (auto [activationWord, nodeMask] : llvm::enumerate(group.nodes)) {
            if (nodeMask == 0)
              continue;
            Value address =
                byteGEP(builder, location, activatedNodes,
                        uint64_t{activationWord} * sizeof(uint64_t));
            Value active =
                LLVM::LoadOp::create(builder, location, i64, address, 8);
            Value selected = arith::SelectOp::create(
                builder, location, triggered,
                llvmConstant(builder, location, i64, nodeMask),
                llvmConstant(builder, location, i64, 0));
            LLVM::StoreOp::create(
                builder, location,
                arith::OrIOp::create(builder, location, active, selected),
                address, 8);
          }
          for (auto [activationWord, directMask] :
               llvm::enumerate(group.direct)) {
            if (directMask == 0)
              continue;
            Value address =
                byteGEP(builder, location, activatedDirect,
                        uint64_t{activationWord} * sizeof(uint64_t));
            Value active =
                LLVM::LoadOp::create(builder, location, i64, address, 8);
            Value selected = arith::SelectOp::create(
                builder, location, triggered,
                llvmConstant(builder, location, i64, directMask),
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
  if (generateGroupedFanout && !evalScheduler)
    LLVM::CallOp::create(
        builder, location, TypeRange{},
        SymbolRefAttr::get(context,
                           "obelisk_rt_v1_scheduler_activate_static_nodes"),
        ValueRange{nbaCommitEntry->getArgument(1), activatedNodes,
                   llvmConstant(builder, location, i32, activationWordCount)});
  if (directActivationWordCount != 0 && !clockKernels.empty()) {
    Value ingress = LLVM::AddressOfOp::create(builder, location, pointer,
                                              clockKernels.front().ingressName);
    Value any = llvmConstant(builder, location, builder.getI1Type(), 0);
    for (uint32_t word = 0; word != directActivationWordCount; ++word) {
      Value activated =
          LLVM::LoadOp::create(builder, location, i64,
                               byteGEP(builder, location, activatedDirect,
                                       uint64_t{word} * sizeof(uint64_t)),
                               8);
      Value selected = activated;
      Value address = byteGEP(builder, location, ingress,
                              uint64_t{word} * sizeof(uint64_t));
      Value previous = LLVM::LoadOp::create(builder, location, i64, address, 8);
      LLVM::StoreOp::create(
          builder, location,
          arith::OrIOp::create(builder, location, previous, selected), address,
          8);
      any = arith::OrIOp::create(
          builder, location, any,
          arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ne,
                                selected,
                                llvmConstant(builder, location, i64, 0)));
    }
    (void)any;
  }
  Value dirtyBase;
  if (nbaDirtyWordCount != 0)
    dirtyBase = LLVM::AddressOfOp::create(builder, location, pointer,
                                          nbaDirtyRootsName);
  for (uint32_t word = 0; word != nbaDirtyWordCount; ++word) {
    if (evalScheduler || !directlyCommittedByWord[word])
      continue;
    Value dirtyAddress = byteGEP(builder, location, dirtyBase,
                                 uint64_t{word} * sizeof(uint64_t));
    Value dirty = LLVM::LoadOp::create(builder, location, i64, dirtyAddress, 8);
    Value committed = LLVM::LoadOp::create(builder, location, i64,
                                           directlyCommittedByWord[word], 8);
    LLVM::StoreOp::create(
        builder, location,
        arith::AndIOp::create(
            builder, location, dirty,
            arith::XOrIOp::create(
                builder, location, committed,
                llvmConstant(builder, location, i64, UINT64_MAX))),
        dirtyAddress, 8);
  }
  Value directCount =
      LLVM::LoadOp::create(builder, location, i32, committedCount, 4);
  if (!evalScheduler)
    LLVM::CallOp::create(
        builder, location, TypeRange{},
        SymbolRefAttr::get(
            context, "obelisk_rt_v1_static_nba_account_generated_commits"),
        ValueRange{nbaCommitEntry->getArgument(1), directCount});

  // The generated scalar path normally consumes the complete dirty leaf for
  // this barrier.  Do not enter the generic ordered-root walker merely so it
  // can observe an empty bitmap and clear the summary level.  Roots belonging
  // to a later region, claimed slow roots, and unsupported wide roots remain
  // set above and still take the canonical path.
  Value remainingDirty = llvmConstant(builder, location, i64, 0);
  for (uint32_t word = 0; word != nbaDirtyWordCount; ++word) {
    Value dirtyAddress = byteGEP(builder, location, dirtyBase,
                                 uint64_t{word} * sizeof(uint64_t));
    Value dirty = LLVM::LoadOp::create(builder, location, i64, dirtyAddress, 8);
    remainingDirty =
        arith::OrIOp::create(builder, location, remainingDirty, dirty);
  }
  Value noRemainingDirty = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::eq, remainingDirty,
      llvmConstant(builder, location, i64, 0));
  Value allDirect =
      arith::AndIOp::create(builder, location, directEnabled, noRemainingDirty);
  Block *directDone = new Block;
  nbaCommit.getBody().push_back(directDone);
  Block *callGeneric = nullptr;
  if (evalScheduler) {
    cf::BranchOp::create(builder, location, directDone);
  } else {
    callGeneric = new Block;
    nbaCommit.getBody().push_back(callGeneric);
    cf::CondBranchOp::create(builder, location, allDirect, directDone,
                             ValueRange{}, callGeneric, ValueRange{});
  }

  builder.setInsertionPointToStart(directDone);
  if (!evalScheduler && nbaDirtySummaryWordCount != 0) {
    Value summaryBase = LLVM::AddressOfOp::create(builder, location, pointer,
                                                  nbaDirtySummaryName);
    for (uint32_t word = 0; word != nbaDirtySummaryWordCount; ++word)
      LLVM::StoreOp::create(builder, location,
                            llvmConstant(builder, location, i64, 0),
                            byteGEP(builder, location, summaryBase,
                                    uint64_t{word} * sizeof(uint64_t)),
                            8);
  }
  LLVM::ReturnOp::create(builder, location,
                         llvmConstant(builder, location, i32, OBELISK_RT_OK));

  if (callGeneric) {
    builder.setInsertionPointToStart(callGeneric);
    Value nbaCommitStatus =
        LLVM::CallOp::create(
            builder, location, TypeRange{i32},
            SymbolRefAttr::get(context,
                               "obelisk_rt_v1_static_nba_commit_roots"),
            ValueRange{nbaCommitEntry->getArgument(1),
                       llvmConstant(builder, location, i32, nbaRoots.size()),
                       nbaCommitEntry->getArgument(2),
                       nbaCommitEntry->getArgument(3)})
            .getResult();
    LLVM::ReturnOp::create(builder, location, nbaCommitStatus);
  }

  auto planType = LLVM::LLVMStructType::getLiteral(
      context, {i32,     i64,     pointer, i64,     i32,     i32,     pointer,
                pointer, i64,     pointer, pointer, pointer, pointer, i32,
                i32,     pointer, i64,     pointer, i64,     pointer, i64,
                pointer, pointer, pointer, i32,     i32,     pointer, i32,
                i32,     pointer, i32,     i32,     pointer, i64,     pointer});
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
                         : 0) |
                    (evalScheduler ? OBELISK_RT_NATIVE_SCHEDULE_EVAL : 0)),
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
        value =
            insertValue(initializerBuilder, location, value, dirtyRoots, 23);
        value = insertValue(
            initializerBuilder, location, value,
            llvmConstant(initializerBuilder, location, i32, nbaDirtyWordCount),
            24);
        value =
            insertValue(initializerBuilder, location, value,
                        llvmConstant(initializerBuilder, location, i32, 0), 25);
        Value dirtySummary =
            nbaDirtySummaryWordCount == 0
                ? LLVM::ZeroOp::create(initializerBuilder, location, pointer)
                      .getResult()
                : LLVM::AddressOfOp::create(initializerBuilder, location,
                                            pointer, nbaDirtySummaryName)
                      .getResult();
        value =
            insertValue(initializerBuilder, location, value, dirtySummary, 26);
        value = insertValue(initializerBuilder, location, value,
                            llvmConstant(initializerBuilder, location, i32,
                                         nbaDirtySummaryWordCount),
                            27);
        value =
            insertValue(initializerBuilder, location, value,
                        llvmConstant(initializerBuilder, location, i32, 0), 28);
        Value clocksAddress =
            clockKernels.empty()
                ? LLVM::ZeroOp::create(initializerBuilder, location, pointer)
                      .getResult()
                : LLVM::AddressOfOp::create(initializerBuilder, location,
                                            pointer, clockKernelsName)
                      .getResult();
        value =
            insertValue(initializerBuilder, location, value, clocksAddress, 29);
        value = insertValue(initializerBuilder, location, value,
                            llvmConstant(initializerBuilder, location, i32,
                                         clockKernels.size()),
                            30);
        value =
            insertValue(initializerBuilder, location, value,
                        llvmConstant(initializerBuilder, location, i32, 0), 31);
        Value mergedAddress =
            mergedFragments.empty()
                ? LLVM::ZeroOp::create(initializerBuilder, location, pointer)
                      .getResult()
                : LLVM::AddressOfOp::create(initializerBuilder, location,
                                            pointer, mergedFragmentsName)
                      .getResult();
        value =
            insertValue(initializerBuilder, location, value, mergedAddress, 32);
        value = insertValue(initializerBuilder, location, value,
                            llvmConstant(initializerBuilder, location, i64,
                                         mergedFragments.size()),
                            33);
        Value coordinatorAddress =
            clockKernels.empty()
                ? LLVM::ZeroOp::create(initializerBuilder, location, pointer)
                      .getResult()
                : LLVM::AddressOfOp::create(
                      initializerBuilder, location, pointer,
                      evalScheduler ? evalCoordinatorName : coordinatorName)
                      .getResult();
        return insertValue(initializerBuilder, location, value,
                           coordinatorAddress, 34);
      });
  if (!evalScheduler) {
    if (fullyStatic)
      getOrDeclareLLVMFunction(module, "obelisk_rt_v1_scheduler_run_aot_nodes",
                               i32, {pointer, pointer, i32});
    else
      getOrDeclareLLVMFunction(module, "obelisk_rt_v1_scheduler_run", i32,
                               {pointer});
    getOrDeclareLLVMFunction(module, "obelisk_rt_v1_scheduler_snapshot_aot",
                             i32, {pointer, pointer});
  }
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_static_nba_commit_root", i32,
                           {pointer, i32, i32, pointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_static_nba_commit_roots", i32,
                           {pointer, i32, i32, pointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_static_nba_direct_commit_guard", i32, {pointer});
  getOrDeclareLLVMFunction(module,
                           "obelisk_rt_v1_static_nba_account_generated_commits",
                           LLVM::LLVMVoidType::get(context), {pointer, i32});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_scheduler_activate_static_nodes",
      LLVM::LLVMVoidType::get(context), {pointer, pointer, i32});
  getOrDeclareLLVMFunction(module,
                           "obelisk_rt_v1_scheduler_direct_fragment_enter", i32,
                           {pointer, i32, i32, pointer});
  getOrDeclareLLVMFunction(module,
                           "obelisk_rt_v1_scheduler_direct_fragment_leave", i32,
                           {pointer, i32});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_scheduler_execute_aot_actor",
                           i32, {pointer, i32});
  return success();
}

} // namespace obelisk::detail
