//===- DesignBytecodeLogic.h - Bytecode integer value semantics -*- C++ -*-===//

#ifndef OBELISK_RUNTIME_LIB_DESIGNBYTECODELOGIC_H
#define OBELISK_RUNTIME_LIB_DESIGNBYTECODELOGIC_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <utility>
#include <vector>

namespace obelisk::designbytecode {

// Bytecode values are overwhelmingly one or two machine words wide. Keep
// those limbs in the value itself and use dynamic storage only for wider
// integers.
class LimbVector {
public:
  LimbVector() = default;
  LimbVector(size_t count, uint64_t value = 0) { assign(count, value); }
  LimbVector(std::initializer_list<uint64_t> values) {
    assign(values.begin(), values.size());
  }

  LimbVector(const LimbVector &) = default;
  LimbVector &operator=(const LimbVector &) = default;
  LimbVector(LimbVector &&other) noexcept
      : count(other.count), inlineLimbs(other.inlineLimbs),
        dynamicLimbs(std::move(other.dynamicLimbs)) {
    other.count = 0;
  }
  LimbVector &operator=(LimbVector &&other) noexcept {
    if (this == &other)
      return *this;
    count = other.count;
    inlineLimbs = other.inlineLimbs;
    dynamicLimbs = std::move(other.dynamicLimbs);
    other.count = 0;
    return *this;
  }

  void assign(size_t newCount, uint64_t value) {
    count = newCount;
    if (newCount > inlineLimbs.size()) {
      dynamicLimbs.assign(newCount, value);
      return;
    }
    dynamicLimbs.clear();
    inlineLimbs.fill(value);
  }

  size_t size() const { return count; }
  bool empty() const { return count == 0; }
  uint64_t *data() {
    return count > inlineLimbs.size() ? dynamicLimbs.data()
                                      : inlineLimbs.data();
  }
  const uint64_t *data() const {
    return count > inlineLimbs.size() ? dynamicLimbs.data()
                                      : inlineLimbs.data();
  }
  uint64_t *begin() { return data(); }
  const uint64_t *begin() const { return data(); }
  uint64_t *end() { return data() + count; }
  const uint64_t *end() const { return data() + count; }
  uint64_t &front() { return data()[0]; }
  const uint64_t &front() const { return data()[0]; }
  uint64_t &back() { return data()[count - 1]; }
  const uint64_t &back() const { return data()[count - 1]; }
  uint64_t &operator[](size_t index) { return data()[index]; }
  const uint64_t &operator[](size_t index) const { return data()[index]; }
  std::vector<uint64_t> takeVector() && {
    if (count > inlineLimbs.size()) {
      count = 0;
      return std::move(dynamicLimbs);
    }
    return {inlineLimbs.begin(), inlineLimbs.begin() + count};
  }
  friend bool operator==(const LimbVector &left, const LimbVector &right) {
    if (left.size() != right.size())
      return false;
    for (size_t index = 0; index != left.size(); ++index)
      if (left[index] != right[index])
        return false;
    return true;
  }

private:
  void assign(const uint64_t *values, size_t newCount) {
    count = newCount;
    if (newCount > inlineLimbs.size()) {
      dynamicLimbs.assign(values, values + newCount);
      return;
    }
    dynamicLimbs.clear();
    inlineLimbs.fill(0);
    for (size_t index = 0; index != newCount; ++index)
      inlineLimbs[index] = values[index];
  }

  size_t count = 0;
  std::array<uint64_t, 2> inlineLimbs{};
  std::vector<uint64_t> dynamicLimbs;
};

struct Logic {
  uint32_t width = 0;
  bool fourState = false;
  LimbVector value;
  LimbVector unknown;
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
int compareUnsigned(const LimbVector &left, const LimbVector &right);
Logic negate(Logic value);
double integerToDouble(Logic integer, bool isSigned);
float integerToFloat(Logic integer, bool isSigned);
Logic doubleToInteger(double value, uint32_t width);
Logic add(const Logic &left, const Logic &right, bool subtract);
Logic multiply(const Logic &left, const Logic &right);
bool bit(const LimbVector &value, uint64_t index);
void setBit(LimbVector &value, uint64_t index, bool enabled);
bool bit(const std::vector<uint64_t> &value, uint64_t index);
void setBit(std::vector<uint64_t> &value, uint64_t index, bool enabled);
std::pair<Logic, Logic> divide(const Logic &dividend, const Logic &divisor,
                               bool isSigned);
Logic bitwise(const Logic &left, const Logic &right, uint16_t opcode);
Logic shift(const Logic &input, const Logic &amount, uint16_t opcode);

} // namespace obelisk::designbytecode

#endif // OBELISK_RUNTIME_LIB_DESIGNBYTECODELOGIC_H
