#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

using Bits = unsigned __int128;

extern "C" {
Bits hs_f128_literal(const char*);
Bits hs_f128_add(Bits, Bits);
Bits hs_f128_sub(Bits, Bits);
Bits hs_f128_mul(Bits, Bits);
Bits hs_f128_div(Bits, Bits);
std::uint8_t hs_f128_eq(Bits, Bits);
std::uint8_t hs_f128_ne(Bits, Bits);
std::uint8_t hs_f128_lt(Bits, Bits);
std::uint8_t hs_f128_le(Bits, Bits);
std::uint8_t hs_f128_gt(Bits, Bits);
std::uint8_t hs_f128_ge(Bits, Bits);
Bits hs_f128_from_i64(std::int64_t);
Bits hs_f128_from_u64(std::uint64_t);
Bits hs_f128_from_f32(float);
Bits hs_f128_from_f64(double);
std::int64_t hs_f128_to_i64(Bits);
std::uint64_t hs_f128_to_u64(Bits);
float hs_f128_to_f32(Bits);
double hs_f128_to_f64(Bits);
Bits hs_f128_abs(Bits);
Bits hs_f128_sqrt(Bits);
Bits hs_f128_pow(Bits, Bits);
Bits hs_f128_sin(Bits);
Bits hs_f128_cos(Bits);
Bits hs_f128_tan(Bits);
Bits hs_f128_log(Bits);
Bits hs_f128_exp(Bits);
Bits hs_f128_floor(Bits);
Bits hs_f128_ceil(Bits);
Bits hs_f128_round(Bits);
int hs_f128_format(const void*, char*, std::size_t);
int hs_f128_parse(const char*, void*);
}

namespace {

constexpr Bits bits(std::uint64_t high, std::uint64_t low) {
  return (Bits{high} << 64U) | Bits{low};
}

bool expectEqual(const char* name, Bits actual, Bits expected) {
  if (actual == expected) {
    return true;
  }
  std::fprintf(stderr,
               "%s: expected %016llx%016llx, got %016llx%016llx\n",
               name, static_cast<unsigned long long>(expected >> 64U),
               static_cast<unsigned long long>(expected),
               static_cast<unsigned long long>(actual >> 64U),
               static_cast<unsigned long long>(actual));
  return false;
}

bool expectWithinUlps(const char* name, Bits actual, Bits expected,
                      std::uint64_t limit) {
  const Bits distance = actual >= expected ? actual - expected
                                           : expected - actual;
  if (distance <= limit) {
    return true;
  }
  std::fprintf(stderr, "%s: result differs by more than %llu ULP\n", name,
               static_cast<unsigned long long>(limit));
  return false;
}

bool expectRoundTrip(const char* name, Bits value) {
  char formatted[128]{};
  if (hs_f128_format(&value, formatted, sizeof(formatted)) <= 0) {
    std::fprintf(stderr, "%s: formatting failed\n", name);
    return false;
  }
  Bits parsed = 0;
  if (hs_f128_parse(formatted, &parsed) == 0) {
    std::fprintf(stderr, "%s: parsing '%s' failed\n", name, formatted);
    return false;
  }
  return expectEqual(name, parsed, value);
}

} // namespace

