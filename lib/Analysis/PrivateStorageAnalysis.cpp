#include "obelisk/Analysis/PrivateStorageAnalysis.h"
#include "mlir/Interfaces/CallInterfaces.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
using namespace mlir;
namespace obelisk::analysis {
PrivateStorageAnalysis::PrivateStorageAnalysis(sim::SimDesignOp design) {
  for (auto decl : design.getOps<sim::SimStorageDeclOp>())
    if ((decl.getLifetime() == sim::Lifetime::Static ||
         decl.getLifetime() == sim::Lifetime::Design) &&
        sim::getProvenanceSpan(decl.getType()) &&
        !decl->hasAttr(sim::metadata::coverageToggleObservable))
      roots.try_emplace(decl.getId(), Accesses{decl, {}, {}, {}, {}});
  HandleDataflowAnalysis handles(design);
  DenseMap<Operation *, HandleDataflowResult> snapshots;
  for (auto function : design.getOps<sim::SimFuncOp>())
    if (!function.isExternal())
      snapshots.try_emplace(function, handles.analyze(function));
  DenseSet<uint64_t> escaped;
  bool foreign = false;
  for (auto function : design.getOps<sim::SimFuncOp>()) {
    const auto &facts = snapshots[function];
    function.walk([&](Operation *op) {
      foreign |= isa<sim::SimDPICallOp>(op);
      if (auto call = dyn_cast<sim::SimCallOp>(op)) {
        auto callee = design.lookupSymbol<sim::SimFuncOp>(call.getCallee());
        foreign |= !callee || callee.isExternal();
      }
      // A closed call/spawn transports references; it does not itself read
      // them. Check both ends of every binding, including bindings with an
      // unknown source, before relying on the callee's root facts. All callee
      // uses are audited independently below. Unknown/foreign transports and
      // mismatched capture metadata remain escapes.
      DenseSet<unsigned> transports;
      if (isa<sim::SimCallOp, sim::SimSpawnOp>(op)) {
        auto call = cast<CallOpInterface>(op);
        auto symbol = dyn_cast<SymbolRefAttr>(call.getCallableForCallee());
        auto callee = symbol ? design.lookupSymbol<sim::SimFuncOp>(symbol)
                             : sim::SimFuncOp{};
        if (callee && !callee.isExternal()) {
          const auto &target = snapshots[callee].facts;
          for (auto [index, operand] : llvm::enumerate(call.getArgOperands())) {
            if (!isa<sim::RefType>(operand.getType()) ||
                index >= callee.getBody().front().getNumArguments())
              continue;
            auto argument = callee.getBody().front().getArgument(index);
            auto from = facts.facts.find(operand);
            auto to = target.find(argument);
            bool exact = from != facts.facts.end() && to != target.end() &&
                         from->second == to->second;
            if (exact && from->second.descriptor)
              transports.insert(index);
            else if (to != target.end() && to->second.descriptor)
              escaped.insert(*to->second.descriptor);
          }
        }
      }
      for (auto [index, operand] : llvm::enumerate(op->getOperands())) {
        if (!isa<sim::RefType>(operand.getType()))
          continue;
        auto fact = facts.facts.find(operand);
        if (fact == facts.facts.end() || !fact->second.descriptor)
          continue;
        uint64_t id = *fact->second.descriptor;
        auto found = roots.find(id);
        if (found == roots.end())
          continue;
        if (transports.contains(index))
          continue;
        auto &access = found->second;
        const auto &range = fact->second;
        auto load = dyn_cast<sim::SimRefLoadOp>(op);
        auto store = dyn_cast<sim::SimRefStoreOp>(op);
        bool whole = !range.dynamic && range.low == 0 &&
                     range.width == range.rootWidth &&
                     cast<sim::RefType>(operand.getType()).getElementType() ==
                         access.declaration.getType();
        bool view = isa<sim::SimRefExtractOp, sim::SimRefDynExtractOp,
                        sim::SimRefSubelementOp, sim::SimRefArrayElementOp>(op);
        if (view)
          view = llvm::all_of(op->getResults(), [&](Value result) {
            auto selected = facts.facts.find(result);
            return selected != facts.facts.end() &&
                   selected->second.descriptor == range.descriptor &&
                   selected->second.resource == range.resource;
          });
        if (auto field = dyn_cast<sim::SimRefSubelementOp>(op)) {
          Type type = field.getInput().getType().getElementType();
          for (int64_t index : field.getIndices()) {
            Type child = sim::getAggregateElementType(type, index);
            if (isa<sim::UnpackedUnionType>(type))
              access.promotableViews = false;
            if (auto packed = dyn_cast<sim::PackedUnionType>(type))
              access.promotableViews &=
                  !packed.getIsTagged() &&
                  sim::getPackedWidth(type) == sim::getPackedWidth(child);
            type = child;
          }
        }
        if (isa<sim::SimRefExtractOp, sim::SimRefDynExtractOp>(op)) {
          Type parent = sim::getPackedScalarType(
              cast<sim::RefType>(op->getOperand(0).getType()).getElementType());
          Type child = sim::getPackedScalarType(
              cast<sim::RefType>(op->getResult(0).getType()).getElementType());
          access.promotableViews &=
              parent && child &&
              (isa<sim::LogicType>(parent) || !isa<sim::LogicType>(child));
        }
        if ((!load && !store && !view) ||
            (access.owner && access.owner != function)) {
          escaped.insert(id);
          continue;
        }
        access.owner = function;
        if (whole && !llvm::is_contained(access.bases, operand))
          access.bases.push_back(operand);
        if (load)
          access.loads.push_back(load);
        if (store)
          access.stores.push_back(store);
      }
    });
  }
  if (foreign) {
    roots.clear();
    return;
  }
  for (uint64_t id : escaped)
    roots.erase(id);
}
} // namespace obelisk::analysis
