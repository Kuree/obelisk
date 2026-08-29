//===- SDFDialect.cpp - Transient normalized SDF dialect ----------------===//

#include "obelisk/Dialect/SDF/SDFOps.h"

#include "mlir/IR/Diagnostics.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/TypeSwitch.h"

#include <cctype>

using namespace mlir;

#include "obelisk/Dialect/SDF/SDFDialect.cpp.inc"
#include "obelisk/Dialect/SDF/SDFEnums.cpp.inc"

#define GET_ATTRDEF_CLASSES
#include "obelisk/Dialect/SDF/SDFAttrs.cpp.inc"

#define GET_OP_CLASSES
#include "obelisk/Dialect/SDF/SDFOps.cpp.inc"

namespace obelisk::sdf {

void ObeliskSDFDialect::initialize() {
  addAttributes<
#define GET_ATTRDEF_LIST
#include "obelisk/Dialect/SDF/SDFAttrs.cpp.inc"
      >();
  addOperations<
#define GET_OP_LIST
#include "obelisk/Dialect/SDF/SDFOps.cpp.inc"
      >();
}

static bool isExactDecimal(StringRef value) {
  if (value.empty() || value.size() > 16 * 1024)
    return false;
  size_t cursor = 0;
  if (value[cursor] == '+' || value[cursor] == '-')
    if (++cursor == value.size())
      return false;
  bool sawDigit = false;
  while (cursor < value.size() &&
         std::isdigit(static_cast<unsigned char>(value[cursor]))) {
    sawDigit = true;
    ++cursor;
  }
  if (cursor < value.size() && value[cursor] == '.') {
    ++cursor;
    while (cursor < value.size() &&
           std::isdigit(static_cast<unsigned char>(value[cursor]))) {
      sawDigit = true;
      ++cursor;
    }
  }
  if (!sawDigit)
    return false;
  if (cursor < value.size() &&
      (value[cursor] == 'e' || value[cursor] == 'E')) {
    ++cursor;
    if (cursor < value.size() &&
        (value[cursor] == '+' || value[cursor] == '-'))
      ++cursor;
    size_t exponentStart = cursor;
    while (cursor < value.size() &&
           std::isdigit(static_cast<unsigned char>(value[cursor])))
      ++cursor;
    if (cursor == exponentStart)
      return false;
  }
  return cursor == value.size();
}

LogicalResult
DecimalAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                    StringAttr spelling) {
  // IEEE 1800-2017 32.2.4: keep the decimal token exact here.  Unit and
  // destination-timescale rounding belong to the annotation consumer.
  if (!spelling || !isExactDecimal(spelling.getValue()))
    return emitError() << "expected a bounded exact decimal spelling";
  return success();
}

LogicalResult
TimeScaleAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                      DecimalAttr amount, TimeUnit) {
  if (!amount)
    return emitError() << "TIMESCALE requires an exact amount";
  StringRef value = amount.getSpelling().getValue();
  // IEEE 1800-2017 32.2.4 constrains the TIMESCALE numeric value.  An
  // exponent is scale, not significand: `0e99` is still exactly zero.
  StringRef significand = value.take_front(value.find_first_of("eE"));
  bool nonzero = llvm::any_of(significand, [](char character) {
    return character >= '1' && character <= '9';
  });
  if (value.starts_with('-') || !nonzero)
    return emitError() << "TIMESCALE amount must be positive";
  return success();
}

LogicalResult PortAttr::verify(
    llvm::function_ref<InFlightDiagnostic()> emitError, StringAttr name,
    bool hasIndex, int64_t index, Edge) {
  if (!name || name.getValue().empty())
    return emitError() << "port name must not be empty";
  if (name.getValue().size() > 16 * 1024)
    return emitError() << "port name exceeds the supported size limit";
  if (!hasIndex && index != 0)
    return emitError() << "an unindexed port must use the canonical zero index";
  return success();
}

LogicalResult ConditionTokenAttr::verify(
    llvm::function_ref<InFlightDiagnostic()> emitError,
    ConditionOpcode opcode, PortAttr port, IntegerAttr constant) {
  if (opcode == ConditionOpcode::Port)
    return port && !constant
               ? success()
               : emitError() << "port token requires exactly one port";
  if (opcode == ConditionOpcode::Constant) {
    if (!constant || port)
      return emitError() << "constant token requires exactly one constant";
    int64_t value = constant.getInt();
    return value >= 0 && value <= 3
               ? success()
               : emitError() << "condition constant must encode 0, 1, X, or Z";
  }
  if (port || constant)
    return emitError() << "condition operator cannot carry an operand";
  return success();
}

LogicalResult ConditionAttr::verify(
    llvm::function_ref<InFlightDiagnostic()> emitError, ArrayAttr tokens,
    bool) {
  // IEEE 1800-2017 32.4 preserves condition structure. A bounded postfix
  // stream avoids an opaque expression string and admits linear validation.
  if (!tokens || tokens.empty() || tokens.size() > 4096)
    return emitError() << "condition requires 1..4096 postfix tokens";
  int64_t depth = 0;
  for (Attribute attribute : tokens) {
    auto token = dyn_cast<ConditionTokenAttr>(attribute);
    if (!token)
      return emitError() << "condition contains a non-token attribute";
    switch (token.getOpcode()) {
    case ConditionOpcode::Port:
    case ConditionOpcode::Constant:
      ++depth;
      break;
    case ConditionOpcode::Not:
      if (depth < 1)
        return emitError() << "condition postfix stack underflow";
      break;
    default:
      if (depth < 2)
        return emitError() << "condition postfix stack underflow";
      --depth;
      break;
    }
  }
  return depth == 1 ? success()
                    : emitError() << "condition must produce exactly one value";
}

