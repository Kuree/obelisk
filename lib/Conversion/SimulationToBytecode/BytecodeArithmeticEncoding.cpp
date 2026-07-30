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
    uint16_t predicate = 0;
    switch (op.getPredicate()) {
    case arith::CmpIPredicate::eq:
      predicate = 0;
      break;
    case arith::CmpIPredicate::ne:
      predicate = 1;
      break;
    case arith::CmpIPredicate::ult:
      predicate = 2;
      break;
    case arith::CmpIPredicate::ule:
      predicate = 3;
      break;
    case arith::CmpIPredicate::ugt:
      predicate = 4;
      break;
    case arith::CmpIPredicate::uge:
      predicate = 5;
      break;
    case arith::CmpIPredicate::slt:
      predicate = 6;
      break;
    case arith::CmpIPredicate::sle:
      predicate = 7;
      break;
    case arith::CmpIPredicate::sgt:
      predicate = 8;
      break;
    case arith::CmpIPredicate::sge:
      predicate = 9;
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
      predicate = 0;
      break;
    case arith::CmpFPredicate::UNE:
      predicate = 1;
      break;
    case arith::CmpFPredicate::OLT:
      predicate = 2;
      break;
    case arith::CmpFPredicate::OLE:
      predicate = 3;
      break;
    case arith::CmpFPredicate::OGT:
      predicate = 4;
      break;
    case arith::CmpFPredicate::OGE:
      predicate = 5;
      break;
    default:
      return op.emitOpError("floating comparison predicate is not executable");
    }
    emit({FCompare, static_cast<uint16_t>(predicate), reg(plan, op.getResult()),
          reg(plan, op.getLhs()), reg(plan, op.getRhs())});
    return success();
  }
  if (auto op = dyn_cast<arith::SelectOp>(operation)) {
    emit({Select, 0, reg(plan, op.getResult()), reg(plan, op.getTrueValue()),
          reg(plan, op.getFalseValue()), reg(plan, op.getCondition())});
    return success();
  }
  if (auto op = dyn_cast<arith::ExtUIOp>(operation)) {
    emit({Extract, 0, reg(plan, op.getResult()), reg(plan, op.getIn()),
          kInvalidRegister});
    return success();
  }
  if (auto op = dyn_cast<arith::ExtSIOp>(operation)) {
    emit({Extract, 1, reg(plan, op.getResult()), reg(plan, op.getIn()),
          kInvalidRegister});
    return success();
  }
  if (auto op = dyn_cast<arith::TruncIOp>(operation)) {
    emit({Extract, 0, reg(plan, op.getResult()), reg(plan, op.getIn()),
          kInvalidRegister});
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
    emit({Extract, 0, reg(plan, op.getResult()), reg(plan, op.getIn()),
          kInvalidRegister});
    return success();
  }
  return std::nullopt;
}

} // namespace obelisk::bytecode
