//===- SimulationToStandardAPITest.cpp - Composition smoke test ----------===//

#include "talos/Conversion/SimulationToStandard.h"
#include "talos/Dialect/Simulation/SimulationOps.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/DialectRegistry.h"
#include "mlir/Parser/Parser.h"
#include "mlir/Transforms/DialectConversion.h"

#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/raw_ostream.h"

using namespace mlir;

namespace {

class ConvertTimeConstant final
    : public OpConversionPattern<talos::sim::SimTimeConstantOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(talos::sim::SimTimeConstantOp op, OpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    auto type = rewriter.getI64Type();
    rewriter.replaceOpWithNewOp<arith::ConstantOp>(
        op, type, rewriter.getIntegerAttr(type, op.getValue()));
    return success();
  }
};

} // namespace

int main(int argc, char **argv) {
  llvm::InitLLVM initLLVM(argc, argv);
  if (argc != 2) {
    llvm::errs() << "usage: talos-sim-standard-api-test <input.mlir>\n";
    return 1;
  }

  DialectRegistry registry;
  registry.insert<arith::ArithDialect, func::FuncDialect,
                  talos::sim::TalosSimulationDialect>();
  MLIRContext context(registry);
  OwningOpRef<ModuleOp> module = parseSourceFile<ModuleOp>(argv[1], &context);
  if (!module)
    return 1;

  talos::SimulationToStandardTypeConverter converter;
  // Model the future runtime conversion extending the public converter.
  converter.addConversion([](talos::sim::TimeType type) -> Type {
    return IntegerType::get(type.getContext(), 64);
  });

  RewritePatternSet patterns(&context);
  const TypeConverter &compositeConverter = converter;
  talos::populateSimulationToStandardPatterns(compositeConverter, patterns);
  patterns.add<ConvertTimeConstant>(converter, &context);

  ConversionTarget target(context);
  target.addLegalDialect<arith::ArithDialect>();
  target.addIllegalDialect<talos::sim::TalosSimulationDialect>();
  target.addDynamicallyLegalOp<func::FuncOp>([&](func::FuncOp function) {
    return converter.isSignatureLegal(function.getFunctionType()) &&
           converter.isLegal(&function.getBody());
  });
  target.addDynamicallyLegalOp<func::ReturnOp>(
      [&](func::ReturnOp op) { return converter.isLegal(op); });
  target.addLegalOp<ModuleOp>();

  if (failed(applyFullConversion(*module, target, std::move(patterns))))
    return 1;
  module->print(llvm::outs());
  llvm::outs() << '\n';
  return 0;
}
