//===- SimulationManagedPreparation.cpp - Managed IR preparation -----===//

#include "SimulationToLLVMCoroutinePrivate.h"

#include "obelisk/Analysis/ManagedClassLayoutAnalysis.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Runtime/Runtime.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/PatternMatch.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/IR/DataLayout.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <tuple>

using namespace mlir;

namespace obelisk::detail {

namespace {

struct ManagedTraceLayout {
  uint64_t offset = 0;
  bool weak = false;
  uint32_t slotKind = OBELISK_RT_MANAGED_SLOT_CLASS;
};

struct ManagedClassLayout {
  struct Interface {
    sim::SimClassDeclOp declaration;
    SmallVector<uint32_t> methodSlots;
  };

  sim::SimClassDeclOp declaration;
  uint64_t size = sizeof(void *);
  uint32_t alignment = alignof(void *);
  SmallVector<ManagedTraceLayout> tracedFields;
  SmallVector<sim::SimClassMethodDeclOp> methods;
  SmallVector<Interface> interfaces;
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
  SmallVector<sim::SimClassMethodDeclOp> methods;
  module.walk([&](sim::SimClassDeclOp op) { classes.push_back(op); });
  module.walk([&](sim::SimClassMethodDeclOp op) { methods.push_back(op); });
  if (classes.empty())
    return success();

  llvm::StringMap<sim::SimClassDeclOp> classesByName;
  llvm::StringMap<SmallVector<sim::SimClassMethodDeclOp>> methodsByOwner;
  for (sim::SimClassDeclOp declaration : classes)
    classesByName[declaration.getSymName()] = declaration;
  for (sim::SimClassMethodDeclOp method : methods)
    methodsByOwner[method.getOwner()].push_back(method);

  FailureOr<analysis::ManagedClassLayoutAnalysis> analyzed =
      analysis::ManagedClassLayoutAnalysis::compute(
          classes.front()->getParentOfType<sim::SimDesignOp>(), dataLayout);
  if (failed(analyzed) ||
      failed(analysis::materializeManagedClassFieldOffsets(*analyzed)))
    return failure();
  for (const analysis::ManagedClassLayoutAnalysis::Class &shared :
       analyzed->classes) {
    sim::SimClassDeclOp declaration = shared.declaration;
    ManagedClassLayout layout;
    layout.declaration = declaration;
    if (auto baseName = declaration.getBase()) {
      const ManagedClassLayout &baseLayout = layouts[*baseName];
      layout.tracedFields = baseLayout.tracedFields;
      layout.methods = baseLayout.methods;
    }
    layout.size = shared.size;
    layout.alignment = shared.alignment;
    if (shared.weakReferentOffset)
      layout.tracedFields.push_back(
          {*shared.weakReferentOffset, true, OBELISK_RT_MANAGED_SLOT_CLASS});

    for (const analysis::ManagedClassLayoutAnalysis::Field &sharedField :
         shared.fields) {
      sim::SimClassFieldDeclOp field = sharedField.declaration;
      SmallVector<std::pair<uint64_t, uint32_t>, 2> traceSlots;
      if (failed(collectManagedTraceSlots(field.getType(), 0, traceSlots)) ||
          traceSlots.size() != sharedField.storage.managedRootOffsets.size())
        return field.emitError("class property has no typed managed layout");
      for (auto [rootOffset, slotKind] : traceSlots)
        layout.tracedFields.push_back(
            {sharedField.offset + rootOffset,
             isa<sim::ClassHandleType>(field.getType()) && field.getIsWeak(),
             slotKind});
    }

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
    layouts[declaration.getSymName()] = std::move(layout);
  }

