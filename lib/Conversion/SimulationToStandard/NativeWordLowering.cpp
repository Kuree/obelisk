#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "obelisk/Conversion/SimulationToStandard.h"
#include "obelisk/Dialect/Schedule/ScheduleOps.h"
#include "obelisk/Dialect/Simulation/SimulationTypes.h"
#include "llvm/ADT/DenseMap.h"
#include <numeric>

using namespace mlir;
namespace obelisk {
namespace {
// Reduced ordered Boolean decision diagrams with complemented edges. The
// truth table is tiny (<=6 variables); choose the cheapest variable ordering
// jointly for both planes. Every node is a word Boolean operation, independent
// of the number of simulation lanes. No runtime table or helper is emitted.
struct DecisionDiagram {
  struct Node {
    unsigned variable, low, high;
  };
  SmallVector<Node> nodes;
  DenseMap<std::tuple<unsigned, unsigned, unsigned>, unsigned> unique;
  unsigned value = 0, unknown = 0;
  unsigned build(const APInt &table, ArrayRef<unsigned> order) {
    if (table.isZero())
      return 0;
    if (table.isAllOnes())
      return 1;
    unsigned half = table.getBitWidth() / 2;
    APInt low(half, 0), high(half, 0);
    for (unsigned i = 0; i < half; ++i) {
      low.setBitVal(i, table[2 * i]);
      high.setBitVal(i, table[2 * i + 1]);
    }
    unsigned lo = build(low, order.drop_front());
    unsigned hi = build(high, order.drop_front());
    if (lo == hi)
      return lo;
    unsigned complement = lo & 1;
    lo ^= complement;
    hi ^= complement;
    auto [it, inserted] = unique.try_emplace(std::tuple{order.front(), lo, hi},
                                             (nodes.size() + 1) * 2);
    if (inserted)
      nodes.push_back({order.front(), lo, hi});
    return it->second ^ complement;
  }
  unsigned cost() const {
    unsigned count = 0;
    for (auto node : nodes)
      count += node.low == 0 && node.high == 1 ? 0
               : node.low == 0 || node.high == 1 || node.low == (node.high ^ 1)
                   ? 1
                   : 3;
    return count;
  }
};
APInt reorder(const APInt &table, ArrayRef<unsigned> order) {
  APInt result(table.getBitWidth(), 0);
  for (unsigned row = 0; row < table.getBitWidth(); ++row) {
    unsigned original = 0;
    for (auto [position, variable] : llvm::enumerate(order))
      original |= ((row >> position) & 1) << variable;
    result.setBitVal(row, table[original]);
  }
  return result;
}
class WordTableConversion final
    : public OpConversionPattern<schedule::NativeWordTableOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(schedule::NativeWordTableOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    SmallVector<Value> inputs;
    for (ValueRange operand : adaptor.getOperands())
      llvm::append_range(inputs, operand);
    unsigned width = *sim::getPackedWidth(op.getResult().getType());
    auto type = rewriter.getIntegerType(width);
    SmallVector<unsigned> order(inputs.size());
    std::iota(order.begin(), order.end(), 0);
    std::optional<DecisionDiagram> best;
    do {
      DecisionDiagram diagram;
      diagram.value = diagram.build(reorder(op.getValueTable(), order), order);
      diagram.unknown =
          diagram.build(reorder(op.getUnknownTable(), order), order);
      if (!best || diagram.cost() < best->cost())
        best = std::move(diagram);
    } while (std::next_permutation(order.begin(), order.end()));
    Location loc = op.getLoc();
    Value zero = arith::ConstantOp::create(rewriter, loc, type,
                                           rewriter.getIntegerAttr(type, 0));
    Value ones = arith::ConstantOp::create(
        rewriter, loc, type,
        rewriter.getIntegerAttr(type, APInt::getAllOnes(width)));
    DenseMap<unsigned, Value> values{{0, zero}, {1, ones}};
    auto get = [&](unsigned edge) -> Value {
      if (auto found = values.find(edge); found != values.end())
        return found->second;
      Value result =
          arith::XOrIOp::create(rewriter, loc, values.lookup(edge ^ 1), ones);
      values[edge] = result;
      return result;
    };
    for (auto [index, node] : llvm::enumerate(best->nodes)) {
      Value condition = inputs[node.variable];
      Value low = get(node.low), high = get(node.high), result;
      if (node.low == 0 && node.high == 1)
        result = condition;
      else if (node.low == 0)
        result = arith::AndIOp::create(rewriter, loc, condition, high);
      else if (node.high == 1)
        result = arith::OrIOp::create(rewriter, loc, condition, low);
      else if (node.low == (node.high ^ 1))
        result = arith::XOrIOp::create(rewriter, loc, condition, low);
      else {
        Value inverse = arith::XOrIOp::create(rewriter, loc, condition, ones);
        Value yes = arith::AndIOp::create(rewriter, loc, condition, high);
        Value no = arith::AndIOp::create(rewriter, loc, inverse, low);
        result = arith::OrIOp::create(rewriter, loc, yes, no);
      }
      values[(index + 1) * 2] = result;
    }
    SmallVector<Value> result{get(best->value)};
    if (isa<sim::LogicType>(op.getResult().getType()))
      result.push_back(get(best->unknown));
    rewriter.replaceOpWithMultiple(op, {ValueRange(result)});
    return success();
  }
};
class WordReverseConversion final
    : public OpConversionPattern<schedule::NativeWordReverseOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(schedule::NativeWordReverseOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    SmallVector<Value> planes;
    for (Value value : adaptor.getInput()) {
      if (op.getLaneWidth() == 1) {
        planes.push_back(
            LLVM::BitReverseOp::create(rewriter, op.getLoc(), value));
        continue;
      }
      auto type = cast<IntegerType>(value.getType());
      unsigned width = type.getWidth(), lane = op.getLaneWidth();
      auto constant = [&](APInt bits) -> Value {
        return arith::ConstantOp::create(rewriter, op.getLoc(), type,
                                         rewriter.getIntegerAttr(type, bits));
      };
      Value mask = constant(APInt::getLowBitsSet(width, lane));
      Value result = constant(APInt(width, 0));
      // Reverse elements rather than their bits. Constant shifts/masks fold
      // to byte/halfword reversal instructions where the target supports them.
      for (unsigned offset = 0; offset < width; offset += lane) {
        Value part = arith::ShRUIOp::create(rewriter, op.getLoc(), value,
                                            constant(APInt(width, offset)));
        part = arith::AndIOp::create(rewriter, op.getLoc(), part, mask);
        part = arith::ShLIOp::create(
            rewriter, op.getLoc(), part,
            constant(APInt(width, width - lane - offset)));
        result = arith::OrIOp::create(rewriter, op.getLoc(), result, part);
      }
      planes.push_back(result);
    }
    rewriter.replaceOpWithMultiple(op, {ValueRange(planes)});
    return success();
  }
};
class WordResizeLanesConversion final
    : public OpConversionPattern<schedule::NativeWordResizeLanesOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(schedule::NativeWordResizeLanesOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    unsigned from = op.getInputLaneWidth(), to = op.getResultLaneWidth();
    unsigned width = *sim::getPackedWidth(op.getInput().getType());
    auto inputType =
        VectorType::get({width / from}, rewriter.getIntegerType(from));
    auto resultType =
        VectorType::get({width / from}, rewriter.getIntegerType(to));
    SmallVector<Value> planes;
    for (Value value : adaptor.getInput()) {
      if (from != to) {
        value = LLVM::BitcastOp::create(rewriter, op.getLoc(), inputType, value);
        value = from < to
                    ? LLVM::ZExtOp::create(rewriter, op.getLoc(), resultType,
                                           value).getResult()
                    : LLVM::TruncOp::create(rewriter, op.getLoc(), resultType,
                                            value).getResult();
        value = LLVM::BitcastOp::create(
            rewriter, op.getLoc(), rewriter.getIntegerType(width / from * to),
            value);
      }
      planes.push_back(value);
    }
    rewriter.replaceOpWithMultiple(op, {ValueRange(planes)});
    return success();
  }
};
} // namespace
void populateNativeWordValuePatterns(const TypeConverter &converter,
                                     RewritePatternSet &patterns) {
  patterns.add<WordTableConversion, WordReverseConversion,
               WordResizeLanesConversion>(
      converter, patterns.getContext());
}
} // namespace obelisk
