//===- LowerUnitDumpSystemCalls.cpp - Lower waveform dump tasks ---------===//
//
// `$dumpvars` and friends configure a runtime facility rather than observing
// values themselves: collection is a once-per-time-slot difference over the
// canonical state planes. These lowerings therefore carry only the selection,
// never a per-variable site.
//
//===----------------------------------------------------------------------===//

#include "LowerUnit.h"

#include "mlir/Dialect/Arith/IR/Arith.h"

using namespace mlir;

namespace obelisk::simlowering {

FailureOr<Value>
UnitLowering::lowerDumpSystemCall(semantic::SVCallExpressionOp op) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  StringRef name = op.getCalleeName();
  Value context = function.getBody().front().getArgument(0);
  auto i32 = builder.getI32Type();
  auto i64 = builder.getI64Type();

  auto constant = [&](IntegerType type, int64_t value) -> Value {
    return arith::ConstantOp::create(builder, location, type,
                                     builder.getIntegerAttr(type, value));
  };
  auto dummyTaskResult = [&]() -> Value {
    return constant(builder.getI1Type(), 0);
  };
  auto stringLiteral = [&](Operation *child) {
    Operation *spelling = child;
    while (isa<semantic::SVConversionExpressionOp>(spelling)) {
      SmallVector<Operation *> convertedChildren = getChildren(spelling);
      if (convertedChildren.size() != 1)
        break;
      spelling = convertedChildren.front();
    }
    return dyn_cast<semantic::SVStringLiteralOp>(spelling);
  };
  auto bytesConstant = [&](Location where, StringRef text) -> Value {
    return sim::SimBytesConstantOp::create(builder, where, text).getResult();
  };

  // The header carries the elaborated design precision as a decimal exponent
  // in seconds; the runtime cannot recover it from the state image.
  auto declareTimescale = [&]() {
    auto design = function->getParentOfType<sim::SimDesignOp>();
    if (!design)
      return;
    IntegerAttr precisionFs = design.getTimePrecisionFsAttr();
    if (!precisionFs)
      return;
    int32_t exponent = -15;
    for (uint64_t scale = precisionFs.getValue().getZExtValue(); scale > 1;
         scale /= 10)
      ++exponent;
    sim::SimDumpTimescaleOp::create(builder, location, context,
                                    constant(i32, exponent));
  };

  if (name == "$dumpfile") {
    if (children.size() != 1) {
      emitError(location) << "$dumpfile requires one file name";
      return failure();
    }
    auto literal = stringLiteral(children.front());
    if (!literal) {
      // The traced set and the file are fixed for the whole run, so a computed
      // name buys nothing that a literal does not. Reject it plainly rather
      // than carrying a managed string through every execution tier.
      emitError(getSemanticLocation(children.front()))
          << "$dumpfile requires a string literal file name";
      return failure();
    }
    declareTimescale();
    sim::SimDumpOpenOp::create(
        builder, location, context,
        bytesConstant(getSemanticLocation(literal), literal.getConstantValue()));
    return dummyTaskResult();
  }

  if (name == "$dumpvars") {
    declareTimescale();
    if (children.empty()) {
      // `$dumpvars` with no arguments selects the whole design.
      sim::SimDumpVarsOp::create(builder, location, context, constant(i64, 0),
                                 bytesConstant(location, ""));
      return dummyTaskResult();
    }
    // The first argument is a constant depth; the rest are hierarchical
    // references, which elaboration has already resolved to full paths.
    FailureOr<Value> levels = lowerExpression(children.front());
    if (failed(levels))
      return failure();
    FailureOr<Value> depth =
        convert(*levels, i64, isSignedNode(children.front()), location);
    if (failed(depth))
      return failure();
    if (children.size() == 1) {
      sim::SimDumpVarsOp::create(builder, location, context, *depth,
                                 bytesConstant(location, ""));
      return dummyTaskResult();
    }
    for (Operation *child : llvm::drop_begin(children)) {
      auto path = child->getAttrOfType<StringAttr>("referenced_path");
      if (!path) {
        emitError(getSemanticLocation(child))
            << "$dumpvars requires a hierarchical scope or variable reference";
        return failure();
      }
      sim::SimDumpVarsOp::create(builder, location, context, *depth,
                                 bytesConstant(getSemanticLocation(child),
                                               path.getValue()));
    }
    return dummyTaskResult();
  }

  if (name == "$dumpoff" || name == "$dumpon") {
    if (!children.empty()) {
      emitError(location) << name << " accepts no arguments";
      return failure();
    }
    sim::SimDumpControlOp::create(builder, location, context,
                                  name == "$dumpon");
    return dummyTaskResult();
  }

  if (name == "$dumpall") {
    if (!children.empty()) {
      emitError(location) << "$dumpall accepts no arguments";
      return failure();
    }
    sim::SimDumpAllOp::create(builder, location, context);
    return dummyTaskResult();
  }

  if (name == "$dumpflush") {
    if (!children.empty()) {
      emitError(location) << "$dumpflush accepts no arguments";
      return failure();
    }
    sim::SimDumpFlushOp::create(builder, location, context);
    return dummyTaskResult();
  }

  if (name == "$dumplimit") {
    if (children.size() != 1) {
      emitError(location) << "$dumplimit requires one byte count";
      return failure();
    }
    FailureOr<Value> bytes = lowerExpression(children.front());
    if (failed(bytes))
      return failure();
    FailureOr<Value> limit =
        convert(*bytes, i64, isSignedNode(children.front()), location);
    if (failed(limit))
      return failure();
    sim::SimDumpLimitOp::create(builder, location, context, *limit);
    return dummyTaskResult();
  }

  emitError(location) << "unsupported waveform system call " << name;
  return failure();
}

} // namespace obelisk::simlowering
