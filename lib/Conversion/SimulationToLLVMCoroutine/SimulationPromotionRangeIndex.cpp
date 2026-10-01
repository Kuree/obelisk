//===- SimulationPromotionRangeIndex.cpp - Scoped proof invalidation -----===//

#include "SimulationPackedLowering.h"
#include "obelisk/Dialect/Schedule/ScheduleAttrs.h"
#include "obelisk/Dialect/Schedule/ScheduleFields.h"

#include "obelisk/Runtime/PromotionRangeIndex.h"

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/Matchers.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassManager.h"
#include "llvm/ADT/STLExtras.h"

using namespace mlir;

namespace obelisk::detail {

LogicalResult materializeNativePromotionRangeIndex(ModuleOp module) {
  auto hook = module.lookupSymbol<LLVM::LLVMFuncOp>(
      "__obelisk_eval_promotion_invalidate_range_v1");
  if (!hook)
    return success();
  if (!hook.empty())
    return hook.emitError("range invalidator was materialized twice");
  auto stateBits =
      module->getAttrOfType<IntegerAttr>("obelisk.execution.state_bits");
  if (!stateBits)
    return hook.emitError("range invalidator has no canonical state layout");

  struct Certificate {
    LLVM::GlobalOp global;
    uint64_t latch = 0;
    uint64_t pendingBit = 0;
    DenseI64ArrayAttr ranges;
    bool nba = false;
    bool route = false;
  };
  SmallVector<Certificate> certificates;
  constexpr auto kernelAttr =
      ::obelisk::schedule::Field::EvalKernelProofDependencies;
  constexpr auto routeAttr =
      ::obelisk::schedule::Field::EvalRouteProofDependencies;
  constexpr auto nbaAttr = ::obelisk::schedule::Field::EvalNbaProofDependencies;
  auto pending = module.lookupSymbol<LLVM::GlobalOp>(
      "__obelisk_eval_promotion_pending_mask_v1");
  auto routePending = module.lookupSymbol<LLVM::GlobalOp>(
      "__obelisk_eval_route_promotion_pending_v1");
  for (auto global : module.getOps<LLVM::GlobalOp>()) {
    if (auto kernels = ::obelisk::schedule::get<kernelAttr>(global)) {
      auto latchType = dyn_cast<LLVM::LLVMArrayType>(global.getGlobalType());
      auto pendingType =
          pending ? dyn_cast<LLVM::LLVMArrayType>(pending.getGlobalType())
                  : LLVM::LLVMArrayType{};
      if (global.getSymName() != "__obelisk_eval_kernel_promotion_latched_v1" ||
          !latchType || !latchType.getElementType().isInteger(8) ||
          !pendingType || !pendingType.getElementType().isInteger(64))
        return global.emitError("kernel proof index has invalid latch storage");
      for (Attribute attr : kernels) {
        auto kernel = dyn_cast<schedule::KernelProofDependencyAttr>(attr);
        auto latch = kernel ? kernel.getLatch() : IntegerAttr{};
        auto bit = kernel ? kernel.getPendingBit() : IntegerAttr{};
        auto ranges = kernel ? kernel.getRanges() : DenseI64ArrayAttr{};
        if (!latch || !bit || !ranges ||
            latch.getUInt() >= latchType.getNumElements() ||
            bit.getUInt() / 64 >= pendingType.getNumElements())
          return global.emitError(
              "kernel proof index has invalid certificate identity");
        certificates.push_back(
            {global, latch.getUInt(), bit.getUInt(), ranges});
      }
      ::obelisk::schedule::remove<kernelAttr>(global);
    }
    if (auto roots = ::obelisk::schedule::get<nbaAttr>(global)) {
      auto wordType = dyn_cast<LLVM::LLVMArrayType>(global.getGlobalType());
      if (global.getSymName() != "__obelisk_eval_fast_nba_roots_v1" ||
          !wordType || !wordType.getElementType().isInteger(64))
        return global.emitError(
            "NBA proof index has invalid knownness storage");
      for (Attribute attr : roots) {
        auto root = dyn_cast<schedule::NBAProofDependencyAttr>(attr);
        auto bit = root ? root.getBit() : IntegerAttr{};
        auto ranges = root ? root.getRanges() : DenseI64ArrayAttr{};
        if (!bit || !ranges || bit.getUInt() / 64 >= wordType.getNumElements())
          return global.emitError(
              "NBA proof index has invalid certificate identity");
        certificates.push_back({global, bit.getUInt(), 0, ranges, true});
      }
      ::obelisk::schedule::remove<nbaAttr>(global);
    }
    if (auto route = ::obelisk::schedule::get<routeAttr>(global)) {
      auto ranges = route.getRanges();
      auto pendingBit = route.getPendingBit();
      auto pendingType =
          routePending
              ? dyn_cast<LLVM::LLVMArrayType>(routePending.getGlobalType())
              : LLVM::LLVMArrayType{};
      if (!global.getSymName().starts_with(
              "__obelisk_eval_selected_variant_v1_") ||
          !ranges || !pendingBit || !pendingType ||
          !pendingType.getElementType().isInteger(64) ||
          pendingBit.getUInt() / 64 >= pendingType.getNumElements() ||
          !global.getGlobalType().isInteger(8))
        return global.emitError(
            "route proof index has invalid selector storage");
      certificates.push_back(
          {global, 0, pendingBit.getUInt(), ranges, false, true});
      ::obelisk::schedule::remove<routeAttr>(global);
    }
  }
  // Stable dense certificate IDs are independent of scheduler owner bits.
  llvm::sort(certificates, [](auto a, auto b) {
    return std::tuple{a.global.getSymName(), a.latch} <
           std::tuple{b.global.getSymName(), b.latch};
  });
  std::vector<obelisk_rt_native_promotion_dependency> entries;
  for (auto [id, certificate] : llvm::enumerate(certificates)) {
    auto values = certificate.ranges.asArrayRef();
    if (values.size() % 2)
      return certificate.global.emitError("proof index has an odd range list");
    for (size_t index = 0; index != values.size(); index += 2) {
      if (values[index] < 0 || values[index + 1] <= 0)
        return certificate.global.emitError("proof index has an invalid range");
      uint64_t begin = values[index], width = values[index + 1];
      if (begin > stateBits.getUInt() || width > stateBits.getUInt() - begin)
        return certificate.global.emitError(
            "proof index exceeds canonical state");
      entries.push_back({begin, begin + width, 0, id});
    }
  }
  if (!runtime::buildPromotionRangeIndex(entries, stateBits.getUInt(),
                                         certificates.size()))
    return hook.emitError("could not verify canonical proof dependency index");

  MLIRContext *context = module.getContext();
  OpBuilder builder(context);
  Location location = hook.getLoc();
  Type pointer = LLVM::LLVMPointerType::get(context);
  Type i64 = builder.getI64Type();
  Type voidType = LLVM::LLVMVoidType::get(context);
  Block *entry = hook.addEntryBlock(builder);
  hook->setAttr("passthrough",
                builder.getArrayAttr({builder.getStringAttr("cold"),
                                      builder.getStringAttr("noinline")}));
  auto recheck = getOrDeclareLLVMFunction(
      module, "__obelisk_eval_promotion_recheck_range_v1", voidType,
      {i64, i64});
  if (!recheck.empty())
    return recheck.emitError("range recheck was materialized twice");
  recheck->setAttr("passthrough", hook->getAttr("passthrough"));
  Block *recheckEntry = recheck.addEntryBlock(builder);
  if (entries.empty()) {
    builder.setInsertionPointToStart(entry);
    LLVM::ReturnOp::create(builder, location, ValueRange{});
    builder.setInsertionPointToStart(recheckEntry);
    LLVM::ReturnOp::create(builder, location, ValueRange{});
    return success();
  }

  // The runtime record is four consecutive uint64_t fields. A nested i64
  // array has the same layout on every supported target. Use a dense constant
  // instead of repeatedly inserting into an N-record LLVM constant array:
  // translation would rebuild and unique that entire array for every record.
  SmallVector<uint64_t> dependencyWords;
  dependencyWords.reserve(entries.size() * 4);
  for (const auto &dependency : entries)
    llvm::append_range(dependencyWords,
                       ArrayRef<uint64_t>{dependency.begin, dependency.end,
                                          dependency.prefix_end,
                                          dependency.certificate});
  auto dependencyArray = LLVM::LLVMArrayType::get(
      LLVM::LLVMArrayType::get(i64, 4), entries.size());
  auto denseType =
      RankedTensorType::get({static_cast<int64_t>(entries.size()), 4}, i64);
  builder.setInsertionPointToStart(module.getBody());
  auto dependencies = LLVM::GlobalOp::create(
      builder, location, dependencyArray, true, LLVM::Linkage::Internal,
      "__obelisk_eval_proof_dependencies_v1",
      DenseIntElementsAttr::get(denseType, ArrayRef<uint64_t>(dependencyWords)),
      8);
  // The publication pass consumes this union of the same verified index.
  // It is coverage, not another ownership or alias inventory. Constant stores
  // outside it cannot change any certificate and need no runtime guard.
  SmallVector<int64_t> coverage;
  for (const auto &dependency : entries) {
    if (!coverage.empty() &&
        dependency.begin <= static_cast<uint64_t>(coverage.back())) {
      coverage.back() =
          std::max(coverage.back(), static_cast<int64_t>(dependency.end));
    } else {
      coverage.push_back(dependency.begin);
      coverage.push_back(dependency.end);
    }
  }
  ::obelisk::schedule::set<::obelisk::schedule::Field::EvalProofCoverage>(
      dependencies, builder.getDenseI64ArrayAttr(coverage));

  // Pointer fields use the target LLVM layout; only packed bit positions and
  // uint64_t pending words have fixed byte offsets across native and wasm32.
  Type certificateType = LLVM::LLVMStructType::getLiteral(
      context, {pointer, pointer, i64, pointer, pointer});
  auto certificateArray =
      LLVM::LLVMArrayType::get(certificateType, certificates.size());
  auto actions = makeConstantGlobal(
      module, location, certificateArray,
      "__obelisk_eval_proof_certificates_v1", LLVM::Linkage::Internal, 8,
      [&](OpBuilder &b) {
        Value array = LLVM::ZeroOp::create(b, location, certificateArray);
        for (auto [index, certificate] : llvm::enumerate(certificates)) {
          Value record = LLVM::ZeroOp::create(b, location, certificateType);
          Value address = LLVM::AddressOfOp::create(
              b, location, pointer, certificate.global.getSymName());
          auto insert = [&](Value value, int64_t field) {
            record = LLVM::InsertValueOp::create(b, location, record, value,
                                                 ArrayRef<int64_t>{field});
          };
          if (certificate.route) {
            insert(address, 0);
            insert(byteGEP(b, location,
                           LLVM::AddressOfOp::create(b, location, pointer,
                                                     routePending.getSymName()),
                           (certificate.pendingBit / 64) * sizeof(uint64_t)),
                   1);
            insert(llvmConstant(b, location, i64,
                                uint64_t{1} << (certificate.pendingBit % 64)),
                   2);
          } else if (certificate.nba) {
            insert(byteGEP(b, location, address,
                           (certificate.latch / 64) * sizeof(uint64_t)),
                   1);
            insert(llvmConstant(b, location, i64,
                                uint64_t{1} << (certificate.latch % 64)),
                   2);
          } else {
            insert(byteGEP(b, location, address, certificate.latch), 0);
            insert(byteGEP(b, location,
                           LLVM::AddressOfOp::create(b, location, pointer,
                                                     pending.getSymName()),
                           (certificate.pendingBit / 64) * sizeof(uint64_t)),
                   1);
            insert(llvmConstant(b, location, i64,
                                uint64_t{1} << (certificate.pendingBit % 64)),
                   2);
          }
          array = LLVM::InsertValueOp::create(
              b, location, array, record,
              ArrayRef<int64_t>{static_cast<int64_t>(index)});
        }
        return array;
      });

  // Aggregate entry shortcuts must be recomputed after a dependent proof is
  // disturbed. Per-kernel latches and per-route selectors remain independent;
  // the lookup helper changes only those in its exact overlap result.
  auto reset = getOrDeclareLLVMFunction(
      module, "__obelisk_eval_proof_aggregate_invalidate_v1", voidType, {});
  builder.setInsertionPointToStart(reset.addEntryBlock(builder));
  for (StringRef name : {"__obelisk_eval_promotion_latched_v1"})
    if (auto global = module.lookupSymbol<LLVM::GlobalOp>(name))
      LLVM::StoreOp::create(
          builder, location,
          LLVM::ZeroOp::create(builder, location, global.getGlobalType()),
          LLVM::AddressOfOp::create(builder, location, pointer, name));
  if (auto dirty = module.lookupSymbol<LLVM::GlobalOp>(
          "__obelisk_eval_route_promotion_dirty_v1"))
    LLVM::StoreOp::create(
        builder, location,
        llvmConstant(builder, location, builder.getI8Type(), 1),
        LLVM::AddressOfOp::create(builder, location, pointer,
                                  dirty.getSymName()));
  LLVM::ReturnOp::create(builder, location, ValueRange{});

  auto request = getOrDeclareLLVMFunction(
      module, "__obelisk_eval_proof_aggregate_recheck_v1", voidType, {});
  builder.setInsertionPointToStart(request.addEntryBlock(builder));
  // A gain in knownness cannot invalidate a positive proof. Only cached
  // failed scans need to become eligible for another boundary check.
  for (StringRef name : {"__obelisk_eval_route_promotion_dirty_v1"})
    if (auto global = module.lookupSymbol<LLVM::GlobalOp>(name))
      LLVM::StoreOp::create(
          builder, location,
          llvmConstant(builder, location, builder.getI8Type(), 1),
          LLVM::AddressOfOp::create(builder, location, pointer, name));
  LLVM::ReturnOp::create(builder, location, ValueRange{});
  builder.setInsertionPointToStart(recheckEntry);
  auto recheckLookup = getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_native_promotion_recheck_ranges", voidType,
      {pointer, i64, pointer, pointer, i64, i64});
  LLVM::CallOp::create(
      builder, location, recheckLookup,
      ValueRange{LLVM::AddressOfOp::create(builder, location, pointer,
                                           dependencies.getSymName()),
                 llvmConstant(builder, location, i64, entries.size()),
                 LLVM::AddressOfOp::create(builder, location, pointer,
                                           actions.getSymName()),
                 LLVM::AddressOfOp::create(builder, location, pointer,
                                           request.getSymName()),
                 recheckEntry->getArgument(0), recheckEntry->getArgument(1)});
  LLVM::ReturnOp::create(builder, location, ValueRange{});

