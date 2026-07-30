//===- DesignBytecodeLogic.h - Bytecode integer value semantics -*- C++ -*-===//

#ifndef OBELISK_RUNTIME_LIB_DESIGNBYTECODELOGIC_H
#define OBELISK_RUNTIME_LIB_DESIGNBYTECODELOGIC_H

#include <cstdint>
#include <utility>
#include <vector>

namespace obelisk::designbytecode {

struct Logic {
  uint32_t width = 0;
  bool fourState = false;
  std::vector<uint64_t> value;
  std::vector<uint64_t> unknown;
};

constexpr uint64_t limbCount(uint64_t width) { return (width + 63) / 64; }

constexpr uint64_t finalMask(uint64_t width) {
  unsigned tail = static_cast<unsigned>(width % 64);
  return tail == 0 ? UINT64_MAX : (uint64_t{1} << tail) - 1;
}

bool anyUnknown(const Logic &value);
bool isZero(const Logic &value);
Logic allX(uint32_t width, bool fourState = true);
void mask(Logic &value);
int compareUnsigned(const std::vector<uint64_t> &left,
                    const std::vector<uint64_t> &right);
Logic negate(Logic value);
double integerToDouble(Logic integer, bool isSigned);
float integerToFloat(Logic integer, bool isSigned);
Logic doubleToInteger(double value, uint32_t width);
Logic add(const Logic &left, const Logic &right, bool subtract);
Logic multiply(const Logic &left, const Logic &right);
bool bit(const std::vector<uint64_t> &value, uint64_t index);
void setBit(std::vector<uint64_t> &value, uint64_t index, bool enabled);
std::pair<Logic, Logic> divide(const Logic &dividend, const Logic &divisor,
                               bool isSigned);
Logic bitwise(const Logic &left, const Logic &right, uint16_t opcode);
Logic shift(const Logic &input, const Logic &amount, uint16_t opcode);

} // namespace obelisk::designbytecode

#endif // OBELISK_RUNTIME_LIB_DESIGNBYTECODELOGIC_H