LogicalResult TimingEventAttr::verify(
    llvm::function_ref<InFlightDiagnostic()> emitError, PortAttr port,
    ConditionAttr) {
  if (!port)
    return emitError() << "timing event requires a port";
  return success();
}

LogicalResult DelayValueAttr::verify(
    llvm::function_ref<InFlightDiagnostic()> emitError, DelayValueForm form,
    DecimalAttr min, DecimalAttr typ, DecimalAttr max) {
  // IEEE 1800-2017 32.3 permits empty fields in min:typ:max tuples.  Absence
  // is therefore semantic data, not malformed input.
  if (form == DelayValueForm::Empty)
    return !min && !typ && !max
               ? success()
               : emitError() << "empty delay value cannot carry decimals";
  if (form == DelayValueForm::Scalar)
    return !min && typ && !max
               ? success()
               : emitError() << "scalar delay value requires only typ";
  // `(::)` is a sparse min:typ:max tuple and is distinct from the empty
  // scalar delay `()`.  Clause 32.3 assigns both retention semantics, but the
  // transient IR must preserve their source forms for normalization tools.
  return success();
}

static bool hasLegalDelayArity(ArrayAttr delays) {
  if (!delays)
    return false;
  switch (delays.size()) {
  case 1:
  case 2:
  case 3:
  case 6:
  case 12:
    return true;
  default:
    return false;
  }
}

LogicalResult SDFDelayFileOp::verify() {
  if (!isa<ModuleOp>((*this)->getParentOp()))
    return emitOpError("must be directly nested under builtin.module");
  if (getSource().empty() || getSdfVersion().empty())
    return emitOpError("requires nonempty source and SDFVERSION attributes");
  if (auto divider = getDividerAttr())
    if (divider.getValue() != "." && divider.getValue() != "/")
      return emitOpError("DIVIDER must be '.' or '/'");
  if (getBody().empty())
    return emitOpError("requires one body block");
  for (Operation &operation : getBody().front())
    if (!isa<SDFCellOp>(operation))
      return emitOpError("body may contain only obelisk_sdf.cell operations");
  return success();
}

LogicalResult SDFCellOp::verify() {
  if (getCellType().empty())
    return emitOpError("requires a nonempty CELLTYPE");
  if (getWildcard() && getInstanceAttr())
    return emitOpError("INSTANCE cannot be both named and wildcard");
  if (getBody().empty())
    return emitOpError("requires one body block");
  for (Operation &operation : getBody().front())
    if (!isa<SDFPathDelayOp, SDFTimingCheckOp, SDFLabelOp, SDFInterconnectDelayOp,
             SDFTerminalDelayOp, SDFPulseOp>(operation))
      return emitOpError("body contains a non-SDF annotation record");
  return success();
}

LogicalResult SDFPathDelayOp::verify() {
  if (!hasLegalDelayArity(getDelays()))
    return emitOpError("requires 1, 2, 3, 6, or 12 delay values");
  bool hasCondition = static_cast<bool>(getConditionAttr());
  if (hasCondition != (getKind() == PathKind::Cond))
    return emitOpError("COND requires, and IOPATH/CONDELSE forbid, a condition");
  return success();
}

LogicalResult SDFTimingCheckOp::verify() {
  size_t events = getEvents().size();
  size_t limits = getLimits().size();
  size_t expectedEvents =
      (getKind() == TimingCheckKind::Period ||
       getKind() == TimingCheckKind::Width)
          ? 1
          : 2;
  size_t expectedLimits =
      (getKind() == TimingCheckKind::SetupHold ||
       getKind() == TimingCheckKind::Recrem ||
       getKind() == TimingCheckKind::FullSkew ||
       getKind() == TimingCheckKind::NoChange)
          ? 2
          : 1;
  if (events != expectedEvents || limits != expectedLimits)
    return emitOpError() << "requires " << expectedEvents << " event(s) and "
                         << expectedLimits << " limit(s) for its timing-check kind";
  return success();
}

LogicalResult SDFLabelOp::verify() {
  return getName().empty() ? emitOpError("requires a nonempty label name")
                           : success();
}

LogicalResult SDFInterconnectDelayOp::verify() {
  return hasLegalDelayArity(getDelays())
             ? success()
             : emitOpError("requires 1, 2, 3, 6, or 12 delay values");
}

LogicalResult SDFTerminalDelayOp::verify() {
  if (!hasLegalDelayArity(getDelays()))
    return emitOpError("requires 1, 2, 3, 6, or 12 delay values");
  if (getKind() != TerminalDelayKind::Device && !getTerminalAttr())
    return emitOpError("PORT and NETDELAY require a terminal");
  return success();
}

LogicalResult SDFPulseOp::verify() {
  if (static_cast<bool>(getInputAttr()) != static_cast<bool>(getOutputAttr()))
    return emitOpError("path endpoints must be both present or both absent");
  return success();
}

} // namespace obelisk::sdf
