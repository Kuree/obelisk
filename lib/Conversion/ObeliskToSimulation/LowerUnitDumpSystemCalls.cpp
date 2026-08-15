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
  auto timescaleExponent = [&]() -> Value {
    int32_t exponent = -9;
    if (auto design = function->getParentOfType<sim::SimDesignOp>())
      if (IntegerAttr precisionFs = design.getTimePrecisionFsAttr()) {
        exponent = -15;
        for (uint64_t scale = precisionFs.getValue().getZExtValue(); scale > 1;
             scale /= 10)
          ++exponent;
      }
    return constant(i32, exponent);
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

  auto lowerDumpPortsPath = [&](Operation *child,
                                bool defaultPath) -> FailureOr<Value> {
    Type stringType = sim::StringType::get(function.getContext());
    if (!child)
      return sim::SimStringLiteralOp::create(
                 builder, location, stringType,
                 builder.getStringAttr(defaultPath ? "dumpports.vcd" : ""))
          .getResult();
    FailureOr<Value> value = failure();
    if (auto arbitrary =
            dyn_cast<semantic::SVArbitrarySymbolExpressionOp>(child))
      value = lowerReferencedValue(child, arbitrary.getReferencedPath(), false);
    else
      value = lowerExpression(child);
    if (failed(value))
      return failure();
    return convert(*value, stringType, isSignedNode(child),
                   getSemanticLocation(child));
  };

  if (name == "$dumpports") {
    // Slang deliberately accepts the common compatibility extension of zero
    // or more module selections followed by one optional filename, including
    // a filename-only call. Preserve that already-validated AST contract;
    // the strict LRM forms are a subset, including `(, filename)`.
    Operation *pathChild = nullptr;
    SmallVector<Operation *> scopes(children.begin(), children.end());
    if (!scopes.empty()) {
      Operation *last = scopes.back();
      if (auto arbitrary =
              dyn_cast<semantic::SVArbitrarySymbolExpressionOp>(last)) {
        StringRef path = arbitrary.getReferencedPath();
        if (values.lookup(path) || lvalues.lookup(path)) {
          pathChild = last;
          scopes.pop_back();
        }
      } else if (!isa<semantic::SVEmptyArgumentExpressionOp>(last)) {
        pathChild = last;
        scopes.pop_back();
      }
    }
    if (!scopes.empty() &&
        isa<semantic::SVEmptyArgumentExpressionOp>(scopes.front()))
      scopes.erase(scopes.begin());
    if (llvm::any_of(scopes, [](Operation *scope) {
          return isa<semantic::SVEmptyArgumentExpressionOp>(scope) ||
                 !isa<semantic::SVArbitrarySymbolExpressionOp>(scope) ||
                 !scope->hasAttr("referenced_path");
        })) {
      emitError(location)
          << "$dumpports requires module-instance selections followed by an "
             "optional file name";
      return failure();
    }
    for (Operation *scope : scopes) {
      if (!scope->hasAttr("obelisk_sim.dumpports_scope")) {
        emitError(getSemanticLocation(scope))
            << "$dumpports selection must name a module instance";
        return failure();
      }
    }
    FailureOr<Value> path = lowerDumpPortsPath(pathChild, true);
    if (failed(path))
      return failure();
    SmallVector<StringRef> scopePaths;
    if (scopes.empty()) {
      auto current = op->getAttrOfType<StringAttr>("system_scope_path");
      if (!current || current.getValue().empty()) {
        emitError(location)
            << "$dumpports has no calling module for its default selection";
        return failure();
      }
      scopePaths.push_back(current.getValue());
    } else {
      for (Operation *scope : scopes)
        scopePaths.push_back(
            scope->getAttrOfType<StringAttr>("referenced_path").getValue());
    }
    for (StringRef scope : scopePaths)
      sim::SimDumpPortsOp::create(
          builder, location, context, *path,
          sim::SimStringLiteralOp::create(
              builder, location, sim::StringType::get(function.getContext()),
              builder.getStringAttr(scope)),
          timescaleExponent());
    return dummyTaskResult();
  }

  if (name == "$dumpportsoff" || name == "$dumpportson" ||
      name == "$dumpportsall" || name == "$dumpportsflush") {
    if (children.size() > 1) {
      emitError(location) << name << " accepts at most one file name";
      return failure();
    }
    FailureOr<Value> path = lowerDumpPortsPath(
        children.empty() ? nullptr : children.front(), false);
    if (failed(path))
      return failure();
    sim::DumpPortsAction action =
        name == "$dumpportsoff"   ? sim::DumpPortsAction::Off
        : name == "$dumpportson"  ? sim::DumpPortsAction::On
        : name == "$dumpportsall" ? sim::DumpPortsAction::All
                                    : sim::DumpPortsAction::Flush;
    sim::SimDumpPortsControlOp::create(builder, location, context, *path,
                                       action, constant(i64, 0));
    return dummyTaskResult();
  }

  if (name == "$dumpportslimit") {
    if (children.empty() || children.size() > 2) {
      emitError(location)
          << "$dumpportslimit requires a byte count and optional file name";
      return failure();
    }
    FailureOr<Value> bytes = lowerExpression(children.front());
    if (failed(bytes))
      return failure();
    FailureOr<Value> limit =
        convert(*bytes, i64, isSignedNode(children.front()), location);
    FailureOr<Value> path = lowerDumpPortsPath(
        children.size() == 2 ? children.back() : nullptr, false);
    if (failed(limit) || failed(path))
      return failure();
    sim::SimDumpPortsControlOp::create(builder, location, context, *path,
                                       sim::DumpPortsAction::Limit, *limit);
    return dummyTaskResult();
  }

  if (name == "$dumpfile") {
    if (children.size() > 1) {
      emitError(location) << "$dumpfile accepts at most one file name";
      return failure();
    }
    declareTimescale();
    if (children.empty()) {
      sim::SimDumpOpenOp::create(builder, location, context,
                                 bytesConstant(location, "dump.vcd"));
      return dummyTaskResult();
    }
    if (auto literal = stringLiteral(children.front())) {
      sim::SimDumpOpenOp::create(builder, location, context,
                                 bytesConstant(getSemanticLocation(literal),
                                               literal.getConstantValue()));
      return dummyTaskResult();
    }
    FailureOr<Value> pathValue = lowerExpression(children.front());
    if (failed(pathValue))
      return failure();
    Type stringType = sim::StringType::get(function.getContext());
    FailureOr<Value> path = convert(*pathValue, stringType,
                                    isSignedNode(children.front()), location);
    if (failed(path))
      return failure();
    sim::SimDumpOpenStringOp::create(builder, location, context, *path);
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
      sim::SimDumpVarsOp::create(
          builder, location, context, *depth,
          bytesConstant(getSemanticLocation(child), path.getValue()));
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
