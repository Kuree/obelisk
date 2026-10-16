//===- GeneratedVPIConstantEvaluationTest.cpp - Constant arithmetic -------===//

#include "../lib/VPIExpressionValue.h"
#include "obelisk/Dialect/Simulation/SimulationVPIConstantValues.h"
#include "gtest/gtest.h"
#include <array>
#include <vector>

TEST(VPIConstantEvaluation, CompilerRuntimeParity) {
  using namespace obelisk;
  using Shape = reflection::VPIIntegralExpressionShape;
  unsigned checked = 0;
  for (unsigned width : {1u, 8u, 32u, 64u, 65u, 130u})
    for (bool sign : {false, true})
      for (bool state : {false, true})
        for (uint32_t code = 1; code <= 89; ++code) {
          if (reflection::integralOperatorRule(code) ==
              reflection::VPIIntegralOperatorRule::Unsupported)
            continue;
          const unsigned count = reflection::findVPISourceOperator(code)->arity;
          bool context = reflection::hasIntegralOperatorContext(code);
          Shape result{context ? width : 1, context && sign,
                       state || code == 12 || code == 13 || code == 43, code};
          if (code == 16 || code == 17)
            result.fourState = false;
          std::array<Shape, 3> shapes;
          for (auto &shape : shapes)
            shape = {width, sign, state, 0};
          ASSERT_TRUE(reflection::validVPIIntegralOperator(
              code, result, shapes.data(), count));
          for (unsigned sample = 0; sample != 20; ++sample) {
            SCOPED_TRACE(::testing::Message()
                         << width << '/' << sign << '/' << state << '/' << code
                         << '/' << sample);
            std::vector<sim::VPIConstantValue> constants;
            std::array<designbytecode::Logic, 3> operands;
            for (unsigned index = 0; index != count; ++index) {
              llvm::APInt value(width, (sample + 3 * index) % 9, false, true);
              if ((sample + index) % 4 == 0)
                value = ~value;
              if ((sample + index) % 7 == 0)
                value.setBit(width - 1);
              llvm::APInt unknown(width, 0);
              if (state && sample % 3 == 0) {
                unknown.setBit((sample + index) % width);
                if (sample % 2)
                  unknown.setBit(width - 1);
              }
              constants.push_back({value, unknown});
              operands[index].width = width;
              operands[index].fourState = state;
              operands[index].value.assign(value.getNumWords(), 0);
              operands[index].unknown.assign(unknown.getNumWords(), 0);
              for (unsigned word = 0; word != value.getNumWords(); ++word) {
                operands[index].value[word] = value.getRawData()[word];
                operands[index].unknown[word] = unknown.getRawData()[word];
              }
            }
            auto constant = sim::evaluateVPIIntegralConstant(
                code, result, constants,
                llvm::ArrayRef<Shape>(shapes.data(), count));
            ASSERT_TRUE(constant);
            auto runtime =
                vpiexpression::evaluate(code, result, operands, shapes);
            EXPECT_EQ(constant->value,
                      llvm::APInt(result.width, llvm::ArrayRef<uint64_t>(
                                                    runtime.value.data(),
                                                    runtime.value.size())));
            EXPECT_EQ(constant->unknown,
                      llvm::APInt(result.width, llvm::ArrayRef<uint64_t>(
                                                    runtime.unknown.data(),
                                                    runtime.unknown.size())));
            ++checked;
          }
        }
  EXPECT_GT(checked, 10000u);
}

TEST(VPIConstantEvaluation, CheckedIntegerBounds) {
  using namespace obelisk::sim;
  using llvm::APInt;
  EXPECT_EQ(knownVPIConstantInteger(
                {APInt(65, UINT64_MAX).sextOrTrunc(65), APInt(65, 0)}, true),
            std::nullopt);
  EXPECT_EQ(
      knownVPIConstantInteger({APInt::getAllOnes(130), APInt(130, 0)}, true),
      -1);
  EXPECT_EQ(
      knownVPIConstantInteger({APInt(64, INT64_MAX), APInt(64, 0)}, false),
      INT64_MAX);
  EXPECT_EQ(knownVPIConstantInteger(
                {APInt(64, uint64_t{1} << 63), APInt(64, 0)}, false),
            std::nullopt);
  EXPECT_EQ(knownVPIConstantInteger(
                {APInt(64, uint64_t{1} << 63), APInt(64, 0)}, true),
            INT64_MIN);
  EXPECT_EQ(knownVPIConstantInteger({APInt(32, 1), APInt(32, 1)}, true),
            std::nullopt);
}

