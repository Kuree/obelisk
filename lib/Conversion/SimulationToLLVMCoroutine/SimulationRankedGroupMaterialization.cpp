//===- SimulationRankedGroupMaterialization.cpp - Shared ranked sweeps
//-----===//

#include "SimulationAOTPlanning.h"
#include "SimulationEvalReadySet.h"
#include "obelisk/Dialect/Runtime/RuntimeDialect.h"
#include "obelisk/Dialect/Schedule/ScheduleEnums.h"
#include "obelisk/Dialect/Schedule/ScheduleFields.h"
#include "obelisk/Dialect/Schedule/ScheduleMetadata.h"

#include "obelisk/Dialect/Simulation/SimulationMetadata.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "llvm/ADT/StringMap.h"

using namespace mlir;

namespace obelisk::detail {

FailureOr<SmallVector<std::string>>
materializeNativeRankedGroups(ModuleOp module,
                              const NativeEvalCoordinatorPlan &plan) {
  SmallVector<std::string> executors(plan.fragments.size());
  if (plan.clockKernels.empty() || plan.rankedNodes.empty())
    return executors;
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
  OpBuilder builder(module.getContext());
  Location loc = module.getLoc();
  Type pointer = LLVM::LLVMPointerType::get(module.getContext());
  Type i64 = builder.getI64Type();
  const runtime::ReadySetLayout readyLayout(
      std::max<size_t>(64, plan.fragments.size()));
  SmallVector<const NativeRankedEvalNode *> members;
  uint64_t cost = 0;
  unsigned groupID = 0;
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
  auto emit = [&]() {
    if (members.size() < 2) {
      // An ungrouped singleton still uses its own executor. Do not bridge
      // across it when joining the preceding and following bounded helpers.
      if (!members.empty())
        finishSegment();
      members.clear();
      cost = 0;
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
      Value ingress = loadOwnerWord(builder, loc, ready, owner / 64);
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
    if (llvm::all_of(members, [&](const auto *node) {
          return !node->twoStateBody.empty() &&
                 !plan.twoStateExecutors[node->owner].empty();
        })) {
      builder.setInsertionPointToEnd(module.getBody());
      auto candidate = LLVM::LLVMFuncOp::create(
          builder, loc, name + ".dataflow",
          LLVM::LLVMFunctionType::get(
              LLVM::LLVMVoidType::get(module.getContext()), {pointer}, false));
      ::obelisk::schedule::set<::obelisk::schedule::Field::EvalRankedMembers>(
          candidate, builder.getDenseI32ArrayAttr(identities));
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
      for (const auto *node : members) {
        uint32_t owner = node->owner;
        Value bit =
            llvmConstant(builder, loc, i64, uint64_t{1} << (owner % 64));
        Value ingress = loadOwnerWord(builder, loc, ready, owner / 64);
        ::obelisk::schedule::set<
            ::obelisk::schedule::Field::EvalActivationEntry>(
            ingress.getDefiningOp(), builder.getI32IntegerAttr(owner));
        Value dirty = arith::AndIOp::create(builder, loc, ingress, bit);
        Value active =
            arith::CmpIOp::create(builder, loc, arith::CmpIPredicate::ne, dirty,
                                  llvmConstant(builder, loc, i64, 0));
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
    }
    members.clear();
    cost = 0;
  };
  for (const auto &node : plan.rankedNodes) {
    if (!members.empty() && node.island != members.front()->island) {
      emit();
      finishSegment();
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
    if (weight > budget) {
      emit();
      finishSegment();
      continue;
    }
    if (weight > budget - cost)
      emit();
    members.push_back(&node);
    cost += weight;
  }
  emit();
  finishSegment();
  if (module->hasAttr("obelisk.debug.native_timing")) {
    llvm::errs() << "obelisk ranked groups: candidates="
                 << plan.rankedNodes.size() << " groups=" << groupID
                 << " segments=" << segmentCount << " members="
                 << llvm::count_if(
                        executors,
                        [](const auto &name) { return !name.empty(); })
                 << '\n';
    // Tie runtime entry counts back to the actual native ownership plan.
    // The helper dependency graph excludes boundary actors, so print the
    // full owner inventory as well; its compute-node IDs retain global rank.
    for (auto [index, record] : llvm::enumerate(plan.fragments))
      llvm::errs() << "obelisk eval owner: bit=" << record.bit
                   << " actor=" << record.actor_slot
                   << " continuation=" << record.continuation
                   << " compute_node=" << record.compute_node << " group="
                   << (executors[index].empty() ? "-" : executors[index])
                   << " executor=" << plan.fourStateExecutors[index] << '\n';
    for (const auto &node : plan.rankedNodes) {
      llvm::errs() << "obelisk eval dependency: owner=" << node.owner
                   << " island=" << node.island << " body=" << node.body
                   << " successors=";
      llvm::interleaveComma(node.successors, llvm::errs());
      llvm::errs() << '\n';
    }
  }
  return executors;
}

} // namespace obelisk::detail
