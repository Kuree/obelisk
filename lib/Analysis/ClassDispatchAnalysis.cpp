//===- ClassDispatchAnalysis.cpp - Managed class dispatch ---------------===//

#include "obelisk/Analysis/ClassDispatchAnalysis.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringSet.h"

#include <limits>
#include <tuple>
#include <utility>

using namespace mlir;

namespace obelisk::analysis {

ClassDispatchAnalysis::ClassDispatchAnalysis(sim::SimDesignOp design) {
  design.walk([&](sim::SimClassDeclOp declaration) {
    classes.push_back(declaration);
  });
  llvm::sort(classes, [](auto lhs, auto rhs) {
    return std::make_pair(lhs.getId(), lhs.getSymName()) <
           std::make_pair(rhs.getId(), rhs.getSymName());
  });
  for (auto [index, declaration] : llvm::enumerate(classes))
    classIndices.try_emplace(declaration.getSymName(), index);

  design.walk([&](sim::SimClassMethodDeclOp method) {
    methods[method.getOwner()].push_back(method);
  });
  for (auto &entry : methods)
    llvm::sort(entry.second, [](auto lhs, auto rhs) {
      return std::make_tuple(lhs.getSlot().value_or(0),
                             lhs.getSignatureId().value_or(0),
                             lhs.getSymName()) <
             std::make_tuple(rhs.getSlot().value_or(0),
                             rhs.getSignatureId().value_or(0),
                             rhs.getSymName());
    });
}

sim::SimClassDeclOp ClassDispatchAnalysis::lookup(StringRef name) const {
  auto found = classIndices.find(name);
  return found == classIndices.end() ? sim::SimClassDeclOp{}
                                     : classes[found->second];
}

sim::SimClassDeclOp
ClassDispatchAnalysis::lookup(sim::ClassHandleType type) const {
  return lookup(type.getClassName().getRootReference());
}

bool ClassDispatchAnalysis::isInstanceOf(sim::SimClassDeclOp dynamicClass,
                                         sim::SimClassDeclOp target) const {
  if (!dynamicClass || !target)
    return false;
  DenseSet<Operation *> visited;
  for (sim::SimClassDeclOp current = dynamicClass;
       current && visited.insert(current).second;
       current = current.getBase() ? lookup(*current.getBase())
                                   : sim::SimClassDeclOp{}) {
    if (current == target)
      return true;
    if (!target.getIsInterface())
      continue;
    if (ArrayAttr interfaces = current.getInterfacesAttr())
      for (Attribute attribute : interfaces)
        if (auto reference = dyn_cast<FlatSymbolRefAttr>(attribute);
            reference && reference.getValue() == target.getSymName())
          return true;
  }
  return false;
}

sim::SimClassMethodDeclOp
ClassDispatchAnalysis::resolve(sim::SimClassDeclOp dynamicClass, uint64_t slot,
                               uint64_t signatureId) const {
  DenseSet<Operation *> visited;
  DenseMap<uint64_t, sim::SimClassMethodDeclOp> effectiveMethods;
  for (sim::SimClassDeclOp current = dynamicClass;
       current && visited.insert(current).second;
       current = current.getBase() ? lookup(*current.getBase())
                                   : sim::SimClassDeclOp{}) {
    auto found = methods.find(current.getSymName());
    if (found == methods.end())
      continue;
    for (sim::SimClassMethodDeclOp method : found->second)
      if (method.getSlotAttr())
        effectiveMethods.try_emplace(*method.getSlot(), method);
  }

  if (slot != getInterfaceDispatchSlot()) {
    auto found = effectiveMethods.find(slot);
    return found == effectiveMethods.end() ? sim::SimClassMethodDeclOp{}
                                           : found->second;
  }

  uint64_t selectedSlot = std::numeric_limits<uint64_t>::max();
  sim::SimClassMethodDeclOp selected;
  for (auto [effectiveSlot, method] : effectiveMethods)
    if (effectiveSlot < selectedSlot && method.getSignatureIdAttr() &&
        method.getSignatureId() == signatureId) {
      selectedSlot = effectiveSlot;
      selected = method;
    }
  return selected;
}

SmallVector<sim::SimClassDeclOp>
ClassDispatchAnalysis::compatibleConcreteClasses(
    sim::SimClassDeclOp staticClass) const {
  SmallVector<sim::SimClassDeclOp> result;
  for (sim::SimClassDeclOp candidate : classes)
    if (!candidate.getIsAbstract() && !candidate.getIsInterface() &&
        isInstanceOf(candidate, staticClass))
      result.push_back(candidate);
  return result;
}

SmallVector<sim::SimClassMethodDeclOp>
ClassDispatchAnalysis::compatibleImplementations(
    sim::SimClassDeclOp staticClass, uint64_t slot, uint64_t signatureId,
    bool isTask) const {
  SmallVector<sim::SimClassMethodDeclOp> result;
  llvm::StringSet<> seen;
  for (sim::SimClassDeclOp candidate :
       compatibleConcreteClasses(staticClass)) {
    sim::SimClassMethodDeclOp method =
        resolve(candidate, slot, signatureId);
    if (!method || method.getIsPure() || method.getIsTask() != isTask ||
        !method.getImplementationAttr() || !method.getSignatureIdAttr() ||
        method.getSignatureId() != signatureId ||
        !seen.insert(*method.getImplementation()).second)
      continue;
    result.push_back(method);
  }
  return result;
}

} // namespace obelisk::analysis
