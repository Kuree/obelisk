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
  design.walk(
      [&](sim::SimClassDeclOp declaration) { classes.push_back(declaration); });
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

  // SimDesignOp verification rejects missing classes and inheritance cycles
  // before this analysis runs. Materialize the immutable hierarchy and
  // effective vtables once; UVM otherwise rebuilds the same closure at
  // thousands of virtual call sites.
  ancestors.assign(classes.size(), llvm::BitVector(classes.size()));
  compatibleConcrete.resize(classes.size());
  effectiveMethods.resize(classes.size());
  SmallVector<uint8_t> hierarchyState(classes.size());
  struct HierarchyFrame {
    size_t index;
    bool exit;
  };
  SmallVector<HierarchyFrame, 32> hierarchyStack;
  for (size_t root = 0; root != classes.size(); ++root) {
    if (hierarchyState[root] == 2)
      continue;
    hierarchyStack.push_back({root, false});
    while (!hierarchyStack.empty()) {
      HierarchyFrame frame = hierarchyStack.pop_back_val();
      size_t index = frame.index;
      if (frame.exit) {
        sim::SimClassDeclOp declaration = classes[index];
        ancestors[index].set(index);
        auto inherit = [&](StringRef name) {
          auto found = classIndices.find(name);
          if (found != classIndices.end() && hierarchyState[found->second] == 2)
            ancestors[index] |= ancestors[found->second];
        };
        if (auto base = declaration.getBase()) {
          inherit(*base);
          auto found = classIndices.find(*base);
          if (found != classIndices.end() && hierarchyState[found->second] == 2)
            effectiveMethods[index] = effectiveMethods[found->second];
        }
        if (ArrayAttr interfaces = declaration.getInterfacesAttr())
          for (Attribute attribute : interfaces)
            if (auto reference = dyn_cast<FlatSymbolRefAttr>(attribute))
              inherit(reference.getValue());
        if (auto found = methods.find(declaration.getSymName());
            found != methods.end())
          for (sim::SimClassMethodDeclOp method : found->second)
            if (auto slot = method.getSlot())
              effectiveMethods[index][*slot] = method;
        hierarchyState[index] = 2;
        continue;
      }
      if (hierarchyState[index] == 2)
        continue;
      // Invalid cyclic IR is diagnosed by SimDesignOp verification. Keep this
      // guard defensive for analysis-only clients that disable verification.
      if (hierarchyState[index] == 1)
        continue;
      hierarchyState[index] = 1;
      hierarchyStack.push_back({index, true});
      SmallVector<size_t, 4> dependencies;
      sim::SimClassDeclOp declaration = classes[index];
      if (auto base = declaration.getBase())
        if (auto found = classIndices.find(*base);
            found != classIndices.end() && hierarchyState[found->second] == 0)
          dependencies.push_back(found->second);
      if (ArrayAttr interfaces = declaration.getInterfacesAttr())
        for (Attribute attribute : interfaces)
          if (auto reference = dyn_cast<FlatSymbolRefAttr>(attribute))
            if (auto found = classIndices.find(reference.getValue());
                found != classIndices.end() &&
                hierarchyState[found->second] == 0)
              dependencies.push_back(found->second);
      for (size_t dependency : llvm::reverse(dependencies))
        hierarchyStack.push_back({dependency, false});
    }
  }
  for (auto [candidateIndex, candidate] : llvm::enumerate(classes)) {
    if (candidate.getIsAbstract() || candidate.getIsInterface())
      continue;
    for (int ancestor = ancestors[candidateIndex].find_first(); ancestor >= 0;
         ancestor = ancestors[candidateIndex].find_next(ancestor))
      compatibleConcrete[ancestor].push_back(candidate);
  }
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
  auto dynamic = classIndices.find(dynamicClass.getSymName());
  auto targetIndex = classIndices.find(target.getSymName());
  return dynamic != classIndices.end() && targetIndex != classIndices.end() &&
         ancestors[dynamic->second].test(targetIndex->second);
}

sim::SimClassMethodDeclOp
ClassDispatchAnalysis::resolve(sim::SimClassDeclOp dynamicClass, uint64_t slot,
                               uint64_t signatureId) const {
  if (!dynamicClass)
    return {};
  auto dynamic = classIndices.find(dynamicClass.getSymName());
  if (dynamic == classIndices.end())
    return {};
  const auto &vtable = effectiveMethods[dynamic->second];

  if (slot != getInterfaceDispatchSlot()) {
    auto found = vtable.find(slot);
    return found == vtable.end() ? sim::SimClassMethodDeclOp{} : found->second;
  }

  uint64_t selectedSlot = std::numeric_limits<uint64_t>::max();
  sim::SimClassMethodDeclOp selected;
  for (auto [effectiveSlot, method] : vtable)
    if (effectiveSlot < selectedSlot && method.getSignatureIdAttr() &&
        method.getSignatureId() == signatureId) {
      selectedSlot = effectiveSlot;
      selected = method;
    }
  return selected;
}

ArrayRef<sim::SimClassDeclOp>
ClassDispatchAnalysis::compatibleConcreteClasses(
    sim::SimClassDeclOp staticClass) const {
  if (!staticClass)
    return {};
  auto found = classIndices.find(staticClass.getSymName());
  return found == classIndices.end()
             ? ArrayRef<sim::SimClassDeclOp>{}
             : ArrayRef<sim::SimClassDeclOp>(compatibleConcrete[found->second]);
}

SmallVector<sim::SimClassMethodDeclOp>
ClassDispatchAnalysis::compatibleImplementations(
    sim::SimClassDeclOp staticClass, uint64_t slot, uint64_t signatureId,
    bool isTask) const {
  SmallVector<sim::SimClassMethodDeclOp> result;
  llvm::StringSet<> seen;
  for (sim::SimClassDeclOp candidate : compatibleConcreteClasses(staticClass)) {
    sim::SimClassMethodDeclOp method = resolve(candidate, slot, signatureId);
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