  auto lookup = getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_native_promotion_invalidate_ranges", voidType,
      {pointer, i64, pointer, pointer, i64, i64});
  builder.setInsertionPointToStart(entry);
  LLVM::CallOp::create(
      builder, location, lookup,
      ValueRange{LLVM::AddressOfOp::create(builder, location, pointer,
                                           dependencies.getSymName()),
                 llvmConstant(builder, location, i64, entries.size()),
                 LLVM::AddressOfOp::create(builder, location, pointer,
                                           actions.getSymName()),
                 LLVM::AddressOfOp::create(builder, location, pointer,
                                           reset.getSymName()),
                 entry->getArgument(0), entry->getArgument(1)});
  LLVM::ReturnOp::create(builder, location, ValueRange{});
  return success();
}

namespace {
struct NativeProofPublicationInputs {
  explicit NativeProofPublicationInputs(Operation *operation) {
    auto module = cast<ModuleOp>(operation);
    auto hook = module.lookupSymbol<LLVM::LLVMFuncOp>(
        "__obelisk_eval_promotion_invalidate_range_v1");
    auto dependencies = module.lookupSymbol<LLVM::GlobalOp>(
        "__obelisk_eval_proof_dependencies_v1");
    active = hook && dependencies;
    if (!active)
      return;
    coverage = schedule::get<schedule::Field::EvalProofCoverage>(dependencies);
    if (!coverage || coverage.size() % 2) {
      dependencies.emitError("missing verified proof coverage");
      valid = false;
    }
    bits = module->getAttrOfType<IntegerAttr>("obelisk.execution.state_bits");
  }
  DenseI64ArrayAttr coverage;
  IntegerAttr bits;
  bool active = false;
  bool valid = true;
};

struct NativeFunctionProofPublications {
  NativeFunctionProofPublications(Operation *operation,
                                  AnalysisManager &manager) {
    auto function = cast<LLVM::LLVMFuncOp>(operation);
    auto cached = manager.getCachedParentAnalysis<NativeProofPublicationInputs>(
        function->getParentOfType<ModuleOp>());
    if (!cached || !cached->get().valid) {
      function.emitError("proof publication requires cached dependency inputs");
      valid = false;
      return;
    }
    const auto &inputs = cached->get();
    function.walk([&](LLVM::StoreOp store) { allStores.push_back(store); });
    if (!inputs.active)
      return;
    auto disjointRange = [&](uint64_t begin, uint64_t width) {
      if (width > UINT64_MAX - begin)
        return false;
      auto ranges = inputs.coverage.asArrayRef();
      // Ranges are sorted and disjoint; find the first end past the store
      // start.
      size_t low = 0, high = ranges.size() / 2;
      while (low != high) {
        size_t middle = low + (high - low) / 2;
        if (static_cast<uint64_t>(ranges[middle * 2 + 1]) <= begin)
          low = middle + 1;
        else
          high = middle;
      }
      return low == ranges.size() / 2 ||
             static_cast<uint64_t>(ranges[low * 2]) >= begin + width;
    };
    auto bits = inputs.bits;
    auto disjointStore = [&](LLVM::StoreOp store) {
      if (auto range = ::obelisk::schedule::get<
              ::obelisk::schedule::Field::EvalUnknownWriteRange>(store)) {
        // The generated dynamic commit's clipped mask proves containment.
        // Missing or malformed evidence is conservative, never a guessed lane.
        if (bits && range.size() == 2 && range[0] >= 0 && range[1] > 0 &&
            static_cast<uint64_t>(range[0]) <= bits.getUInt() &&
            static_cast<uint64_t>(range[1]) <= bits.getUInt() - range[0])
          return disjointRange(range[0], range[1]);
      }
      auto type = dyn_cast<IntegerType>(store.getValue().getType());
      if (!type)
        return false;
      Value address = store.getAddr();
      uint64_t bytes = 0;
      while (auto gep = address.getDefiningOp<LLVM::GEPOp>()) {
        if (!gep.getElemType().isInteger(8) || gep.getIndices().size() != 1)
          return false;
        auto index = gep.getIndices()[0];
        APInt constant;
        if (auto integer = dyn_cast<IntegerAttr>(index))
          constant = integer.getValue();
        else if (!matchPattern(cast<Value>(index), m_ConstantInt(&constant)))
          return false;
        if (constant.isNegative() || constant.getActiveBits() > 64 ||
            constant.getZExtValue() > UINT64_MAX - bytes)
          return false;
        bytes += constant.getZExtValue();
        address = gep.getBase();
      }
      if (bytes > UINT64_MAX / 8)
        return false;
      uint64_t begin = bytes * 8;
      uint64_t width = ((uint64_t{type.getWidth()} + 7) / 8) * 8;
      return disjointRange(begin, width);
    };
    for (auto store : allStores) {
      Value address = store.getAddr();
      while (auto gep = address.getDefiningOp<LLVM::GEPOp>())
        address = gep.getBase();
      auto global = address.getDefiningOp<LLVM::AddressOfOp>();
      if (!global || global.getGlobalName() != "__obelisk_state_unknown" ||
          disjointStore(store))
        continue;
      if (!isa<IntegerType>(store.getValue().getType())) {
        store.emitError(
            "canonical proof publication requires packed integer storage");
        valid = false;
      }
      stores.push_back(store);
    }
  }
  SmallVector<LLVM::StoreOp> allStores;
  SmallVector<LLVM::StoreOp> stores;
  bool valid = true;
};

class PublishNativeFunctionProofsPass final
    : public PassWrapper<PublishNativeFunctionProofsPass,
                         OperationPass<LLVM::LLVMFuncOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(PublishNativeFunctionProofsPass)
  StringRef getArgument() const final {
    return "publish-native-function-proofs";
  }
  void runOnOperation() override {
    if (getOperation().isExternal()) {
      markAllAnalysesPreserved();
      return;
    }
    const auto &work = getAnalysis<NativeFunctionProofPublications>();
    if (!work.valid)
      return signalPassFailure();
    bool changed = !work.stores.empty();
    for (auto store : work.allStores) {
      changed |= schedule::has<schedule::Field::EvalUnknownWriteRange>(store);
      schedule::remove<schedule::Field::EvalUnknownWriteRange>(store);
    }
    if (!changed) {
      markAllAnalysesPreserved();
      return;
    }
    MLIRContext *context = &getContext();
    // Run after value-domain cloning: proven two-state stores have disappeared.
    // Diff the actual canonical bytes, including masked and dynamically
    // selected NBA stores. Preserved neighbors produce no delta; a known NBA
    // payload must still clear an unknown destination before publication (IEEE
    // 1800 10.4.2). Neither staging a future update nor a known-to-known store
    // invalidates a certificate. No actor or observer executes between the
    // store and this proof publication, which precedes all dependent
    // computation/fanout.
    OpBuilder builder(context);
    Type pointer = LLVM::LLVMPointerType::get(context);
    Type i64 = builder.getI64Type();
    for (auto store : work.stores) {
      Location location = store.getLoc();
      auto type = cast<IntegerType>(store.getValue().getType());
      builder.setInsertionPoint(store);
      Value old =
          LLVM::LoadOp::create(builder, location, type, store.getAddr(), 1);
      Value delta =
          LLVM::XOrOp::create(builder, location, old, store.getValue());
      Value base = LLVM::AddressOfOp::create(builder, location, pointer,
                                             "__obelisk_state_unknown");
      Value bytes = LLVM::SubOp::create(
          builder, location,
          LLVM::PtrToIntOp::create(builder, location, i64, store.getAddr()),
          LLVM::PtrToIntOp::create(builder, location, i64, base));
      Value bitOffset = LLVM::ShlOp::create(
          builder, location, bytes, llvmConstant(builder, location, i64, 3));
      builder.setInsertionPointAfter(store);
      for (uint64_t bit = 0; bit < type.getWidth(); bit += 64) {
        Value chunk = delta;
        if (bit)
          chunk =
              LLVM::LShrOp::create(builder, location, chunk,
                                   llvmConstant(builder, location, type, bit));
        if (type.getWidth() > 64)
          chunk = LLVM::TruncOp::create(builder, location, i64, chunk);
        else if (type.getWidth() < 64)
          chunk = LLVM::ZExtOp::create(builder, location, i64, chunk);
        Value offset =
            bit ? LLVM::AddOp::create(builder, location, bitOffset,
                                      llvmConstant(builder, location, i64, bit))
                      .getResult()
                : bitOffset;
        // Keep the empty-mask check in this body. An alwaysinline wrapper can
        // land in a different native partition, leaving an ordinary call on
        // every unchanged store despite its attribute.
        Value hasChange = LLVM::ICmpOp::create(
            builder, location, LLVM::ICmpPredicate::ne, chunk,
            llvmConstant(builder, location, i64, 0));
        Block *head = builder.getInsertionBlock();
        Block *continuation = head->splitBlock(builder.getInsertionPoint());
        Block *changed = new Block;
        head->getParent()->getBlocks().insert(continuation->getIterator(),
                                              changed);
        builder.setInsertionPointToEnd(head);
        LLVM::CondBrOp::create(builder, location, hasChange, changed,
                               continuation);
        builder.setInsertionPointToStart(changed);
        Value nextChunk = store.getValue();
        if (bit)
          nextChunk =
              LLVM::LShrOp::create(builder, location, nextChunk,
                                   llvmConstant(builder, location, type, bit));
        if (type.getWidth() > 64)
          nextChunk = LLVM::TruncOp::create(builder, location, i64, nextChunk);
        else if (type.getWidth() < 64)
          nextChunk = LLVM::ZExtOp::create(builder, location, i64, nextChunk);
        LLVM::CallOp::create(
            builder, location, TypeRange{},
            SymbolRefAttr::get(context,
                               "__obelisk_eval_promotion_publish_unknown_v1"),
            ValueRange{offset, chunk, nextChunk});
        LLVM::BrOp::create(builder, location, ValueRange{}, continuation);
        builder.setInsertionPointToStart(continuation);
      }
    }
  }
};
} // namespace

