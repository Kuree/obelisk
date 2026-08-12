//===- SimulationStorageAnalysis.cpp - Canonical storage facts ----------===//

#include "obelisk/Analysis/SimulationStorageAnalysis.h"

#include "obelisk/Analysis/SimulationAnalysis.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/LLVMContext.h"

using namespace mlir;

namespace obelisk::analysis {
FailureOr<SimulationStorageProperties>
getSimulationStorageProperties(Type type, const llvm::DataLayout &dataLayout,
                               llvm::LLVMContext &llvmContext) {
  llvm::Type *llvmType = nullptr;
  bool fourState = false;
  if (auto logic = dyn_cast<sim::LogicType>(type)) {
    llvmType = llvm::IntegerType::get(llvmContext, logic.getWidth());
    fourState = true;
  } else if (auto integer = dyn_cast<IntegerType>(type)) {
    llvmType = llvm::IntegerType::get(llvmContext, integer.getWidth());
  } else if (type.isF64()) {
    llvmType = llvm::Type::getDoubleTy(llvmContext);
  } else if (type.isF32()) {
    llvmType = llvm::Type::getFloatTy(llvmContext);
  } else if (isa<sim::TimeType>(type)) {
    llvmType = llvm::Type::getInt64Ty(llvmContext);
  } else if (sim::isManagedHandleType(type)) {
    llvmType = llvm::PointerType::get(llvmContext, 0);
  } else if (isa<sim::ManagedRefType>(type)) {
    // A managed reference is physically {object, byte offset}. Each word has
    // pointer size and alignment; process-frame layout describes the object
    // word separately as a precise managed root.
    llvmType = llvm::PointerType::get(llvmContext, 0);
  } else if (isa<sim::ArgumentRefType>(type)) {
    // {owner root, ordinary handle or managed byte offset, managed tag}.
    llvmType = llvm::IntegerType::get(llvmContext, 192);
  } else if (isa<sim::RefType, sim::NetType, sim::DriverType, sim::EventType,
                 sim::ProcessType, sim::ControlType, sim::CovergroupHandleType,
                 sim::VirtualInterfaceType>(type)) {
    // Simulation handles remain frame-relative stable IDs. They must never
    // become host pointers in the canonical frame shared with bytecode.
    llvmType = llvm::Type::getInt64Ty(llvmContext);
  } else if (sim::isAggregateType(type)) {
    std::optional<unsigned> width = getSimulationStorageBitWidth(type);
    if (!width)
      return failure();
    llvmType = llvm::IntegerType::get(llvmContext, *width);
    fourState = containsFourStateLogic(type);
  } else {
    return failure();
  }

  // Canonical class and process storage consists of explicitly aligned fields,
  // not LLVM arrays. Reserve the bytes transferred by a load or store and use
  // the target ABI alignment when placing the next field. This matters for
  // non-power-of-two integers such as flattened aggregate payloads: their LLVM
  // allocation stride may include tail padding that is not part of the value.
  llvm::TypeSize typeSize = dataLayout.getTypeStoreSize(llvmType);
  if (typeSize.isScalable() || typeSize.getFixedValue() == 0)
    return failure();

  llvm::Type *pointerType = llvm::PointerType::get(llvmContext, 0);
  llvm::TypeSize pointerSize = dataLayout.getTypeAllocSize(pointerType);
  if (pointerSize.isScalable() || pointerSize.getFixedValue() == 0)
    return failure();
  uint64_t managedRootSize = pointerSize.getFixedValue();
  uint32_t managedRootAlignment =
      dataLayout.getABITypeAlign(pointerType).value();
  SmallVector<sim::ManagedHandleSlot, 2> managedRootSlots;
  SmallVector<uint64_t, 2> managedRootOffsets;
  if (isa<sim::ManagedRefType>(type)) {
    managedRootSlots.push_back(
        {0, static_cast<uint32_t>(sim::ManagedHandleKind::Class), false});
  } else if (isa<sim::ArgumentRefType>(type)) {
    managedRootSlots.push_back(
        {0,
         static_cast<uint32_t>(sim::ManagedHandleKind::Class) |
             static_cast<uint32_t>(sim::ManagedHandleKind::ReferencePath),
         false});
  } else {
    SmallVector<sim::ManagedHandleSlot, 2> bitSlots;
    if (!sim::getManagedHandleSlots(type, bitSlots))
      return failure();
    for (sim::ManagedHandleSlot slot : bitSlots) {
      if ((slot.bitOffset & 7) != 0)
        return failure();
      uint64_t byteOffset = slot.bitOffset / 8;
      if (byteOffset > typeSize.getFixedValue() ||
          managedRootSize > typeSize.getFixedValue() - byteOffset)
        return failure();
      slot.bitOffset = byteOffset;
      managedRootSlots.push_back(slot);
    }
  }
  for (const sim::ManagedHandleSlot &slot : managedRootSlots)
    managedRootOffsets.push_back(slot.bitOffset);

  return SimulationStorageProperties{
      typeSize.getFixedValue(),
      static_cast<uint32_t>(dataLayout.getABITypeAlign(llvmType).value()),
      fourState,
      isa<sim::ManagedRefType>(type),
      std::move(managedRootSlots),
      std::move(managedRootOffsets),
      managedRootSize,
      managedRootAlignment};
}

size_t
getSimulationPhysicalStorageCount(const SimulationStorageProperties &storage) {
  return storage.fourState || storage.managedReference ? 2 : 1;
}

} // namespace obelisk::analysis
