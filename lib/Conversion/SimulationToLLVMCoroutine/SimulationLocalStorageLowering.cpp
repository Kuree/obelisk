//===- SimulationLocalStorageLowering.cpp - Native local storage ----------===//

#include "SimulationLocalStorageLowering.h"
#include "SimulationToLLVMCoroutinePrivate.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/Matchers.h"
#include "mlir/Target/LLVMIR/TypeFromLLVM.h"
#include "mlir/Transforms/DialectConversion.h"
#include "obelisk/Analysis/LocalStorageAnalysis.h"
#include "obelisk/Analysis/NativeStorageLayout.h"
#include "obelisk/Analysis/SSAValueAnalysis.h"
#include "obelisk/Dialect/Schedule/ScheduleOps.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/MathExtras.h"

using namespace mlir;
namespace obelisk::detail {

LocalStorageLowering::LocalStorageLowering(
    ArrayRef<Operation *> functions, const llvm::DataLayout &targetLayout,
    const DenseSet<Value> &byteAddressableState) {
  // LLVM caches StructLayouts by LLVM type identity. Each conversion worker
  // owns its LLVMContext, so give it a private cache as well; copy the already
  // validated target layout rather than reparsing its string representation.
  llvm::DataLayout dataLayout(targetLayout);
  // Unpacked aggregates use the target's ordinary array/record layout. Packed
  // values remain indivisible bitfields, preserving their source bit order.
  if (functions.empty())
    return;
  analysis::NativeStorageLayout nativeLayout(dataLayout);
  LLVM::TypeFromLLVMIRTranslator translator(*functions.front()->getContext(),
                                            /*importStructsAsLiterals=*/true);
  auto buildLayout = [&](Type type, auto &buildLayout) -> Type {
    if (auto found = layouts.find(type); found != layouts.end())
      return found->second.type;
    llvm::Type *backend = nativeLayout.getType(type);
    Type storage = translator.translateType(backend);
    SmallVector<uint64_t> offsets;
    if (auto array = dyn_cast<sim::UnpackedArrayType>(type)) {
      buildLayout(array.getElementType(), buildLayout);
      uint64_t stride = nativeLayout.getEncoding(type).getStride();
      for (unsigned i = 0; i != sim::getAggregateNumElements(type); ++i)
        offsets.push_back(i * stride);
    } else if (isa<sim::UnpackedStructType>(type)) {
      for (unsigned i = 0; i != sim::getAggregateNumElements(type); ++i)
        buildLayout(sim::getAggregateElementType(type, i), buildLayout);
      for (int64_t offset :
           nativeLayout.getEncoding(type).getOffsets().asArrayRef())
        offsets.push_back(offset);
    }
    layouts.try_emplace(
        type,
        Layout{
            storage, dataLayout.getTypeAllocSize(backend).getFixedValue(),
            static_cast<unsigned>(dataLayout.getABITypeAlign(backend).value()),
            std::move(offsets)});
    return storage;
  };
  for (Operation *operation : functions) {
    auto function = cast<sim::SimFuncOp>(operation);
    analysis::LocalStorageAnalysis analysis(function);
    analysis::SemanticValueRootAnalysis roots(function);
    DenseSet<Operation *> flatViews;
    for (auto [reference, allocation] : analysis.getReferences()) {
      Type type = cast<sim::RefType>(reference.getType()).getElementType();
      if (roots.lookup(reference) == allocation.getResult())
        wholeAllocations.insert(reference);
      buildLayout(type, buildLayout);
      // A flat bit view crossing unpacked elements requires the canonical
      // bitstream layout. Typed element views preserve native layout instead.
      if (isa<sim::UnpackedArrayType, sim::UnpackedStructType>(type))
        for (Operation *user : reference.getUsers())
          if (isa<sim::SimRefExtractOp, schedule::NativeWordViewOp>(user))
            flatViews.insert(allocation);
    }
    // Bufferize closed copy-only SSA values. Their snapshots cannot escape
    // through a call, return or loop-carried value, so fixed invocation-local
    // buffers preserve the load's value semantics on every execution.
    SmallVector<sim::SimRefLoadOp> memoryLoads;
    bool suspends = function
                        .walk([](Operation *op) {
                          return sim::isSuspensionOp(op)
                                     ? WalkResult::interrupt()
                                     : WalkResult::advance();
                        })
                        .wasInterrupted();
    if (dataLayout.isLittleEndian() && !suspends)
      function.walk([&](sim::SimRefLoadOp load) {
        auto width = nativeStateWidth(load.getType());
        SmallVector<sim::ManagedHandleSlot> managed;
        if (!width || *width <= 64 || *width > 32768 || load->use_empty() ||
            !sim::getManagedHandleSlots(load.getType(), managed) ||
            !managed.empty())
          return;
        auto source = analysis.lookup(load.getReference());
        if (source && (!isWholeAllocation(load.getReference()) ||
                       source.getInitialValue().getType() != load.getType()))
          return;
        for (Operation *user : load->getUsers()) {
          Value target;
          if (auto alloc = dyn_cast<sim::SimRefAllocOp>(user)) {
            if (alloc.getInitialValue() != load.getResult())
              return;
            target = alloc.getResult();
            if (!analysis.lookup(target))
              return;
          } else if (auto store = dyn_cast<sim::SimRefStoreOp>(user)) {
            if (store.getValue() != load.getResult())
              return;
            target = store.getReference();
          } else {
            return;
          }
          if (auto allocation = analysis.lookup(target)) {
            if (!isWholeAllocation(target) ||
                allocation.getInitialValue().getType() != load.getType())
              return;
          } else {
            // Preserve the existing dynamic packed-store legalization. A
            // state-to-state memcpy requires the sparse handle analysis's
            // constant, byte-aligned address certificate for both ends.
            if (!source &&
                (!byteAddressableState.contains(load.getReference()) ||
                 !byteAddressableState.contains(target)))
              return;
          }
        }
        memoryLoads.push_back(load);
      });
    for (auto load : memoryLoads) {
      memoryValues.insert(load.getResult());
      auto source = analysis.lookup(load.getReference());
      if (source && !flatViews.contains(source) &&
          llvm::all_of(load->getUsers(), [&](Operation *user) {
            if (auto alloc = dyn_cast<sim::SimRefAllocOp>(user))
              return bool(analysis.lookup(alloc.getResult()));
            return bool(
                analysis.lookup(cast<sim::SimRefStoreOp>(user).getReference()));
          }))
        nativeMemoryValues.insert(load.getResult());
    }
    // Plan aliases before rewriting any operation. A dense must-analysis
    // preserves a loaded version through CFG/region joins only while no
    // possible clobber reaches a consumer.
    auto borrowable = analysis.getBorrowableLoads(memoryLoads);
    borrowedValues.insert(borrowable.begin(), borrowable.end());
    DenseMap<Operation *, unsigned> allocations;
    function.walk([&](sim::SimRefAllocOp allocation) {
      if (!analysis.lookup(allocation.getResult()))
        return;
      OpBuilder builder(allocation);
      Type type = allocation.getInitialValue().getType();
      bool native = !flatViews.contains(allocation);
      const Layout &layout = getLayout(type);
      // A trailing initialized byte makes an unaligned bitfield window safe
      // even when its final byte is only partially used (LRM 11.5.1).
      unsigned bytes =
          (native ? layout.bytes
                  : llvm::divideCeil(*nativeStateWidth(type), 8u)) +
          1;
      auto allocate = [&] {
        Type storage =
            native
                ? Type(LLVM::LLVMStructType::getLiteral(
                      builder.getContext(), {layout.type, builder.getI8Type()}))
                : Type(builder.getI8Type());
        return entryAlloca(builder, allocation.getLoc(), storage,
                           native ? 1 : bytes, native ? layout.alignment : 8);
      };
      allocations.try_emplace(allocation, slots.size());
      slots.push_back({allocate(), containsLogic(type) ? allocate() : Value{},
                       bytes * 8, native});
    });
    for (auto [reference, allocation] : analysis.getReferences()) {
      references.try_emplace(reference, allocations.lookup(allocation));
      if (flatViews.contains(allocation))
        continue;
      Value current = roots.lookup(reference);
      llvm::SmallDenseSet<Value, 8> visited;
      while (current != allocation.getResult() &&
             visited.insert(current).second) {
        if (auto element = current.getDefiningOp<sim::SimRefArrayElementOp>()) {
          if (!isa<sim::UnpackedArrayType>(
                  element.getInput().getType().getElementType()))
            break;
          current = roots.lookup(element.getInput());
          continue;
        }
        if (auto field = current.getDefiningOp<sim::SimRefSubelementOp>()) {
          Type type = field.getInput().getType().getElementType();
          bool unpacked = true;
          for (int64_t index : field.getIndices()) {
            unpacked &=
                isa<sim::UnpackedArrayType, sim::UnpackedStructType>(type);
            type = sim::getAggregateElementType(type, index);
          }
          if (!unpacked)
            break;
          current = roots.lookup(field.getInput());
          continue;
        }
        break;
      }
      if (current == allocation.getResult())
        nativeObjects.insert(reference);
    }
    PackedStorageLayout packed(function->getParentOfType<ModuleOp>(),
                               dataLayout);
    for (auto [reference, allocation] : analysis.getReferences()) {
      if (flatViews.contains(allocation))
        continue;
      // Plan against the typed reference graph before conversion erases it.
      // A native packed object starts at a byte boundary, including a dynamic
      // element of an unpacked array. Constant packed subviews share its units.
      Value current = roots.lookup(reference);
      uint64_t offset = 0;
      unsigned width = *nativeStateWidth(
          cast<sim::RefType>(reference.getType()).getElementType());
      llvm::SmallDenseSet<Value, 8> visited;
      while (current && visited.insert(current).second) {
        Type type = cast<sim::RefType>(current.getType()).getElementType();
        if (isNativeObject(current)) {
          if (isa<sim::PackedStructType>(type))
            if (auto unit = packed.get(type, 0, offset, width))
              accessUnits.try_emplace(reference, *unit);
          break;
        }
        if (auto field = current.getDefiningOp<sim::SimRefSubelementOp>()) {
          Type parent = field.getInput().getType().getElementType();
          for (int64_t index : field.getIndices()) {
            offset += getElementOffset(field.getInput(), parent, index);
            parent = sim::getAggregateElementType(parent, index);
          }
          current = roots.lookup(field.getInput());
        } else if (auto extract =
                       current.getDefiningOp<sim::SimRefExtractOp>()) {
          offset += extract.getLowBit();
          current = roots.lookup(extract.getInput());
        } else {
          break;
        }
      }
    }
  }
}

void LocalStorageLowering::addValueConversions(TypeConverter &converter) const {
  converter.addConversion(
      [this](Value value,
             SmallVectorImpl<Type> &results) -> std::optional<LogicalResult> {
        if (!isMemoryValue(value))
          return std::nullopt;
        results.push_back(LLVM::LLVMPointerType::get(value.getContext()));
        if (containsLogic(value.getType()))
          results.push_back(results.front());
        return success();
      });
}

uint64_t LocalStorageLowering::getElementOffset(Value reference, Type type,
                                                unsigned index) const {
  if (lookup(reference)->nativeLayout &&
      isa<sim::UnpackedArrayType, sim::UnpackedStructType>(type))
    return getLayout(type).elementOffsets[index] * 8;
  return sim::getAggregateProvenanceSubelement(type, index)->first;
}

uint64_t LocalStorageLowering::getArrayStride(Value reference) const {
  Type type = cast<sim::RefType>(reference.getType()).getElementType();
  Type child = sim::getAggregateElementType(type, 0);
  if (lookup(reference)->nativeLayout && isa<sim::UnpackedArrayType>(type))
    return getLayout(child).bytes * 8;
  return *sim::getArrayElementStorageStride(type);
}

namespace {
struct Window {
  Value byteOffset;
  Value shift;
  Value valid;
  IntegerType spanType;
};
Window getWindow(ConversionPatternRewriter &rewriter, Location location,
                 Value offset, unsigned width, unsigned containerWidth = 0,
                 std::optional<PackedAccessUnit> unit = {}) {
  auto i64 = rewriter.getI64Type();
  Value zero = llvmConstant(rewriter, location, i64, 0);
  Value valid = rewriter.createOrFold<arith::CmpIOp>(
      location, arith::CmpIPredicate::ne, offset,
      llvmConstant(rewriter, location, i64, UINT64_MAX));
  if (unit && !containerWidth)
    offset = rewriter.createOrFold<arith::SubIOp>(
        location, offset,
        llvmConstant(rewriter, location, i64, unit->bitOffset));
  offset =
      rewriter.createOrFold<arith::SelectOp>(location, valid, offset, zero);
  Value byte = arith::ShRUIOp::create(rewriter, location, offset,
                                      llvmConstant(rewriter, location, i64, 3));
  if (containerWidth) {
    auto span = rewriter.getIntegerType(containerWidth);
    return {byte, llvmConstant(rewriter, location, span, 0), valid, span};
  }
  if (unit) {
    auto span = rewriter.getIntegerType(unit->width);
    return {byte, llvmConstant(rewriter, location, span, unit->bitOffset),
            valid, span};
  }
  Value bit = arith::AndIOp::create(rewriter, location, offset,
                                    llvmConstant(rewriter, location, i64, 7));
  APInt knownOffset;
  unsigned leadingBits = 7;
  if (matchPattern(offset, m_ConstantInt(&knownOffset)))
    leadingBits = knownOffset.getZExtValue() & 7;
  auto span = rewriter.getIntegerType(llvm::alignTo(width + leadingBits, 8u));
  return {byte, resizeNativeInteger(rewriter, location, bit, span), valid,
          span};
}
Value windowAddress(ConversionPatternRewriter &rewriter, Location location,
                    Value base, const Window &window) {
  return LLVM::GEPOp::create(rewriter, location, base.getType(),
                             rewriter.getI8Type(), base,
                             ValueRange{window.byteOffset});
}
Value loadWindow(ConversionPatternRewriter &rewriter, Location location,
                 Value base, const Window &window, IntegerType result) {
  Value address = windowAddress(rewriter, location, base, window);
  Value value =
      LLVM::LoadOp::create(rewriter, location, window.spanType, address, 1);
  value = arith::ShRUIOp::create(rewriter, location, value, window.shift);
  return resizeNativeInteger(rewriter, location, value, result);
}

// Visit the scalar leaves of an unpacked object, keeping canonical value bits
// separate from target memory offsets. Packing is needed only at value ABI
// boundaries; references within the invocation use native element addresses.
void visitLeaves(Type type, const LocalStorageLowering &locals, bool native,
                 uint64_t logical, uint64_t physical,
                 llvm::function_ref<void(Type, uint64_t, uint64_t)> visit) {
  if (native && isa<sim::UnpackedArrayType, sim::UnpackedStructType>(type)) {
    const auto &layout = locals.getLayout(type);
    for (unsigned i = 0; i != sim::getAggregateNumElements(type); ++i) {
      auto source = sim::getAggregateProvenanceSubelement(type, i);
      visitLeaves(sim::getAggregateElementType(type, i), locals, native,
                  logical + source->first,
                  physical + layout.elementOffsets[i] * 8, visit);
    }
    return;
  }
  visit(type, logical, physical);
}

Value addOffset(ConversionPatternRewriter &rewriter, Location location,
                Value offset, uint64_t amount) {
  if (!amount)
    return offset;
  Value invalid =
      llvmConstant(rewriter, location, rewriter.getI64Type(), UINT64_MAX);
  Value valid = rewriter.createOrFold<arith::CmpIOp>(
      location, arith::CmpIPredicate::ne, offset, invalid);
  Value sum = rewriter.createOrFold<arith::AddIOp>(
      location, offset,
      llvmConstant(rewriter, location, rewriter.getI64Type(), amount));
  return rewriter.createOrFold<arith::SelectOp>(location, valid, sum, invalid);
}

Value loadPlane(ConversionPatternRewriter &rewriter, Location location,
                Value base, Value offset, Type type,
                const LocalStorageLowering &locals, bool native,
                bool wholeObject = false,
                std::optional<PackedAccessUnit> unit = {}) {
  auto resultType = rewriter.getIntegerType(*nativeStateWidth(type));
  Value result = llvmConstant(rewriter, location, resultType, 0);
  visitLeaves(
      type, locals, native, 0, 0,
      [&](Type leaf, uint64_t logical, uint64_t physical) {
        auto plane = rewriter.getIntegerType(*nativeStateWidth(leaf));
        Window window = getWindow(
            rewriter, location, addOffset(rewriter, location, offset, physical),
            plane.getWidth(),
            native && wholeObject && plane.getWidth() <= 64 &&
                    locals.getLayout(leaf).bytes <= 8
                ? locals.getLayout(leaf).bytes * 8
                : 0,
            unit);
        Value value = loadWindow(rewriter, location, base, window, plane);
        value = resizeNativeInteger(rewriter, location, value, resultType);
        if (logical)
          value = rewriter.createOrFold<arith::ShLIOp>(
              location, value,
              llvmConstant(rewriter, location, resultType, logical));
        result = rewriter.createOrFold<arith::OrIOp>(location, result, value);
      });
  return result;
}

void storePlane(ConversionPatternRewriter &rewriter, Location location,
                Value base, Value offset, Type type, Value input,
                const LocalStorageLowering &locals, bool native,
                bool wholeObject = false,
                std::optional<PackedAccessUnit> unit = {}) {
  if (isa<FloatType>(input.getType()))
    input = arith::BitcastOp::create(
        rewriter, location, rewriter.getIntegerType(*nativeStateWidth(type)),
        input);
  visitLeaves(
      type, locals, native, 0, 0,
      [&](Type leaf, uint64_t logical, uint64_t physical) {
        unsigned width = *nativeStateWidth(leaf);
        unsigned containerWidth = native && wholeObject && width <= 64 &&
                                          locals.getLayout(leaf).bytes <= 8
                                      ? locals.getLayout(leaf).bytes * 8
                                      : 0;
        Window window = getWindow(
            rewriter, location, addOffset(rewriter, location, offset, physical),
            width, containerWidth, unit);
        Value value = input;
        if (logical)
          value = rewriter.createOrFold<arith::ShRUIOp>(
              location, value,
              llvmConstant(rewriter, location, input.getType(), logical));
        value = resizeNativeInteger(rewriter, location, value,
                                    rewriter.getIntegerType(width));
        value = resizeNativeInteger(rewriter, location, value, window.spanType);
        if (containerWidth) {
          // This is the complete native element, including its owned padding.
          // A packed field view cannot use this path. Preserve invalid-index
          // semantics; LLVM removes the old load when validity is proved.
          Value address = windowAddress(rewriter, location, base, window);
          if (!matchPattern(window.valid, m_One())) {
            Value old = LLVM::LoadOp::create(rewriter, location,
                                             window.spanType, address, 1);
            value = rewriter.createOrFold<arith::SelectOp>(
                location, window.valid, value, old);
          }
          LLVM::StoreOp::create(rewriter, location, value, address, 1);
          return;
        }
        Value mask = arith::ConstantOp::create(
            rewriter, location, window.spanType,
            rewriter.getIntegerAttr(
                window.spanType,
                APInt::getLowBitsSet(window.spanType.getWidth(), width)));
        mask =
            rewriter.createOrFold<arith::ShLIOp>(location, mask, window.shift);
        value =
            rewriter.createOrFold<arith::ShLIOp>(location, value, window.shift);
        mask = rewriter.createOrFold<arith::SelectOp>(
            location, window.valid, mask,
            llvmConstant(rewriter, location, window.spanType, 0));
        Value address = windowAddress(rewriter, location, base, window);
        Value old = LLVM::LoadOp::create(rewriter, location, window.spanType,
                                         address, 1);
        Value delta =
            rewriter.createOrFold<arith::XOrIOp>(location, old, value);
        delta = rewriter.createOrFold<arith::AndIOp>(location, delta, mask);
        Value merged =
            rewriter.createOrFold<arith::XOrIOp>(location, old, delta);
        LLVM::StoreOp::create(rewriter, location, merged, address, 1);
      });
}

// Legalize layout conversions as byte copies and bounded integer windows.
// Packed leaves retain their bit order; unpacked elements retain their target
// alignment. Only the destination's head and tail require read/modify/write.
void copyMemoryPlane(ConversionPatternRewriter &rewriter, Location location,
                     Value destination, bool destinationNative, Value source,
                     bool sourceNative, Type type,
                     const LocalStorageLowering &locals) {
  auto constant = [&](uint64_t value) {
    return llvmConstant(rewriter, location, rewriter.getI64Type(), value);
  };
  auto copyBytes = [&](uint64_t to, uint64_t from, uint64_t bytes) {
    if (bytes)
      LLVM::MemcpyOp::create(
          rewriter, location, byteGEP(rewriter, location, destination, to),
          byteGEP(rewriter, location, source, from), constant(bytes), false);
  };
  if (destinationNative == sourceNative) {
    copyBytes(0, 0,
              sourceNative ? locals.getLayout(type).bytes
                           : llvm::divideCeil(*nativeStateWidth(type), 8u));
    return;
  }
  visitLeaves(type, locals, true, 0, 0,
              [&](Type leaf, uint64_t logical, uint64_t physical) {
                uint64_t to = destinationNative ? physical : logical;
                uint64_t from = sourceNative ? physical : logical;
                unsigned remaining = *nativeStateWidth(leaf);
                auto copyWindow = [&](unsigned bits) {
                  auto integer = rewriter.getIntegerType(bits);
                  Value value = loadWindow(
                      rewriter, location, source,
                      getWindow(rewriter, location, constant(from), bits),
                      integer);
                  storePlane(rewriter, location, destination, constant(to),
                             integer, value, locals, false);
                  to += bits;
                  from += bits;
                  remaining -= bits;
                };
                if (to % 8)
                  copyWindow(std::min<uint64_t>(remaining, 8 - to % 8));
                unsigned bytes = remaining / 8;
                if (from % 8 == 0) {
                  copyBytes(to / 8, from / 8, bytes);
                  to += bytes * 8;
                  from += bytes * 8;
                  remaining -= bytes * 8;
                } else {
                  // Split a fixed-width bit copy into machine-word accesses.
                  // Source loops are unchanged; LLVM decides whether to unroll
                  // or vectorize those.
                  while (remaining >= 8)
                    copyWindow(std::min(64u, remaining / 8 * 8));
                }
                if (remaining)
                  copyWindow(remaining);
              });
}

class LocalAllocConversion final
    : public OpConversionPattern<sim::SimRefAllocOp> {
public:
  LocalAllocConversion(const TypeConverter &converter, MLIRContext *context,
                       const LocalStorageLowering &locals)
      : OpConversionPattern(converter, context, PatternBenefit(2)),
        locals(locals) {}
  LogicalResult
  matchAndRewrite(sim::SimRefAllocOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    const auto *slot = locals.lookup(op.getResult());
    if (!slot)
      return failure();
    Value zero = llvmConstant(rewriter, op.getLoc(), rewriter.getI64Type(), 0);
    auto initialize = [&](Value address, Value value) {
      LLVM::MemsetOp::create(
          rewriter, op.getLoc(), address,
          llvmConstant(rewriter, op.getLoc(), rewriter.getI8Type(), 0),
          llvmConstant(rewriter, op.getLoc(), rewriter.getI64Type(),
                       slot->storageWidth / 8),
          false);
      if (isa<LLVM::LLVMPointerType>(value.getType())) {
        copyMemoryPlane(rewriter, op.getLoc(), address, slot->nativeLayout,
                        value,
                        locals.usesNativeMemoryLayout(op.getInitialValue()),
                        op.getInitialValue().getType(), locals);
        return;
      }
      storePlane(rewriter, op.getLoc(), address, zero,
                 op.getInitialValue().getType(), value, locals,
                 slot->nativeLayout, true);
    };
    initialize(slot->value, adaptor.getInitialValue().front());
    if (slot->unknown)
      initialize(slot->unknown, adaptor.getInitialValue()[1]);
    rewriter.replaceOp(op, zero);
    return success();
  }

private:
  const LocalStorageLowering &locals;
};

class LocalLoadConversion final
    : public OpConversionPattern<sim::SimRefLoadOp> {
public:
  LocalLoadConversion(const TypeConverter &converter, MLIRContext *context,
                      const LocalStorageLowering &locals)
      : OpConversionPattern(converter, context, PatternBenefit(2)),
        locals(locals) {}
  LogicalResult
  matchAndRewrite(sim::SimRefLoadOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    const auto *slot = locals.lookup(op.getReference());
    if (!slot)
      return failure();
    Location location = op.getLoc();
    Type type = op.getResult().getType();
    if (locals.isMemoryValue(op.getResult())) {
      assert(locals.isWholeAllocation(op.getReference()));
      bool native = locals.usesNativeMemoryLayout(op.getResult());
      bool compatible =
          native == slot->nativeLayout || !slot->nativeLayout ||
          !isa<sim::UnpackedArrayType, sim::UnpackedStructType>(type);
      if (compatible && locals.canBorrowMemory(op.getResult())) {
        SmallVector<Value> planes{slot->value};
        if (slot->unknown)
          planes.push_back(slot->unknown);
        rewriter.replaceOpWithMultiple(op, {ValueRange(planes)});
        return success();
      }
      // A conflicting live version or a different memory layout needs a
      // snapshot. Keep private-to-private copies in their native layout.
      SmallVector<Value> results;
      unsigned bytes = native ? locals.getLayout(type).bytes
                              : llvm::divideCeil(*nativeStateWidth(type), 8u);
      for (Value source : {slot->value, slot->unknown}) {
        if (!source)
          continue;
        Value buffer =
            entryAlloca(rewriter, location, rewriter.getI8Type(), bytes, 1);
        LLVM::MemsetOp::create(
            rewriter, location, buffer,
            llvmConstant(rewriter, location, rewriter.getI8Type(), 0),
            llvmConstant(rewriter, location, rewriter.getI64Type(), bytes),
            false);
        copyMemoryPlane(rewriter, location, buffer, native, source,
                        slot->nativeLayout, type, locals);
        results.push_back(buffer);
      }
      rewriter.replaceOpWithMultiple(op, {ValueRange(results)});
      return success();
    }
    auto plane = rewriter.getIntegerType(*nativeStateWidth(type));
    auto window = getWindow(rewriter, location, adaptor.getReference().front(),
                            plane.getWidth());
    Value value = loadPlane(
        rewriter, location, slot->value, adaptor.getReference().front(), type,
        locals, slot->nativeLayout, locals.isNativeObject(op.getReference()),
        locals.getAccessUnit(op.getReference()));
    Value zero = llvmConstant(rewriter, location, plane, 0);
    Value unknown = slot->unknown
                        ? loadPlane(rewriter, location, slot->unknown,
                                    adaptor.getReference().front(), type,
                                    locals, slot->nativeLayout,
                                    locals.isNativeObject(op.getReference()),
                                    locals.getAccessUnit(op.getReference()))
                        : zero;
    if (!containsLogic(type) && slot->unknown) {
      Value known = arith::XOrIOp::create(
          rewriter, location, unknown,
          arith::ConstantOp::create(
              rewriter, location, plane,
              rewriter.getIntegerAttr(plane,
                                      APInt::getAllOnes(plane.getWidth()))));
      value = arith::AndIOp::create(rewriter, location, value, known);
    }
    value =
        arith::SelectOp::create(rewriter, location, window.valid, value, zero);
    if (isa<FloatType>(type))
      value = arith::BitcastOp::create(rewriter, location, type, value);
    SmallVector<Value> values{value};
    if (containsLogic(type)) {
      Value fallback = arith::ConstantOp::create(
          rewriter, location, plane,
          rewriter.getIntegerAttr(plane,
                                  analysis::getDefaultUnknownMask(type)));
      values.push_back(arith::SelectOp::create(rewriter, location, window.valid,
                                               unknown, fallback));
    }
    rewriter.replaceOpWithMultiple(op,
                                   SmallVector<ValueRange>{ValueRange(values)});
    return success();
  }

private:
  const LocalStorageLowering &locals;
};

class LocalStoreConversion final
    : public OpConversionPattern<sim::SimRefStoreOp> {
public:
  LocalStoreConversion(const TypeConverter &converter, MLIRContext *context,
                       const LocalStorageLowering &locals)
      : OpConversionPattern(converter, context, PatternBenefit(2)),
        locals(locals) {}
  LogicalResult
  matchAndRewrite(sim::SimRefStoreOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    const auto *slot = locals.lookup(op.getReference());
    if (!slot)
      return failure();
    Location location = op.getLoc();
    Type type = op.getValue().getType();
    if (isa<LLVM::LLVMPointerType>(adaptor.getValue().front().getType())) {
      assert(locals.isWholeAllocation(op.getReference()));
      auto copy = [&](Value to, Value from) {
        copyMemoryPlane(rewriter, location, to, slot->nativeLayout, from,
                        locals.usesNativeMemoryLayout(op.getValue()), type,
                        locals);
      };
      copy(slot->value, adaptor.getValue().front());
      if (slot->unknown)
        copy(slot->unknown, adaptor.getValue()[1]);
      rewriter.eraseOp(op);
      return success();
    }
    storePlane(rewriter, location, slot->value, adaptor.getReference().front(),
               type, adaptor.getValue().front(), locals, slot->nativeLayout,
               locals.isNativeObject(op.getReference()),
               locals.getAccessUnit(op.getReference()));
    if (slot->unknown) {
      Value unknown =
          adaptor.getValue().size() == 2
              ? adaptor.getValue()[1]
              : llvmConstant(rewriter, location,
                             rewriter.getIntegerType(*nativeStateWidth(type)),
                             0);
      storePlane(rewriter, location, slot->unknown,
                 adaptor.getReference().front(), type, unknown, locals,
                 slot->nativeLayout, locals.isNativeObject(op.getReference()),
                 locals.getAccessUnit(op.getReference()));
    }
    rewriter.eraseOp(op);
    return success();
  }

private:
  const LocalStorageLowering &locals;
};
} // namespace

void populateLocalStorageConversionPatterns(
    RewritePatternSet &patterns, TypeConverter &converter,
    const LocalStorageLowering &locals) {
  patterns.add<LocalAllocConversion, LocalLoadConversion, LocalStoreConversion>(
      converter, patterns.getContext(), locals);
}
} // namespace obelisk::detail