TEST(VPIConstantEvaluation, IntegralCornerCases) {
  using namespace obelisk;
  using Shape = reflection::VPIIntegralExpressionShape;
  using llvm::APInt;
  auto check = [&](uint32_t code, std::initializer_list<uint8_t> bits,
                   std::initializer_list<uint8_t> unknown, uint8_t expected,
                   uint8_t expectedUnknown) {
    std::vector<sim::VPIConstantValue> inputs;
    std::vector<Shape> shapes;
    auto u = unknown.begin();
    for (uint8_t value : bits) {
      inputs.push_back({APInt(8, value), APInt(8, *u++)});
      shapes.push_back({8, true, true, 0});
    }
    auto result = sim::evaluateVPIIntegralConstant(code, {8, true, true, code},
                                                   inputs, shapes);
    ASSERT_TRUE(result);
    EXPECT_EQ(result->value, APInt(8, expected));
    EXPECT_EQ(result->unknown, APInt(8, expectedUnknown));
  };
  check(12, {128, 255}, {0, 0}, 128, 0); // Signed minimum / -1 wraps.
  check(13, {128, 255}, {0, 0}, 0, 0);
  check(42, {252, 255}, {0, 0}, 255, 0); // Large unsigned shift amount.
  check(22, {252, 255}, {0, 0}, 0, 0);
  check(28, {0, 255}, {0, 255}, 0, 0); // Known zero controls Z.
  check(29, {255, 0}, {0, 255}, 255, 0);
  check(32, {0, 255, 255}, {255, 255, 255}, 0, 255);
  check(43, {255, 253}, {0, 0}, 255, 0); // (-1) ** -3.
  check(43, {2, 255}, {0, 0}, 0, 0);
  check(43, {0, 255}, {0, 0}, 0, 255);
}

TEST(VPIConstantEvaluation, IntegralCasts) {
  using namespace obelisk;
  using Shape = reflection::VPIIntegralExpressionShape;
  using llvm::APInt;
  // Independent bit-string oracles cover crossing limb boundaries, truncation,
  // source-signed extension, and X/Z replacement when entering a 2-state type.
  auto check = [&](std::string input, bool sourceSign, bool targetSign,
                   bool targetState, std::string expected) {
    unsigned inputWidth = input.size(), outputWidth = expected.size();
    APInt value(inputWidth, 0), unknown(inputWidth, 0);
    for (unsigned index = 0; index != inputWidth; ++index) {
      char bit = input[inputWidth - index - 1];
      if (bit == '1' || bit == 'z')
        value.setBit(index);
      if (bit == 'x' || bit == 'z')
        unknown.setBit(index);
    }
    Shape result{outputWidth, targetSign, targetState, 67};
    std::array<Shape, 3> shapes{{{inputWidth, sourceSign, true, 0}, {}, {}}};
    sim::VPIConstantValue inputValue{value, unknown};
    auto constant = sim::evaluateVPIIntegralConstant(
        67, result, llvm::ArrayRef<sim::VPIConstantValue>(inputValue),
        llvm::ArrayRef<Shape>(shapes.data(), 1));
    ASSERT_TRUE(constant);
    std::array<designbytecode::Logic, 3> values;
    values[0].width = inputWidth;
    values[0].fourState = true;
    values[0].value.assign(value.getNumWords(), 0);
    values[0].unknown.assign(unknown.getNumWords(), 0);
    for (unsigned word = 0; word != value.getNumWords(); ++word) {
      values[0].value[word] = value.getRawData()[word];
      values[0].unknown[word] = unknown.getRawData()[word];
    }
    auto runtime = vpiexpression::evaluate(67, result, values, shapes);
    std::string compilerBits, runtimeBits;
    for (unsigned bit = outputWidth; bit-- != 0;) {
      bool v = constant->value[bit], u = constant->unknown[bit];
      compilerBits += u ? (v ? 'z' : 'x') : (v ? '1' : '0');
      v = designbytecode::bit(runtime.value, bit);
      u = designbytecode::bit(runtime.unknown, bit);
      runtimeBits += u ? (v ? 'z' : 'x') : (v ? '1' : '0');
    }
    EXPECT_EQ(compilerBits, expected);
    EXPECT_EQ(runtimeBits, expected);
  };
  check("11111010", false, true, false, "0000000011111010");
  check("11111010", true, false, false, "1111111111111010");
  check("11111010", true, false, true, std::string(122, '1') + "11111010");
  check(std::string(120, '1') + "0101010101", false, true, false, "01010101");
  check("x10z01x1", true, true, false, "0000000001000101");
  check("z10x01z1", true, false, true, std::string(57, 'z') + "z10x01z1");
  check("z10x01z1", false, true, false, std::string(122, '0') + "01000101");
  Shape child{8, false, true, 24};
  EXPECT_FALSE(reflection::validVPIIntegralOperator(67, {32, true, false, 67},
                                                    &child, 1));
  child.width = 32;
  EXPECT_TRUE(reflection::validVPIIntegralOperator(67, {32, true, false, 67},
                                                   &child, 1));
  EXPECT_TRUE(reflection::validVPIIntegralOperator(67, {8, true, false, 67},
                                                   &child, 1));
  EXPECT_FALSE(reflection::validVPISourceOnlyArity(67, 1));
}