int main() {
  bool passed = true;
  const Bits zero = bits(0x0000000000000000ULL, 0x0000000000000000ULL);
  const Bits negativeZero = bits(0x8000000000000000ULL, 0x0000000000000000ULL);
  const Bits one = bits(0x3fff000000000000ULL, 0x0000000000000000ULL);
  const Bits two = bits(0x4000000000000000ULL, 0x0000000000000000ULL);
  const Bits three = bits(0x4000800000000000ULL, 0x0000000000000000ULL);
  const Bits four = bits(0x4001000000000000ULL, 0x0000000000000000ULL);
  const Bits eight = bits(0x4002000000000000ULL, 0x0000000000000000ULL);
  const Bits infinity = bits(0x7fff000000000000ULL, 0x0000000000000000ULL);
  const Bits maxFinite = bits(0x7ffeffffffffffffULL, 0xffffffffffffffffULL);
  const Bits minNormal = bits(0x0001000000000000ULL, 0x0000000000000000ULL);
  const Bits minSubnormal = bits(0x0000000000000000ULL, 0x0000000000000001ULL);
  const Bits nextAfterOne =
      bits(0x3fff000000000000ULL, 0x0000000000000001ULL);
  const Bits secondAfterOne =
      bits(0x3fff000000000000ULL, 0x0000000000000002ULL);

  passed &= expectEqual("literal", hs_f128_literal("1.5"),
                        bits(0x3fff800000000000ULL, 0));
  passed &= expectEqual("negative zero", hs_f128_literal("-0"), negativeZero);
  passed &= expectEqual(
      "literal ties to even",
      hs_f128_literal(
          "1.00000000000000000000000000000000009629649721936179265279889712924636592690508241076940976199693977832794189453125"),
      one);
  passed &= expectEqual(
      "literal above halfway",
      hs_f128_literal(
          "1.00000000000000000000000000000000009629649721936179265279889775154789371301925312517581514000936383735315876664796331011166147896988340353834411839448231257136169569665895551224821"),
      nextAfterOne);
  passed &= expectEqual("add", hs_f128_add(one, two), three);
  passed &= expectEqual("sub", hs_f128_sub(three, one), two);
  passed &= expectEqual("mul", hs_f128_mul(two, two), four);
  passed &= expectEqual("div", hs_f128_div(four, two), two);
  passed &= expectEqual("subnormal", hs_f128_add(minSubnormal, zero),
                        minSubnormal);
  const Bits halfUlpAtOne = hs_f128_literal(
      "9.629649721936179265279889712924636592690508241076940976199693977832794189453125e-35");
  passed &= expectEqual("add ties to even", hs_f128_add(one, halfUlpAtOne),
                        one);
  passed &= expectEqual("add odd tie rounds to even",
                        hs_f128_add(nextAfterOne, halfUlpAtOne),
                        secondAfterOne);
  passed &= expectEqual("subnormal underflow ties to even",
                        hs_f128_div(minSubnormal, two), zero);
  passed &= expectEqual("infinity", hs_f128_add(infinity, one), infinity);
  passed &= expectEqual("overflow", hs_f128_mul(maxFinite, two), infinity);
  passed &= expectEqual("negative zero multiplication",
                        hs_f128_mul(negativeZero, two), negativeZero);
  passed &= expectEqual("negative zero division",
                        hs_f128_div(negativeZero, two), negativeZero);
  const Bits nan = hs_f128_div(zero, zero);
  passed &= ((nan >> 112U) & 0x7fffU) == 0x7fffU &&
            (nan & ((Bits{1} << 112U) - 1U)) != 0;

  passed &= hs_f128_eq(one, one) && hs_f128_ne(one, two) &&
            hs_f128_lt(one, two) && hs_f128_le(one, one) &&
            hs_f128_gt(two, one) && hs_f128_ge(two, two);
  passed &= !hs_f128_eq(nan, nan) && hs_f128_ne(nan, nan) &&
            !hs_f128_lt(nan, one) && !hs_f128_le(nan, one) &&
            !hs_f128_gt(nan, one) && !hs_f128_ge(nan, one);
  passed &= expectEqual("from i64", hs_f128_from_i64(2), two);
  passed &= expectEqual("from u64", hs_f128_from_u64(2), two);
  passed &= expectEqual("from f32", hs_f128_from_f32(2.0F), two);
  passed &= expectEqual("from f64", hs_f128_from_f64(2.0), two);
  passed &= hs_f128_to_i64(two) == 2 && hs_f128_to_u64(two) == 2 &&
            hs_f128_to_f32(two) == 2.0F && hs_f128_to_f64(two) == 2.0;
  passed &= expectEqual("from negative f64 zero", hs_f128_from_f64(-0.0),
                        negativeZero);

  passed &= expectEqual("abs", hs_f128_abs(negativeZero), zero);
  passed &= expectEqual("sqrt", hs_f128_sqrt(four), two);
  passed &= expectEqual("pow", hs_f128_pow(two, three), eight);
  passed &= expectEqual("floor", hs_f128_floor(hs_f128_literal("1.75")), one);
  passed &= expectEqual("ceil", hs_f128_ceil(hs_f128_literal("1.25")), two);
  passed &= expectEqual("round", hs_f128_round(hs_f128_literal("1.5")), two);

  passed &= expectWithinUlps(
      "sin", hs_f128_sin(one),
      bits(0x3ffeaed548f090ceULL, 0xe0418dd3d2138a1eULL), 8);
  passed &= expectWithinUlps(
      "cos", hs_f128_cos(one),
      bits(0x3ffe14a280fb5068ULL, 0xb923848cdb2ed0e3ULL), 8);
  passed &= expectWithinUlps(
      "tan", hs_f128_tan(one),
      bits(0x3fff8eb245cbee3aULL, 0x5b8acc7d41323141ULL), 8);
  passed &= expectWithinUlps(
      "log", hs_f128_log(two),
      bits(0x3ffe62e42fefa39eULL, 0xf35793c7673007e6ULL), 8);
  passed &= expectWithinUlps(
      "exp", hs_f128_exp(one),
      bits(0x40005bf0a8b14576ULL, 0x95355fb8ac404e7aULL), 8);

  passed &= expectRoundTrip("normal decimal round trip", three);
  passed &= expectRoundTrip("minimum normal decimal round trip", minNormal);
  passed &= expectRoundTrip("minimum subnormal decimal round trip",
                            minSubnormal);
  passed &= expectRoundTrip("maximum finite decimal round trip", maxFinite);
  passed &= expectRoundTrip("negative zero decimal round trip", negativeZero);

  char negativeZeroText[8]{};
  passed &= hs_f128_format(&negativeZero, negativeZeroText,
                           sizeof(negativeZeroText)) == 2;
  passed &= std::strcmp(negativeZeroText, "-0") == 0;

  if (!passed) {
    return 1;
  }
  std::puts("HitSimple software f128 assertions passed");
  return 0;
}
