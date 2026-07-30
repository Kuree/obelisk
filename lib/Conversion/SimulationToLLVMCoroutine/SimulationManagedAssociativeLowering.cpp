//===- SimulationManagedAssociativeLowering.cpp - Assoc patterns -----===//

#include "SimulationToLLVMCoroutinePrivate.h"

#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Runtime/Runtime.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Transforms/DialectConversion.h"

#include <cstddef>

using namespace mlir;

namespace obelisk::detail {

Value makeNativeAssocKey(OpBuilder &builder, Location location,
                         sim::AssocArrayType array, ValueRange values) {
  Type i8 = builder.getI8Type();
  Type i32 = builder.getI32Type();
  Type i64 = builder.getI64Type();
  Value storage =
      entryAlloca(builder, location, i8, sizeof(obelisk_rt_assoc_key_v1), 8);
  auto store32 = [&](uint64_t offset, uint32_t value) {
    LLVM::StoreOp::create(builder, location,
                          llvmConstant(builder, location, i32, value),
                          byteGEP(builder, location, storage, offset), 4);
  };
  auto store64 = [&](uint64_t offset, Value value) {
    LLVM::StoreOp::create(builder, location,
                          castIntegerWidth(builder, location, value, i64),
                          byteGEP(builder, location, storage, offset), 8);
  };
  bool stringKey = isa<sim::StringType>(array.getKeyType());
  uint32_t keyKind =
      stringKey ? OBELISK_RT_ASSOC_KEY_STRING
                : (array.getSignedKey() ? OBELISK_RT_ASSOC_KEY_SIGNED
                                        : OBELISK_RT_ASSOC_KEY_UNSIGNED);
  uint64_t keyWidth = stringKey ? 0 : *sim::getPackedWidth(array.getKeyType());
  store32(offsetof(obelisk_rt_assoc_key_v1, kind), keyKind);
  store32(offsetof(obelisk_rt_assoc_key_v1, reserved), 0);
  LLVM::StoreOp::create(builder, location,
                        llvmConstant(builder, location, i64, keyWidth),
                        byteGEP(builder, location, storage,
                                offsetof(obelisk_rt_assoc_key_v1, width)),
                        8);
  Value zero = llvmConstant(builder, location, i64, 0);
  if (stringKey) {
    LLVM::StoreOp::create(builder, location, zero,
                          byteGEP(builder, location, storage,
                                  offsetof(obelisk_rt_assoc_key_v1, value)),
                          8);
    LLVM::StoreOp::create(builder, location, zero,
                          byteGEP(builder, location, storage,
                                  offsetof(obelisk_rt_assoc_key_v1, unknown)),
                          8);
    store64(offsetof(obelisk_rt_assoc_key_v1, string), values.front());
  } else {
    store64(offsetof(obelisk_rt_assoc_key_v1, value), values.front());
    if (values.size() == 2)
      store64(offsetof(obelisk_rt_assoc_key_v1, unknown), values[1]);
    else
      LLVM::StoreOp::create(builder, location, zero,
                            byteGEP(builder, location, storage,
                                    offsetof(obelisk_rt_assoc_key_v1, unknown)),
                            8);
    LLVM::StoreOp::create(builder, location, zero,
                          byteGEP(builder, location, storage,
                                  offsetof(obelisk_rt_assoc_key_v1, string)),
                          8);
  }
  return storage;
}

namespace {

static SmallVector<Value> makeNativeValueStorage(OpBuilder &builder,
                                                 Location location,
                                                 ValueRange values) {
  SmallVector<Value> storage;
  for (Value value : values) {
    Value slot = entryAlloca(builder, location, value.getType(), 1, 8);
    LLVM::StoreOp::create(builder, location, value, slot, 8);
    storage.push_back(slot);
  }
  return storage;
}

static Value makeNativeAssocTrace(OpBuilder &builder, Location location,
                                  Operation *operation, uint64_t typeID,
                                  ArrayRef<int64_t> offsets,
                                  ArrayRef<int32_t> kinds) {
  Type pointer = LLVM::LLVMPointerType::get(builder.getContext());
  if (offsets.empty())
    return LLVM::ZeroOp::create(builder, location, pointer);
  std::string bytes(offsets.size() * sizeof(obelisk_rt_element_trace_slot_v1),
                    '\0');
  for (auto [index, offset, kind] : llvm::enumerate(offsets, kinds)) {
    obelisk_rt_element_trace_slot_v1 slot{
        static_cast<uint64_t>(offset),
        static_cast<obelisk_rt_managed_slot_kind_v1>(kind), 0};
    std::memcpy(bytes.data() + index * sizeof(obelisk_rt_element_trace_slot_v1),
                &slot, sizeof(slot));
  }
  ModuleOp module = operation->getParentOfType<ModuleOp>();
  std::string name = "__obelisk_element_trace_" + std::to_string(typeID);
  LLVM::GlobalOp global = module.lookupSymbol<LLVM::GlobalOp>(name);
  if (!global)
    global = makeByteArrayGlobal(module, location, name, bytes);
  return LLVM::AddressOfOp::create(builder, location, global);
}

class AssocCreateConversion final
    : public OpConversionPattern<sim::SimAssocCreateOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimAssocCreateOp op, OneToNOpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    Type i32 = rewriter.getI32Type();
    Type i64 = rewriter.getI64Type();
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    Value output = entryAlloca(rewriter, op.getLoc(), pointer, 1, 8);
    LLVM::StoreOp::create(rewriter, op.getLoc(),
                          LLVM::ZeroOp::create(rewriter, op.getLoc(), pointer),
                          output, 8);
    Value trace =
        makeNativeAssocTrace(rewriter, op.getLoc(), op, op.getTypeId(),
                             op.getTraceOffsets(), op.getTraceKinds());
    auto c32 = [&](uint32_t value) {
      return llvmConstant(rewriter, op.getLoc(), i32, value);
    };
    auto c64 = [&](uint64_t value) {
      return llvmConstant(rewriter, op.getLoc(), i64, value);
    };
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{i32},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_assoc_create_typed"),
            ValueRange{lane, c64(op.getTypeId()), c32(op.getElementKind()),
                       c32(op.getElementFlags()), c64(op.getValueSize()),
                       c64(op.getAlignment()), c64(op.getBitWidth()), trace,
                       c64(op.getTraceOffsets().size()), c32(op.getKeyKind()),
                       c64(op.getKeyWidth()), output})
            .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    Value result =
        LLVM::LoadOp::create(rewriter, op.getLoc(), pointer, output, 8);
    rewriter.replaceOp(op, managedObjectHandle(rewriter, op.getLoc(), result));
    return success();
  }
};