LogicalResult materializeNativePromotionWrites(ModuleOp module) {
  auto hook = module.lookupSymbol<LLVM::LLVMFuncOp>(
      "__obelisk_eval_promotion_invalidate_range_v1");
  auto dependencies = module.lookupSymbol<LLVM::GlobalOp>(
      "__obelisk_eval_proof_dependencies_v1");
  if (!hook || !dependencies)
    return success();
  auto coverage =
      ::obelisk::schedule::get<::obelisk::schedule::Field::EvalProofCoverage>(
          dependencies);
  if (!coverage || coverage.size() % 2)
    return dependencies.emitError("missing verified proof coverage");
  ::obelisk::schedule::remove<::obelisk::schedule::Field::EvalProofCoverage>(
      dependencies);
  MLIRContext *context = module.getContext();
  OpBuilder builder(context);
  Location location = hook.getLoc();
  Type i64 = builder.getI64Type();
  Type voidType = LLVM::LLVMVoidType::get(context);
  auto maskHook = getOrDeclareLLVMFunction(
      module, "__obelisk_eval_promotion_invalidate_mask_v1", voidType,
      {i64, i64});
  maskHook->setAttr("passthrough",
                    builder.getArrayAttr({builder.getStringAttr("cold"),
                                          builder.getStringAttr("noinline")}));
  Block *entry = maskHook.addEntryBlock(builder);
  Block *loop = new Block, *visit = new Block, *done = new Block;
  loop->addArgument(i64, location);
  maskHook.getBody().push_back(loop);
  maskHook.getBody().push_back(visit);
  maskHook.getBody().push_back(done);
  builder.setInsertionPointToStart(entry);
  LLVM::BrOp::create(builder, location, ValueRange{entry->getArgument(1)},
                     loop);
  builder.setInsertionPointToStart(loop);
  Value mask = loop->getArgument(0);
  Value nonempty =
      LLVM::ICmpOp::create(builder, location, LLVM::ICmpPredicate::ne, mask,
                           llvmConstant(builder, location, i64, 0));
  LLVM::CondBrOp::create(builder, location, nonempty, visit, done);
  builder.setInsertionPointToStart(visit);
  Value first =
      LLVM::CountTrailingZerosOp::create(builder, location, i64, mask, true);
  Value shifted = LLVM::LShrOp::create(builder, location, mask, first);
  Value complement =
      LLVM::XOrOp::create(builder, location, shifted,
                          llvmConstant(builder, location, i64, UINT64_MAX));
  // Zero is defined as 64 here: a complete changed word is one range. Clearing
  // the lowest run by modular addition avoids an undefined shift by 64.
  Value width = LLVM::CountTrailingZerosOp::create(builder, location, i64,
                                                   complement, false);
  Value offset =
      LLVM::AddOp::create(builder, location, entry->getArgument(0), first);
  LLVM::CallOp::create(builder, location, hook, ValueRange{offset, width});
  Value lowest = LLVM::ShlOp::create(
      builder, location, llvmConstant(builder, location, i64, 1), first);
  Value next =
      LLVM::AndOp::create(builder, location, mask,
                          LLVM::AddOp::create(builder, location, mask, lowest));
  LLVM::BrOp::create(builder, location, ValueRange{next}, loop);
  builder.setInsertionPointToStart(done);
  LLVM::ReturnOp::create(builder, location, ValueRange{});

  builder.setInsertionPointAfter(maskHook);
  auto recheckMask =
      cast<LLVM::LLVMFuncOp>(builder.clone(*maskHook.getOperation()));
  recheckMask.setSymName("__obelisk_eval_promotion_recheck_mask_v1");
  recheckMask.walk([&](LLVM::CallOp call) {
    if (call.getCallee() == hook.getSymName())
      call.setCallee("__obelisk_eval_promotion_recheck_range_v1");
  });
  auto publish = getOrDeclareLLVMFunction(
      module, "__obelisk_eval_promotion_publish_unknown_v1", voidType,
      {i64, i64, i64});
  publish->setAttr("passthrough", maskHook->getAttr("passthrough"));
  Block *publication = publish.addEntryBlock(builder);
  builder.setInsertionPointToStart(publication);
  Value changedMask = publication->getArgument(1);
  Value newUnknown = publication->getArgument(2);
  Value lost = LLVM::AndOp::create(builder, location, changedMask, newUnknown);
  Value gained = LLVM::XOrOp::create(builder, location, changedMask, lost);
  LLVM::CallOp::create(builder, location, maskHook,
                       ValueRange{publication->getArgument(0), lost});
  LLVM::CallOp::create(builder, location, recheckMask,
                       ValueRange{publication->getArgument(0), gained});
  LLVM::ReturnOp::create(builder, location, ValueRange{});

  return success();
}

LogicalResult prepareNativeProofPublicationInputs(ModuleOp module,
                                                  AnalysisManager manager) {
  const auto &inputs = manager.getAnalysis<NativeProofPublicationInputs>();
  if (!inputs.valid)
    return failure();
  return materializeNativePromotionWrites(module);
}

std::unique_ptr<Pass> createPublishNativeFunctionProofsPass() {
  return std::make_unique<PublishNativeFunctionProofsPass>();
}

} // namespace obelisk::detail
