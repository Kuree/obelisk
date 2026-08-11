//===- DevirtualizeClassCalls.cpp - Resolve exact class dispatches -------===//

#include "obelisk/Conversion/ObeliskToSimulation.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/SymbolTable.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"

#include <cstdint>
#include <limits>

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_OBELISKSIMDEVIRTUALIZECLASSCALLSPASS
#include "obelisk/Conversion/Passes.h.inc"

namespace {

namespace sim = ::obelisk::sim;

constexpr uint64_t interfaceDispatchSlot =
    std::numeric_limits<uint32_t>::max();

class ClassDispatchInventory {
public:
  explicit ClassDispatchInventory(sim::SimDesignOp design) {
    design.walk([&](sim::SimClassDeclOp declaration) {
      classes.try_emplace(declaration.getSymName(), declaration);
    });
    design.walk([&](sim::SimClassMethodDeclOp method) {
      methods[method.getOwner()].push_back(method);
    });
  }

  sim::SimClassDeclOp lookup(StringRef name) const {
    auto found = classes.find(name);
    return found == classes.end() ? sim::SimClassDeclOp{} : found->second;
  }

  sim::SimClassDeclOp lookup(sim::ClassHandleType type) const {
    return lookup(type.getClassName().getRootReference());
  }

  bool isInstanceOf(sim::SimClassDeclOp dynamicClass,
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
  resolve(sim::SimClassDeclOp dynamicClass,
          sim::SimClassVirtualCallOp call) const {
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

    if (call.getSlot() != interfaceDispatchSlot) {
      auto found = effectiveMethods.find(call.getSlot());
      return found == effectiveMethods.end() ? sim::SimClassMethodDeclOp{}
                                             : found->second;
    }

    uint64_t selectedSlot = std::numeric_limits<uint64_t>::max();
    sim::SimClassMethodDeclOp selected;
    for (auto [slot, method] : effectiveMethods)
      if (slot < selectedSlot && method.getSignatureIdAttr() &&
          method.getSignatureId() == call.getSignatureId()) {
        selectedSlot = slot;
        selected = method;
      }
    return selected;
  }

private:
  llvm::StringMap<sim::SimClassDeclOp> classes;
  llvm::StringMap<SmallVector<sim::SimClassMethodDeclOp>> methods;
};

class ExactClassResolver {
public:
  explicit ExactClassResolver(const ClassDispatchInventory &inventory)
      : inventory(inventory) {}