class AssocReadConversion final
    : public OpConversionPattern<sim::SimAssocReadOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimAssocReadOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getArray().size() != 1 || adaptor.getKey().empty() ||
        adaptor.getKey().size() > 2)
      return failure();
    SmallVector<Type> types;
    if (failed(
            getTypeConverter()->convertType(op.getResult().getType(), types)) ||
        types.empty() || types.size() > 2)
      return failure();
    SmallVector<Value> storage;
    for (Type type : types) {
      Value zero = zeroNativeValue(rewriter, op.getLoc(), type);
      if (!zero)
        return failure();
      Value slot = entryAlloca(rewriter, op.getLoc(), type, 1, 8);
      LLVM::StoreOp::create(rewriter, op.getLoc(), zero, slot, 8);
      storage.push_back(slot);
    }
    sim::AssocArrayType array = op.getArray().getType();
    Value key =
        makeNativeAssocKey(rewriter, op.getLoc(), array, adaptor.getKey());
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    Value unknown =
        storage.size() == 2
            ? storage[1]
            : Value(LLVM::ZeroOp::create(rewriter, op.getLoc(), pointer));
    Value present =
        entryAlloca(rewriter, op.getLoc(), rewriter.getI32Type(), 1, 4);
    Value planeSize =
        llvmConstant(rewriter, op.getLoc(), rewriter.getI64Type(),
                     (sim::getPackedWidth(op.getResult().getType())
                          .value_or(static_cast<unsigned>(
                              types.front().getIntOrFloatBitWidth())) +
                      7) /
                         8);
    if (sim::isManagedHandleType(op.getResult().getType()))
      planeSize = llvmConstant(rewriter, op.getLoc(), rewriter.getI64Type(),
                               sizeof(void *));
    Value unknownSize =
        storage.size() == 2
            ? planeSize
            : llvmConstant(rewriter, op.getLoc(), rewriter.getI64Type(), 0);
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_assoc_read_checked"),
            ValueRange{managedObjectPointer(rewriter, op.getLoc(),
                                            adaptor.getArray().front()),
                       key, storage.front(), planeSize, unknown, unknownSize,
                       present})
            .getResult();
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    (void)lane;
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    SmallVector<Value> values;
    for (auto [type, slot] : llvm::zip_equal(types, storage))
      values.push_back(
          LLVM::LoadOp::create(rewriter, op.getLoc(), type, slot, 8));
    rewriter.replaceOpWithMultiple(op, {ValueRange(values)});
    return success();
  }
};