  // Flatten each class's complete interface closure and map every stable
  // interface-method ordinal to the effective class vtable slot. Unresolved
  // entries are retained only for abstract descriptors and may be completed
  // by a concrete derived class.
  for (auto &entry : layouts) {
    ManagedClassLayout &layout = entry.second;
    SmallVector<sim::SimClassDeclOp> interfaces;
    llvm::SmallPtrSet<Operation *, 8> visited;
    std::function<LogicalResult(sim::SimClassDeclOp)> addInterface =
        [&](sim::SimClassDeclOp declaration) -> LogicalResult {
      if (!visited.insert(declaration).second)
        return success();
      if (!declaration.getIsInterface())
        return declaration.emitError("interface closure contains a class");
      if (ArrayAttr bases = declaration.getInterfacesAttr())
        for (Attribute attribute : bases) {
          auto reference = cast<FlatSymbolRefAttr>(attribute);
          auto found = classesByName.find(reference.getValue());
          if (found == classesByName.end() ||
              failed(addInterface(found->second)))
            return failure();
        }
      interfaces.push_back(declaration);
      return success();
    };
    for (sim::SimClassDeclOp current = layout.declaration; current;) {
      if (ArrayAttr declared = current.getInterfacesAttr())
        for (Attribute attribute : declared) {
          auto reference = cast<FlatSymbolRefAttr>(attribute);
          auto found = classesByName.find(reference.getValue());
          if (found == classesByName.end() ||
              failed(addInterface(found->second)))
            return current.emitError("managed interface descriptor is missing");
        }
      if (!current.getBaseAttr())
        break;
      auto found = classesByName.find(*current.getBase());
      if (found == classesByName.end())
        return current.emitError("managed base descriptor is missing");
      current = found->second;
    }
    llvm::sort(interfaces,
               [](sim::SimClassDeclOp left, sim::SimClassDeclOp right) {
                 return std::tuple(left.getId(), left.getSymName()) <
                        std::tuple(right.getId(), right.getSymName());
               });
    for (sim::SimClassDeclOp interface : interfaces) {
      ManagedClassLayout::Interface dispatch;
      dispatch.declaration = interface;
      SmallVector<sim::SimClassMethodDeclOp> interfaceMethods;
      for (sim::SimClassMethodDeclOp method :
           methodsByOwner[interface.getSymName()])
        if (method.getInterfaceOrdinalAttr())
          interfaceMethods.push_back(method);
      llvm::sort(interfaceMethods, [](sim::SimClassMethodDeclOp left,
                                      sim::SimClassMethodDeclOp right) {
        return *left.getInterfaceOrdinal() < *right.getInterfaceOrdinal();
      });
      dispatch.methodSlots.assign(interfaceMethods.size(), UINT32_MAX);
      for (sim::SimClassMethodDeclOp method : interfaceMethods) {
        uint64_t ordinal = *method.getInterfaceOrdinal();
        if (ordinal >= dispatch.methodSlots.size())
          return method.emitError("interface method ordinals are not dense");
        for (auto [slot, effective] : llvm::enumerate(layout.methods))
          if (effective.getSignatureId() == method.getSignatureId()) {
            // A pure declaration still occupies and shadows its effective
            // class slot, but it does not implement the interface method.
            if (!effective.getIsPure() && effective.getImplementationAttr()) {
              if (slot > UINT32_MAX)
                return effective.emitError(
                    "interface vtable slot is too large");
              dispatch.methodSlots[ordinal] = static_cast<uint32_t>(slot);
            }
            break;
          }
      }
      if (!layout.declaration.getIsAbstract() &&
          llvm::is_contained(dispatch.methodSlots, UINT32_MAX))
        return layout.declaration.emitError()
               << "concrete class does not implement interface "
               << interface.getSymName();
      layout.interfaces.push_back(std::move(dispatch));
    }
  }

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
  Type interfaceType =
      LLVM::LLVMStructType::getLiteral(context, {i64, pointer, i64});
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

      SmallVector<std::string> interfaceSlotNames;
      for (auto [index, interface] : llvm::enumerate(layout.interfaces)) {
        std::string slotsName = prefix + ".__obelisk_interface_" +
                                llvm::Twine(index).str() + "_slots";
        interfaceSlotNames.push_back(slotsName);
        Type slotsType =
            LLVM::LLVMArrayType::get(i32, interface.methodSlots.size());
        if (!interface.methodSlots.empty())
          makeConstantGlobal(
              module, location, slotsType, slotsName, LLVM::Linkage::Internal,
              4, [&](OpBuilder &builder) {
                Value array =
                    LLVM::ZeroOp::create(builder, location, slotsType);
                for (auto [ordinal, slot] :
                     llvm::enumerate(interface.methodSlots))
                  array = LLVM::InsertValueOp::create(
                      builder, location, array,
                      llvmConstant(builder, location, i32, slot),
                      ArrayRef<int64_t>{static_cast<int64_t>(ordinal)});
                return array;
              });
      }
      Type interfacesType =
          LLVM::LLVMArrayType::get(interfaceType, layout.interfaces.size());
      if (!layout.interfaces.empty())
        makeConstantGlobal(
            module, location, interfacesType, interfacesName,
            LLVM::Linkage::Internal, 8, [&](OpBuilder &builder) {
              Value array =
                  LLVM::ZeroOp::create(builder, location, interfacesType);
              for (auto [index, interface] :
                   llvm::enumerate(layout.interfaces)) {
                Value record =
                    LLVM::ZeroOp::create(builder, location, interfaceType);
                record =
                    insertValue(builder, location, record,
                                llvmConstant(builder, location, i64,
                                             interface.declaration.getId()),
                                0);
                if (!interface.methodSlots.empty())
                  record = insertValue(
                      builder, location, record,
                      LLVM::AddressOfOp::create(builder, location, pointer,
                                                interfaceSlotNames[index]),
                      1);
                record = insertValue(builder, location, record,
                                     llvmConstant(builder, location, i64,
                                                  interface.methodSlots.size()),
                                     2);
                array = LLVM::InsertValueOp::create(
                    builder, location, array, record,
                    ArrayRef<int64_t>{static_cast<int64_t>(index)});
              }
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
            if (!layout.interfaces.empty())
              descriptor =
                  insertValue(builder, location, descriptor,
                              LLVM::AddressOfOp::create(
                                  builder, location, pointer, interfacesName),
                              6);
            descriptor = insertValue(
                builder, location, descriptor,
                llvmConstant(builder, location, i64, layout.interfaces.size()),
                7);
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
      failed(sim::normalizeClassDirectCalls(module)))
    return failure();
  expandManagedSelectsToCFG(module);
  return success();
}

} // namespace obelisk::detail