  sim::SimClassDeclOp resolve(Value value) {
    if (auto found = exact.find(value); found != exact.end())
      return found->second;
    if (unknown.contains(value) || !visiting.insert(value).second)
      return {};

    sim::SimClassDeclOp result;
    if (auto allocation = value.getDefiningOp<sim::SimClassAllocOp>()) {
      result = inventory.lookup(
          cast<sim::ClassHandleType>(allocation.getResult().getType()));
    } else if (auto copy = value.getDefiningOp<sim::SimClassCopyOp>()) {
      result = resolve(copy.getSource());
    } else if (auto castOp = value.getDefiningOp<sim::SimClassCastOp>()) {
      sim::SimClassDeclOp dynamicClass = resolve(castOp.getObject());
      sim::SimClassDeclOp target = inventory.lookup(
          cast<sim::ClassHandleType>(castOp.getResult().getType()));
      if (inventory.isInstanceOf(dynamicClass, target))
        result = dynamicClass;
    }

    visiting.erase(value);
    if (result)
      exact.try_emplace(value, result);
    else
      unknown.insert(value);
    return result;
  }

private:
  const ClassDispatchInventory &inventory;
  DenseMap<Value, sim::SimClassDeclOp> exact;
  DenseSet<Value> unknown;
  DenseSet<Value> visiting;
};

FailureOr<sim::SimCallOp>
createDirectCall(IRRewriter &rewriter, Operation *operation,
                 FlatSymbolRefAttr callee, Value receiver, ValueRange arguments,
                 TypeRange resultTypes) {
  sim::SimFuncOp caller = operation->getParentOfType<sim::SimFuncOp>();
  auto implementation =
      SymbolTable::lookupNearestSymbolFrom<sim::SimFuncOp>(operation, callee);
  if (!caller || caller.getBody().empty() ||
      caller.getBody().front().getNumArguments() == 0 || !implementation ||
      implementation.getEntryKind() != sim::EntryKind::Function ||
      implementation.getFunctionType().getNumInputs() < 2)
    return failure();

  Type expectedReceiver = implementation.getFunctionType().getInput(1);
  Value adjusted = receiver;
  if (adjusted.getType() != expectedReceiver)
    adjusted = sim::SimClassCastOp::create(rewriter, operation->getLoc(),
                                           expectedReceiver, adjusted);

  SmallVector<Value> operands{caller.getBody().front().getArgument(0),
                              adjusted};
  llvm::append_range(operands, arguments);
  return sim::SimCallOp::create(rewriter, operation->getLoc(), resultTypes,
                                callee, operands, ArrayAttr{}, ArrayAttr{});
}

class ObeliskSimDevirtualizeClassCallsPass final
    : public impl::ObeliskSimDevirtualizeClassCallsPassBase<
          ObeliskSimDevirtualizeClassCallsPass> {
public:
  using Base = impl::ObeliskSimDevirtualizeClassCallsPassBase<
      ObeliskSimDevirtualizeClassCallsPass>;
  using Base::Base;
  ObeliskSimDevirtualizeClassCallsPass(
      const ObeliskSimDevirtualizeClassCallsPass &other)
      : Base(other) {}

  void runOnOperation() override;

private:
  Statistic exactCalls{this, "exact-calls",
                       "virtual calls resolved from exact non-null classes"};
  Statistic directCalls{this, "direct-calls",
                        "managed direct calls normalized to ordinary calls"};
  Statistic unresolvedCalls{this, "unresolved-calls",
                            "virtual calls retained conservatively"};
};

void ObeliskSimDevirtualizeClassCallsPass::runOnOperation() {
  sim::SimDesignOp design = getOperation();
  ClassDispatchInventory inventory(design);
  ExactClassResolver resolver(inventory);
  IRRewriter rewriter(design.getContext());

  SmallVector<sim::SimClassVirtualCallOp> virtualCalls;
  design.walk([&](sim::SimClassVirtualCallOp call) {
    virtualCalls.push_back(call);
  });
  for (sim::SimClassVirtualCallOp call : virtualCalls) {
    sim::SimClassDeclOp dynamicClass = resolver.resolve(call.getReceiver());
    sim::SimClassMethodDeclOp method =
        dynamicClass ? inventory.resolve(dynamicClass, call)
                     : sim::SimClassMethodDeclOp{};
    if (!method || !method.getSignatureIdAttr() ||
        method.getSignatureId() != call.getSignatureId() ||
        method.getIsPure() || method.getIsTask() ||
        !method.getImplementationAttr()) {
      ++unresolvedCalls;
      continue;
    }
    rewriter.setInsertionPoint(call);
    FailureOr<sim::SimCallOp> replacement = createDirectCall(
        rewriter, call, method.getImplementationAttr(), call.getReceiver(),
        call.getArguments(), call.getResultTypes());
    if (failed(replacement)) {
      call.emitError("resolved virtual method has no valid function implementation");
      return signalPassFailure();
    }
    rewriter.replaceOp(call, replacement->getResults());
    ++exactCalls;
  }

  SmallVector<sim::SimClassDirectCallOp> directCallsToNormalize;
  design.walk([&](sim::SimClassDirectCallOp call) {
    directCallsToNormalize.push_back(call);
  });
  for (sim::SimClassDirectCallOp call : directCallsToNormalize) {
    rewriter.setInsertionPoint(call);
    FailureOr<sim::SimCallOp> replacement = createDirectCall(
        rewriter, call, call.getCalleeAttr(), call.getReceiver(),
        call.getArguments(), call.getResultTypes());
    if (failed(replacement)) {
      call.emitError("direct class call has no valid function implementation");
      return signalPassFailure();
    }
    rewriter.replaceOp(call, replacement->getResults());
    ++directCalls;
  }
}

} // namespace
} // namespace obelisk