template <typename Op, typename Adaptor>
static LogicalResult
lowerAssocValueMutation(Op op, Adaptor adaptor,
                        ConversionPatternRewriter &rewriter, StringRef callee) {
  if (adaptor.getArray().size() != 1 || adaptor.getValue().empty() ||
      adaptor.getValue().size() > 2)
    return failure();
  SmallVector<Value> storage =
      makeNativeValueStorage(rewriter, op.getLoc(), adaptor.getValue());
  Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
  Value unknown =
      storage.size() == 2
          ? storage[1]
          : Value(LLVM::ZeroOp::create(rewriter, op.getLoc(), pointer));
  uint64_t bytes = 0;
  if (auto width = sim::getPackedWidth(op.getValue().getType()))
    bytes = (*width + 7) / 8;
  else if (sim::isManagedHandleType(op.getValue().getType()))
    bytes = sizeof(void *);
  else if (isa<sim::EventType>(op.getValue().getType()))
    bytes = sizeof(uint64_t);
  else if (auto floating = dyn_cast<FloatType>(op.getValue().getType()))
    bytes = floating.getWidth() / 8;
  else if (auto span = sim::getProvenanceSpan(op.getValue().getType()))
    bytes = (*span + 7) / 8;
  if (bytes == 0)
    return failure();
  Value size =
      llvmConstant(rewriter, op.getLoc(), rewriter.getI64Type(), bytes);
  Value unknownSize =
      storage.size() == 2
          ? size
          : llvmConstant(rewriter, op.getLoc(), rewriter.getI64Type(), 0);
  SmallVector<Value> arguments{
      managedContextAndLane(rewriter, op.getLoc()).second,
      managedObjectPointer(rewriter, op.getLoc(), adaptor.getArray().front())};
  if constexpr (std::is_same_v<Op, sim::SimAssocWriteOp>) {
    if (adaptor.getKey().empty() || adaptor.getKey().size() > 2)
      return failure();
    arguments.push_back(makeNativeAssocKey(
        rewriter, op.getLoc(), op.getArray().getType(), adaptor.getKey()));
  }
  arguments.append({storage.front(), size, unknown, unknownSize});
  auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
  (void)lane;
  Value status =
      LLVM::CallOp::create(
          rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
          SymbolRefAttr::get(rewriter.getContext(), callee), arguments)
          .getResult();
  reportManagedStatus(rewriter, op.getLoc(), context, status);
  rewriter.eraseOp(op);
  return success();
}

class AssocWriteConversion final
    : public OpConversionPattern<sim::SimAssocWriteOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimAssocWriteOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    return lowerAssocValueMutation(op, adaptor, rewriter,
                                   "obelisk_rt_v1_assoc_write_checked");
  }
};

class AssocDefaultConversion final
    : public OpConversionPattern<sim::SimAssocSetDefaultOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimAssocSetDefaultOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    return lowerAssocValueMutation(op, adaptor, rewriter,
                                   "obelisk_rt_v1_assoc_set_default_checked");
  }
};

class AssocExistsConversion final
    : public OpConversionPattern<sim::SimAssocExistsOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimAssocExistsOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getArray().size() != 1 || adaptor.getKey().empty() ||
        adaptor.getKey().size() > 2)
      return failure();
    Value key = makeNativeAssocKey(rewriter, op.getLoc(),
                                   op.getArray().getType(), adaptor.getKey());
    Value output =
        entryAlloca(rewriter, op.getLoc(), rewriter.getI32Type(), 1, 4);
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_assoc_exists"),
            ValueRange{managedObjectPointer(rewriter, op.getLoc(),
                                            adaptor.getArray().front()),
                       key, output})
            .getResult();
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    (void)lane;
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    Value loaded = LLVM::LoadOp::create(rewriter, op.getLoc(),
                                        rewriter.getI32Type(), output, 4);
    rewriter.replaceOpWithNewOp<arith::TruncIOp>(op, rewriter.getI1Type(),
                                                 loaded);
    return success();
  }
};

