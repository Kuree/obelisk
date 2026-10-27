//===- RandomRealZ3.h - IEEE conversion relations for compiler Z3 ---------===//

#pragma once

#include <cstdint>
#include <z3.h>

namespace talos::solver::detail {

/// IEEE 1800-2023 6.12.1 rounds integral casts halfway away from zero;
/// 20.5 truncates $rtoi. Finite results retain their destination's low bits,
/// including widths beyond a machine integer. Nonfinite two-state results
/// are zero; the caller separately retains a four-state unknown plane.
inline Z3_ast realIntegerBits(Z3_context context, Z3_ast payload,
                              unsigned precision, unsigned width,
                              bool truncate) {
  if ((precision != 32 && precision != 64) || !width)
    return nullptr;
  auto constant = [&](uint64_t value, unsigned bits) {
    return Z3_mk_unsigned_int64(context, value, Z3_mk_bv_sort(context, bits));
  };
  auto resize = [&](Z3_ast value, unsigned source, unsigned target) {
    return source < target   ? Z3_mk_zero_ext(context, target - source, value)
           : source > target ? Z3_mk_extract(context, target - 1, 0, value)
                             : value;
  };
  auto conjunction = [&](Z3_ast a, Z3_ast b) {
    Z3_ast values[] = {a, b};
    return Z3_mk_and(context, 2, values);
  };
  unsigned fraction = precision == 32 ? 23 : 52;
  unsigned exponentBits = precision == 32 ? 8 : 11;
  unsigned bias = precision == 32 ? 127 : 1023;
  Z3_ast exponent = resize(
      Z3_mk_extract(context, fraction + exponentBits - 1, fraction, payload),
      exponentBits, 32);
  Z3_ast finite = Z3_mk_not(
      context, Z3_mk_eq(context, exponent,
                        constant((uint64_t{1} << exponentBits) - 1, 32)));
  Z3_ast significand =
      Z3_mk_bvor(context,
                 resize(Z3_mk_extract(context, fraction - 1, 0, payload),
                        fraction, precision),
                 constant(uint64_t{1} << fraction, precision));
  Z3_ast small = conjunction(
      Z3_mk_bvuge(context, exponent, constant(bias, 32)),
      Z3_mk_bvult(context, exponent, constant(bias + fraction, 32)));
  Z3_ast shift = Z3_mk_bvsub(context, constant(bias + fraction, 32), exponent);
  Z3_ast smallMagnitude =
      Z3_mk_bvlshr(context, significand, resize(shift, 32, precision));
  if (!truncate) {
    Z3_ast round = Z3_mk_bvand(
        context,
        Z3_mk_bvlshr(context, significand,
                     resize(Z3_mk_bvsub(context, shift, constant(1, 32)), 32,
                            precision)),
        constant(1, precision));
    smallMagnitude = Z3_mk_bvadd(context, smallMagnitude, round);
  }
  smallMagnitude = resize(smallMagnitude, precision, width);
  shift = Z3_mk_bvsub(context, exponent, constant(bias + fraction, 32));
  Z3_ast large = conjunction(
      finite,
      conjunction(Z3_mk_bvuge(context, exponent, constant(bias + fraction, 32)),
                  Z3_mk_bvult(context, shift, constant(width, 32))));
  Z3_ast largeMagnitude = Z3_mk_bvshl(
      context, resize(significand, precision, width), resize(shift, 32, width));
  Z3_ast magnitude =
      Z3_mk_ite(context, small, smallMagnitude,
                Z3_mk_ite(context, large, largeMagnitude, constant(0, width)));
  if (!truncate)
    magnitude =
        Z3_mk_ite(context, Z3_mk_eq(context, exponent, constant(bias - 1, 32)),
                  constant(1, width), magnitude);
  Z3_ast negative = Z3_mk_eq(
      context, Z3_mk_extract(context, precision - 1, precision - 1, payload),
      constant(1, 1));
  return Z3_mk_ite(context, negative, Z3_mk_bvneg(context, magnitude),
                   magnitude);
}

} // namespace talos::solver::detail
