//===- SimulationToStandardAPITest.cpp - Composition smoke test ----------===//

#include "obelisk/Conversion/SimulationToStandard.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

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
    : public OpConversionPattern<obelisk::sim::SimTimeConstantOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(obelisk::sim::SimTimeConstantOp op, OpAdaptor,
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
    llvm::errs() << "usage: obelisk-sim-standard-api-test <input.mlir>\n";
    return 1;
  }

  DialectRegistry registry;
  registry.insert<arith::ArithDialect, func::FuncDialect,
                  obelisk::sim::ObeliskSimulationDialect>();
  MLIRContext context(registry);
  OwningOpRef<ModuleOp> module = parseSourceFile<ModuleOp>(argv[1], &context);
  if (!module)
    return 1;

  obelisk::SimulationToStandardTypeConverter converter;
  // Model the future runtime conversion extending the public converter.
  converter.addConversion([](obelisk::sim::TimeType type) -> Type {
    return IntegerType::get(type.getContext(), 64);
  });

  RewritePatternSet patterns(&context);
  const TypeConverter &compositeConverter = converter;
  obelisk::populateSimulationToStandardPatterns(compositeConverter, patterns);
  patterns.add<ConvertTimeConstant>(converter, &context);

  ConversionTarget target(context);
  target.addLegalDialect<arith::ArithDialect>();
  target.addIllegalDialect<obelisk::sim::ObeliskSimulationDialect>();
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