class AssocDeleteConversion final
    : public OpConversionPattern<sim::SimAssocDeleteOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimAssocDeleteOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getArray().size() != 1 || adaptor.getKey().empty() ||
        adaptor.getKey().size() > 2)
      return failure();
    Value key = makeNativeAssocKey(rewriter, op.getLoc(),
                                   op.getArray().getType(), adaptor.getKey());
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_assoc_delete"),
            ValueRange{managedObjectPointer(rewriter, op.getLoc(),
                                            adaptor.getArray().front()),
                       key})
            .getResult();
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    (void)lane;
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    rewriter.eraseOp(op);
    return success();
  }
};

class AssocTraverseConversion final
    : public OpConversionPattern<sim::SimAssocTraverseOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimAssocTraverseOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getArray().size() != 1 || adaptor.getKey().empty() ||
        adaptor.getKey().size() > 2)
      return failure();
    sim::AssocArrayType array = op.getArray().getType();
    Value key =
        makeNativeAssocKey(rewriter, op.getLoc(), array, adaptor.getKey());
    Value successSlot =
        entryAlloca(rewriter, op.getLoc(), rewriter.getI32Type(), 1, 4);
    StringRef callee;
    int32_t direction = static_cast<int32_t>(op.getDirection());
    if (op.getEndpoint())
      callee = direction > 0 ? "obelisk_rt_v1_assoc_first"
                             : "obelisk_rt_v1_assoc_last";
    else
      callee = direction > 0 ? "obelisk_rt_v1_assoc_next"
                             : "obelisk_rt_v1_assoc_prev";
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(), callee),
            ValueRange{lane,
                       managedObjectPointer(rewriter, op.getLoc(),
                                            adaptor.getArray().front()),
                       key, successSlot})
            .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    SmallVector<Value> keyValues;
    Type i64 = rewriter.getI64Type();
    if (isa<sim::StringType>(array.getKeyType())) {
      Value loaded = LLVM::LoadOp::create(
          rewriter, op.getLoc(), i64,
          byteGEP(rewriter, op.getLoc(), key,
                  offsetof(obelisk_rt_assoc_key_v1, string)),
          8);
      keyValues.push_back(loaded);
    } else {
      SmallVector<Type> converted;
      if (failed(
              getTypeConverter()->convertType(array.getKeyType(), converted)) ||
          converted.empty() || converted.size() > 2)
        return failure();
      for (auto [index, type] : llvm::enumerate(converted)) {
        uint64_t offset = index == 0
                              ? offsetof(obelisk_rt_assoc_key_v1, value)
                              : offsetof(obelisk_rt_assoc_key_v1, unknown);
        Value loaded = LLVM::LoadOp::create(
            rewriter, op.getLoc(), i64,
            byteGEP(rewriter, op.getLoc(), key, offset), 8);
        keyValues.push_back(
            castIntegerWidth(rewriter, op.getLoc(), loaded, type));
      }
    }
    Value success32 = LLVM::LoadOp::create(
        rewriter, op.getLoc(), rewriter.getI32Type(), successSlot, 4);
    Value success1 = arith::TruncIOp::create(rewriter, op.getLoc(),
                                             rewriter.getI1Type(), success32);
    SmallVector<ValueRange> replacements;
    replacements.push_back(keyValues);
    replacements.push_back(ValueRange{success1});
    rewriter.replaceOpWithMultiple(op, replacements);
    return success();
  }
};

} // namespace

void populateManagedAssociativeToLLVMConversionPatterns(
    RewritePatternSet &patterns, TypeConverter &converter) {
  MLIRContext *context = patterns.getContext();
  patterns.add<AssocCreateConversion, AssocReadConversion,
               AssocWriteConversion, AssocDefaultConversion,
               AssocExistsConversion, AssocDeleteConversion,
               AssocTraverseConversion>(converter, context);
}

} // namespace obelisk::detail

