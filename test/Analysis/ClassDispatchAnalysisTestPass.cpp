//===- ClassDispatchAnalysisTestPass.cpp - Inspect class dispatch --------===//

#include "AnalysisTestPasses.h"

#include "talos/Analysis/ClassDispatchAnalysis.h"

#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/raw_ostream.h"

using namespace mlir;

namespace {
class ClassDispatchAnalysisTestPass
    : public PassWrapper<ClassDispatchAnalysisTestPass,
                         OperationPass<ModuleOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(ClassDispatchAnalysisTestPass)

  StringRef getArgument() const final {
    return "test-talos-class-dispatch-analysis";
  }
  StringRef getDescription() const final {
    return "inspect sparse hierarchy and shared method-table facts";
  }

  void runOnOperation() final {
    ModuleOp module = getOperation();
    module.walk([&](talos::sim::SimDesignOp design) {
      talos::analysis::ClassDispatchAnalysis dispatch(design);
      size_t nodes = dispatch.getNumMethodTableNodes();
      if (auto limit = module->getAttrOfType<IntegerAttr>(
              "test.dispatch_table_node_limit")) {
        if (nodes > limit.getValue().getZExtValue()) {
          module.emitError(
              "class dispatch exceeded shared method-table budget");
          signalPassFailure();
          return;
        }
        llvm::errs() << "bounded-dispatch-metadata classes="
                     << dispatch.getClasses().size() << "\n";
      }
      if (auto queries =
              module->getAttrOfType<ArrayAttr>("test.dispatch_queries"))
        for (Attribute attribute : queries) {
          auto query = cast<DictionaryAttr>(attribute);
          auto dynamic = dispatch.lookup(
              cast<FlatSymbolRefAttr>(query.get("dynamic")).getValue());
          auto target = dispatch.lookup(
              cast<FlatSymbolRefAttr>(query.get("target")).getValue());
          if (!dynamic || !target) {
            module.emitError("class dispatch query names an unknown class");
            signalPassFailure();
            return;
          }
          uint64_t slot = cast<IntegerAttr>(query.get("slot")).getUInt();
          uint64_t signature =
              cast<IntegerAttr>(query.get("signature")).getUInt();
          bool isTask = cast<BoolAttr>(query.get("is_task")).getValue();
          auto method = dispatch.resolve(dynamic, slot, signature);
          auto implementations = dispatch.compatibleImplementations(
              target, slot, signature, isTask);
          llvm::errs() << "dispatch " << dynamic.getSymName() << " -> "
                       << target.getSymName() << " is-instance="
                       << dispatch.isInstanceOf(dynamic, target)
                       << " resolved=";
          if (method)
            llvm::errs() << method.getSymName();
          else
            llvm::errs() << "none";
          llvm::errs() << " compatible="
                       << dispatch.compatibleConcreteClasses(target).size()
                       << " implementations=[";
          llvm::interleaveComma(implementations, llvm::errs(), [&](auto entry) {
            llvm::errs() << entry.getSymName();
          });
          llvm::errs() << "]\n";
        }
      if (dispatch.getNumMethodTableNodes() != nodes) {
        module.emitError(
            "class queries changed the immutable dispatch inventory");
        signalPassFailure();
      }
    });
    markAllAnalysesPreserved();
  }
};
} // namespace

namespace talos {
void registerClassDispatchAnalysisTestPass() {
  PassRegistration<ClassDispatchAnalysisTestPass>();
}
} // namespace talos