TEST(VPIConstantEvaluation, Concatenations) {
  using namespace obelisk;
  using Shape = reflection::VPIIntegralExpressionShape;
  using llvm::APInt;
  auto check = [&](uint32_t code, std::vector<std::string> bits,
                   std::vector<Shape> shapes, Shape result,
                   std::optional<std::string> expected) {
    std::vector<sim::VPIConstantValue> constants;
    std::vector<designbytecode::Logic> values;
    ASSERT_EQ(bits.size(), shapes.size());
    for (size_t index = 0; index != bits.size(); ++index) {
      const auto &input = bits[index];
      unsigned width = input.size();
      ASSERT_EQ(width, shapes[index].width);
      APInt value(width, 0), unknown(width, 0);
      designbytecode::Logic runtime{width, shapes[index].fourState, {}, {}};
      runtime.value.assign(designbytecode::limbCount(width), 0);
      runtime.unknown.assign(designbytecode::limbCount(width), 0);
      for (unsigned bit = 0; bit != width; ++bit) {
        char digit = input[width - bit - 1];
        bool v = digit == '1' || digit == 'z';
        bool u = digit == 'x' || digit == 'z';
        if (v)
          value.setBit(bit);
        if (u)
          unknown.setBit(bit);
        designbytecode::setBit(runtime.value, bit, v);
        designbytecode::setBit(runtime.unknown, bit, u);
      }
      constants.push_back({std::move(value), std::move(unknown)});
      values.push_back(std::move(runtime));
    }
    auto compiler =
        sim::evaluateVPIIntegralConstant(code, result, constants, shapes);
    auto runtime = vpiexpression::evaluateConcatenation(
        code, result, values.data(), shapes.data(), shapes.size());
    if (!expected) {
      EXPECT_FALSE(compiler);
      EXPECT_FALSE(runtime);
      return;
    }
    ASSERT_TRUE(compiler);
    ASSERT_TRUE(runtime);
    std::string compilerBits, runtimeBits;
    for (unsigned bit = result.width; bit-- != 0;) {
      bool v = compiler->value[bit], u = compiler->unknown[bit];
      compilerBits += u ? (v ? 'z' : 'x') : (v ? '1' : '0');
      v = designbytecode::bit(runtime->value, bit);
      u = designbytecode::bit(runtime->unknown, bit);
      runtimeBits += u ? (v ? 'z' : 'x') : (v ? '1' : '0');
    }
    EXPECT_EQ(compilerBits, *expected);
    EXPECT_EQ(runtimeBits, *expected);
  };
  // More than three operands, mixed state domains, and limb boundaries. The
  // independent string oracle also proves operand order and X/Z preservation.
  check(33, {"10", "xz1", "0", std::string(65, '1')},
        {{2, true, false},
         {3, false, true},
         {1, false, false},
         {65, false, false}},
        {71, false, true}, "10xz10" + std::string(65, '1'));
  check(34, {"10", "10", "xz"},
        {{2, false, false}, {2, true, false}, {2, false, true}},
        {8, false, true}, "10xz10xz");
  check(34, {"10", std::string(64, '1') + "0"},
        {{2, false, false}, {65, false, false}}, {130, false, false},
        std::string(64, '1') + "0" + std::string(64, '1') + "0");
  // A known four-state multiplier does not make two-state data four-state.
  check(34, {"10", "01"}, {{2, false, true}, {2, true, false}},
        {4, false, false}, "0101");
  check(34, {"1x", "01"}, {{2, false, true}, {2, true, false}},
        {4, false, false}, std::nullopt);
  check(34, {"10", "01"}, {{2, true, false}, {2, false, false}},
        {4, false, false}, std::nullopt);
  check(34, {"00", "01"}, {{2, false, false}, {2, false, false}},
        {4, false, false}, std::nullopt);
  check(34, {std::string(65, '1'), "01"},
        {{65, false, false}, {2, false, false}}, {4, false, false},
        std::nullopt);
  check(33, {"10", "01"}, {{2, false, false}, {2, false, false}},
        {3, false, false}, std::nullopt);
  check(33, {"10", "01"}, {{2, false, false}, {2, false, false}},
        {4, true, false}, std::nullopt);
  check(33, {"10", "01"}, {{2, false, true}, {2, false, false}},
        {4, false, false}, std::nullopt);
}
