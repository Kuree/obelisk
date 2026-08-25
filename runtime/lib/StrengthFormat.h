//===- StrengthFormat.h - Canonical scalar strength fields ------*- C++ -*-===//

#ifndef OBELISK_RUNTIME_LIB_STRENGTHFORMAT_H
#define OBELISK_RUNTIME_LIB_STRENGTHFORMAT_H

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>

inline std::string obelisk_rt_strength_name(unsigned magnitude) {
  static constexpr std::string_view names[] = {"",   "Sm", "Me", "We",
                                               "La", "Pu", "St", "Su"};
  return magnitude < std::size(names) ? std::string(names[magnitude]) : "";
}

// Canonical IEEE 1800-2017 21.2.1.5 rendering shared by output formatting and
// the exhaustive scanner round-trip test.
inline std::string obelisk_rt_format_strength_range(uint16_t strengths) {
  strengths &= (uint16_t{1} << 15) - 1;
  if (strengths == 0)
    return {};
  int low = -7;
  while (low <= 7 && (strengths & (uint16_t{1} << (low + 7))) == 0)
    ++low;
  int high = 7;
  while (high >= -7 && (strengths & (uint16_t{1} << (high + 7))) == 0)
    --high;
  if (low == 0 && high == 0)
    return "HiZ";
  if (low == high) {
    unsigned magnitude = static_cast<unsigned>(low < 0 ? -low : low);
    return obelisk_rt_strength_name(magnitude) + (low < 0 ? "0" : "1");
  }
  if (low < 0 && high == 0)
    return obelisk_rt_strength_name(static_cast<unsigned>(-low)) + "L";
  if (low == 0 && high > 0)
    return obelisk_rt_strength_name(static_cast<unsigned>(high)) + "H";
  if (low < 0 && high > 0) {
    if (-low == high)
      return obelisk_rt_strength_name(static_cast<unsigned>(high)) + "X";
    return std::to_string(-low) + std::to_string(high) + "X";
  }
  if (high < 0)
    return std::to_string(-low) + std::to_string(-high) + "0";
  return std::to_string(high) + std::to_string(low) + "1";
}

// Parse exactly one canonical three-character strength field and return its
// four-state logic component. The caller owns any cursor or stream rollback.
inline bool obelisk_rt_parse_strength_field(const char *data, uint64_t size,
                                            char &logic) noexcept {
  if (!data || size != 3)
    return false;
  char first = data[0];
  char second = data[1];
  char component = data[2];

  bool firstDigit = first >= '1' && first <= '7';
  bool secondDigit = second >= '1' && second <= '7';
  if ((first >= '0' && first <= '9') ||
      (second >= '0' && second <= '9')) {
    if (!firstDigit || !secondDigit)
      return false;
    unsigned firstLevel = static_cast<unsigned>(first - '0');
    unsigned secondLevel = static_cast<unsigned>(second - '0');
    if ((component == '0' || component == '1')) {
      if (firstLevel <= secondLevel)
        return false;
    } else if (component == 'X') {
      if (firstLevel == secondLevel)
        return false;
    } else {
      return false;
    }
  } else {
    std::string_view mnemonic(data, 2);
    if (mnemonic == "Hi") {
      if (component != 'Z')
        return false;
    } else {
      static constexpr std::array<std::string_view, 7> mnemonics = {
          "Su", "St", "Pu", "La", "We", "Me", "Sm"};
      if (std::find(mnemonics.begin(), mnemonics.end(), mnemonic) ==
          mnemonics.end())
        return false;
      if (component != '0' && component != '1' && component != 'X' &&
          component != 'L' && component != 'H')
        return false;
    }
  }

  logic = component == 'L' ? '0' : component == 'H' ? '1' : component;
  return true;
}

#endif // OBELISK_RUNTIME_LIB_STRENGTHFORMAT_H
