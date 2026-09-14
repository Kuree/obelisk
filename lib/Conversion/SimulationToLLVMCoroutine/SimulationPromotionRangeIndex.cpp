//===- SimulationPromotionRangeIndex.cpp - Scoped proof invalidation -----===//

#include "SimulationToLLVMCoroutinePrivate.h"

#include "obelisk/Runtime/PromotionRangeIndex.h"

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
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
    FlatSymbolRefAttr fallback;
    uint64_t latch = 0;
    uint64_t pendingBit = 0;
    DenseI64ArrayAttr ranges;
  };
  SmallVector<Certificate> certificates;
  constexpr StringLiteral kernelAttr = "obelisk.eval.kernel_proof_dependencies";
  constexpr StringLiteral routeAttr = "obelisk.eval.route_proof_dependencies";
  auto pending = module.lookupSymbol<LLVM::GlobalOp>(
      "__obelisk_eval_promotion_pending_mask_v1");
  for (auto global : module.getOps<LLVM::GlobalOp>()) {
    if (auto kernels = global->getAttrOfType<ArrayAttr>(kernelAttr)) {
      auto latchType = dyn_cast<LLVM::LLVMArrayType>(global.getGlobalType());
      auto pendingType =
          pending ? dyn_cast<LLVM::LLVMArrayType>(pending.getGlobalType())
                  : LLVM::LLVMArrayType{};
      if (!latchType || !latchType.getElementType().isInteger(8) ||
          !pendingType || !pendingType.getElementType().isInteger(64))
        return global.emitError("kernel proof index has invalid latch storage");
      for (Attribute attr : kernels) {
        auto kernel = dyn_cast<DictionaryAttr>(attr);
        auto latch =
            kernel ? kernel.getAs<IntegerAttr>("latch") : IntegerAttr{};
        auto bit =
            kernel ? kernel.getAs<IntegerAttr>("pending_bit") : IntegerAttr{};
        auto ranges = kernel ? kernel.getAs<DenseI64ArrayAttr>("ranges")
                             : DenseI64ArrayAttr{};
        if (!latch || !bit || !ranges ||
            latch.getUInt() >= latchType.getNumElements() ||
            bit.getUInt() / 64 >= pendingType.getNumElements())
          return global.emitError(
              "kernel proof index has invalid certificate identity");
        certificates.push_back(
            {global, {}, latch.getUInt(), bit.getUInt(), ranges});
      }
      global->removeAttr(kernelAttr);
    }
    if (auto route = global->getAttrOfType<DictionaryAttr>(routeAttr)) {
      auto fallback = route.getAs<FlatSymbolRefAttr>("fallback");
      auto ranges = route.getAs<DenseI64ArrayAttr>("ranges");
      if (!fallback || !ranges ||
          !isa<LLVM::LLVMPointerType>(global.getGlobalType()) ||
          !module.lookupSymbol<LLVM::LLVMFuncOp>(fallback.getValue()))
        return global.emitError(
            "route proof index has invalid fallback storage");
      certificates.push_back({global, fallback, 0, 0, ranges});
      global->removeAttr(routeAttr);
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
  if (entries.empty()) {
    builder.setInsertionPointToStart(entry);
    LLVM::ReturnOp::create(builder, location, ValueRange{});
    return success();
  }

  Type dependencyType =
      LLVM::LLVMStructType::getLiteral(context, {i64, i64, i64, i64});
  auto dependencyArray =
      LLVM::LLVMArrayType::get(dependencyType, entries.size());
  auto dependencies = makeConstantGlobal(
      module, location, dependencyArray, "__obelisk_eval_proof_dependencies_v1",
      LLVM::Linkage::Internal, 8, [&](OpBuilder &b) {
        Value array = LLVM::ZeroOp::create(b, location, dependencyArray);
        for (auto [index, dependency] : llvm::enumerate(entries)) {
          Value record = LLVM::ZeroOp::create(b, location, dependencyType);
          uint64_t fields[] = {dependency.begin, dependency.end,
                               dependency.prefix_end, dependency.certificate};
          for (auto [field, value] : llvm::enumerate(fields))
            record = LLVM::InsertValueOp::create(
                b, location, record, llvmConstant(b, location, i64, value),
                ArrayRef<int64_t>{static_cast<int64_t>(field)});
          array = LLVM::InsertValueOp::create(
              b, location, array, record,
              ArrayRef<int64_t>{static_cast<int64_t>(index)});
        }
        return array;
      });
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
          if (certificate.fallback) {
            insert(address, 3);
            insert(LLVM::AddressOfOp::create(b, location, pointer,
                                             certificate.fallback.getValue()),
                   4);
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
  // disturbed. Per-kernel latches and per-route pointers remain independent;
  // the lookup helper changes only those in its exact overlap result.
  auto reset = getOrDeclareLLVMFunction(
      module, "__obelisk_eval_proof_aggregate_invalidate_v1", voidType, {});
  builder.setInsertionPointToStart(reset.addEntryBlock(builder));
  for (StringRef name : {"__obelisk_eval_promotion_latched_v1",
                         "__obelisk_eval_periodic_promotion_latched_v1",
                         "__obelisk_eval_periodic_entry_promotion_latched_v1",
                         "__obelisk_eval_periodic_promotion_scanned_v1",
                         "__obelisk_eval_fast_nba_latched_v1",
                         "__obelisk_eval_fast_nba_roots_v1"})
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

} // namespace obelisk::detail
