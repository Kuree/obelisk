//===- SimulationManagedPreparation.cpp - Managed IR preparation -----===//

#include "SimulationToLLVMCoroutinePrivate.h"

#include "obelisk/Analysis/SimulationStorageAnalysis.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Runtime/Runtime.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/PatternMatch.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/LLVMContext.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

using namespace mlir;

namespace obelisk::detail {

namespace {

struct ManagedFieldLayout {
  sim::SimClassFieldDeclOp declaration;
  uint64_t offset = 0;
  uint64_t planeSize = 0;
  uint32_t alignment = 1;
  bool fourState = false;
};

struct ManagedTraceLayout {
  uint64_t offset = 0;
  bool weak = false;
  uint32_t slotKind = OBELISK_RT_MANAGED_SLOT_CLASS;
};

struct ManagedClassLayout {
  sim::SimClassDeclOp declaration;
  uint64_t size = sizeof(void *);
  uint32_t alignment = alignof(void *);
  SmallVector<ManagedFieldLayout> fields;
  SmallVector<ManagedTraceLayout> tracedFields;
  SmallVector<sim::SimClassMethodDeclOp> methods;
};

LogicalResult collectManagedTraceSlots(
    Type type, uint64_t baseBitOffset,
    SmallVectorImpl<std::pair<uint64_t, uint32_t>> &slots) {
  if (sim::isManagedHandleType(type)) {
    if ((baseBitOffset & 7) != 0)
      return failure();
    uint32_t kind = OBELISK_RT_MANAGED_SLOT_CLASS;
    if (isa<sim::StringType>(type))
      kind = OBELISK_RT_MANAGED_SLOT_STRING;
    else if (isa<sim::DynamicArrayType, sim::QueueType, sim::AssocArrayType>(
                 type))
      kind = OBELISK_RT_MANAGED_SLOT_CONTAINER;
    slots.push_back({baseBitOffset / 8, kind});
    return success();
  }
  if (!sim::isAggregateType(type))
    return success();
  for (unsigned index = 0; index < sim::getAggregateNumElements(type);
       ++index) {
    auto child = sim::getAggregateProvenanceSubelement(type, index);
    if (!child || child->first > UINT64_MAX - baseBitOffset)
      return failure();
    if (failed(
            collectManagedTraceSlots(sim::getAggregateElementType(type, index),
                                     baseBitOffset + child->first, slots)))
      return failure();
  }
  return success();
}

LogicalResult
prepareManagedClassInventory(ModuleOp module,
                             const llvm::DataLayout &dataLayout,
                             llvm::StringMap<ManagedClassLayout> &layouts) {
  SmallVector<sim::SimClassDeclOp> classes;
  SmallVector<sim::SimClassFieldDeclOp> fields;
  SmallVector<sim::SimClassMethodDeclOp> methods;
  module.walk([&](sim::SimClassDeclOp op) { classes.push_back(op); });
  module.walk([&](sim::SimClassFieldDeclOp op) { fields.push_back(op); });
  module.walk([&](sim::SimClassMethodDeclOp op) { methods.push_back(op); });
  if (classes.empty())
    return success();

  llvm::StringMap<sim::SimClassDeclOp> classesByName;
  llvm::StringMap<SmallVector<sim::SimClassFieldDeclOp>> fieldsByOwner;
  llvm::StringMap<SmallVector<sim::SimClassMethodDeclOp>> methodsByOwner;
  for (sim::SimClassDeclOp declaration : classes)
    classesByName[declaration.getSymName()] = declaration;
  for (sim::SimClassFieldDeclOp field : fields)
    fieldsByOwner[field.getOwner()].push_back(field);
  for (sim::SimClassMethodDeclOp method : methods)
    methodsByOwner[method.getOwner()].push_back(method);
  for (auto &entry : fieldsByOwner)
    llvm::sort(entry.second, [](auto lhs, auto rhs) {
      return lhs.getOrdinal() < rhs.getOrdinal();
    });

  llvm::SmallPtrSet<Operation *, 8> active;
  std::function<LogicalResult(sim::SimClassDeclOp)> compute =
      [&](sim::SimClassDeclOp declaration) -> LogicalResult {
    if (layouts.count(declaration.getSymName()))
      return success();
    if (!active.insert(declaration).second)
      return declaration.emitError("managed class layout contains a cycle");

    ManagedClassLayout layout;
    layout.declaration = declaration;
    if (auto baseName = declaration.getBase()) {
      auto base = classesByName.find(*baseName);
      if (base == classesByName.end() || failed(compute(base->second)))
        return declaration.emitError(
            "managed class layout references an unknown base");
      const ManagedClassLayout &baseLayout = layouts[base->getKey()];
      layout.size = baseLayout.size;
      layout.alignment = baseLayout.alignment;
      layout.tracedFields = baseLayout.tracedFields;
      layout.methods = baseLayout.methods;
    }
    if (declaration.getWeakReferentAttr()) {
      uint64_t referentOffset;
      if (!alignUp(layout.size, alignof(void *), referentOffset) ||
          referentOffset >
              std::numeric_limits<uint64_t>::max() - sizeof(void *))
        return declaration.emitError("weak referent layout overflow");
      layout.size = referentOffset + sizeof(void *);
      layout.alignment = std::max<uint32_t>(layout.alignment, alignof(void *));
      layout.tracedFields.push_back(
          {referentOffset, true, OBELISK_RT_MANAGED_SLOT_CLASS});
    }

    llvm::DataLayout localDataLayout(dataLayout.getStringRepresentation());
    llvm::LLVMContext llvmContext;
    for (sim::SimClassFieldDeclOp field :
         fieldsByOwner[declaration.getSymName()]) {
      if (field.getIsStatic())
        continue;
      FailureOr<analysis::SimulationStorageProperties> storage =
          analysis::getSimulationStorageProperties(
              field.getType(), localDataLayout, llvmContext);
      if (failed(storage))
        return field.emitError(
            "class property has no fixed native managed layout");
      uint64_t offset;
      if (!alignUp(layout.size, storage->alignment, offset))
        return field.emitError("class property offset overflow");
      uint64_t planes = storage->fourState ? 2 : 1;
      if (storage->size >
          (std::numeric_limits<uint64_t>::max() - offset) / planes)
        return field.emitError("class property size overflow");
      layout.size = offset + storage->size * planes;
      layout.alignment =
          std::max<uint32_t>(layout.alignment, storage->alignment);
      ManagedFieldLayout fieldLayout{field, offset, storage->size,
                                     storage->alignment, storage->fourState};
      layout.fields.push_back(fieldLayout);
      SmallVector<std::pair<uint64_t, uint32_t>, 2> traceSlots;
      if (failed(collectManagedTraceSlots(field.getType(), 0, traceSlots)) ||
          traceSlots.size() != storage->managedRootOffsets.size())
        return field.emitError("class property has no typed managed layout");
      for (auto [rootOffset, slotKind] : traceSlots)
        layout.tracedFields.push_back(
            {offset + rootOffset,
             isa<sim::ClassHandleType>(field.getType()) && field.getIsWeak(),
             slotKind});
      if (auto existing = field->getAttrOfType<IntegerAttr>("offset");
          existing && existing.getValue().getZExtValue() != offset)
        return field.emitError("native and bytecode class layouts disagree");
      field->setAttr(
          "offset",
          IntegerAttr::get(IntegerType::get(module.getContext(), 64), offset));
    }
    uint64_t alignedSize;
    if (!alignUp(layout.size, layout.alignment, alignedSize))
      return declaration.emitError("class instance size overflow");
    layout.size = alignedSize;

    for (sim::SimClassMethodDeclOp method :
         methodsByOwner[declaration.getSymName()]) {
      if (!method.getSlot())
        continue;
      if (declaration.getIsInterface())
        continue;
      uint64_t slot = *method.getSlot();
      if (slot >= std::numeric_limits<size_t>::max())
        return method.emitError("virtual method slot exceeds host limits");
      if (layout.methods.size() <= slot)
        layout.methods.resize(static_cast<size_t>(slot) + 1);
      layout.methods[slot] = method;
    }
    for (auto [slot, method] : llvm::enumerate(layout.methods))
      if (!method)
        return declaration.emitError()
               << "virtual method table has an empty slot " << slot;

    declaration->setAttr(
        "obelisk.native.instance_size",
        IntegerAttr::get(IntegerType::get(module.getContext(), 64),
                         layout.size));
    declaration->setAttr(
        "obelisk.native.instance_alignment",
        IntegerAttr::get(IntegerType::get(module.getContext(), 32),
                         layout.alignment));
    active.erase(declaration);
    layouts[declaration.getSymName()] = std::move(layout);
    return success();
  };
  for (sim::SimClassDeclOp declaration : classes)
    if (failed(compute(declaration)))
      return failure();

  MLIRContext *context = module.getContext();
  Type pointer = LLVM::LLVMPointerType::get(context);
  Type i32 = IntegerType::get(context, 32);
  Type i64 = IntegerType::get(context, 64);
  Type traceEntryType = LLVM::LLVMStructType::getLiteral(
      context, {i64, i64, i64, i32, i32, pointer});
  Type traceLayoutType = LLVM::LLVMStructType::getLiteral(
      context, {i32, i32, i64, i64, pointer, i64});
  Type methodType = LLVM::LLVMStructType::getLiteral(
      context, {i64, i32, i32, pointer, pointer});
  Type classType = LLVM::LLVMStructType::getLiteral(
      context, {i32, i32, i64, i64, i64, pointer, pointer, i64, pointer,
                pointer, i64, pointer, i64});

  // Base descriptors must exist before derived initializers take their
  // addresses. Repeatedly materialize classes whose base is ready.
  llvm::StringSet<> materialized;
  while (materialized.size() != classes.size()) {
    bool progress = false;
    for (sim::SimClassDeclOp declaration : classes) {
      if (materialized.count(declaration.getSymName()))
        continue;
      if (auto base = declaration.getBase(); base && !materialized.count(*base))
        continue;
      progress = true;
      ManagedClassLayout &layout = layouts[declaration.getSymName()];
      std::string prefix = declaration.getSymName().str();
      std::string entriesName = prefix + ".__obelisk_trace_entries";
      std::string traceName = prefix + ".__obelisk_trace_layout";
      std::string methodsName = prefix + ".__obelisk_methods";
      std::string interfacesName = prefix + ".__obelisk_interfaces";
      std::string debugName = prefix + ".__obelisk_debug_name";
      std::string descriptorName = managedClassDescriptorName(
          FlatSymbolRefAttr::get(context, declaration.getSymName()));
      Location location = declaration.getLoc();

      Type entriesType =
          LLVM::LLVMArrayType::get(traceEntryType, layout.tracedFields.size());
      if (!layout.tracedFields.empty())
        makeConstantGlobal(
            module, location, entriesType, entriesName, LLVM::Linkage::Internal,
            8, [&](OpBuilder &builder) {
              Value array =
                  LLVM::ZeroOp::create(builder, location, entriesType);
              for (auto [index, field] : llvm::enumerate(layout.tracedFields)) {
                Value entry =
                    LLVM::ZeroOp::create(builder, location, traceEntryType);
                entry = insertValue(
                    builder, location, entry,
                    llvmConstant(builder, location, i64, field.offset), 0);
                entry = insertValue(builder, location, entry,
                                    llvmConstant(builder, location, i64, 1), 2);
                entry = insertValue(builder, location, entry,
                                    llvmConstant(builder, location, i32,
                                                 field.weak
                                                     ? OBELISK_RT_TRACE_WEAK
                                                     : OBELISK_RT_TRACE_STRONG),
                                    3);
                entry = insertValue(
                    builder, location, entry,
                    llvmConstant(builder, location, i32, field.slotKind), 4);
                array = LLVM::InsertValueOp::create(
                    builder, location, array, entry,
                    ArrayRef<int64_t>{static_cast<int64_t>(index)});
              }
              return array;
            });
      makeConstantGlobal(
          module, location, traceLayoutType, traceName, LLVM::Linkage::Internal,
          8, [&](OpBuilder &builder) {
            Value trace =
                LLVM::ZeroOp::create(builder, location, traceLayoutType);
            trace = insertValue(
                builder, location, trace,
                llvmConstant(builder, location, i32, OBELISK_RT_VERSION), 0);
            trace = insertValue(
                builder, location, trace,
                llvmConstant(builder, location, i64, layout.size), 2);
            trace = insertValue(
                builder, location, trace,
                llvmConstant(builder, location, i64, layout.alignment), 3);
            if (!layout.tracedFields.empty())
              trace = insertValue(builder, location, trace,
                                  LLVM::AddressOfOp::create(
                                      builder, location, pointer, entriesName),
                                  4);
            trace = insertValue(builder, location, trace,
                                llvmConstant(builder, location, i64,
                                             layout.tracedFields.size()),
                                5);
            return trace;
          });

      Type methodsType =
          LLVM::LLVMArrayType::get(methodType, layout.methods.size());
      if (!layout.methods.empty())
        makeConstantGlobal(
            module, location, methodsType, methodsName, LLVM::Linkage::Internal,
            8, [&](OpBuilder &builder) {
              Value array =
                  LLVM::ZeroOp::create(builder, location, methodsType);
              for (auto indexedMethod : llvm::enumerate(layout.methods)) {
                auto index = indexedMethod.index();
                auto method = indexedMethod.value();
                Value entry =
                    LLVM::ZeroOp::create(builder, location, methodType);
                entry = insertValue(builder, location, entry,
                                    llvmConstant(builder, location, i64,
                                                 *method.getSignatureId()),
                                    0);
                uint32_t flags =
                    method.getIsTask() ? OBELISK_RT_METHOD_TASK : 0;
                if (method.getIsPure())
                  flags |= OBELISK_RT_METHOD_PURE;
                entry =
                    insertValue(builder, location, entry,
                                llvmConstant(builder, location, i32, flags), 1);
                entry = insertValue(
                    builder, location, entry,
                    llvmConstant(
                        builder, location, i32,
                        [&] {
                          if (method.getImplementation()) {
                            if (auto function =
                                    SymbolTable::lookupNearestSymbolFrom<
                                        sim::SimFuncOp>(
                                        method, method.getImplementationAttr()))
                              if (auto index =
                                      function->getAttrOfType<IntegerAttr>(
                                          "obelisk.bytecode.function"))
                                return static_cast<uint32_t>(
                                    index.getValue().getZExtValue());
                          }
                          return uint32_t{OBELISK_RT_METHOD_NO_BYTECODE};
                        }()),
                    2);
                if (!method.getIsPure())
                  entry = insertValue(
                      builder, location, entry,
                      LLVM::AddressOfOp::create(
                          builder, location, pointer,
                          managedMethodThunkName(method.getSymName())),
                      3);
                array = LLVM::InsertValueOp::create(
                    builder, location, array, entry,
                    ArrayRef<int64_t>{static_cast<int64_t>(index)});
              }
              return array;
            });

      SmallVector<uint64_t> interfaceIDs;
      if (ArrayAttr interfaces = declaration.getInterfacesAttr())
        for (Attribute attribute : interfaces) {
          auto reference = cast<FlatSymbolRefAttr>(attribute);
          auto found = classesByName.find(reference.getValue());
          if (found == classesByName.end())
            return declaration.emitError(
                "managed interface descriptor is missing");
          interfaceIDs.push_back(found->second.getId());
        }
      Type interfacesType = LLVM::LLVMArrayType::get(i64, interfaceIDs.size());
      if (!interfaceIDs.empty())
        makeConstantGlobal(
            module, location, interfacesType, interfacesName,
            LLVM::Linkage::Internal, 8, [&](OpBuilder &builder) {
              Value array =
                  LLVM::ZeroOp::create(builder, location, interfacesType);
              for (auto [index, id] : llvm::enumerate(interfaceIDs))
                array = LLVM::InsertValueOp::create(
                    builder, location, array,
                    llvmConstant(builder, location, i64, id),
                    ArrayRef<int64_t>{static_cast<int64_t>(index)});
              return array;
            });

      StringRef debug = declaration.getDebugNameAttr()
                            ? declaration.getDebugNameAttr().getValue()
                            : StringRef{};
      if (!debug.empty())
        makeByteArrayGlobal(module, location, debugName, debug);
      makeConstantGlobal(
          module, location, classType, descriptorName, LLVM::Linkage::Internal,
          8, [&](OpBuilder &builder) {
            Value descriptor =
                LLVM::ZeroOp::create(builder, location, classType);
            descriptor = insertValue(
                builder, location, descriptor,
                llvmConstant(builder, location, i32, OBELISK_RT_VERSION), 0);
            uint32_t flags =
                declaration.getIsAbstract() ? OBELISK_RT_CLASS_ABSTRACT : 0;
            if (declaration.getIsInterface())
              flags |= OBELISK_RT_CLASS_INTERFACE;
            if (declaration.getIsFinal())
              flags |= OBELISK_RT_CLASS_FINAL;
            if (declaration.getWeakReferentAttr())
              flags |= OBELISK_RT_CLASS_WEAK_WRAPPER;
            descriptor =
                insertValue(builder, location, descriptor,
                            llvmConstant(builder, location, i32, flags), 1);
            descriptor = insertValue(
                builder, location, descriptor,
                llvmConstant(builder, location, i64, declaration.getId()), 2);
            descriptor = insertValue(
                builder, location, descriptor,
                llvmConstant(builder, location, i64, layout.size), 3);
            descriptor = insertValue(
                builder, location, descriptor,
                llvmConstant(builder, location, i64, layout.alignment), 4);
            if (auto base = declaration.getBase())
              descriptor =
                  insertValue(builder, location, descriptor,
                              LLVM::AddressOfOp::create(
                                  builder, location, pointer,
                                  managedClassDescriptorName(
                                      FlatSymbolRefAttr::get(context, *base))),
                              5);
            if (!interfaceIDs.empty())
              descriptor =
                  insertValue(builder, location, descriptor,
                              LLVM::AddressOfOp::create(
                                  builder, location, pointer, interfacesName),
                              6);
            descriptor = insertValue(
                builder, location, descriptor,
                llvmConstant(builder, location, i64, interfaceIDs.size()), 7);
            descriptor = insertValue(builder, location, descriptor,
                                     LLVM::AddressOfOp::create(
                                         builder, location, pointer, traceName),
                                     8);
            if (!layout.methods.empty())
              descriptor =
                  insertValue(builder, location, descriptor,
                              LLVM::AddressOfOp::create(builder, location,
                                                        pointer, methodsName),
                              9);
            descriptor = insertValue(
                builder, location, descriptor,
                llvmConstant(builder, location, i64, layout.methods.size()),
                10);
            if (!debug.empty())
              descriptor =
                  insertValue(builder, location, descriptor,
                              LLVM::AddressOfOp::create(builder, location,
                                                        pointer, debugName),
                              11);
            descriptor = insertValue(
                builder, location, descriptor,
                llvmConstant(builder, location, i64, debug.size()), 12);
            return descriptor;
          });
      materialized.insert(declaration.getSymName());
    }
    if (!progress)
      return module.emitError(
          "could not topologically materialize managed class descriptors");
  }
  return success();
}

LogicalResult normalizeManagedDirectCalls(ModuleOp module) {
  SmallVector<sim::SimClassDirectCallOp> calls;
  module.walk([&](sim::SimClassDirectCallOp call) { calls.push_back(call); });
  IRRewriter rewriter(module.getContext());
  for (sim::SimClassDirectCallOp call : calls) {
    sim::SimFuncOp function = call->getParentOfType<sim::SimFuncOp>();
    if (!function || function.getBody().empty() ||
        function.getBody().front().getNumArguments() == 0 ||
        !isa<sim::ContextType>(
            function.getBody().front().getArgument(0).getType()))
      return call.emitError(
          "managed direct call has no dominating simulation context");
    SmallVector<Value> operands{function.getBody().front().getArgument(0),
                                call.getReceiver()};
    llvm::append_range(operands, call.getArguments());
    rewriter.setInsertionPoint(call);
    auto replacement = sim::SimCallOp::create(
        rewriter, call.getLoc(), call.getResultTypes(), call.getCalleeAttr(),
        operands, ArrayAttr{}, ArrayAttr{});
    rewriter.replaceOp(call, replacement.getResults());
  }
  return success();
}

/// Arithmetic selects only support arithmetic and shaped result types. The
/// simulation dialect nevertheless permits class handles to flow through SSA
/// merges, and a conditional class assignment can therefore be represented as
/// an arith.select before native type conversion. Do not rely on the arithmetic
/// folder seeing that foreign type: spell these selects as ordinary CFG joins
/// while their managed type and root liveness are still visible.
void expandManagedSelectsToCFG(ModuleOp module) {
  SmallVector<arith::SelectOp> selects;
  module.walk([&](arith::SelectOp select) {
    if (sim::isManagedHandleType(select.getType()))
      selects.push_back(select);
  });

  IRRewriter rewriter(module.getContext());
  for (arith::SelectOp select : selects) {
    Block *head = select->getBlock();
    Block *continuation = rewriter.splitBlock(head, select->getIterator());
    BlockArgument merged =
        continuation->addArgument(select.getType(), select.getLoc());
    select.getResult().replaceAllUsesWith(merged);
    Value condition = select.getCondition();
    Value trueValue = select.getTrueValue();
    Value falseValue = select.getFalseValue();
    rewriter.eraseOp(select);

    rewriter.setInsertionPointToEnd(head);
    cf::CondBranchOp::create(rewriter, merged.getLoc(), condition, continuation,
                             ValueRange{trueValue}, continuation,
                             ValueRange{falseValue});
  }
}

} // namespace

LogicalResult prepareManagedLowering(ModuleOp module,
                                     const llvm::DataLayout &dataLayout) {
  llvm::StringMap<ManagedClassLayout> layouts;
  if (failed(prepareManagedClassInventory(module, dataLayout, layouts)) ||
      failed(normalizeManagedDirectCalls(module)))
    return failure();
  expandManagedSelectsToCFG(module);
  return success();
}

} // namespace obelisk::detail
