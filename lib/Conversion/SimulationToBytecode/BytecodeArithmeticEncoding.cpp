//===- BytecodeArithmeticEncoding.cpp - Arithmetic instruction selection -===//

#include "BytecodeEncoder.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Math/IR/Math.h"

using namespace mlir;

namespace obelisk::bytecode {

std::optional<LogicalResult>
Encoder::encodeArithmeticOperation(FunctionPlan &plan, Operation *operation) {
  if (auto constant = dyn_cast<arith::ConstantOp>(operation)) {
    if (auto integer = dyn_cast<IntegerAttr>(constant.getValue()))
      return emitConstant(plan, constant.getResult(), integer.getValue());
    if (auto floating = dyn_cast<FloatAttr>(constant.getValue()))
      return emitConstant(plan, constant.getResult(),
                          floating.getValue().bitcastToAPInt());
    return operation->emitOpError(
        "bytecode requires integer or floating-point constants");
  }
  auto binary = [&](uint16_t opcode, Value result, Value left, Value right) {
    emit({opcode, 0, reg(plan, result), reg(plan, left), reg(plan, right)});
    return success();
  };
  if (auto op = dyn_cast<arith::AddIOp>(operation))
    return binary(Add, op.getResult(), op.getLhs(), op.getRhs());
  if (auto op = dyn_cast<arith::SubIOp>(operation))
    return binary(Sub, op.getResult(), op.getLhs(), op.getRhs());
  if (auto op = dyn_cast<arith::MulIOp>(operation))
    return binary(Mul, op.getResult(), op.getLhs(), op.getRhs());
  if (auto op = dyn_cast<arith::AddFOp>(operation))
    return binary(FAdd, op.getResult(), op.getLhs(), op.getRhs());
  if (auto op = dyn_cast<arith::SubFOp>(operation))
    return binary(FSub, op.getResult(), op.getLhs(), op.getRhs());
  if (auto op = dyn_cast<arith::MulFOp>(operation))
    return binary(FMul, op.getResult(), op.getLhs(), op.getRhs());
  if (auto op = dyn_cast<arith::DivFOp>(operation))
    return binary(FDiv, op.getResult(), op.getLhs(), op.getRhs());
  if (auto op = dyn_cast<arith::NegFOp>(operation)) {
    emit({FNeg, 0, reg(plan, op.getResult()), reg(plan, op.getOperand())});
    return success();
  }
  if (auto op = dyn_cast<arith::DivUIOp>(operation))
    return binary(UDiv, op.getResult(), op.getLhs(), op.getRhs());
  if (auto op = dyn_cast<arith::DivSIOp>(operation))
    return binary(SDiv, op.getResult(), op.getLhs(), op.getRhs());
  if (auto op = dyn_cast<arith::RemUIOp>(operation))
    return binary(URem, op.getResult(), op.getLhs(), op.getRhs());
  if (auto op = dyn_cast<arith::RemSIOp>(operation))
    return binary(SRem, op.getResult(), op.getLhs(), op.getRhs());
  if (auto op = dyn_cast<arith::AndIOp>(operation))
    return binary(And, op.getResult(), op.getLhs(), op.getRhs());
  if (auto op = dyn_cast<arith::OrIOp>(operation))
    return binary(Or, op.getResult(), op.getLhs(), op.getRhs());
  if (auto op = dyn_cast<arith::XOrIOp>(operation))
    return binary(Xor, op.getResult(), op.getLhs(), op.getRhs());
  if (auto op = dyn_cast<arith::ShLIOp>(operation))
    return binary(Shl, op.getResult(), op.getLhs(), op.getRhs());
  if (auto op = dyn_cast<arith::ShRUIOp>(operation))
    return binary(LShr, op.getResult(), op.getLhs(), op.getRhs());
  if (auto op = dyn_cast<arith::ShRSIOp>(operation))
    return binary(AShr, op.getResult(), op.getLhs(), op.getRhs());
  if (auto op = dyn_cast<arith::CmpIOp>(operation)) {
    obelisk_rt_design_compare_kind predicate = OBELISK_RT_DB_CMP_EQ;
    switch (op.getPredicate()) {
    case arith::CmpIPredicate::eq:
      predicate = OBELISK_RT_DB_CMP_EQ;
      break;
    case arith::CmpIPredicate::ne:
      predicate = OBELISK_RT_DB_CMP_NE;
      break;
    case arith::CmpIPredicate::ult:
      predicate = OBELISK_RT_DB_CMP_ULT;
      break;
    case arith::CmpIPredicate::ule:
      predicate = OBELISK_RT_DB_CMP_ULE;
      break;
    case arith::CmpIPredicate::ugt:
      predicate = OBELISK_RT_DB_CMP_UGT;
      break;
    case arith::CmpIPredicate::uge:
      predicate = OBELISK_RT_DB_CMP_UGE;
      break;
    case arith::CmpIPredicate::slt:
      predicate = OBELISK_RT_DB_CMP_SLT;
      break;
    case arith::CmpIPredicate::sle:
      predicate = OBELISK_RT_DB_CMP_SLE;
      break;
    case arith::CmpIPredicate::sgt:
      predicate = OBELISK_RT_DB_CMP_SGT;
      break;
    case arith::CmpIPredicate::sge:
      predicate = OBELISK_RT_DB_CMP_SGE;
      break;
    }
    emit({Compare, predicate, reg(plan, op.getResult()), reg(plan, op.getLhs()),
          reg(plan, op.getRhs())});
    return success();
  }
  if (auto op = dyn_cast<arith::CmpFOp>(operation)) {
    uint32_t predicate;
    switch (op.getPredicate()) {
    case arith::CmpFPredicate::OEQ:
      predicate = OBELISK_RT_DB_FCMP_EQ;
      break;
    case arith::CmpFPredicate::UNE:
      predicate = OBELISK_RT_DB_FCMP_NE;
      break;
    case arith::CmpFPredicate::OLT:
      predicate = OBELISK_RT_DB_FCMP_LT;
      break;
    case arith::CmpFPredicate::OLE:
      predicate = OBELISK_RT_DB_FCMP_LE;
      break;
    case arith::CmpFPredicate::OGT:
      predicate = OBELISK_RT_DB_FCMP_GT;
      break;
    case arith::CmpFPredicate::OGE:
      predicate = OBELISK_RT_DB_FCMP_GE;
      break;
    default:
      return op.emitOpError("floating comparison predicate is not executable");
    }
    emit({FCompare, static_cast<uint16_t>(predicate), reg(plan, op.getResult()),
          reg(plan, op.getLhs()), reg(plan, op.getRhs())});
    return success();
  }
  if (auto op = dyn_cast<arith::SelectOp>(operation)) {
    emit({Select, OBELISK_RT_DB_SELECT_BINARY, reg(plan, op.getResult()),
          reg(plan, op.getTrueValue()), reg(plan, op.getFalseValue()),
          reg(plan, op.getCondition())});
    return success();
  }
  if (auto op = dyn_cast<arith::ExtUIOp>(operation)) {
    emit({Extract, OBELISK_RT_DB_EXTRACT_ZERO_EXTEND,
          reg(plan, op.getResult()), reg(plan, op.getIn()), kInvalidRegister});
    return success();
  }
  if (auto op = dyn_cast<arith::ExtSIOp>(operation)) {
    emit({Extract, OBELISK_RT_DB_EXTRACT_SIGN_EXTEND,
          reg(plan, op.getResult()), reg(plan, op.getIn()), kInvalidRegister});
    return success();
  }
  if (auto op = dyn_cast<arith::TruncIOp>(operation)) {
    emit({Extract, OBELISK_RT_DB_EXTRACT_ZERO_EXTEND,
          reg(plan, op.getResult()), reg(plan, op.getIn()), kInvalidRegister});
    return success();
  }
  if (auto op = dyn_cast<arith::ExtFOp>(operation)) {
    emit({FExt, 0, reg(plan, op.getResult()), reg(plan, op.getIn())});
    return success();
  }
  if (auto op = dyn_cast<arith::TruncFOp>(operation)) {
    emit({FTrunc, 0, reg(plan, op.getResult()), reg(plan, op.getIn())});
    return success();
  }
  if (auto op = dyn_cast<math::PowFOp>(operation))
    return binary(FPow, op.getResult(), op.getLhs(), op.getRhs());
  if (auto op = dyn_cast<arith::IndexCastOp>(operation)) {
    emit({Extract, OBELISK_RT_DB_EXTRACT_ZERO_EXTEND,
          reg(plan, op.getResult()), reg(plan, op.getIn()), kInvalidRegister});
    return success();
  }
  return std::nullopt;
}

} // namespace obelisk::bytecode
