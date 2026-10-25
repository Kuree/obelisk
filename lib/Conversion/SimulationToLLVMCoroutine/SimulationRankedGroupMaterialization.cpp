//===- SimulationRankedGroupMaterialization.cpp - Shared ranked sweeps
//-----===//

#include "SimulationAOTPlanning.h"
#include "SimulationEvalReadySet.h"
#include "obelisk/Dialect/Native/NativeDialect.h"
#include "obelisk/Dialect/Runtime/RuntimeDialect.h"
#include "obelisk/Dialect/Schedule/ScheduleEnums.h"
#include "obelisk/Dialect/Schedule/ScheduleFields.h"
#include "obelisk/Dialect/Schedule/ScheduleMetadata.h"

#include "obelisk/Dialect/Simulation/SimulationMetadata.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/Matchers.h"
#include "llvm/ADT/StringMap.h"
#include <optional>

using namespace mlir;

namespace obelisk::detail {

FailureOr<NativeRankedGroupPlan>
planNativeRankedGroups(ModuleOp module, const NativeEvalCoordinatorPlan &plan,
                       ArrayRef<SmallVector<NativePromotionRange>> ranges,
                       uint64_t stateBits) {
  NativeRankedGroupPlan result;
  if (plan.clockKernels.empty() || plan.rankedNodes.empty() || plan.writableVPI)
    return result;
  llvm::StringMap<sim::SimFuncOp> functions;
  ::obelisk::detail::walkNativeFunctions<sim::SimFuncOp>(
      module, [&](sim::SimFuncOp function) {
        functions.try_emplace(function.getSymName(), function);
      });
  // Only closed, raw-capture computation may bypass its fragment wrapper.
  // Graph effects establish activation safety; the executable body must also
  // have no runtime/status boundary. Unproved bodies keep their executor.
  auto bodyCost = [&](StringRef name) -> uint64_t {
    auto function = functions.lookup(name);
    if (!function ||
        !::obelisk::schedule::has<::obelisk::schedule::Field::EvalRawCaptures>(
            function) ||
        !function.getFunctionType().getResults().empty())
      return 0;
    bool safe = true;
    uint64_t cost = 0;
    function.walk([&](Operation *op) {
      ++cost;
      safe &=
          !isa_and_nonnull<runtime::ObeliskRuntimeDialect>(op->getDialect()) &&
          !isa<sim::SimCallOp, sim::SimStatusCheckOp, LLVM::CallOp,
               func::CallOp>(op);
    });
    return safe ? cost : 0;
  };
  auto limit =
      ::obelisk::schedule::get<::obelisk::schedule::Field::MaxInlineOps>(
          module);
  uint64_t budget = limit ? limit.getUInt() : 5000;
  if (!budget)
    budget = UINT64_MAX;
  SmallVector<NativeRankedEvalNode> members;
  uint64_t cost = 0;
  bool invalidRange = false;
  auto emit = [&]() {
    if (members.empty())
      return;
    NativeRankedGroupChunk chunk;
    chunk.members.swap(members);
    cost = 0;
    // Profitability only: share a certificate for a substantial candidate
    // with a bounded scan table. These limits do not establish knownness.
    bool eligible = chunk.members.size() >= 8 &&
                    llvm::all_of(chunk.members, [&](const auto &node) {
                      return !node.clocked && !node.twoStateBody.empty() &&
                             !plan.twoStateExecutors[node.owner].empty() &&
                             node.owner < ranges.size() &&
                             !ranges[node.owner].empty();
                    });
    if (eligible) {
      SmallVector<std::pair<uint64_t, uint64_t>> intervals, merged;
      SmallVector<int32_t> identities;
      for (const auto &node : chunk.members) {
        identities.push_back(node.owner);
        if (node.owner >= ranges.size()) {
          invalidRange = true;
          continue;
        }
        for (const auto &range : ranges[node.owner]) {
          if (!range.bitWidth || range.bitOffset > stateBits ||
              range.bitWidth > stateBits - range.bitOffset) {
            invalidRange = true;
            continue;
          }
          intervals.emplace_back(range.bitOffset,
                                 range.bitOffset + range.bitWidth);
        }
      }
      llvm::sort(intervals);
      for (auto [begin, end] : intervals) {
        if (!merged.empty() && begin <= merged.back().second)
          merged.back().second = std::max(merged.back().second, end);
        else
          merged.emplace_back(begin, end);
      }
      if (!merged.empty() && merged.size() <= 64) {
        // IEEE 1800-2023 6.3.1: only these exact bits must exclude X/Z.
        // Reuse every member's complete input/old-destination certificate,
        // merge overlaps without filling holes, and keep runnable identities
        // unchanged. A later dataflow rejection still uses the member proofs.
        SmallVector<int64_t> encoded;
        for (auto [begin, end] : merged) {
          encoded.push_back(begin);
          encoded.push_back(end - begin);
        }
        Builder builder(module.getContext());
        chunk.proof = schedule::GroupProofDependencyAttr::get(
            module.getContext(),
            builder.getI64IntegerAttr(result.domains.size()),
            builder.getDenseI32ArrayAttr(identities),
            builder.getDenseI64ArrayAttr(encoded));
        result.domains.push_back(chunk.proof);
      }
    }
    result.chunks.push_back(std::move(chunk));
  };
  auto finishSegment = [&]() {
    if (!result.chunks.empty())
      result.chunks.back().endsSegment = true;
  };
  // Singleton dependency islands have no edges between certified bodies, but
  // often share an NBA activation. Give independent singletons in one ready
  // word a bounded predicated sweep instead of a dispatcher trip per owner.
  // Keep connected islands in their proven topological order. Readiness is
  // still checked at each member, so this never invents an activation.
  DenseMap<uint32_t, unsigned> islandSizes;
  uint32_t nextIsland = 0;
  for (const auto &node : plan.rankedNodes) {
    ++islandSizes[node.island];
    nextIsland = std::max(nextIsland, node.island + 1);
  }
  SmallVector<NativeRankedEvalNode> ordered(plan.rankedNodes);
  for (auto &node : ordered)
    if (!node.clocked && islandSizes.lookup(node.island) == 1)
      node.island = nextIsland + node.owner / 64;
  llvm::stable_sort(ordered, [](const auto &a, const auto &b) {
    return a.island < b.island;
  });
  std::optional<uint32_t> currentIsland;
  for (const auto &node : ordered) {
    if (currentIsland && node.island != *currentIsland) {
      emit();
      finishSegment();
    }
    currentIsland = node.island;
    if (node.boundary) {
      emit();
      finishSegment();
      continue;
    }
    uint64_t four = bodyCost(node.body);
    uint64_t two = node.twoStateBody.empty() ? 0 : bodyCost(node.twoStateBody);
    if (!four || (!node.twoStateBody.empty() && !two)) {
      emit();
      finishSegment();
      continue;
    }
    // Charge both domain bodies and pending/proof control to this helper,
    // rather than spending one model-wide budget on fragment wrappers.
    uint64_t weight = four + two + 64;
    if (weight > budget - cost)
      emit();
    members.push_back(node);
    cost += weight;
    // A large certified body is a direct call in its own bounded helper.
    // The inliner still enforces its budget; code size does not introduce a
    // scheduling boundary or break the surrounding predicated segment.
    if (cost >= budget)
      emit();
  }
  emit();
  finishSegment();
  if (invalidRange)
    return module.emitError("ranked group proof exceeds native state"),
           failure();
  if (!result.domains.empty() && native::has<native::DebugTiming>(module)) {
    uint64_t rangeCount = 0, bits = 0;
    for (auto domain : result.domains) {
      auto entries = domain.getRanges().asArrayRef();
      rangeCount += entries.size() / 2;
      for (size_t i = 1; i < entries.size(); i += 2)
        bits += entries[i];
    }
    llvm::errs() << "obelisk group proof domains: domains="
                 << result.domains.size() << " ranges=" << rangeCount
                 << " bits=" << bits << '\n';
  }
  return result;
}

FailureOr<SmallVector<std::string>>
materializeNativeRankedGroups(ModuleOp module,
                              const NativeEvalCoordinatorPlan &plan,
                              const NativeRankedGroupPlan &groups) {
  SmallVector<std::string> executors(plan.fragments.size());
  if (plan.clockKernels.empty() || plan.rankedNodes.empty() || plan.writableVPI)
    return executors;
  llvm::StringMap<sim::SimFuncOp> functions;
  ::obelisk::detail::walkNativeFunctions<sim::SimFuncOp>(
      module, [&](sim::SimFuncOp function) {
        functions.try_emplace(function.getSymName(), function);
      });
  OpBuilder builder(module.getContext());
  Location loc = module.getLoc();
  Type pointer = LLVM::LLVMPointerType::get(module.getContext());
  Type i64 = builder.getI64Type();
  const runtime::ReadySetLayout readyLayout(
      std::max<size_t>(64, plan.fragments.size()));
  SmallVector<const NativeRankedEvalNode *> members;
  unsigned groupID = 0;
  unsigned clockedMembers = 0;
  unsigned unconditionalMembers = 0;
  struct Chunk {
    std::string name;
    SmallVector<uint32_t> owners;
    SmallVector<std::pair<unsigned, uint64_t>> words;
  };
  SmallVector<Chunk> chunks;
  unsigned segmentCount = 0;
  auto finishSegment = [&]() {
    if (chunks.size() < 2) {
      chunks.clear();
      return;
    }
    // A code-size split is a direct helper call, not another scheduling
    // boundary. This is the same finite, predicated sweep that an unsplit
    // helper would execute. Each child consumes only its own pending work;
    // publications to an earlier child survive until the shared loop returns.
    builder.setInsertionPointToEnd(module.getBody());
    std::string name = chunks.front().name + ".segment";
    auto segment = LLVM::LLVMFuncOp::create(
        builder, loc, name,
        LLVM::LLVMFunctionType::get(
            LLVM::LLVMVoidType::get(module.getContext()), {pointer}, false));
    ::obelisk::schedule::set<schedule::metadata::evalCallClosureRoot>(
        segment, builder.getUnitAttr());
    SmallVector<Attribute> helpers;
    for (const Chunk &chunk : chunks)
      helpers.push_back(
          FlatSymbolRefAttr::get(module.getContext(), chunk.name));
    ::obelisk::schedule::set<::obelisk::schedule::Field::EvalSegmentHelpers>(
        segment, builder.getArrayAttr(helpers));
    Block *entry = segment.addEntryBlock(builder);
    builder.setInsertionPointToStart(entry);
    Value ready =
        LLVM::AddressOfOp::create(builder, loc, pointer, evalModelIngressName);
    for (const Chunk &chunk : chunks) {
      Value dirty;
      for (auto [word, mask] : chunk.words) {
        Value bits = loadOwnerWord(builder, loc, ready, word);
        if (mask != UINT64_MAX)
          bits = arith::AndIOp::create(builder, loc, bits,
                                       llvmConstant(builder, loc, i64, mask));
        dirty = dirty ? arith::OrIOp::create(builder, loc, dirty, bits) : bits;
      }
      Value pending =
          arith::CmpIOp::create(builder, loc, arith::CmpIPredicate::ne, dirty,
                                llvmConstant(builder, loc, i64, 0));
      Block *execute = new Block, *next = new Block;
      segment.getBody().push_back(execute);
      segment.getBody().push_back(next);
      cf::CondBranchOp::create(builder, loc, pending, execute, ValueRange{},
                               next, ValueRange{});
      builder.setInsertionPointToStart(execute);
      LLVM::CallOp::create(builder, loc, TypeRange{}, chunk.name,
                           entry->getArguments());
      cf::BranchOp::create(builder, loc, next);
      builder.setInsertionPointToStart(next);
      for (uint32_t owner : chunk.owners)
        executors[owner] = name;
    }
    LLVM::ReturnOp::create(builder, loc, ValueRange{});
    ++segmentCount;
    chunks.clear();
  };
  auto emit = [&](schedule::GroupProofDependencyAttr proof) {
    if (members.empty()) {
      members.clear();
      return;
    }
    builder.setInsertionPointToEnd(module.getBody());
    std::string name =
        "__obelisk_eval_ranked_group_" + std::to_string(groupID++);
    auto group = LLVM::LLVMFuncOp::create(
        builder, loc, name,
        LLVM::LLVMFunctionType::get(
            LLVM::LLVMVoidType::get(module.getContext()), {pointer}, false));
    SmallVector<int32_t> identities;
    for (auto *member : members) {
      identities.push_back(member->owner);
      executors[member->owner] = name;
      clockedMembers += member->clocked;
    }
    Chunk chunk{name, {}, {}};
    for (auto *member : members)
      chunk.owners.push_back(member->owner);
    llvm::sort(chunk.owners);
    for (uint32_t owner : chunk.owners) {
      unsigned word = owner / 64;
      if (chunk.words.empty() || chunk.words.back().first != word)
        chunk.words.push_back({word, 0});
      chunk.words.back().second |= uint64_t{1} << (owner % 64);
    }
    chunks.push_back(std::move(chunk));
    ::obelisk::schedule::set<::obelisk::schedule::Field::EvalRankedMembers>(
        group, builder.getDenseI32ArrayAttr(identities));
    if (proof)
      schedule::set<schedule::Field::EvalGroupProofDomain>(group, proof);
    ::obelisk::schedule::set<::obelisk::schedule::Field::EvalReadyWordCount>(
        group, builder.getI64IntegerAttr(readyLayout.counts[0]));
    ::obelisk::schedule::set<::obelisk::schedule::Field::EvalGroupIngress>(
        group,
        FlatSymbolRefAttr::get(module.getContext(), evalModelIngressName));
    ::obelisk::schedule::set<schedule::metadata::evalCallClosureRoot>(
        group, builder.getUnitAttr());
    Block *entry = group.addEntryBlock(builder);
    builder.setInsertionPointToStart(entry);
    Value ready =
        LLVM::AddressOfOp::create(builder, loc, pointer, evalModelIngressName);
    Value promotion = LLVM::AddressOfOp::create(
        builder, loc, pointer, "__obelisk_eval_promotion_pending_mask_v1");
    auto rawCall = [&](StringRef body, uint32_t owner) {
      SmallVector<Value> arguments;
      for (Type type : functions.lookup(body).getFunctionType().getInputs())
        arguments.push_back(
            isa<sim::ContextType>(type)
                ? entry->getArgument(0)
                : LLVM::PoisonOp::create(
                      builder, loc,
                      convertProcessType(type, module.getContext()))
                      .getResult());
      auto call =
          func::CallOp::create(builder, loc, body, TypeRange{}, arguments);
      ::obelisk::schedule::set<::obelisk::schedule::Field::EvalDirectCall>(
          call, builder.getUnitAttr());
      ::obelisk::schedule::set<::obelisk::schedule::Field::EvalGroupMember>(
          call, builder.getI32IntegerAttr(owner));
      // The group has selected this value-domain branch explicitly. Route
      // lowering must not introduce a mutable function pointer here.
      ::obelisk::schedule::set<
          ::obelisk::schedule::Field::EvalGroupDomainSelected>(
          call, builder.getUnitAttr());
    };
    for (const auto *node : members) {
      uint32_t owner = node->owner;
      Value bit = llvmConstant(builder, loc, i64, uint64_t{1} << (owner % 64));
      Value ingress = schedule::NativeReadyReadOp::create(
          builder, loc, i64, ready, readyLayout.capacity, owner / 64);
      ::obelisk::schedule::set<::obelisk::schedule::Field::EvalActivationEntry>(
          ingress.getDefiningOp(), builder.getI32IntegerAttr(owner));
      Value dirty = arith::AndIOp::create(builder, loc, ingress, bit);
      Value pending =
          arith::CmpIOp::create(builder, loc, arith::CmpIPredicate::ne, dirty,
                                llvmConstant(builder, loc, i64, 0));
      Block *execute = new Block, *next = new Block;
      group.getBody().push_back(execute);
      group.getBody().push_back(next);
      cf::CondBranchOp::create(builder, loc, pending, execute, ValueRange{},
                               next, ValueRange{});
      builder.setInsertionPointToStart(execute);
      // Consume before execution. Later members may republish this owner;
      // that backward work must survive the sweep (LRM 4.4--4.7). Self-reading
      // sources are excluded by the activation certificate.
      updateEvalReadyWord(builder, loc, ready, readyLayout, owner / 64, bit,
                          /*clear=*/true);
      bool twoState =
          !node->twoStateBody.empty() && !plan.twoStateExecutors[owner].empty();
      Block *four = execute;
      if (twoState) {
        four = new Block;
        Block *check = new Block, *two = new Block;
        group.getBody().push_back(four);
        group.getBody().push_back(check);
        group.getBody().push_back(two);
        Value unknown = arith::AndIOp::create(
            builder, loc, loadOwnerWord(builder, loc, promotion, owner / 64),
            bit);
        Value needsProof =
            arith::CmpIOp::create(builder, loc, arith::CmpIPredicate::ne,
                                  unknown, llvmConstant(builder, loc, i64, 0));
        cf::CondBranchOp::create(builder, loc, needsProof, check, ValueRange{},
                                 two, ValueRange{});
        builder.setInsertionPointToStart(check);
        Value known = LLVM::CallOp::create(
                          builder, loc, TypeRange{builder.getI1Type()},
                          SymbolRefAttr::get(module.getContext(),
                                             kernelPromotionReadyName),
                          ValueRange{llvmConstant(builder, loc, i64, owner)})
                          .getResult();
        cf::CondBranchOp::create(builder, loc, known, two, ValueRange{}, four,
                                 ValueRange{});
        builder.setInsertionPointToStart(two);
        rawCall(node->twoStateBody, owner);
        cf::BranchOp::create(builder, loc, next);
        builder.setInsertionPointToStart(four);
      }
      LLVM::StoreOp::create(builder, loc,
                            llvmConstant(builder, loc, builder.getI8Type(), 1),
                            LLVM::AddressOfOp::create(
                                builder, loc, pointer,
                                "__obelisk_eval_step_four_state_fallback_v1"),
                            1);
      for (unsigned word = 0; word < plan.nbaTaintWordCount; ++word) {
        uint64_t mask = plan.nbaTaintMasks[owner][word];
        if (!mask)
          continue;
        Value base = LLVM::AddressOfOp::create(
            builder, loc, pointer,
            "__obelisk_eval_step_four_state_nba_roots_v1");
        updateOwnerWord(builder, loc, base, word,
                        llvmConstant(builder, loc, i64, mask));
      }
      rawCall(node->body, owner);
      cf::BranchOp::create(builder, loc, next);
      builder.setInsertionPointToStart(next);
    }
    LLVM::ReturnOp::create(builder, loc, ValueRange{});
    // This is only a candidate. After lowering, dense memory/control-flow
    // analysis must prove that its computation can be predicated without
    // speculative effects. Until then the ordinary ranked helper owns entry.
    if (members.size() > 1 && llvm::all_of(members, [&](const auto *node) {
          return !node->clocked && !node->twoStateBody.empty() &&
                 !plan.twoStateExecutors[node->owner].empty();
        })) {
      builder.setInsertionPointToEnd(module.getBody());
      auto candidate = LLVM::LLVMFuncOp::create(
          builder, loc, name + ".dataflow",
          LLVM::LLVMFunctionType::get(
              LLVM::LLVMVoidType::get(module.getContext()), {pointer}, false));
      ::obelisk::schedule::set<::obelisk::schedule::Field::EvalRankedMembers>(
          candidate, builder.getDenseI32ArrayAttr(identities));
      if (proof)
        schedule::set<schedule::Field::EvalGroupProofDomain>(candidate, proof);
      ::obelisk::schedule::set<::obelisk::schedule::Field::EvalGroupIngress>(
          candidate, ::obelisk::schedule::get<
                         ::obelisk::schedule::Field::EvalGroupIngress>(group));
      ::obelisk::schedule::set<::obelisk::schedule::Field::EvalReadyWordCount>(
          candidate,
          ::obelisk::schedule::get<
              ::obelisk::schedule::Field::EvalReadyWordCount>(group));
      ::obelisk::schedule::set<
          ::obelisk::schedule::Field::EvalDataflowCandidate>(
          candidate, FlatSymbolRefAttr::get(module.getContext(), name));
      ::obelisk::schedule::set<schedule::metadata::evalCallClosureRoot>(
          candidate, builder.getUnitAttr());
      entry = candidate.addEntryBlock(builder);
      builder.setInsertionPointToStart(entry);
      ready = LLVM::AddressOfOp::create(builder, loc, pointer,
                                        evalModelIngressName);
      SmallVector<int32_t> unconditional;
      for (const auto *node : members) {
        uint32_t owner = node->owner;
        Value bit =
            llvmConstant(builder, loc, i64, uint64_t{1} << (owner % 64));
        Value ingress = schedule::NativeReadyReadOp::create(
            builder, loc, i64, ready, readyLayout.capacity, owner / 64);
        ::obelisk::schedule::set<
            ::obelisk::schedule::Field::EvalActivationEntry>(
            ingress.getDefiningOp(), builder.getI32IntegerAttr(owner));
        Value dirty = arith::AndIOp::create(builder, loc, ingress, bit);
        Value active =
            arith::CmpIOp::create(builder, loc, arith::CmpIPredicate::ne, dirty,
                                  llvmConstant(builder, loc, i64, 0));
        // The source-revision proof covers complete sensitivity, exclusive
        // outputs, and absence of repeated publications. Repeating this pure
        // computation when inactive is observationally inert: it writes the
        // values its outputs already hold, which is no update event (IEEE
        // 1800-2023 4.3, 4.4.1, 4.9.1, 9.2.2.2, 9.4.2). This candidate still
        // needs the lowered nontrapping/effect proof. An independent group
        // certificate proves knownness, not activation, so it does not
        // change this decision. Such a member runs whenever the group runs,
        // in its proved rank order; publications from earlier members to it
        // are therefore redundant and are removed after inlining (see
        // elideUnconditionalMemberPublications). Its pending bit is still
        // the ingress that selects this group and is consumed here.
        if (node->repeatable) {
          active = llvmConstant(builder, loc, builder.getI1Type(), 1);
          unconditional.push_back(owner);
          ++unconditionalMembers;
        }
        Block *execute = new Block, *next = new Block;
        candidate.getBody().push_back(execute);
        candidate.getBody().push_back(next);
        cf::CondBranchOp::create(builder, loc, active, execute, ValueRange{},
                                 next, ValueRange{});
        builder.setInsertionPointToStart(execute);
        updateEvalReadyWord(builder, loc, ready, readyLayout, owner / 64, bit,
                            /*clear=*/true);
        rawCall(node->twoStateBody, owner);
        cf::BranchOp::create(builder, loc, next);
        builder.setInsertionPointToStart(next);
      }
      LLVM::ReturnOp::create(builder, loc, ValueRange{});
      if (!unconditional.empty())
        ::obelisk::schedule::set<
            ::obelisk::schedule::Field::EvalUnconditionalMembers>(
            candidate, builder.getDenseI32ArrayAttr(unconditional));
    }
    members.clear();
  };
  for (const auto &chunk : groups.chunks) {
    for (const auto &node : chunk.members)
      members.push_back(&node);
    emit(chunk.proof);
    if (chunk.endsSegment)
      finishSegment();
  }
  if (native::has<native::DebugTiming>(module)) {
    llvm::errs()
        << "obelisk ranked groups: candidates="
        << llvm::count_if(plan.rankedNodes,
                          [](const auto &node) { return !node.boundary; })
        << " groups=" << groupID << " segments=" << segmentCount << " members="
        << llvm::count_if(executors,
                          [](const auto &name) { return !name.empty(); })
        << " clocked_members=" << clockedMembers << '\n';
    llvm::errs() << "obelisk repeatable ranked members: "
                 << llvm::count_if(plan.rankedNodes,
                                   [&](const auto &node) {
                                     return node.repeatable &&
                                            !executors[node.owner].empty();
                                   })
                 << " unconditional=" << unconditionalMembers << '\n';
    // Tie runtime entry counts back to the actual native ownership plan.
    // Include owners without direct fragments as well. Their compute-node
    // IDs retain the source graph's global scheduling rank.
    for (auto [index, record] : llvm::enumerate(plan.fragments))
      llvm::errs() << "obelisk eval owner: bit=" << record.bit
                   << " actor=" << record.actor_slot
                   << " continuation=" << record.continuation
                   << " compute_node=" << record.compute_node << " group="
                   << (executors[index].empty() ? "-" : executors[index])
                   << " executor=" << plan.fourStateExecutors[index]
                   << (record.bit >= plan.firstDeferredBit ? " deferred" : "")
                   << '\n';
    for (const auto &node : plan.rankedNodes) {
      llvm::errs() << (node.boundary ? "obelisk eval boundary: owner="
                                     : "obelisk eval dependency: owner=")
                   << node.owner << " island=" << node.island
                   << " body=" << node.body << " successors=";
      llvm::interleaveComma(node.successors, llvm::errs());
      llvm::errs() << '\n';
    }
    llvm::errs() << "obelisk eval repeatable owners:";
    for (const auto &node : plan.rankedNodes)
      if (node.repeatable)
        llvm::errs() << ' ' << node.owner;
    llvm::errs() << '\n';
  }
  return executors;
}

unsigned elideUnconditionalMemberPublications(LLVM::LLVMFuncOp function) {
  auto members = ::obelisk::schedule::get<
      ::obelisk::schedule::Field::EvalUnconditionalMembers>(function);
  auto ingress =
      ::obelisk::schedule::get<::obelisk::schedule::Field::EvalGroupIngress>(
          function);
  if (!members || members.empty() || !ingress)
    return 0;
  // Pending bits of unconditional members, per ingress word.
  DenseMap<uint64_t, uint64_t> unconditional;
  for (int32_t owner : members.asArrayRef()) {
    if (owner < 0)
      return 0;
    unconditional[uint64_t(owner) / 64] |= uint64_t{1} << (owner % 64);
  }
  // The bits a mask can add: a constant, or a select between constants (the
  // inline publication form `select(triggered, bit, 0)`). Anything else may
  // publish to a member that still has its predicate, so it is kept.
  auto constantBits = [](Value mask) -> std::optional<uint64_t> {
    APInt value;
    if (matchPattern(mask, m_ConstantInt(&value)))
      return value.getZExtValue();
    Value yesValue, noValue;
    if (auto select = mask.getDefiningOp<arith::SelectOp>()) {
      yesValue = select.getTrueValue();
      noValue = select.getFalseValue();
    } else if (auto select = mask.getDefiningOp<LLVM::SelectOp>()) {
      yesValue = select.getTrueValue();
      noValue = select.getFalseValue();
    } else {
      return std::nullopt;
    }
    APInt yes, no;
    if (!matchPattern(yesValue, m_ConstantInt(&yes)) ||
        !matchPattern(noValue, m_ConstantInt(&no)))
      return std::nullopt;
    return (yes | no).getZExtValue();
  };
  SmallVector<schedule::NativeReadyUpdateOp> redundant;
  function.walk([&](schedule::NativeReadyUpdateOp update) {
    if (update.getConsume())
      return;
    auto base = update.getBase().getDefiningOp<LLVM::AddressOfOp>();
    if (!base || base.getGlobalName() != ingress.getValue())
      return;
    std::optional<uint64_t> bits = constantBits(update.getMask());
    auto word = unconditional.find(update.getWord());
    if (!bits || !*bits || word == unconditional.end() ||
        (*bits & ~word->second) != 0)
      return;
    redundant.push_back(update);
  });
  // The publication's change/edge computation becomes dead with it; ordinary
  // dead-code elimination removes it before the dataflow proof. The member's
  // own store, and every publication to a predicated member, another group,
  // a runtime observer or the four-state fallback, remain (IEEE 1800-2023
  // 4.6, 9.4.2).
  for (auto update : redundant)
    update.erase();
  return redundant.size();
}

} // namespace obelisk::detail
